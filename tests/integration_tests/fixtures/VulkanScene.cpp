#include "VulkanScene.hpp"
#include "Device.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "Queue.hpp"
#include "RhiTypes.hpp"
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
    if (swapchain != nullptr)
    {
        frameLimiter.SetTargetFramerate(static_cast<double>(swapchain->RefreshRate()));
    }
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
    imageAvailableSemaphores.reserve(numFramebuffers);
    renderFinishedSemaphores.reserve(numFramebuffers);
    inFlightFences.reserve(numFramebuffers);

    for (uint32_t i = 0; i < numFramebuffers; ++i)
    {
        const BinarySemaphore& imageAvailable = imageAvailableSemaphores.emplace_back(device->Handle());
        if (!imageAvailable.Handle().IsValid())
        {
            throw std::runtime_error("Failed to create the frame synchronization objects");
        }
        RhiSystem::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, imageAvailable.Handle().Get(), std::format("ImageAvailableSemaphore_{}", i).c_str());

        const BinarySemaphore& renderFinished = renderFinishedSemaphores.emplace_back(device->Handle());
        if (!renderFinished.Handle().IsValid())
        {
            throw std::runtime_error("Failed to create the frame synchronization objects");
        }
        RhiSystem::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, renderFinished.Handle().Get(), std::format("RenderFinishedSemaphore_{}", i).c_str());

        const Fence& inFlight = inFlightFences.emplace_back(device->Handle(), CreateFenceSignaled{ true });
        if (!inFlight.Handle().IsValid())
        {
            throw std::runtime_error("Failed to create the frame synchronization objects");
        }
        RhiSystem::SetObjectName(VK_OBJECT_TYPE_FENCE, inFlight.Handle().Get(), std::format("InFlightFence_{}", i).c_str());
    }

    firstFrame.resize(numFramebuffers, true);
}

void VulkanScene::destroyFrameSyncObjects()
{
    for (const rhi::Fence& fence : inFlightFences)
    {
        if (!fence.Wait(1000000000))
        {
            throw std::runtime_error("A frame fence did not signal");
        }
    }

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    firstFrame.clear();
    currentFrame = 0;
}

void VulkanScene::beginFrame()
{
    if (!inFlightFences[currentFrame].Wait(1000000000))
    {
        throw std::runtime_error("The frame fence did not signal");
    }
}

void VulkanScene::resetFrameFence()
{
    inFlightFences[currentFrame].Reset();
}

void VulkanScene::submitFrame(const rhi::CommandBufferHandle& command_buffer)
{
    const rhi::SemaphoreSubmitInfo imageAvailable{ imageAvailableSemaphores[currentFrame].Handle(), rhi::PipelineStage::ColorAttachmentOutput };
    const rhi::SemaphoreSubmitInfo renderFinished{ renderFinishedSemaphores[currentAcquiredImage].Handle(), rhi::PipelineStage::AllGraphics };
    const rhi::SubmitInfo submit{ std::span{ &imageAvailable, 1 }, std::span{ &command_buffer, 1 }, std::span{ &renderFinished, 1 } };
    RhiAssert(rhi::QueueSubmit(device->GetGraphicsQueue(0), submit, inFlightFences[currentFrame].Handle()));
}

void VulkanScene::recreateSwapchain()
{
    device->WaitDeviceIdle();
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
    if (swapchain != nullptr)
    {
        frameLimiter.SetTargetFramerate(static_cast<double>(swapchain->RefreshRate()));
        frameLimiter.Reset();
    }
}

void VulkanScene::limitFrame()
{
    frameLimiter.WaitNextFrame();
}

bool VulkanScene::acquireImage()
{
    VkSemaphore currAcquireSemaphore = imageAvailableSemaphores[currentFrame].Handle().As<VkSemaphore>();
    VkSwapchainKHR vkSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain->GetNativeHandle());
    // we should only ever be using the first device, and this is a bitmask so that means it can't be zero (its not an index)
    constexpr static uint32_t k_RootDeviceMask = 0x1;

    const VkAcquireNextImageInfoKHR acquireInfo
    {
        VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
        nullptr,
        vkSwapchain,
        1000000000,
        currAcquireSemaphore,
        VK_NULL_HANDLE,
        k_RootDeviceMask
    };

    const VkResult result = vkAcquireNextImage2KHR(vkDevice, &acquireInfo, &currentAcquiredImage);
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
    VkResult presentResults[1]{ VK_SUCCESS };
    VkSemaphore currRenderCompleteSemaphore = renderFinishedSemaphores[currentAcquiredImage].Handle().As<VkSemaphore>();
    VkSwapchainKHR vkSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain->GetNativeHandle());

    const VkPresentInfoKHR presentInfo
    {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,
        &currRenderCompleteSemaphore,
        1,
        &vkSwapchain,
        &currentAcquiredImage,
        presentResults
    };

    VkQueue graphicsQueue = device->GetGraphicsQueue(0).As<VkQueue>();
    const VkResult result = vkQueuePresentKHR(graphicsQueue, &presentInfo);
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
