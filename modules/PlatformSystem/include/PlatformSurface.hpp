#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SURFACE_HPP
#define DIAMOND_DOGS_PLATFORM_SURFACE_HPP
#include <cstdint>
#include <memory>

struct PlatformSurfaceImpl;

/**
 * Platform abstraction for Vulkan surface creation and management.
 * Currently wraps VkSurfaceKHR with GLFW support, designed to potentially
 * support other graphics APIs in the future through dynamic dispatch.
 *
 * The surface needs only the instance and the window, so it can exist before the physical device is chosen. The
 * device planner checks present support (RhiSystem's DeviceStartInfo::PresentSurface).
 */
class PlatformSurface
{
    // Non-copyable since it represents a unique Vulkan surface
    PlatformSurface(const PlatformSurface&) = delete;
    PlatformSurface& operator=(const PlatformSurface&) = delete;

public:

    /** @brief Creates the surface. IsValid() is false when glfwCreateWindowSurface fails. */
    PlatformSurface(uint64_t vk_instance_handle, void* window) noexcept;
    ~PlatformSurface();

    /**
     * Destroys and recreates the surface. Returns IsValid().
     */
    bool Recreate() noexcept;

    [[nodiscard]] bool IsValid() const noexcept;

    /**
     * Gets the underlying Vulkan surface handle.
     * eturn The VkSurfaceKHR handle as a uint64_t, or 0 when creation failed
     */
    [[nodiscard]] uint64_t GetVkSurface() const noexcept;

private:
    void createSurface() noexcept;
    void destroySurface() noexcept;
    // just holds the Vulkan objects, to avoid leaking Vulkan headers
    std::unique_ptr<PlatformSurfaceImpl> impl;
};

#endif // DIAMOND_DOGS_PLATFORM_SURFACE_HPP
