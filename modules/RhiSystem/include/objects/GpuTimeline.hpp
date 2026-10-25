#pragma once
#ifndef DIAMOND_DOGS_RHI_GPU_TIMELINE_HPP
#define DIAMOND_DOGS_RHI_GPU_TIMELINE_HPP
#include "RhiHandle.hpp"
#include "Semaphore.hpp"
#include "async/Timeline.hpp"
#include "async/TimelineSource.hpp"
#include <cstdint>

namespace rhi
{

    /**@brief Mirrors a GPU timeline semaphore into a foundation::Timeline, so that a coroutine can co_await a queue
     * submit. A submit names Semaphore() and a value from NextSignalValue(). Register the GpuTimeline with
     * Scheduler::AddSource, and each Tick then reads the semaphore and advances the mirror. Waiters hold the address of
     * the mirror, so a GpuTimeline never moves. */
    class GpuTimeline final : public foundation::TimelineSource
    {
    public:
        explicit GpuTimeline(const DeviceHandle& device);
        ~GpuTimeline() override;
        GpuTimeline(const GpuTimeline&) = delete;
        GpuTimeline& operator=(const GpuTimeline&) = delete;
        GpuTimeline(GpuTimeline&&) = delete;
        GpuTimeline& operator=(GpuTimeline&&) = delete;

        /** @brief Returns the timeline that coroutines wait on with Until. It advances only in Poll. */
        [[nodiscard]] const foundation::Timeline& Mirror() const noexcept;
        /** @brief Returns the semaphore to name in the signal of a submit. */
        [[nodiscard]] TimelineSemaphoreHandle Semaphore() const noexcept;
        /** @brief Reserves the next value to signal, starting at 1. Only the thread that submits calls this. A submit must
         * signal the values in the order that this function returns them. */
        [[nodiscard]] uint64_t NextSignalValue() noexcept;
        /** @brief Reads the semaphore counter, and advances the mirror when the counter advanced. Called by the
         * Scheduler on its owner thread. */
        void Poll() override;

    private:
        TimelineSemaphore semaphore;
        foundation::Timeline mirror;
        uint64_t lastReservedValue{ 0u };
    };

}

#endif // !DIAMOND_DOGS_RHI_GPU_TIMELINE_HPP
