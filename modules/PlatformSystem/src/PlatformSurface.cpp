#include "PlatformSurface.hpp"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <memory>

struct PlatformSurfaceImpl
{
    VkInstance Instance{ VK_NULL_HANDLE };
    GLFWwindow* Window{ nullptr };
    VkSurfaceKHR Surface{ VK_NULL_HANDLE };
};

PlatformSurface::PlatformSurface(uint64_t vk_instance_handle, void* window) noexcept :
    impl(std::make_unique<PlatformSurfaceImpl>())
{
    impl->Instance = reinterpret_cast<VkInstance>(vk_instance_handle);
    impl->Window = static_cast<GLFWwindow*>(window);
    createSurface();
}

PlatformSurface::~PlatformSurface()
{
    destroySurface();
}

bool PlatformSurface::Recreate() noexcept
{
    destroySurface();
    createSurface();
    return IsValid();
}

bool PlatformSurface::IsValid() const noexcept
{
    return impl->Surface != VK_NULL_HANDLE;
}

uint64_t PlatformSurface::GetVkSurface() const noexcept
{
    return reinterpret_cast<uint64_t>(impl->Surface);
}

void PlatformSurface::createSurface() noexcept
{
    const VkResult result = glfwCreateWindowSurface(impl->Instance, impl->Window, nullptr, &impl->Surface);

    if (result != VK_SUCCESS)
    {
        impl->Surface = VK_NULL_HANDLE;
    }
}

void PlatformSurface::destroySurface() noexcept
{
    if (impl->Surface == VK_NULL_HANDLE)
    {
        return;
    }

    vkDestroySurfaceKHR(impl->Instance, impl->Surface, nullptr);
    impl->Surface = VK_NULL_HANDLE;
}
