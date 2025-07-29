#include "VulkanScene.hpp"
#include "LogicalDevice.hpp"
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
    currentFrameBuffer = 0;
    numFramebuffers = 0;
    limiterA = std::chrono::system_clock::now();
    limiterB = std::chrono::system_clock::now();
}

VulkanScene::~VulkanScene() {}

void VulkanScene::Render(void* user_data)
{
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
    return static_cast<size_t>(currentFrameBuffer);
}

void VulkanScene::createSemaphores()
{
    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        imageAcquireSemaphores.emplace_back(std::make_unique<vpr::Semaphore>(vprObjects.device->vkHandle()));
        std::string semaphore_name = std::format("ImageAcquireSemaphore_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)imageAcquireSemaphores.back()->vkHandle(), semaphore_name.c_str());
        renderCompleteSemaphores.emplace_back(std::make_unique<vpr::Semaphore>(vprObjects.device->vkHandle()));
        semaphore_name = std::format("RenderCompleteSemaphore_{}", i);
        RenderingContext::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)renderCompleteSemaphores.back()->vkHandle(), semaphore_name.c_str());
    }
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
        &currentFrameBuffer);
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
        &currentFrameBuffer,
        present_results
    };

    VkResult result = vkQueuePresentKHR(vprObjects.device->GraphicsQueue(), &present_info);
    VkAssert(result);

}

void VulkanScene::endFrame()
{
    currentFrame = (currentFrame + 1) % numFramebuffers;
}
