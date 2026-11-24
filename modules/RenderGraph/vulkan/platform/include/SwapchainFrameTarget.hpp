#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_SWAPCHAIN_FRAME_TARGET_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_SWAPCHAIN_FRAME_TARGET_HPP
#include "FrameTarget.hpp"
#include <cstdint>
#include <optional>

class Swapchain;
enum class PresentMode : uint8_t;
enum class PresentModeChange : uint8_t;

namespace rhi
{
    class Device;
}

// loom_vulkan_platform: the swapchain as a Loom frame target (step 7 Q6). It lives in its own target so that
// loom_vulkan does not link PlatformSystem. PlatformSystem's Swapchain owns the acquire, the present, every semaphore
// and the present fences (presentation plan P1); this class only adapts it to FrameTarget and decides when to recreate.
namespace loom
{

    class SwapchainFrameTarget final : public FrameTarget
    {
    public:
        SwapchainFrameTarget(const rhi::Device& device, Swapchain& swapchain);
        ~SwapchainFrameTarget() override;
        SwapchainFrameTarget(const SwapchainFrameTarget&) = delete;
        SwapchainFrameTarget& operator=(const SwapchainFrameTarget&) = delete;
        SwapchainFrameTarget(SwapchainFrameTarget&&) = delete;
        SwapchainFrameTarget& operator=(SwapchainFrameTarget&&) = delete;

        [[nodiscard]] Extent2D CurrentExtent() const override;
        [[nodiscard]] bool ResizeFinished() const override;
        // Out of date or suboptimal: recreates the swapchain, but only once last_submit has passed (the submits that
        // waited on its acquire semaphores finished). With swapchain maintenance1 that is the whole wait; without it,
        // the device must also go idle. Until then, and while the window has no area, it acquires nothing.
        [[nodiscard]] std::optional<AcquiredTarget> TryAcquire(const rhi::GpuWait& last_submit) override;
        void Present() override;
        [[nodiscard]] bool Release() override;
        [[nodiscard]] rhi::ImageLayout FinalLayout() const override;
        [[nodiscard]] bool WaitForPresents(uint64_t timeout_ns) override;

        // Changes the present mode: at the next present when the swapchain can switch to it, else through a recreate
        // that the next TryAcquire makes (presentation plan P2)
        [[nodiscard]] PresentModeChange SetPresentMode(PresentMode mode);

        // The count of swapchain recreations, for tests that resize the window
        [[nodiscard]] uint32_t RecreateCount() const noexcept;

    private:
        [[nodiscard]] bool recreateWhenIdle(const rhi::GpuWait& last_submit);

        const rhi::Device& device;
        Swapchain& swapchain;
        uint32_t acquiredIndex{ 0u };
        bool needsRecreate{ false };
        uint32_t recreateCount{ 0u };
    };

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_SWAPCHAIN_FRAME_TARGET_HPP
