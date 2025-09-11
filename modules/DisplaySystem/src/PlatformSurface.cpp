#include "PlatformSurface.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <cassert>
#include <utility>

PlatformSurface::PlatformSurface(VkInstance instance, VkPhysicalDevice physicalDevice, void* window) noexcept : 
    instance(instance),
    physicalDevice(physicalDevice),
    window(reinterpret_cast<GLFWwindow*>(window)),
    surface(VK_NULL_HANDLE)
{
    CreateSurface();
}

PlatformSurface::PlatformSurface(PlatformSurface&& other) noexcept :
    instance(other.instance), 
    physicalDevice(other.physicalDevice),
    window(other.window), 
    surface(other.surface)
{
    // Reset the moved-from object
    other.instance = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
    other.window = nullptr;
    other.surface = VK_NULL_HANDLE;
}

PlatformSurface& PlatformSurface::operator=(PlatformSurface&& other) noexcept
{
    if (this != &other)
    {
        // Destroy current resources
        DestroySurface();

        // Move data from other
        instance = other.instance;
        physicalDevice = other.physicalDevice;
        window = other.window;
        surface = other.surface;

        // Reset the moved-from object
        other.instance = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
        other.window = nullptr;
        other.surface = VK_NULL_HANDLE;
    }
    return *this;
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

const VkSurfaceKHR& PlatformSurface::GetVkSurface() const noexcept
{
    return surface;
}

VkBool32 PlatformSurface::VerifyPresentationSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
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

void PlatformSurface::CreateSurface()
{
    assert(window != nullptr);
    assert(instance != VK_NULL_HANDLE);

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

void PlatformSurface::DestroySurface()
{
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
}
