#pragma once
#ifndef DIAMOND_DOGS_RHI_SEMAPHORE_HPP
#define DIMOND_DOGS_RHI_SEMAPHORE_HPP
#include "RhiHandle.hpp"

namespace rhi
{
    class Device;

    class BinarySemaphore
    {
    public:
        BinarySemaphore(const Device* device);
        ~BinarySemaphore();
        BinarySemaphore(const BinarySemaphore&) = delete;
        BinarySemaphore& operator=(const BinarySemaphore&) = delete;
        BinarySemaphore(BinarySemaphore&& other) noexcept;
        BinarySemaphore& operator=(BinarySemaphore&& other) noexcept;
        uint64_t ApiHandle() const noexcept;
    private:
        const Device* device;
        uint64_t handle;
    };

    class TimelineSemaphore
    {
    public:
        TimelineSemaphore(const Device* device);
        ~TimelineSemaphore();
        TimelineSemaphore(const TimelineSemaphore&) = delete;
        TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
        TimelineSemaphore(TimelineSemaphore&& other) noexcept;
        TimelineSemaphore& operator=(TimelineSemaphore&& other) noexcept;
        uint64_t ApiHandle() const noexcept;
        uint64_t GetValue() const noexcept;
        void SetValue(uint64_t value) noexcept;
        uint64_t Increment() noexcept;
    private:
        const Device* device;
        uint64_t handle;
        uint64_t currentValue;
    };

}

#endif // !DIAMOND_DOGS_RHI_SEMAPHORE_HPP
