#pragma once
#ifndef DIAMOND_DOGS_RHI_FENCE_HPP
#define DIAMOND_DOGS_RHI_FENCE_HPP

namespace rhi
{
    class Device;

    class Fence
    {
    public:
        Fence(const Device* device);
        ~Fence();
        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;
        Fence(Fence&& other) noexcept;
        Fence& operator=(Fence&& other) noexcept;

        VkFence vkHandle() const noexcept;
        bool IsSignaled() const;
        void Reset();
        bool Wait(uint64_t timeoutNs = UINT64_MAX) const;
    };

}

#endif // !DIAMOND_DOGS_RHI_FENCE_HPP
