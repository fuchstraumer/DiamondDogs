#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_SWAPCHAIN_FRAME_TARGET_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_SWAPCHAIN_FRAME_TARGET_HPP
#include "FrameTarget.hpp"
#include "Semaphore.hpp"
#include <cstdint>
#include <optional>
#include <vector>

class Swapchain;

namespace rhi
{
    class Device;
}

// loom_vulkan_platform: the swapchain as a Loom frame target (step 7 Q6). It lives in its own target so that
// loom_vulkan does not link PlatformSystem. PlatformSystem's Swapchain has no acquire or present yet, so this file calls
// Vulkan for both (listed in the Loom agent's handbook for the RhiSystem agent).
namespace loom
{

    class SwapchainFrameTarget final : public FrameTarget
    {
    public:
        // frame_slots is the graph's RealizeInputs::FrameSlots: each frame in flight has its own acquire semaphore
        SwapchainFrameTarget(const rhi::Device& device, Swapchain& swapchain, int32_t frame_slots);
        ~SwapchainFrameTarget() override;
        SwapchainFrameTarget(const SwapchainFrameTarget&) = delete;
        SwapchainFrameTarget& operator=(const SwapchainFrameTarget&) = delete;
        SwapchainFrameTarget(SwapchainFrameTarget&&) = delete;
        SwapchainFrameTarget& operator=(SwapchainFrameTarget&&) = delete;

        [[nodiscard]] Extent2D CurrentExtent() const override;
        [[nodiscard]] bool ResizeFinished() const override;
        // Out of date or suboptimal: recreates the swapchain, but only once last_submit has passed, so it waits for
        // no frame of the graph. Until then, and while the window has no area, it acquires nothing.
        [[nodiscard]] std::optional<AcquiredTarget> TryAcquire(const rhi::GpuWait& last_submit) override;
        void Present(rhi::BinarySemaphoreHandle render_done) override;
        [[nodiscard]] rhi::ImageLayout FinalLayout() const override;

        // The count of swapchain recreations, for tests that resize the window
        [[nodiscard]] uint32_t RecreateCount() const noexcept;

    private:
        [[nodiscard]] bool recreateWhenIdle(const rhi::GpuWait& last_submit);

        const rhi::Device& device;
        Swapchain& swapchain;
        std::vector<rhi::BinarySemaphore> acquireSemaphores;
        uint32_t nextSemaphore{ 0u };
        uint32_t acquiredIndex{ 0u };
        bool needsRecreate{ false };
        uint32_t recreateCount{ 0u };
    };

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_SWAPCHAIN_FRAME_TARGET_HPP
