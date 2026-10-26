#pragma once
#ifndef VULKAN_SCENE_TEST_FIXTURE_HPP
#define VULKAN_SCENE_TEST_FIXTURE_HPP
#include "Fence.hpp"
#include "RhiHandle.hpp"
#include "Semaphore.hpp"
#include "threading/FrameLimiter.hpp"
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <chrono>
#include <memory>
#include <vector>

namespace rhi
{
    class RhiSystem;
    class Device;
}

class PlatformWindowSystem;
class Swapchain;

class VulkanScene
{
protected:
    virtual ~VulkanScene();
    VulkanScene(const VulkanScene&) = delete;
    VulkanScene& operator=(const VulkanScene&) = delete;
public:
    VulkanScene(rhi::RhiSystem* rhiSystem, PlatformWindowSystem* platformSystem);

    virtual void Initialize(void* user_data) = 0;
    virtual void Destroy() = 0;
    virtual void Render(void* user_data);
    size_t CurrentFramebufferIdx() const;
    /** @brief Returns how many times the scene recreated the swapchain after an out of date or suboptimal result */
    uint32_t SwapchainRecreationCount() const noexcept;

protected:

    void createFrameSyncObjects();
    void destroyFrameSyncObjects();

    virtual void beginFrame();
    void resetFrameFence();
    virtual void limitFrame();
    virtual void update() = 0;
    /** @brief Returns false when the swapchain is out of date, and no image was acquired */
    virtual bool acquireImage();
    virtual void recordCommands() = 0;
    virtual void draw() = 0;
    virtual void present();
    virtual void endFrame();
    /** @brief Submits the frame's command buffer: waits on the acquire semaphore, signals the present semaphore and the frame fence */
    void submitFrame(const rhi::CommandBufferHandle& command_buffer);
    void recreateSwapchain();
    /** @brief Called after the swapchain was recreated, to rebuild anything sized to it */
    virtual void onSwapchainRecreated();

    bool swapchainOutOfDate{ false };
    uint32_t swapchainRecreationCount{ 0u };

    foundation::FrameLimiter frameLimiter;
    uint32_t currentFrame; // Index of the current frame, but *NOT* the current framebuffer
    uint32_t currentAcquiredImage;
    uint32_t numFramebuffers;
    
    rhi::RhiSystem* rhiSystem{ nullptr };
    rhi::Device* device{ nullptr };
    // shortcutting to Vulkan device handle for convenience, even though we have RHI device
    VkDevice vkDevice{ VK_NULL_HANDLE };
    PlatformWindowSystem* platformSystem{ nullptr };
    const Swapchain* swapchain{ nullptr };
    VkPhysicalDevice vkPhysicalDevice{ VK_NULL_HANDLE };
    std::vector<rhi::BinarySemaphore> imageAvailableSemaphores;
    // indexed by the acquired swapchain image: presentation holds it until that image is acquired again
    std::vector<rhi::BinarySemaphore> renderFinishedSemaphores;
    std::vector<rhi::Fence> inFlightFences;
    std::vector<bool> firstFrame;

};

#endif //!VULKAN_SCENE_TEST_FIXTURE_HPP
