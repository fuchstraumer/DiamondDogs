#include "VulkanScene.hpp"
#include "LogicalDevice.hpp"
#include "Swapchain.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include <vulkan/vulkan.h>
#include <thread>

VulkanScene::VulkanScene()
{
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

size_t VulkanScene::CurrentFrameIdx() const
{
    return static_cast<size_t>(currentBuffer);
}

void VulkanScene::createSemaphores()
{
    imageAcquireSemaphore = std::make_unique<vpr::Semaphore>(vprObjects.device->vkHandle());
    renderCompleteSemaphore = std::make_unique<vpr::Semaphore>(vprObjects.device->vkHandle());
}

void VulkanScene::limitFrame()
{
    limiterA = std::chrono::system_clock::now();
    std::chrono::duration<double, std::milli> work_time = limiterA - limiterB;
    if (work_time.count() < 16.0) {
        std::chrono::duration<double, std::milli> delta_ms(16.0 - work_time.count());
            auto delta_ms_dur = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_dur.count()));
    }
    limiterB = std::chrono::system_clock::now();
}

void VulkanScene::acquireImage()
{
    VkResult result = vkAcquireNextImageKHR(vprObjects.device->vkHandle(), vprObjects.swapchain->vkHandle(), UINT64_MAX, imageAcquireSemaphore->vkHandle(), VK_NULL_HANDLE, &currentBuffer);
    VkAssert(result);
}

void VulkanScene::present()
{

    VkResult present_results[1]{ VK_SUCCESS };

    const VkPresentInfoKHR present_info {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,
        &renderCompleteSemaphore->vkHandle(),
        1,
        &vprObjects.swapchain->vkHandle(),
        &currentBuffer,
        present_results
    };

    VkResult result = vkQueuePresentKHR(vprObjects.device->GraphicsQueue(), &present_info);
    VkAssert(result);

}

void VulkanScene::endFrame()
{
}
