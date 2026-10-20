#pragma once
#ifndef DIAMOND_DOGS_RHI_FENCE_HPP
#define DIAMOND_DOGS_RHI_FENCE_HPP
#include "utility/TaggedBool.hpp"
#include "RhiHandle.hpp"
#include <cstdint>

namespace rhi
{
    using CreateFenceSignaled = TaggedBool<detail::FenceTag>;

    class Fence
    {
    public:
        Fence(const DeviceHandle& device, CreateFenceSignaled signaled = CreateFenceSignaled{ false });
        ~Fence();
        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;
        Fence(Fence&& other) noexcept;
        Fence& operator=(Fence&& other) noexcept;

        [[nodiscard]] FenceHandle Handle() const noexcept;
        [[nodiscard]] bool IsSignaled() const;
        void Reset();
        [[nodiscard]] bool Wait(uint64_t timeout_ns = UINT64_MAX) const;
    private:
        DeviceHandle device;
        FenceHandle handle;
    };

}

#endif // !DIAMOND_DOGS_RHI_FENCE_HPP
