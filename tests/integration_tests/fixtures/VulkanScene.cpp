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
#include <array>
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

rhi::TimelineSemaphoreHandle VulkanScene::FrameTimeline() const noexcept
{
    return frameTimeline != nullptr ? frameTimeline->Handle() : rhi::TimelineSemaphoreHandle{};
}

uint64_t VulkanScene::FrameValueBeingRecorded() const noexcept
{
    return frameValue;
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

    frameTimeline = std::make_unique<TimelineSemaphore>(device->Handle());
    if (!frameTimeline->Handle().IsValid())
    {
        throw std::runtime_error("Failed to create the frame synchronization objects");
    }

    RhiSystem::SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, frameTimeline->Handle().Get(), "FrameTimeline");
    frameValue = 1u;

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
    }

    firstFrame.resize(numFramebuffers, true);
}

void VulkanScene::destroyFrameSyncObjects()
{
    // the last submitted frame signaled frameValue - 1
    if (!waitForFrameValue(frameValue - 1u))
    {
        throw std::runtime_error("The frame timeline did not reach the last submitted frame");
    }

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    frameTimeline.reset();
    firstFrame.clear();
    currentFrame = 0;
}

void VulkanScene::beginFrame()
{
    // the frame numFramebuffers before this one used the same slot
    if (frameValue > numFramebuffers && !waitForFrameValue(frameValue - numFramebuffers))
    {
        throw std::runtime_error("The frame timeline did not signal");
    }
}

bool VulkanScene::waitForFrameValue(uint64_t value) const
{
    if (value == 0u)
    {
        return true;
    }

    const VkSemaphore timeline = frameTimeline->Handle().As<VkSemaphore>();
    const VkSemaphoreWaitInfo waitInfo
    {
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        nullptr,
        0u,
        1u,
        &timeline,
        &value
    };

    return vkWaitSemaphores(vkDevice, &waitInfo, 1000000000) == VK_SUCCESS;
}

void VulkanScene::submitFrame(const rhi::CommandBufferHandle& command_buffer)
{
    const rhi::SemaphoreSubmitInfo imageAvailable{ imageAvailableSemaphores[currentFrame].Handle(), rhi::PipelineStage::ColorAttachmentOutput };
    const std::array<rhi::SemaphoreSubmitInfo, 2> signals
    {
        rhi::SemaphoreSubmitInfo{ renderFinishedSemaphores[currentAcquiredImage].Handle(), rhi::PipelineStage::AllGraphics },
        rhi::SemaphoreSubmitInfo{ frameTimeline->Handle(), frameValue, rhi::PipelineStage::AllGraphics }
    };
    const rhi::SubmitInfo submit{ std::span{ &imageAvailable, 1 }, std::span{ &command_buffer, 1 }, std::span{ signals } };
    RhiAssert(rhi::QueueSubmit(device->GetGraphicsQueue(0), submit, rhi::FenceHandle{}));
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
    // endFrame runs only after a submit, so every value up to frameValue - 1 has a signal
    ++frameValue;
}
