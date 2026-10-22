#include "GpuTimers.hpp"
#if RHI_GPU_TIMERS_ENABLED
#include "CommandBuffer.hpp"
#include "DebugUtilFns.hpp"
#include "Device.hpp"
#include "QueryPool.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
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
        // debug utils labels need a terminated string; longer names are cut in the label only
        constexpr size_t k_MaxLabelLength = 127u;
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

        // the recording thread owns Scopes and Dropped while the block is Recording; the state changes under the
        // mutex, which also publishes them to Collect
        struct Block
        {
            BlockState State{ BlockState::Free };
            uint64_t Tag{ 0u };
            uint64_t Sequence{ 0u };
            std::vector<ScopeRecord> Scopes;
            uint32_t Dropped{ 0u };
        };

        std::optional<QueryPool> Pool;
        std::string InactiveReason;
        uint32_t MaxScopes{ 0u };
        double TimestampPeriod{ 1.0 };
        uint64_t ValidMask{ ~0ull };
        PFN_vkCmdBeginDebugUtilsLabelEXT BeginLabel{ nullptr };
        PFN_vkCmdEndDebugUtilsLabelEXT EndLabel{ nullptr };

        mutable std::mutex Mutex;
        std::vector<Block> Blocks;
        uint64_t NextSequence{ 0u };
        uint64_t DroppedBlocks{ 0u };
        std::optional<GpuTimingBlock> Latest;
        uint64_t LatestSequence{ 0u };
        std::deque<GpuTimingBlock> Completed;
        double BusyNanoseconds{ 0.0 };

        mutable std::shared_mutex NamesMutex;
        std::unordered_map<uint64_t, std::string> Names;
        std::unordered_set<uint64_t> ReportedCollisions;

        [[nodiscard]] uint32_t FirstQuery(uint32_t block, uint32_t scope) const noexcept
        {
            return (block * MaxScopes + scope) * 2u;
        }

        // the first name seen for a hash is copied, so nothing points into the caller's string
        void InternName(uint64_t hash, std::string_view name)
        {
            {
                const std::shared_lock lock(NamesMutex);
                const auto found = Names.find(hash);

                if (found != Names.end() && found->second == name)
                {
                    return;
                }
            }

            const std::unique_lock lock(NamesMutex);
            const auto [found, inserted] = Names.try_emplace(hash, name);

            if (!inserted && found->second != name && ReportedCollisions.insert(hash).second)
            {
                std::cerr << std::format("GpuTimers: \"{}\" and \"{}\" hash to the same value {:#018x}\n", found->second, name, hash);
            }
        }

        [[nodiscard]] std::optional<GpuTimingBlock> ReadBlock(Block& block, uint32_t index) const
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
        impl->Blocks.resize(create_info.BlockCount);

        for (GpuTimersImpl::Block& block : impl->Blocks)
        {
            block.Scopes.reserve(create_info.MaxScopesPerBlock);
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

        const std::lock_guard lock(impl->Mutex);
        // in the order the blocks were begun, so Completed stays oldest first
        std::vector<uint32_t> pending;

        for (uint32_t index = 0; index < impl->Blocks.size(); ++index)
        {
            if (impl->Blocks[index].State == GpuTimersImpl::BlockState::Pending)
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
            if (!block.Scopes.empty())
            {
                impl->Pool->Reset(impl->FirstQuery(index, 0u), static_cast<uint32_t>(block.Scopes.size()) * 2u);
            }

            impl->BusyNanoseconds += result->TopLevelNanoseconds;

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
            block.State = GpuTimersImpl::BlockState::Free;
        }
    }

    std::optional<GpuTimingBlock> GpuTimers::Latest() const
    {
        const std::lock_guard lock(impl->Mutex);
        return impl->Latest;
    }

    std::vector<GpuTimingBlock> GpuTimers::TakeCompleted()
    {
        const std::lock_guard lock(impl->Mutex);
        std::vector<GpuTimingBlock> completed{ std::make_move_iterator(impl->Completed.begin()), std::make_move_iterator(impl->Completed.end()) };
        impl->Completed.clear();
        return completed;
    }

    double GpuTimers::TakeBusyNanoseconds()
    {
        const std::lock_guard lock(impl->Mutex);
        const double busy = impl->BusyNanoseconds;
        impl->BusyNanoseconds = 0.0;
        return busy;
    }

    std::optional<double> GpuTimers::Find(uint64_t hash) const
    {
        const std::lock_guard lock(impl->Mutex);

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
        const std::shared_lock lock(impl->NamesMutex);
        const auto found = impl->Names.find(hash);
        // the map is node-based and never erases, so the view stays valid after the lock
        return found != impl->Names.end() ? std::string_view{ found->second } : std::string_view{};
    }

    uint64_t GpuTimers::DroppedBlocks() const noexcept
    {
        const std::lock_guard lock(impl->Mutex);
        return impl->DroppedBlocks;
    }

    GpuTimerBlock::GpuTimerBlock(GpuTimers& _timers, CommandBuffer& command_buffer, uint64_t tag) : timers{ &_timers }, commandBuffer{ &command_buffer }
    {
        if (!timers->Active())
        {
            return;
        }

        GpuTimersImpl& impl = *timers->impl;
        const std::lock_guard lock(impl.Mutex);
        const auto freeBlock = std::ranges::find(impl.Blocks, GpuTimersImpl::BlockState::Free, &GpuTimersImpl::Block::State);

        if (freeBlock == impl.Blocks.end())
        {
            ++impl.DroppedBlocks;
            return;
        }

        freeBlock->State = GpuTimersImpl::BlockState::Recording;
        freeBlock->Tag = tag;
        freeBlock->Sequence = impl.NextSequence++;
        index = static_cast<uint32_t>(freeBlock - impl.Blocks.begin());
    }

    GpuTimerBlock::~GpuTimerBlock()
    {
        if (!Active())
        {
            return;
        }

        GpuTimersImpl& impl = *timers->impl;
        const std::lock_guard lock(impl.Mutex);
        impl.Blocks[index].State = GpuTimersImpl::BlockState::Pending;
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

        GpuTimersImpl& impl = *block->timers->impl;
        GpuTimersImpl::Block& record = impl.Blocks[block->index];

        if (record.Scopes.size() >= impl.MaxScopes)
        {
            ++record.Dropped;
            return;
        }

        const uint64_t hash = HashGpuTimerName(name);
        impl.InternName(hash, name);
        scope = static_cast<uint32_t>(record.Scopes.size());
        record.Scopes.push_back(GpuTimersImpl::ScopeRecord{ hash, block->depth });
        ++block->depth;

        block->commandBuffer->WriteTimestamp(PipelineStage::TopOfPipe, impl.Pool->Handle(), impl.FirstQuery(block->index, scope));

        if (impl.BeginLabel != nullptr)
        {
            std::array<char, k_MaxLabelLength + 1u> label{};
            const size_t length = std::min(name.size(), k_MaxLabelLength);
            std::memcpy(label.data(), name.data(), length);
            const VkDebugUtilsLabelEXT labelInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, label.data(), { 0.0f, 0.0f, 0.0f, 0.0f } };
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
