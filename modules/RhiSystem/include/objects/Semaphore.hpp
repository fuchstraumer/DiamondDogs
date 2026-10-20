#pragma once
#ifndef DIAMOND_DOGS_RHI_SEMAPHORE_HPP
#define DIAMOND_DOGS_RHI_SEMAPHORE_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include <cstdint>

namespace rhi
{

    class BinarySemaphore
    {
    public:
        BinarySemaphore(const DeviceHandle& device);
        ~BinarySemaphore();
        BinarySemaphore(const BinarySemaphore&) = delete;
        BinarySemaphore& operator=(const BinarySemaphore&) = delete;
        BinarySemaphore(BinarySemaphore&& other) noexcept;
        BinarySemaphore& operator=(BinarySemaphore&& other) noexcept;
        [[nodiscard]] BinarySemaphoreHandle Handle() const noexcept;
    private:
        DeviceHandle device;
        BinarySemaphoreHandle handle;
    };

    class TimelineSemaphore
    {
    public:
        TimelineSemaphore(const DeviceHandle& device);
        ~TimelineSemaphore();
        TimelineSemaphore(const TimelineSemaphore&) = delete;
        TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;
        TimelineSemaphore(TimelineSemaphore&& other) noexcept;
        TimelineSemaphore& operator=(TimelineSemaphore&& other) noexcept;
        TimelineSemaphoreHandle Handle() const noexcept;
        Result GetValue(uint64_t& value) const noexcept;
        Result SetValue(uint64_t value) noexcept;
        Result Increment(uint64_t amount = 1) noexcept;
    private:
        DeviceHandle device;
        TimelineSemaphoreHandle handle;
        mutable uint64_t currentValue;
    };

}

#endif // !DIAMOND_DOGS_RHI_SEMAPHORE_HPP
