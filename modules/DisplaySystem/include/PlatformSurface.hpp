#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SURFACE_HPP
#define DIAMOND_DOGS_PLATFORM_SURFACE_HPP
#include <vulkan/vulkan_core.h>

struct GLFWwindow;

/**
 * Platform abstraction for Vulkan surface creation and management.
 * Currently wraps VkSurfaceKHR with GLFW support, designed to potentially
 * support other graphics APIs in the future through dynamic dispatch.
 */
class PlatformSurface
{
    // Non-copyable since it represents a unique Vulkan surface
    PlatformSurface(const PlatformSurface&) = delete;
    PlatformSurface& operator=(const PlatformSurface&) = delete;

public:
    /**
     * Creates a platform surface for the given Vulkan instance and physical device.
     * \param instance The Vulkan instance handle
     * \param physicalDevice The Vulkan physical device handle
     * \param window Pointer to the platform window (currently GLFWwindow*)
     */
    PlatformSurface(VkInstance instance, VkPhysicalDevice physicalDevice, void* window) noexcept;

    PlatformSurface(PlatformSurface&& other) noexcept;
    PlatformSurface& operator=(PlatformSurface&& other) noexcept;

    // Rule of 5: Destructor
    ~PlatformSurface();

    /**
     * Destroys and recreates the surface. Required during swapchain recreation.
     */
    void Recreate();

    /**
     * Gets the underlying Vulkan surface handle.
     * \return Reference to the VkSurfaceKHR handle
     */
    const VkSurfaceKHR& GetVkSurface() const noexcept;

    /**
     * Verifies that the physical device supports presentation to this surface.
     * This should be called before attempting to present to the surface.
     * \param physicalDevice The physical device to check
     * \param surface The surface to check presentation support for
     * \return VK_TRUE if presentation is supported, VK_FALSE otherwise
     */
    static VkBool32 VerifyPresentationSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

private:
    void CreateSurface();
    void DestroySurface();

    VkInstance instance{ VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
    GLFWwindow* window{ nullptr };
    VkSurfaceKHR surface{ VK_NULL_HANDLE };
};

#endif // DIAMOND_DOGS_PLATFORM_SURFACE_HPP
