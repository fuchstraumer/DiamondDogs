#pragma once
#ifndef DIAMOND_DOGS_IMGUI_MODULE_HPP
#define DIAMOND_DOGS_IMGUI_MODULE_HPP
#include "ImageDataFormats.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <filesystem>

struct ImFont;

namespace rhi
{
    class Device;
    class Instance;
}

struct ImGuiFontSet
{
    ImFont* Body{ nullptr };
    ImFont* BodyBold{ nullptr };
    ImFont* BodyItalic{ nullptr };
    ImFont* HeaderBlinker{ nullptr };
    ImFont* SectionBlinker{ nullptr };
    ImFont* HeaderMichroma{ nullptr };
    ImFont* SectionMichroma{ nullptr };
};

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
    /** @brief Optional root folder of the engine's TTF fonts (assets/fonts). Empty keeps ImGui's default font. */
    std::filesystem::path FontDirectory{};
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
    /** @brief Returns the loaded font slots. Every slot is non-null: missing files fall back to ImGui's default font. */
    [[nodiscard]] const ImGuiFontSet& Fonts() const noexcept;

private:
    ImGuiFontSet fonts{};
};

#endif // !DIAMOND_DOGS_IMGUI_MODULE_HPP
