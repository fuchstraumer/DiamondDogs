#include "PlatformSurface.hpp"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cassert>
#include <utility>

struct PlatformSurfaceImpl
{
    PlatformSurfaceImpl(const uint64_t vkInstanceHandle, const uint64_t vkPhysicalDeviceHandle, void* window) noexcept :
        instance(reinterpret_cast<VkInstance>(vkInstanceHandle)),
        physicalDevice(reinterpret_cast<VkPhysicalDevice>(vkPhysicalDeviceHandle)),
        window(reinterpret_cast<GLFWwindow*>(window)),
        surface(VK_NULL_HANDLE)
    {}

    ~PlatformSurfaceImpl()
    {
        DestroySurface();
    }

    void CreateSurface()
    {
        VkResult result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface!");
        }

        // Verify that the physical device supports presentation to this surface
        if (!VerifyPresentationSupport(physicalDevice, surface))
        {
            throw std::runtime_error("Physical device does not support presentation to this surface!");
        }
    }

    void DestroySurface()
    {
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
    }

    void ClearHandles()
    {
        instance = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
        window = nullptr;
        surface = VK_NULL_HANDLE;
    }

    VkInstance instance{ VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
    GLFWwindow* window{ nullptr };
    VkSurfaceKHR surface{ VK_NULL_HANDLE };
};

/**
 * Verifies that the physical device supports presentation to this surface.
 * This should be called before attempting to present to the surface.
 * \param physicalDevice The physical device to check
 * \param surface The surface to check presentation support for
 * \return VK_TRUE if presentation is supported, VK_FALSE otherwise
 */
VkBool32 VerifyPresentationSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    // Check presentation support across the first few queue families
    // Most devices will have presentation support in the first queue family
    VkBool32 presentSupport = VK_FALSE;
    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < 3; ++queueFamilyIndex)
    {
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &presentSupport);
        if (presentSupport == VK_TRUE)
        {
            break;
        }
    }
    return presentSupport;
}

PlatformSurface::PlatformSurface(const uint64_t vkInstanceHandle, const uint64_t vkPhysicalDeviceHandle, void* window) noexcept : 
    impl(std::make_unique<PlatformSurfaceImpl>(vkInstanceHandle, vkPhysicalDeviceHandle, window))
{
    CreateSurface();
}

PlatformSurface::~PlatformSurface()
{
    DestroySurface();
}

void PlatformSurface::Recreate()
{
    DestroySurface();
    CreateSurface();
}

const uint64_t PlatformSurface::GetVkSurface() const noexcept
{
    return reinterpret_cast<uint64_t>(impl->surface);
}

void PlatformSurface::CreateSurface()
{
    impl->CreateSurface();
}

void PlatformSurface::DestroySurface()
{
    impl->DestroySurface();
}
