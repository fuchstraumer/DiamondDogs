#pragma once
#ifndef FOUNDATION_ASYNC_FRAME_STATISTICS_HPP
#define FOUNDATION_ASYNC_FRAME_STATISTICS_HPP
#include <cstdint>

namespace foundation
{

struct FrameStatisticsSnapshot
{
    int64_t LiveFrames{ 0 };
    uint64_t LiveBytes{ 0u };
    uint64_t PeakLiveBytes{ 0u };
    int64_t TotalFrames{ 0 };
};

/**@brief Returns the current counters of every coroutine frame in the process, whichever heap allocated it, using a
 * relaxed load, so the values my be slightly desynchronized. Scheduler::FrameStatistics returns the counters of the
 * frames in one Scheduler's heap. */
[[nodiscard]] FrameStatisticsSnapshot FrameStatistics() noexcept;

}

#endif // !FOUNDATION_ASYNC_FRAME_STATISTICS_HPP
