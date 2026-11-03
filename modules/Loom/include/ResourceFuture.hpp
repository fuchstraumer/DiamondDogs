#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOURCE_FUTURE_HPP
#define DIAMOND_DOGS_LOOM_RESOURCE_FUTURE_HPP
#include "RhiHandle.hpp"
#include <cstdint>

namespace loom
{
    enum class FutureState : uint8_t
    {
        Invalid = 0, 
        Pending = 1, // queued
        Created = 2, // resource has been created: commands can be recorded on it
        Complete = 3, // potential transfer/upload has completed fully
        Failed = 4, // resource creation or transfer/upload has failed
    };

    struct GpuWait
    {
        rhi::TimelineSemaphoreHandle Semaphore;
        uint64_t Value{ 0u };
        bool Submitted{ false };
    };

    // basic shape, stub over GraphicsResourceReply until we get the timeline and ticket system
    // implemented so that we can drop the shared_ptr usage
    template<typename T>
    struct ResourceFuture
    {
        [[nodiscard]] FutureState State() const noexcept;
        [[nodiscard]] const T& Get() const noexcept;
        [[nodiscard]] GpuWait Completion() const noexcept;
        // [[nodiscard]] ResourceError Error() const noexcept;
    };
}

#endif // !DIAMOND_DOGS_LOOM_RESOURCE_FUTURE_HPP
