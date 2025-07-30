#include "VulkanScene.hpp"
#include "LogicalDevice.hpp"
#include "Fence.hpp"
#include "Swapchain.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include <vulkan/vulkan.h>
#include <thread>
#include <format>
#include <string>
#include "RenderingContext.hpp"

VulkanScene::VulkanScene()
{
    currentFrame = 0;
    currentAcquiredImage = 0;
    numFramebuffers = 0;
    limiterA = std::chrono::system_clock::now();
    limiterB = std::chrono::system_clock::now();
}

VulkanScene::~VulkanScene() {}

void VulkanScene::Render(void* user_data)
{
    beginFrame();
    acquireImage();
    update();
    recordCommands();
    draw();
    present();
    limitFrame();
    endFrame();
}

size_t VulkanScene::CurrentFrameBufferIdx() const
{
    return static_cast<size_t>(currentAcquiredImage);
}

void VulkanScene::createFrameSyncObjects()
{
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        imageAcquireSemaphores.emplace_back(std::make_unique<vpr::Semaphore>(vprObjects.device->vkHandle()));
        std::string semaphore_name = std::format("ImageAcquireSemaphore_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)imageAcquireSemaphores.back()->vkHandle(), semaphore_name.c_str());
        renderCompleteSemaphores.emplace_back(std::make_unique<vpr::Semaphore>(vprObjects.device->vkHandle()));
        semaphore_name = std::format("RenderCompleteSemaphore_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)renderCompleteSemaphores.back()->vkHandle(), semaphore_name.c_str());
        endFrameFences.emplace_back(std::make_unique<vpr::Fence>(vprObjects.device->vkHandle(), VK_FENCE_CREATE_SIGNALED_BIT));
        semaphore_name = std::format("EndFrameFence_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_FENCE, (uint64_t)endFrameFences.back()->vkHandle(), semaphore_name.c_str());
    }

    firstFrame.resize(numFramebuffers, true);
}

void VulkanScene::destroyFrameSyncObjects()
{
    imageAcquireSemaphores.clear();
    renderCompleteSemaphores.clear();

    for (size_t i = 0; i < endFrameFences.size(); ++i)
    {
        // Wait for each fence and reset it before destruction
        VkFence fence = endFrameFences[i]->vkHandle();
        VkResult result = vkWaitForFences(vprObjects.device->vkHandle(), 1, &fence, VK_TRUE, 1000000000);
        VkAssert(result);
        result = vkResetFences(vprObjects.device->vkHandle(), 1, &fence);
        VkAssert(result);
    }

    endFrameFences.clear();
    firstFrame.clear();
    currentFrame = 0;
}

void VulkanScene::setupSwapchainDebugInfo()
{
    const uint64_t swapchainHandle = reinterpret_cast<uint64_t>(vprObjects.swapchain->vkHandle());
    RenderingContext::SetObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, swapchainHandle, "Swapchain");

    const uint32_t swapchainImageCount = vprObjects.swapchain->ImageCount();
    for (uint32_t i = 0; i < swapchainImageCount; ++i)
    {
        const uint64_t image = reinterpret_cast<uint64_t>(vprObjects.swapchain->Image(i));
        std::string imageName = std::format("SwapchainImage_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE, image, imageName.c_str());
        const uint64_t imageView = reinterpret_cast<uint64_t>(vprObjects.swapchain->ImageView(i));
        std::string imageViewName = std::format("SwapchainImageView_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, imageView, imageViewName.c_str());
    }
}


void VulkanScene::beginFrame()
{
    // this fence was created in signaled state, so the first time through the wait is free. from then on,
    // it makes sure that all previous work using this frames contextual data (command buffers, etc) is done
    VkFence endFrameFence = endFrameFences[currentFrame]->vkHandle();
    VkResult result = vkWaitForFences(vprObjects.device->vkHandle(), 1, &endFrameFence, VK_TRUE, 1000000000);
    VkAssert(result);
    result = vkResetFences(vprObjects.device->vkHandle(), 1, &endFrameFence);
    VkAssert(result);
}

void VulkanScene::limitFrame()
{
    limiterA = std::chrono::system_clock::now();
    std::chrono::duration<double, std::milli> work_time = limiterA - limiterB;
    if (work_time.count() < 16.0)
    {
        std::chrono::duration<double, std::milli> delta_ms(16.0 - work_time.count());
            auto delta_ms_dur = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_dur.count()));
    }
    limiterB = std::chrono::system_clock::now();
}

void VulkanScene::acquireImage()
{
    vpr::Semaphore* imageAcquireSemaphore = imageAcquireSemaphores[currentFrame].get();
    VkResult result = vkAcquireNextImageKHR(
        vprObjects.device->vkHandle(),
        vprObjects.swapchain->vkHandle(),
        1000000000,
        imageAcquireSemaphore->vkHandle(),
        VK_NULL_HANDLE,
        &currentAcquiredImage);
    VkAssert(result);
}

void VulkanScene::present()
{

    VkResult present_results[1]{ VK_SUCCESS };

    vpr::Semaphore* renderCompleteSemaphore = renderCompleteSemaphores[currentFrame].get();

    const VkPresentInfoKHR present_info
    {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,
        &renderCompleteSemaphore->vkHandle(),
        1,
        &vprObjects.swapchain->vkHandle(),
        &currentAcquiredImage,
        present_results
    };

    VkResult result = vkQueuePresentKHR(vprObjects.device->GraphicsQueue(), &present_info);
    VkAssert(result);

}

void VulkanScene::endFrame()
{
    if (firstFrame[currentFrame])
    {
        firstFrame[currentFrame] = false;
    }

    currentFrame = (currentFrame + 1) % numFramebuffers;
}
