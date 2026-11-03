#include "GpuTimeline.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include <cassert>
#include <cstdint>
#include <print>

namespace rhi
{

    GpuTimeline::GpuTimeline(const DeviceHandle& device) : semaphore{ device }
    {
    }

    GpuTimeline::~GpuTimeline() = default;

    const foundation::Timeline& GpuTimeline::Mirror() const noexcept
    {
        return mirror;
    }

    TimelineSemaphoreHandle GpuTimeline::Semaphore() const noexcept
    {
        return semaphore.Handle();
    }

    uint64_t GpuTimeline::NextSignalValue() noexcept
    {
        ++lastReservedValue;
        return lastReservedValue;
    }

    void GpuTimeline::Poll()
    {
        uint64_t counter = 0u;
        const Result result = semaphore.GetValue(counter);

        if (!result.IsSuccess())
        {
            // a lost device ends up here, and every later Poll reports it again
            std::println(stderr, "[async] GpuTimeline::Poll could not read the semaphore counter");
            assert(false && "vkGetSemaphoreCounterValue failed");
            return;
        }

        if (counter > mirror.Value())
        {
            mirror.Signal(counter);
        }
    }

}
