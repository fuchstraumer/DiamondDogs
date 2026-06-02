#pragma once
#ifndef VULKAN_SCENE_TEST_FIXTURE_HPP
#define VULKAN_SCENE_TEST_FIXTURE_HPP
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

protected:

    void createFrameSyncObjects();
    void destroyFrameSyncObjects();

    virtual void beginFrame();
    virtual void limitFrame();
    virtual void update() = 0;
    virtual void acquireImage();
    virtual void recordCommands() = 0;
    virtual void draw() = 0;
    virtual void present();
    virtual void endFrame();

    std::chrono::system_clock::time_point limiterA;
    std::chrono::system_clock::time_point limiterB;
    uint32_t currentFrame; // Index of the current frame, but *NOT* the current framebuffer
    uint32_t currentAcquiredImage;
    uint32_t numFramebuffers;
    
    rhi::RhiSystem* rhiSystem{ nullptr };
    rhi::Device* device{ nullptr };
    // shortcutting to Vulkan device handle for convenience, even though we have RHI device
    VkDevice vkDevice{ VK_NULL_HANDLE };
    PlatformWindowSystem* platformSystem{ nullptr };
    Swapchain* swapchain{ nullptr };
    VkPhysicalDevice vkPhysicalDevice{ VK_NULL_HANDLE };
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<bool> firstFrame;

};

#endif //!VULKAN_SCENE_TEST_FIXTURE_HPP
