#pragma once
#ifndef DIAMOND_DOGS_IMGUI_MODULE_HPP
#define DIAMOND_DOGS_IMGUI_MODULE_HPP
#include "ImageDataFormats.hpp"
#include "RhiHandle.hpp"
#include <cstdint>

namespace rhi
{
    class Device;
    class Instance;
}

struct ImGuiModuleCreateInfo
{
    void* PlatformWindowHandle{ nullptr };
    const rhi::Instance* Instance{ nullptr };
    const rhi::Device* Device{ nullptr };
    /** @brief The packed API version the instance was created with (RhiSystem::GetVulkanApiVersion) */
    uint32_t VulkanApiVersion{ 0u };
    uint32_t MinImageCount{ 0u };
    uint32_t ImageCount{ 0u };
    /** @brief The format of the color attachment that Render draws into, usually the swapchain's */
    ImageFormat ColorFormat{};
    /** @brief Installs the GLFW input callbacks. They chain to the callbacks that were installed before them. */
    bool InstallInputCallbacks{ false };
};

/**
 * @brief Dear ImGui on the official GLFW and Vulkan backends, with dynamic rendering.
 *
 * One instance owns the ImGui context, so at most one may exist at a time. Draw on the thread that owns the window.
 */
class ImGuiModule
{
public:
    explicit ImGuiModule(const ImGuiModuleCreateInfo& create_info);
    ~ImGuiModule();

    ImGuiModule(const ImGuiModule&) = delete;
    ImGuiModule& operator=(const ImGuiModule&) = delete;

    /** @brief Starts an ImGui frame. ImGui calls are valid until Render. */
    void BeginFrame();
    /** @brief Ends the ImGui frame and records its draws. The caller has begun dynamic rendering on cmd, with one color
     *  attachment of the ColorFormat given at creation. */
    void Render(const rhi::CommandBufferHandle& cmd);
    /** @brief Call after the swapchain is recreated with another minimum image count. */
    void SetMinImageCount(uint32_t min_image_count);
};

#endif // !DIAMOND_DOGS_IMGUI_MODULE_HPP
