#include "PlatformSurface.hpp"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cassert>
#include <utility>


/**
 * Verifies that the physical device supports presentation to this surface.
 * This should be called before attempting to present to the surface.
 * \param physicalDevice The physical device to check
 * \param surface The surface to check presentation support for
 * \return VK_TRUE if presentation is supported, VK_FALSE otherwise
 */
VkBool32 VerifyPresentationSupport(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    // Check presentation support across the first few queue families
    // Most devices will have presentation support in the first queue family
    VkBool32 presentSupport = VK_FALSE;
    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < 3; ++queueFamilyIndex)
    {
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queueFamilyIndex, surface, &presentSupport);
        if (presentSupport == VK_TRUE)
        {
            break;
        }
    }
    return presentSupport;
}

struct PlatformSurfaceImpl
{
    PlatformSurfaceImpl(const uint64_t vk_instance_handle, const uint64_t vk_physical_device_handle, void* window) noexcept :
        Instance(reinterpret_cast<VkInstance>(vk_instance_handle)),
        PhysicalDevice(reinterpret_cast<VkPhysicalDevice>(vk_physical_device_handle)),
        Window(reinterpret_cast<GLFWwindow*>(window)),
        Surface(VK_NULL_HANDLE)
    {}

    ~PlatformSurfaceImpl()
    {
        DestroySurface();
    }

    void CreateSurface()
    {
        VkResult result = glfwCreateWindowSurface(Instance, Window, nullptr, &Surface);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface!");
        }

        // Verify that the physical device supports presentation to this surface
        if (!VerifyPresentationSupport(PhysicalDevice, Surface))
        {
            throw std::runtime_error("Physical device does not support presentation to this surface!");
        }
    }

    void DestroySurface()
    {
        if (Surface != VK_NULL_HANDLE && Instance != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(Instance, Surface, nullptr);
            Surface = VK_NULL_HANDLE;
        }
    }

    void ClearHandles()
    {
        Instance = VK_NULL_HANDLE;
        PhysicalDevice = VK_NULL_HANDLE;
        Window = nullptr;
        Surface = VK_NULL_HANDLE;
    }

    VkInstance Instance{ VK_NULL_HANDLE };
    VkPhysicalDevice PhysicalDevice{ VK_NULL_HANDLE };
    GLFWwindow* Window{ nullptr };
    VkSurfaceKHR Surface{ VK_NULL_HANDLE };
};

PlatformSurface::PlatformSurface(const uint64_t vk_instance_handle, const uint64_t vk_physical_device_handle, void* window) noexcept : 
    impl(std::make_unique<PlatformSurfaceImpl>(vk_instance_handle, vk_physical_device_handle, window))
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
    return reinterpret_cast<uint64_t>(impl->Surface);
}

void PlatformSurface::CreateSurface()
{
    impl->CreateSurface();
}

void PlatformSurface::DestroySurface()
{
    impl->DestroySurface();
}
