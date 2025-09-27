#pragma once
#ifndef DIAMOND_DOGS_RHI_SEMAPHORE_HPP
#define DIMOND_DOGS_RHI_SEMAPHORE_HPP
#include "RhiHandle.hpp"

namespace rhi
{
    namespace detail
    {
        struct BinarySemaphoreTag {};
        // distinguishing types in our wrapper because this can be helpful for writing code that uses semaphores
        struct TimelineSemaphoreTag {};
    }

    using BinarySemaphoreHandle = RhiHandle<detail::BinarySemaphoreTag>;
    using TimelineSemaphoreHandle = RhiHandle<detail::TimelineSemaphoreTag>;

    class BinarySemaphore
    {
    public:
        BinarySemaphore(const DeviceHandle device);
        ~BinarySemaphore();
        BinarySemaphore(const BinarySemaphore&) = delete;
        BinarySemaphore& operator=(const BinarySemaphore&) = delete;
        BinarySemaphore(BinarySemaphore&& other) noexcept;
        BinarySemaphore& operator=(BinarySemaphore&& other) noexcept;
        SemaphoreHandle Handle() const noexcept;
    private:
        DeviceHandle device;
        SemaphoreHandle handle;
    };

    class TimelineSemaphore
    {
    public:
        TimelineSemaphore(const DeviceHandle device);
        ~TimelineSemaphore();
        TimelineSemaphore(const TimelineSemaphore&) = delete;
        TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
        TimelineSemaphore(TimelineSemaphore&& other) noexcept;
        TimelineSemaphore& operator=(TimelineSemaphore&& other) noexcept;
        TimelineSemaphoreHandle Handle() const noexcept;
        uint64_t GetValue() const noexcept;
        void SetValue(uint64_t value) noexcept;
        uint64_t Increment() noexcept;
    private:
        DeviceHandle device;
        TimelineSemaphoreHandle handle;
        uint64_t currentValue;
    };

}

#endif // !DIAMOND_DOGS_RHI_SEMAPHORE_HPP
