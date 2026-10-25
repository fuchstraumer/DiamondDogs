#include "async/Promise.hpp"
#include "async/FrameStatistics.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

namespace
{
    // relaxed counters: they are statistics, and nothing else reads them to order its own work
    std::atomic<int64_t> liveFrames{ 0 };
    std::atomic<uint64_t> liveBytes{ 0u };
    std::atomic<uint64_t> peakLiveBytes{ 0u };
    std::atomic<int64_t> totalFrames{ 0 };

    void RaisePeak(uint64_t candidate) noexcept;
}

namespace foundation
{

void* BasePromise::operator new(std::size_t size)
{
    // mimalloc overrides ::operator new for the whole project
    void* frame = ::operator new(size);

    liveFrames.fetch_add(1, std::memory_order_relaxed);
    totalFrames.fetch_add(1, std::memory_order_relaxed);
    const uint64_t bytesAfter = liveBytes.fetch_add(size, std::memory_order_relaxed) + size;
    RaisePeak(bytesAfter);

    return frame;
}

void BasePromise::operator delete(void* frame, std::size_t size) noexcept
{
    liveFrames.fetch_sub(1, std::memory_order_relaxed);
    liveBytes.fetch_sub(size, std::memory_order_relaxed);

    ::operator delete(frame, size);
}

FrameStatisticsSnapshot FrameStatistics() noexcept
{
    FrameStatisticsSnapshot snapshot;
    snapshot.LiveFrames = liveFrames.load(std::memory_order_relaxed);
    snapshot.LiveBytes = liveBytes.load(std::memory_order_relaxed);
    snapshot.PeakLiveBytes = peakLiveBytes.load(std::memory_order_relaxed);
    snapshot.TotalFrames = totalFrames.load(std::memory_order_relaxed);
    return snapshot;
}

}

namespace
{
    void RaisePeak(uint64_t candidate) noexcept
    {
        uint64_t observed = peakLiveBytes.load(std::memory_order_relaxed);

        while (observed < candidate &&
               !peakLiveBytes.compare_exchange_weak(observed, candidate, std::memory_order_relaxed))
        {
        }
    }
}
