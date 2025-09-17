#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SURFACE_HPP
#define DIAMOND_DOGS_PLATFORM_SURFACE_HPP
#include <memory>

struct PlatformSurfaceImpl;

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

    PlatformSurface(const uint64_t vkInstanceHandle, const uint64_t vkPhysicalDeviceHandle, void* window) noexcept;
    ~PlatformSurface();

    /**
     * Destroys and recreates the surface. Required during swapchain recreation.
     */
    void Recreate();

    /**
     * Gets the underlying Vulkan surface handle.
     * \return Reference to the VkSurfaceKHR handle, as a uint64_t
     */
    const uint64_t GetVkSurface() const noexcept;

private:
    void CreateSurface();
    void DestroySurface();
    // just holds the Vulkan objects, to avoid leaking Vulkan headers
    std::unique_ptr<PlatformSurfaceImpl> impl;
};

#endif // DIAMOND_DOGS_PLATFORM_SURFACE_HPP
