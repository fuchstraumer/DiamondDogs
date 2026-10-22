#include "GpuTimers.hpp"
#if RHI_GPU_TIMERS_ENABLED
#include "CommandBuffer.hpp"
#include "DebugUtilFns.hpp"
#include "Device.hpp"
#include "QueryPool.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace rhi
{
    namespace
    {
        constexpr size_t k_MaxCompletedBlocks = 1024u;
        // each scope's name is copied into its block: up to 63 characters and a terminator, which the debug label
        // uses as it is
        constexpr size_t k_NameStride = 64u;
    }

    struct GpuTimersImpl
    {
        enum class BlockState : uint8_t
        {
            Free,
            Recording,
            Pending,
        };

        struct ScopeRecord
        {
            uint64_t Hash{ 0u };
            uint32_t Depth{ 0u };
        };

        // State hands a block between threads. A recording thread takes it Free -> Recording with an acquire CAS, and
        // owns everything else in the block until it stores Pending with release. The coalesce step reads Pending
        // with acquire, and stores Free with release once it is done with the block
        struct Block
        {
            std::atomic<BlockState> State{ BlockState::Free };
            uint64_t Tag{ 0u };
            uint64_t Sequence{ 0u };
            std::vector<ScopeRecord> Scopes;
            std::vector<char> Names;
            uint32_t Dropped{ 0u };
        };

        std::optional<QueryPool> Pool;
        std::string InactiveReason;
        uint32_t MaxScopes{ 0u };
        double TimestampPeriod{ 1.0 };
        uint64_t ValidMask{ ~0ull };
        PFN_vkCmdBeginDebugUtilsLabelEXT BeginLabel{ nullptr };
        PFN_vkCmdEndDebugUtilsLabelEXT EndLabel{ nullptr };

        // shared by recording threads: atomics only
        uint32_t BlockCount{ 0u };
        std::unique_ptr<Block[]> Blocks;
        std::atomic<uint32_t> NextBlock{ 0u };
        std::atomic<uint64_t> NextSequence{ 0u };
        std::atomic<uint64_t> DroppedBlocks{ 0u };
        // whole nanoseconds, so the transfer thread's Collect and the frame's reader need no lock
        std::atomic<uint64_t> BusyNanoseconds{ 0u };

        // the coalesce step's alone
        std::optional<GpuTimingBlock> Latest;
        uint64_t LatestSequence{ 0u };
        std::deque<GpuTimingBlock> Completed;
        std::unordered_map<uint64_t, std::string> Names;
        std::unordered_set<uint64_t> ReportedCollisions;

        [[nodiscard]] uint32_t FirstQuery(uint32_t block, uint32_t scope) const noexcept
        {
            return (block * MaxScopes + scope) * 2u;
        }

        // the first name seen for a hash is kept, so a Name() view stays valid
        void InternName(uint64_t hash, std::string_view name)
        {
            const auto [found, inserted] = Names.try_emplace(hash, name);

            if (!inserted && found->second != name && ReportedCollisions.insert(hash).second)
            {
                std::cerr << std::format("GpuTimers: \"{}\" and \"{}\" hash to the same value {:#018x}\n", found->second, name, hash);
            }
        }

        [[nodiscard]] std::optional<GpuTimingBlock> ReadBlock(const Block& block, uint32_t index) const
        {
            GpuTimingBlock result{};
            result.Tag = block.Tag;
            result.DroppedScopes = block.Dropped;
            const uint32_t used = static_cast<uint32_t>(block.Scopes.size());

            if (used == 0u)
            {
                return result;
            }

            std::vector<uint64_t> ticks(size_t{ used } * 2u);

            if (!Pool->TryGetResults(FirstQuery(index, 0u), used * 2u, ticks))
            {
                return std::nullopt;
            }

            result.Timings.reserve(used);

            for (uint32_t scope = 0; scope < used; ++scope)
            {
                // the counter wraps at its valid bits, so the difference is taken in that width
                const uint64_t elapsed = (ticks[size_t{ scope } * 2u + 1u] - ticks[size_t{ scope } * 2u]) & ValidMask;
                const double nanoseconds = static_cast<double>(elapsed) * TimestampPeriod;
                result.Timings.push_back(GpuTiming{ block.Scopes[scope].Hash, block.Scopes[scope].Depth, nanoseconds });

                if (block.Scopes[scope].Depth == 0u)
                {
                    result.TopLevelNanoseconds += nanoseconds;
                }
            }

            return result;
        }
    };

    GpuTimers::GpuTimers(const Device& device, const GpuTimersCreateInfo& create_info) : impl{ std::make_unique<GpuTimersImpl>() }
    {
        const VkPhysicalDevice physicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        uint32_t familyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

        // the device enables every feature it supports, so supported means enabled here
        VkPhysicalDeviceHostQueryResetFeatures hostQueryReset{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES, nullptr, VK_FALSE };

        if (properties.apiVersion >= VK_API_VERSION_1_2)
        {
            VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &hostQueryReset, {} };
            vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
        }

        if (create_info.BlockCount == 0u || create_info.MaxScopesPerBlock == 0u)
        {
            impl->InactiveReason = "BlockCount and MaxScopesPerBlock must not be 0";
            return;
        }

        if (create_info.QueueFamily >= familyCount)
        {
            impl->InactiveReason = std::format("queue family {} does not exist", create_info.QueueFamily);
            return;
        }

        const uint32_t validBits = families[create_info.QueueFamily].timestampValidBits;

        if (validBits == 0u)
        {
            impl->InactiveReason = std::format("queue family {} has no timestamp support", create_info.QueueFamily);
            return;
        }

        if (hostQueryReset.hostQueryReset != VK_TRUE)
        {
            impl->InactiveReason = "the device has no hostQueryReset";
            return;
        }

        impl->MaxScopes = create_info.MaxScopesPerBlock;
        impl->TimestampPeriod = static_cast<double>(properties.limits.timestampPeriod);
        impl->ValidMask = validBits >= 64u ? ~0ull : (1ull << validBits) - 1ull;
        impl->BlockCount = create_info.BlockCount;
        impl->Blocks = std::make_unique<GpuTimersImpl::Block[]>(create_info.BlockCount);

        for (uint32_t index = 0; index < impl->BlockCount; ++index)
        {
            impl->Blocks[index].Scopes.reserve(create_info.MaxScopesPerBlock);
            impl->Blocks[index].Names.resize(size_t{ create_info.MaxScopesPerBlock } * k_NameStride);
        }

        impl->Pool.emplace(device.Handle(), QueryType::Timestamp, create_info.BlockCount * create_info.MaxScopesPerBlock * 2u);

        const VkDebugUtilsFunctions& debugUtils = device.GetDebugUtilFns();
        impl->BeginLabel = debugUtils.vkCmdBeginDebugUtilsLabel;
        impl->EndLabel = debugUtils.vkCmdEndDebugUtilsLabel;
    }

    GpuTimers::~GpuTimers() = default;

    bool GpuTimers::Active() const noexcept
    {
        return impl->Pool.has_value();
    }

    std::string_view GpuTimers::InactiveReason() const noexcept
    {
        return impl->InactiveReason;
    }

    void GpuTimers::Collect()
    {
        if (!Active())
        {
            return;
        }

        // in the order the blocks were begun, so Completed stays oldest first
        std::vector<uint32_t> pending;

        for (uint32_t index = 0; index < impl->BlockCount; ++index)
        {
            if (impl->Blocks[index].State.load(std::memory_order_acquire) == GpuTimersImpl::BlockState::Pending)
            {
                pending.push_back(index);
            }
        }

        std::ranges::sort(pending, std::less{}, [this](uint32_t index) { return impl->Blocks[index].Sequence; });

        for (const uint32_t index : pending)
        {
            GpuTimersImpl::Block& block = impl->Blocks[index];
            std::optional<GpuTimingBlock> result = impl->ReadBlock(block, index);

            if (!result.has_value())
            {
                continue;
            }

            // every query of the block was written, so the GPU is done with them
            const uint32_t used = static_cast<uint32_t>(block.Scopes.size());

            if (used != 0u)
            {
                impl->Pool->Reset(impl->FirstQuery(index, 0u), used * 2u);
            }

            for (uint32_t scope = 0; scope < used; ++scope)
            {
                const char* name = block.Names.data() + size_t{ scope } * k_NameStride;
                impl->InternName(block.Scopes[scope].Hash, std::string_view{ name, std::strlen(name) });
            }

            impl->BusyNanoseconds.fetch_add(static_cast<uint64_t>(std::llround(result->TopLevelNanoseconds)), std::memory_order_relaxed);

            if (!impl->Latest.has_value() || block.Sequence >= impl->LatestSequence)
            {
                impl->Latest = *result;
                impl->LatestSequence = block.Sequence;
            }

            impl->Completed.push_back(std::move(*result));

            if (impl->Completed.size() > k_MaxCompletedBlocks)
            {
                impl->Completed.pop_front();
            }

            block.Scopes.clear();
            block.Dropped = 0u;
            block.State.store(GpuTimersImpl::BlockState::Free, std::memory_order_release);
        }
    }

    std::optional<GpuTimingBlock> GpuTimers::Latest() const
    {
        return impl->Latest;
    }

    std::vector<GpuTimingBlock> GpuTimers::TakeCompleted()
    {
        std::vector<GpuTimingBlock> completed{ std::make_move_iterator(impl->Completed.begin()), std::make_move_iterator(impl->Completed.end()) };
        impl->Completed.clear();
        return completed;
    }

    double GpuTimers::TakeBusyNanoseconds() noexcept
    {
        return static_cast<double>(impl->BusyNanoseconds.exchange(0u, std::memory_order_relaxed));
    }

    std::optional<double> GpuTimers::Find(uint64_t hash) const
    {
        if (!impl->Latest.has_value())
        {
            return std::nullopt;
        }

        std::optional<double> total;

        for (const GpuTiming& timing : impl->Latest->Timings)
        {
            if (timing.Hash == hash)
            {
                total = total.value_or(0.0) + timing.Nanoseconds;
            }
        }

        return total;
    }

    std::optional<double> GpuTimers::Find(std::string_view name) const
    {
        return Find(HashGpuTimerName(name));
    }

    std::string_view GpuTimers::Name(uint64_t hash) const
    {
        const auto found = impl->Names.find(hash);
        // the map is node-based and never erases, so the view stays valid
        return found != impl->Names.end() ? std::string_view{ found->second } : std::string_view{};
    }

    uint64_t GpuTimers::DroppedBlocks() const noexcept
    {
        return impl->DroppedBlocks.load(std::memory_order_relaxed);
    }

    GpuTimerBlock::GpuTimerBlock(GpuTimers& _timers, CommandBuffer& command_buffer, uint64_t tag) : timers{ &_timers }, commandBuffer{ &command_buffer }
    {
        if (!timers->Active())
        {
            return;
        }

        GpuTimersImpl& impl = *timers->impl;
        // threads start their search at different blocks, so they rarely race for the same one
        const uint32_t start = impl.NextBlock.fetch_add(1u, std::memory_order_relaxed);

        for (uint32_t offset = 0; offset < impl.BlockCount; ++offset)
        {
            const uint32_t candidate = (start + offset) % impl.BlockCount;
            GpuTimersImpl::BlockState expected = GpuTimersImpl::BlockState::Free;

            if (impl.Blocks[candidate].State.compare_exchange_strong(expected, GpuTimersImpl::BlockState::Recording,
                                                                     std::memory_order_acquire, std::memory_order_relaxed))
            {
                index = candidate;
                break;
            }
        }

        if (index == ~0u)
        {
            impl.DroppedBlocks.fetch_add(1u, std::memory_order_relaxed);
            return;
        }

        GpuTimersImpl::Block& block = impl.Blocks[index];
        block.Tag = tag;
        block.Sequence = impl.NextSequence.fetch_add(1u, std::memory_order_relaxed);
    }

    GpuTimerBlock::~GpuTimerBlock()
    {
        if (!Active())
        {
            return;
        }

        timers->impl->Blocks[index].State.store(GpuTimersImpl::BlockState::Pending, std::memory_order_release);
    }

    bool GpuTimerBlock::Active() const noexcept
    {
        return index != ~0u;
    }

    ScopedGpuTimer::ScopedGpuTimer(GpuTimerBlock& _block, std::string_view name) : block{ &_block }
    {
        if (!block->Active())
        {
            return;
        }

        // the block belongs to this thread while it records, so nothing here is shared
        GpuTimersImpl& impl = *block->timers->impl;
        GpuTimersImpl::Block& record = impl.Blocks[block->index];

        if (record.Scopes.size() >= impl.MaxScopes)
        {
            ++record.Dropped;
            return;
        }

        scope = static_cast<uint32_t>(record.Scopes.size());
        record.Scopes.push_back(GpuTimersImpl::ScopeRecord{ HashGpuTimerName(name), block->depth });
        ++block->depth;

        char* nameCopy = record.Names.data() + size_t{ scope } * k_NameStride;
        const size_t length = std::min(name.size(), k_NameStride - 1u);
        std::memcpy(nameCopy, name.data(), length);
        nameCopy[length] = '\0';

        block->commandBuffer->WriteTimestamp(PipelineStage::TopOfPipe, impl.Pool->Handle(), impl.FirstQuery(block->index, scope));

        if (impl.BeginLabel != nullptr)
        {
            const VkDebugUtilsLabelEXT labelInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, nameCopy, { 0.0f, 0.0f, 0.0f, 0.0f } };
            impl.BeginLabel(block->commandBuffer->Handle().As<VkCommandBuffer>(), &labelInfo);
        }
    }

    ScopedGpuTimer::~ScopedGpuTimer()
    {
        if (scope == ~0u)
        {
            return;
        }

        GpuTimersImpl& impl = *block->timers->impl;
        --block->depth;
        // AllCommands: the end waits until every earlier command in the queue has finished
        block->commandBuffer->WriteTimestamp(PipelineStage::AllCommands, impl.Pool->Handle(), impl.FirstQuery(block->index, scope) + 1u);

        if (impl.EndLabel != nullptr)
        {
            impl.EndLabel(block->commandBuffer->Handle().As<VkCommandBuffer>());
        }
    }

}
#endif
