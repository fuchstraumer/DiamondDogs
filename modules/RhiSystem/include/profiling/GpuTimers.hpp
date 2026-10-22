#pragma once
#ifndef DIAMOND_DOGS_RHI_GPU_TIMERS_HPP
#define DIAMOND_DOGS_RHI_GPU_TIMERS_HPP
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

/** GPU timestamp scopes, for every build below Release: rhi_system sets RHI_GPU_TIMERS_ENABLED to 1 in Debug and
 *  RelWithDebInfo, and to 0 in Release and MinSizeRel. With 0 the same classes exist with empty inline bodies, so
 *  call sites need no #if, and no query pool is created.
 *
 *  One GpuTimers per queue that submits: graphics, async compute, transfer. Timestamps are only comparable within
 *  one queue, and each queue family has its own valid bits. A GpuTimerBlock is one command buffer's worth of scopes,
 *  and a ScopedGpuTimer is one begin/end pair inside it, nested like scopes in code:
 *
 *      rhi::GpuTimerBlock block(timers, cmd, frameNumber);
 *      {
 *          rhi::ScopedGpuTimer tick(block, "Ocean tick");
 *          ...
 *      }
 *      // after the submit, on any thread, at any time:
 *      timers.Collect();
 *
 *  Collect reads results with VK_QUERY_RESULT_WITH_AVAILABILITY_BIT and never waits: a block whose queries are not
 *  all written yet stays pending until a later Collect. The query pool is reset on the host, which works for every
 *  queue type (the transfer queue cannot record vkCmdResetQueryPool). */
#ifndef RHI_GPU_TIMERS_ENABLED
#define RHI_GPU_TIMERS_ENABLED 0
#endif

namespace rhi
{
    class CommandBuffer;
    class Device;

    /** @brief 64-bit FNV-1a. Scope names are only hashed: the hash is the key for every record, result and lookup */
    [[nodiscard]] constexpr uint64_t HashGpuTimerName(std::string_view name) noexcept
    {
        uint64_t hash = 0xCBF29CE484222325ull;

        for (const char c : name)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 0x100000001B3ull;
        }

        return hash;
    }

    struct GpuTimersCreateInfo
    {
        /** @brief The family of the queue that submits the command buffers */
        uint32_t QueueFamily{ ~0u };
        /** @brief Blocks that can be recording, in flight, or waiting for Collect at once. For a frame loop that
         *  collects at the start of each frame: frames in flight + 1 */
        uint32_t BlockCount{ 0u };
        /** @brief The estimate of scopes per block. Scopes past it record nothing, and are counted as dropped */
        uint32_t MaxScopesPerBlock{ 0u };
    };

    struct GpuTiming
    {
        uint64_t Hash{ 0u };
        /** @brief 0 for a scope opened directly in the block, 1 inside one of those, and so on */
        uint32_t Depth{ 0u };
        double Nanoseconds{ 0.0 };
    };

    struct GpuTimingBlock
    {
        /** @brief The caller's tag from the GpuTimerBlock, for example a frame number */
        uint64_t Tag{ 0u };
        /** @brief In record order. Record order and depth give the scope tree */
        std::vector<GpuTiming> Timings;
        /** @brief The sum of the depth 0 scopes: how long the queue was busy with this block's scoped work */
        double TopLevelNanoseconds{ 0.0 };
        uint32_t DroppedScopes{ 0u };
    };

#if RHI_GPU_TIMERS_ENABLED
    struct GpuTimersImpl;

    class GpuTimers
    {
    public:
        /** @brief Inactive (Active() false, every scope records nothing) when the queue family has no timestamp valid
         *  bits, the device has no hostQueryReset, or a count is 0. InactiveReason says which */
        GpuTimers(const Device& device, const GpuTimersCreateInfo& create_info);
        ~GpuTimers();
        GpuTimers(const GpuTimers&) = delete;
        GpuTimers& operator=(const GpuTimers&) = delete;

        [[nodiscard]] bool Active() const noexcept;
        [[nodiscard]] std::string_view InactiveReason() const noexcept;

        /** @brief Reads every finished block whose queries are all available, publishes it, and frees it. Never
         *  waits. Safe from any thread */
        void Collect();
        /** @brief The most recently collected block, by the order the blocks were begun */
        [[nodiscard]] std::optional<GpuTimingBlock> Latest() const;
        /** @brief Every block collected since the last call, oldest first (at most the last 1024) */
        [[nodiscard]] std::vector<GpuTimingBlock> TakeCompleted();
        /** @brief The sum of TopLevelNanoseconds over the blocks collected since the last call: queue busy time */
        [[nodiscard]] double TakeBusyNanoseconds();
        /** @brief The total time of every scope with this hash in Latest(), or nothing when it has none */
        [[nodiscard]] std::optional<double> Find(uint64_t hash) const;
        [[nodiscard]] std::optional<double> Find(std::string_view name) const;
        /** @brief The name a scope with this hash was first opened with (GpuTimers keeps its own copy), or empty */
        [[nodiscard]] std::string_view Name(uint64_t hash) const;
        /** @brief Blocks that found no free block, because too many were pending */
        [[nodiscard]] uint64_t DroppedBlocks() const noexcept;

    private:
        friend class GpuTimerBlock;
        friend class ScopedGpuTimer;
        std::unique_ptr<GpuTimersImpl> impl;
    };

    /** @brief One command buffer's scopes. It takes a free block on construction (or none, when every block is
     *  pending: then its scopes record nothing). The destructor hands it to Collect, so destroy it once its last
     *  scope is closed. Recording state is local to the block, so several blocks of one GpuTimers can record on
     *  different threads at once */
    class GpuTimerBlock
    {
    public:
        GpuTimerBlock(GpuTimers& timers, CommandBuffer& command_buffer, uint64_t tag);
        ~GpuTimerBlock();
        GpuTimerBlock(const GpuTimerBlock&) = delete;
        GpuTimerBlock& operator=(const GpuTimerBlock&) = delete;

        [[nodiscard]] bool Active() const noexcept;

    private:
        friend class ScopedGpuTimer;
        GpuTimers* timers{ nullptr };
        CommandBuffer* commandBuffer{ nullptr };
        uint32_t index{ ~0u };
        uint32_t depth{ 0u };
    };

    /** @brief A timestamp at TopOfPipe on construction and at AllCommands on destruction, plus a debug utils label
     *  region with the same name, so RenderDoc, PIX and Nsight show the scopes too */
    class ScopedGpuTimer
    {
    public:
        ScopedGpuTimer(GpuTimerBlock& block, std::string_view name);
        ~ScopedGpuTimer();
        ScopedGpuTimer(const ScopedGpuTimer&) = delete;
        ScopedGpuTimer& operator=(const ScopedGpuTimer&) = delete;

    private:
        GpuTimerBlock* block{ nullptr };
        uint32_t scope{ ~0u };
    };
#else
    // Release: the same interface, recording and allocating nothing
    class GpuTimers
    {
    public:
        GpuTimers([[maybe_unused]] const Device& device, [[maybe_unused]] const GpuTimersCreateInfo& create_info) noexcept {}
        GpuTimers(const GpuTimers&) = delete;
        GpuTimers& operator=(const GpuTimers&) = delete;

        [[nodiscard]] bool Active() const noexcept { return false; }
        [[nodiscard]] std::string_view InactiveReason() const noexcept { return "GPU timers are compiled out of Release builds"; }
        void Collect() noexcept {}
        [[nodiscard]] std::optional<GpuTimingBlock> Latest() const noexcept { return std::nullopt; }
        [[nodiscard]] std::vector<GpuTimingBlock> TakeCompleted() noexcept { return {}; }
        [[nodiscard]] double TakeBusyNanoseconds() noexcept { return 0.0; }
        [[nodiscard]] std::optional<double> Find([[maybe_unused]] uint64_t hash) const noexcept { return std::nullopt; }
        [[nodiscard]] std::optional<double> Find([[maybe_unused]] std::string_view name) const noexcept { return std::nullopt; }
        [[nodiscard]] std::string_view Name([[maybe_unused]] uint64_t hash) const noexcept { return {}; }
        [[nodiscard]] uint64_t DroppedBlocks() const noexcept { return 0u; }
    };

    class GpuTimerBlock
    {
    public:
        GpuTimerBlock([[maybe_unused]] GpuTimers& timers, [[maybe_unused]] CommandBuffer& command_buffer, [[maybe_unused]] uint64_t tag) noexcept {}
        GpuTimerBlock(const GpuTimerBlock&) = delete;
        GpuTimerBlock& operator=(const GpuTimerBlock&) = delete;

        [[nodiscard]] bool Active() const noexcept { return false; }
    };

    class ScopedGpuTimer
    {
    public:
        ScopedGpuTimer([[maybe_unused]] GpuTimerBlock& block, [[maybe_unused]] std::string_view name) noexcept {}
        ScopedGpuTimer(const ScopedGpuTimer&) = delete;
        ScopedGpuTimer& operator=(const ScopedGpuTimer&) = delete;
    };
#endif

}

#endif // !DIAMOND_DOGS_RHI_GPU_TIMERS_HPP
