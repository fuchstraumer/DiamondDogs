#include "GpuTimeline.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
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
        if (mirror.Failed())
        {
            return;
        }

        uint64_t counter = 0u;
        const Result result = semaphore.GetValue(counter);

        if (!result.IsSuccess())
        {
            // a lost device ends up here. Fail wakes every parked frame, and later Polls return at the top
            std::println(stderr, "[async] GpuTimeline::Poll could not read the semaphore counter, so the timeline failed");
            mirror.Fail();
            return;
        }

        if (counter > mirror.Value())
        {
            mirror.Signal(counter);
        }
    }

}
