#include "VulkanScene.hpp"
#include "Device.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "Swapchain.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ratio>
#include <stdexcept>
#include <thread>
#include <format>
#include <string>

VulkanScene::VulkanScene(rhi::RhiSystem* rhi_system, PlatformWindowSystem* platform_system)
    : rhiSystem(rhi_system), platformSystem(platform_system), swapchain(platform_system->GetActiveSwapchain())
{
    currentFrame = 0;
    currentAcquiredImage = 0;
    device = rhiSystem->GetDevice();
    vkDevice = device->Handle().As<VkDevice>();
    vkPhysicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();

    numFramebuffers = swapchain->ImageCount();
    limiterA = std::chrono::system_clock::now();
    limiterB = std::chrono::system_clock::now();
}

VulkanScene::~VulkanScene() {}

void VulkanScene::Render(void* user_data)
{
    beginFrame();
    if (!acquireImage())
    {
        recreateSwapchain();
        return;
    }
    // reset only once a submit will follow: an acquire that fails would leave the fence unsignaled forever
    resetFrameFence();
    update();
    recordCommands();
    draw();
    present();
    limitFrame();
    endFrame();
    if (swapchainOutOfDate)
    {
        recreateSwapchain();
    }
}

uint32_t VulkanScene::SwapchainRecreationCount() const noexcept
{
    return swapchainRecreationCount;
}

size_t VulkanScene::CurrentFramebufferIdx() const
{
    return static_cast<size_t>(currentAcquiredImage);
}

void VulkanScene::createFrameSyncObjects()
{
    using namespace rhi;
    constexpr static VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
    constexpr static VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
    imageAvailableSemaphores.resize(numFramebuffers);
    renderFinishedSemaphores.resize(numFramebuffers);
    inFlightFences.resize(numFramebuffers);

    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        Result result = FromVulkan(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]));
        RhiAssert(result);
        
        RhiSystem::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)imageAvailableSemaphores[i], std::format("ImageAvailableSemaphore_{}", i).c_str());

        result = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
        RhiAssert(result);

        RhiSystem::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)renderFinishedSemaphores[i], std::format("RenderFinishedSemaphore_{}", i).c_str());

        result = vkCreateFence(vkDevice, &fenceInfo, nullptr, &inFlightFences[i]);
        RhiAssert(result);

        RhiSystem::SetObjectName(VK_OBJECT_TYPE_FENCE, (uint64_t)inFlightFences[i], std::format("InFlightFence_{}", i).c_str());
    }

    firstFrame.resize(numFramebuffers, true);
}

void VulkanScene::destroyFrameSyncObjects()
{
    for (size_t i = 0; i < numFramebuffers; ++i)
    {
        vkDestroySemaphore(vkDevice, imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(vkDevice, renderFinishedSemaphores[i], nullptr);
        VkFence currFence = inFlightFences[i];
        rhi::Result result = vkWaitForFences(vkDevice, 1, &currFence, VK_TRUE, 1000000000);
        RhiAssert(result);
        vkDestroyFence(vkDevice, inFlightFences[i], nullptr);
    }

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    firstFrame.clear();
    currentFrame = 0;
}

void VulkanScene::beginFrame()
{
    VkDevice vkDevice = device->Handle().As<VkDevice>();
    VkFence currFence = inFlightFences[currentFrame];
    rhi::Result result = vkWaitForFences(vkDevice, 1, &currFence, VK_TRUE, 1000000000);
    RhiAssert(result);
}

void VulkanScene::resetFrameFence()
{
    VkFence currFence = inFlightFences[currentFrame];
    rhi::Result result = vkResetFences(vkDevice, 1, &currFence);
    RhiAssert(result);
}

void VulkanScene::recreateSwapchain()
{
    rhi::Result result = vkDeviceWaitIdle(vkDevice);
    RhiAssert(result);
    const SwapchainRecreateResult recreateResult = platformSystem->RecreateSwapchain();
    if (recreateResult == SwapchainRecreateResult::SurfaceHasZeroArea)
    {
        // minimized: keep the flag, and try again next frame
        swapchainOutOfDate = true;
        return;
    }
    if (recreateResult != SwapchainRecreateResult::Success)
    {
        throw std::runtime_error("Swapchain recreation failed");
    }
    swapchainOutOfDate = false;
    ++swapchainRecreationCount;
    // the new images start in UNDEFINED
    std::ranges::fill(firstFrame, true);
    onSwapchainRecreated();
}

void VulkanScene::onSwapchainRecreated()
{
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

bool VulkanScene::acquireImage()
{
    VkSemaphore currAcquireSemaphore = imageAvailableSemaphores[currentFrame];
    VkSwapchainKHR vkSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain->GetNativeHandle());
    // we should only ever be using the first device, and this is a bitmask so that means it can't be zero (its not an index)
    constexpr static uint32_t RootDeviceMask = 0x1;

    const VkAcquireNextImageInfoKHR acquire_info
    {
        VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
        nullptr,
        vkSwapchain,
        1000000000,
        currAcquireSemaphore,
        VK_NULL_HANDLE,
        RootDeviceMask
    };

    const VkResult result = vkAcquireNextImage2KHR(vkDevice, &acquire_info, &currentAcquiredImage);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return false;
    }
    // suboptimal still acquired an image and signals the semaphore, so this frame goes on
    if (result == VK_SUBOPTIMAL_KHR)
    {
        swapchainOutOfDate = true;
        return true;
    }
    RhiAssert(rhi::Result(result));
    return true;
}

void VulkanScene::present()
{
    VkResult present_results[1]{ VK_SUCCESS };
    VkSemaphore currRenderCompleteSemaphore = renderFinishedSemaphores[currentFrame];
    VkSwapchainKHR vkSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain->GetNativeHandle());

    const VkPresentInfoKHR present_info
    {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,
        &currRenderCompleteSemaphore,
        1,
        &vkSwapchain,
        &currentAcquiredImage,
        present_results
    };

    VkQueue GraphicsQueue = device->GetGraphicsQueue(0).As<VkQueue>();
    const VkResult result = vkQueuePresentKHR(GraphicsQueue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        swapchainOutOfDate = true;
        return;
    }
    RhiAssert(rhi::Result(result));
}

void VulkanScene::endFrame()
{
    if (firstFrame[currentFrame])
    {
        firstFrame[currentFrame] = false;
    }

    currentFrame = (currentFrame + 1) % numFramebuffers;
}
