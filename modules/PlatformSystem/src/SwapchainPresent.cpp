#include "SwapchainImpl.hpp"
#include "Device.hpp"
#include "Swapchain.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Acquire, present and release (presentation plan P1). The reuse rules for each semaphore and fence are in the
// PlatformSystem README.
namespace
{

    // Enough presents in flight for any image count we create; a present waits only when the ring comes round to a
    // fence that has not signaled, which a slower display than the CPU makes possible but rare
    constexpr size_t k_PresentFenceCount = 8u;
    // Teardown waits this long for the presents; a present engine that takes longer is broken
    constexpr uint64_t k_TeardownTimeoutNs = 5'000'000'000ull;

    [[nodiscard]] VkSemaphore CreateSemaphore(VkDevice device)
    {
        const VkSemaphoreCreateInfo createInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0u };
        VkSemaphore semaphore = VK_NULL_HANDLE;
        vkCreateSemaphore(device, &createInfo, nullptr, &semaphore);
        return semaphore;
    }

    [[nodiscard]] bool Signaled(VkDevice device, VkFence fence)
    {
        return vkGetFenceStatus(device, fence) == VK_SUCCESS;
    }

}

// Once per Swapchain object, before its first swapchain exists: the swapchain's create info depends on maintenance1
void SwapchainImpl::InitPresentation()
{
    if (PresentQueue != VK_NULL_HANDLE)
    {
        return;
    }

    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(CreateInfo.RhiDevice);
    PresentQueue = device->GetGeneralQueue().As<VkQueue>();
    Maintenance1 = !CorePresentationOnly && device->IsFeatureEnabled(k_SwapchainMaintenance1Feature);

    // the KHR and the EXT entry points are the same function; one of the two extensions is enabled
    if (Maintenance1)
    {
        ReleaseImages = reinterpret_cast<PFN_vkReleaseSwapchainImagesKHR>(vkGetDeviceProcAddr(ParentDevice, "vkReleaseSwapchainImagesKHR"));

        if (ReleaseImages == nullptr)
        {
            ReleaseImages = reinterpret_cast<PFN_vkReleaseSwapchainImagesKHR>(vkGetDeviceProcAddr(ParentDevice, "vkReleaseSwapchainImagesEXT"));
        }

        Maintenance1 = ReleaseImages != nullptr;
    }

    if (!Maintenance1)
    {
        return;
    }

    const VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0u };
    PresentFences.resize(k_PresentFenceCount);

    for (PresentFence& fence : PresentFences)
    {
        vkCreateFence(ParentDevice, &fenceInfo, nullptr, &fence.Fence);
    }
}

void SwapchainImpl::CreatePresentation()
{
    // per swapchain: one render-done semaphore per image, and no image acquired yet
    RenderDone.resize(ImageCount, VK_NULL_HANDLE);

    for (VkSemaphore& semaphore : RenderDone)
    {
        semaphore = CreateSemaphore(ParentDevice);
    }

    ImageAcquireSemaphores.assign(ImageCount, VK_NULL_HANDLE);
}

void SwapchainImpl::RetirePresentation(VkSwapchainKHR old_swapchain)
{
    // the caller's submits that waited on these finished (Recreate's rule), so they are free
    for (VkSemaphore& semaphore : ImageAcquireSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            FreeAcquireSemaphores.push_back(semaphore);
            semaphore = VK_NULL_HANDLE;
        }
    }

    pollPresentation();
    const bool presentsPending = Maintenance1 && completedSerial() < PresentSerial;

    // a present on the old swapchain may still wait on its render-done semaphores
    if (presentsPending)
    {
        Retired.push_back(RetiredSwapchain{ .Swapchain = old_swapchain, .RenderDone = std::move(RenderDone),
                                            .LastSerial = PresentSerial });
        RenderDone.clear();
        return;
    }

    // without maintenance1 the caller idled the device
    for (const VkSemaphore semaphore : RenderDone)
    {
        vkDestroySemaphore(ParentDevice, semaphore, nullptr);
    }

    RenderDone.clear();
    vkDestroySwapchainKHR(ParentDevice, old_swapchain, nullptr);
}

void SwapchainImpl::DestroyPresentation()
{
    if (Maintenance1)
    {
        [[maybe_unused]] const bool finished = WaitForPresents(k_TeardownTimeoutNs);
    }

    for (const ConsumedSemaphore& consumed : Consumed)
    {
        [[maybe_unused]] const VkResult waited = vkWaitForFences(ParentDevice, 1u, &consumed.Fence, VK_TRUE, k_TeardownTimeoutNs);
        FreeAcquireSemaphores.push_back(consumed.Semaphore);
        FreeFences.push_back(consumed.Fence);
    }

    Consumed.clear();

    for (const RetiredSwapchain& retired : Retired)
    {
        for (const VkSemaphore semaphore : retired.RenderDone)
        {
            vkDestroySemaphore(ParentDevice, semaphore, nullptr);
        }

        vkDestroySwapchainKHR(ParentDevice, retired.Swapchain, nullptr);
    }

    Retired.clear();

    for (const VkSemaphore semaphore : RenderDone)
    {
        vkDestroySemaphore(ParentDevice, semaphore, nullptr);
    }

    RenderDone.clear();

    for (const VkSemaphore semaphore : ImageAcquireSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(ParentDevice, semaphore, nullptr);
        }
    }

    ImageAcquireSemaphores.clear();

    for (const VkSemaphore semaphore : FreeAcquireSemaphores)
    {
        vkDestroySemaphore(ParentDevice, semaphore, nullptr);
    }

    FreeAcquireSemaphores.clear();

    for (const PresentFence& fence : PresentFences)
    {
        vkDestroyFence(ParentDevice, fence.Fence, nullptr);
    }

    PresentFences.clear();

    for (const VkFence fence : FreeFences)
    {
        vkDestroyFence(ParentDevice, fence, nullptr);
    }

    FreeFences.clear();
}

SwapchainAcquire SwapchainImpl::TryAcquire(uint64_t timeout_ns)
{
    pollPresentation();
    const VkSemaphore semaphore = takeAcquireSemaphore();
    uint32_t index = 0u;
    const VkResult result = vkAcquireNextImageKHR(ParentDevice, Swapchain, timeout_ns, semaphore, VK_NULL_HANDLE, &index);
    SwapchainAcquireStatus status = SwapchainAcquireStatus::Failed;

    switch (result)
    {
    case VK_SUCCESS:
        status = SwapchainAcquireStatus::Acquired;
        break;
    case VK_SUBOPTIMAL_KHR:
        status = SwapchainAcquireStatus::Suboptimal;
        break;
    case VK_ERROR_OUT_OF_DATE_KHR:
        status = SwapchainAcquireStatus::OutOfDate;
        break;
    case VK_NOT_READY:
        [[fallthrough]];
    case VK_TIMEOUT:
        status = SwapchainAcquireStatus::NotReady;
        break;
    default:
        break;
    }

    const bool acquired = status == SwapchainAcquireStatus::Acquired || status == SwapchainAcquireStatus::Suboptimal;

    // nothing signals the semaphore: it is still free
    if (!acquired)
    {
        FreeAcquireSemaphores.push_back(semaphore);
        return SwapchainAcquire{ .Status = status };
    }

    // the image came back, so the work that waited on its last acquire semaphore finished
    VkSemaphore& previous = ImageAcquireSemaphores[index];

    if (previous != VK_NULL_HANDLE)
    {
        FreeAcquireSemaphores.push_back(previous);
    }

    previous = semaphore;

    // with deferred image memory, a view may be made only once its image was acquired
    if (ImageViews[index] == VK_NULL_HANDLE)
    {
        CreateImageView(index);
    }

    return SwapchainAcquire
    {
        .Status = status,
        .ImageIndex = index,
        .Image = reinterpret_cast<void*>(Images[index]),
        .View = reinterpret_cast<void*>(ImageViews[index]),
        .WaitSemaphore = reinterpret_cast<uint64_t>(semaphore),
        .RenderDoneSemaphore = reinterpret_cast<uint64_t>(RenderDone[index]),
    };
}

SwapchainPresentStatus SwapchainImpl::Present(uint32_t image_index)
{
    pollPresentation();
    VkFence fence = VK_NULL_HANDLE;

    if (Maintenance1)
    {
        fence = takePresentFence();
    }

    // the mode of this present: switchable modes change here, with no recreate (SetPresentMode)
    const bool switchable = Maintenance1 && SwitchableModes.size() > 1u;

    const VkSwapchainPresentModeInfoKHR modeInfo
    {
        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR,
        nullptr,
        1u,
        &PresentMode
    };

    const VkSwapchainPresentFenceInfoKHR fenceInfo
    {
        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
        switchable ? &modeInfo : nullptr,
        1u,
        &fence
    };

    const VkPresentInfoKHR presentInfo
    {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        fence != VK_NULL_HANDLE ? &fenceInfo : nullptr,
        1u,
        &RenderDone[image_index],
        1u,
        &Swapchain,
        &image_index,
        nullptr
    };

    const VkResult result = vkQueuePresentKHR(PresentQueue, &presentInfo);

    switch (result)
    {
    case VK_SUCCESS:
        return SwapchainPresentStatus::Presented;
    case VK_SUBOPTIMAL_KHR:
        return SwapchainPresentStatus::Suboptimal;
    case VK_ERROR_OUT_OF_DATE_KHR:
        return SwapchainPresentStatus::OutOfDate;
    default:
        return SwapchainPresentStatus::Failed;
    }
}

SwapchainReleaseStatus SwapchainImpl::Release(uint32_t image_index)
{
    if (!Maintenance1)
    {
        return SwapchainReleaseStatus::Unsupported;
    }

    const VkReleaseSwapchainImagesInfoKHR releaseInfo
    {
        VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_KHR,
        nullptr,
        Swapchain,
        1u,
        &image_index
    };

    if (ReleaseImages(ParentDevice, &releaseInfo) != VK_SUCCESS)
    {
        return SwapchainReleaseStatus::Failed;
    }

    // the acquire signaled this semaphore and nothing waited on it: an empty submit waits, and its fence frees it
    const VkSemaphore semaphore = ImageAcquireSemaphores[image_index];
    ImageAcquireSemaphores[image_index] = VK_NULL_HANDLE;
    const VkFence fence = takeFence();
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    const VkSubmitInfo submit
    {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        1u,
        &semaphore,
        &waitStage,
        0u,
        nullptr,
        0u,
        nullptr
    };

    if (vkQueueSubmit(PresentQueue, 1u, &submit, fence) != VK_SUCCESS)
    {
        FreeFences.push_back(fence);
        return SwapchainReleaseStatus::Failed;
    }

    Consumed.push_back(ConsumedSemaphore{ .Semaphore = semaphore, .Fence = fence });
    return SwapchainReleaseStatus::Released;
}

bool SwapchainImpl::WaitForPresents(uint64_t timeout_ns)
{
    if (!Maintenance1)
    {
        return false;
    }

    for (PresentFence& fence : PresentFences)
    {
        if (!fence.Pending)
        {
            continue;
        }

        if (vkWaitForFences(ParentDevice, 1u, &fence.Fence, VK_TRUE, timeout_ns) != VK_SUCCESS)
        {
            return false;
        }

        fence.Pending = false;
    }

    pollPresentation();
    return true;
}

PresentModeChange SwapchainImpl::SetPresentMode(VkPresentModeKHR mode)
{
    if (mode == PresentMode)
    {
        return PresentModeChange::Unchanged;
    }

    if (std::ranges::find(Info->presentModes, mode) == Info->presentModes.end() || !PresentModeUsable(mode))
    {
        return PresentModeChange::Unsupported;
    }

    // a later Recreate keeps the mode either way
    CreateInfo.SwapchainPresentMode = FromVkPresentMode(mode);

    if (Maintenance1 && std::ranges::find(SwitchableModes, mode) != SwitchableModes.end())
    {
        PresentMode = mode;
        ++Statistics.PresentModeSwitches;
        return PresentModeChange::NextPresent;
    }

    return PresentModeChange::NeedsRecreate;
}

VkSurfacePresentScalingCapabilitiesKHR SwapchainImpl::queryPresentModeSupport(VkPresentModeKHR mode)
{
    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(CreateInfo.RhiDevice);
    const VkPhysicalDevice physicalDevice = device->GetPhysicalDevice().As<VkPhysicalDevice>();
    VkSurfacePresentModeKHR modeInfo{ VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR, nullptr, mode };

    const VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo
    {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        &modeInfo,
        reinterpret_cast<VkSurfaceKHR>(CreateInfo.VkSurfaceHandle)
    };

    // the first call counts the compatible modes, the second fills them
    VkSurfacePresentModeCompatibilityKHR compatibility{ VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR };
    VkSurfacePresentScalingCapabilitiesKHR scaling{ VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR, &compatibility };
    VkSurfaceCapabilities2KHR capabilities{ VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR, &scaling };
    vkGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, &surfaceInfo, &capabilities);

    SwitchableModes.assign(compatibility.presentModeCount, VK_PRESENT_MODE_MAX_ENUM_KHR);
    compatibility.pPresentModes = SwitchableModes.data();
    vkGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, &surfaceInfo, &capabilities);

    const auto unusable = [this](VkPresentModeKHR other)
    {
        return !PresentModeUsable(other);
    };

    erase_if(SwitchableModes, unusable);

    if (std::ranges::find(SwitchableModes, mode) == SwitchableModes.end())
    {
        SwitchableModes.push_back(mode);
    }

    scaling.pNext = nullptr;
    return scaling;
}

bool SwapchainImpl::PresentModeUsable(VkPresentModeKHR mode) const noexcept
{
    if (mode != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR)
    {
        return true;
    }

    const rhi::Device* device = reinterpret_cast<const rhi::Device*>(CreateInfo.RhiDevice);
    return device->IsFeatureEnabled(k_FifoLatestReadyFeature);
}

// Frees what finished: present fences, consumed semaphores, and old swapchains whose presents all finished
void SwapchainImpl::pollPresentation()
{
    for (PresentFence& fence : PresentFences)
    {
        if (fence.Pending && Signaled(ParentDevice, fence.Fence))
        {
            fence.Pending = false;
        }
    }

    const auto consumedDone = [this](const ConsumedSemaphore& consumed)
    {
        if (!Signaled(ParentDevice, consumed.Fence))
        {
            return false;
        }

        FreeAcquireSemaphores.push_back(consumed.Semaphore);
        FreeFences.push_back(consumed.Fence);
        return true;
    };

    erase_if(Consumed, consumedDone);
    const uint64_t completed = completedSerial();

    const auto retiredDone = [this, completed](const RetiredSwapchain& retired)
    {
        if (retired.LastSerial > completed)
        {
            return false;
        }

        for (const VkSemaphore semaphore : retired.RenderDone)
        {
            vkDestroySemaphore(ParentDevice, semaphore, nullptr);
        }

        vkDestroySwapchainKHR(ParentDevice, retired.Swapchain, nullptr);
        return true;
    };

    erase_if(Retired, retiredDone);
}

// Every present with a serial up to this one finished: the oldest pending present, minus one
uint64_t SwapchainImpl::completedSerial() const noexcept
{
    uint64_t completed = PresentSerial;

    for (const PresentFence& fence : PresentFences)
    {
        if (fence.Pending)
        {
            completed = std::min(completed, fence.Serial - 1u);
        }
    }

    return completed;
}

VkSemaphore SwapchainImpl::takeAcquireSemaphore()
{
    if (FreeAcquireSemaphores.empty())
    {
        return CreateSemaphore(ParentDevice);
    }

    const VkSemaphore semaphore = FreeAcquireSemaphores.back();
    FreeAcquireSemaphores.pop_back();
    return semaphore;
}

VkFence SwapchainImpl::takeFence()
{
    if (!FreeFences.empty())
    {
        const VkFence fence = FreeFences.back();
        FreeFences.pop_back();
        vkResetFences(ParentDevice, 1u, &fence);
        return fence;
    }

    const VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0u };
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(ParentDevice, &fenceInfo, nullptr, &fence);
    return fence;
}

// The next fence of the ring, reset, after the present that last used it finished
VkFence SwapchainImpl::takePresentFence()
{
    PresentFence& fence = PresentFences[NextFence];

    if (fence.Pending)
    {
        [[maybe_unused]] const VkResult waited = vkWaitForFences(ParentDevice, 1u, &fence.Fence, VK_TRUE, k_TeardownTimeoutNs);
    }

    vkResetFences(ParentDevice, 1u, &fence.Fence);
    ++PresentSerial;
    fence.Serial = PresentSerial;
    fence.Pending = true;
    NextFence = (NextFence + 1u) % static_cast<uint32_t>(PresentFences.size());
    return fence.Fence;
}
