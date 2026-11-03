#include "async/Promise.hpp"
#include "async/FrameHeap.hpp"
#include "async/FrameStatistics.hpp"
#include <cstddef>

namespace foundation
{

void* BasePromise::operator new(std::size_t size)
{
    return FrameHeap::AllocateFrame(size);
}

void BasePromise::operator delete(void* frame, std::size_t size) noexcept
{
    FrameHeap::FreeFrame(frame, size);
}

FrameStatisticsSnapshot FrameStatistics() noexcept
{
    return FrameHeap::ProcessSnapshot();
}

}
