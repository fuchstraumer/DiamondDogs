#include "ImGuiModule.hpp"
#include "ImageDataFormatsVulkan.hpp"
#include "Device.hpp"
#include "Instance.hpp"
#include "RhiHandle.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "vulkan/vulkan_core.h"
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace
{
    void CheckVkResult(VkResult result);
    [[nodiscard]] ImFont* LoadFontOrFallback(ImFontAtlas& atlas, const std::filesystem::path& file_path, float size_pixels,
                                             float extra_advance_x, ImFont* fallback);
    [[nodiscard]] ImGuiFontSet LoadFontSet(const std::filesystem::path& font_directory);
    void ApplyTelemetryTheme();
}

ImGuiModule::ImGuiModule(const ImGuiModuleCreateInfo& create_info)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // no imgui.ini beside every executable that shows a debug window
    io.IniFilename = nullptr;
    ApplyTelemetryTheme();
    fonts = LoadFontSet(create_info.FontDirectory);

    GLFWwindow* window = static_cast<GLFWwindow*>(create_info.PlatformWindowHandle);
    const bool glfwInitialized = ImGui_ImplGlfw_InitForVulkan(window, create_info.InstallInputCallbacks);
    assert(glfwInitialized);

    const rhi::Device& device = *create_info.Device;
    // the backend deep-copies the format list, so a local is enough
    const VkFormat vkColorFormat = ToVkFormat(create_info.ColorFormat);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = create_info.VulkanApiVersion;
    initInfo.Instance = create_info.Instance->Handle().As<VkInstance>();
    initInfo.PhysicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
    initInfo.Device = device.Handle().As<VkDevice>();
    initInfo.QueueFamily = device.GetQueueFamilyIndices().Graphics;
    initInfo.Queue = device.GetGraphicsQueue(0).As<VkQueue>();
    // the backend creates and owns its descriptor pool
    initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    initInfo.MinImageCount = create_info.MinImageCount;
    initInfo.ImageCount = create_info.ImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1u;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &vkColorFormat;
    initInfo.CheckVkResultFn = CheckVkResult;

    const bool vulkanInitialized = ImGui_ImplVulkan_Init(&initInfo);
    assert(vulkanInitialized);
}

ImGuiModule::~ImGuiModule()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiModule::BeginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiModule::Render(const rhi::CommandBufferHandle& cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd.As<VkCommandBuffer>());
}

void ImGuiModule::SetMinImageCount(uint32_t min_image_count)
{
    ImGui_ImplVulkan_SetMinImageCount(min_image_count);
}

const ImGuiFontSet& ImGuiModule::Fonts() const noexcept
{
    return fonts;
}

namespace
{
    void CheckVkResult(VkResult result)
    {
        if (result != VK_SUCCESS)
        {
            std::cerr << "ImGui Vulkan backend: VkResult " << static_cast<int32_t>(result) << "\n";
            assert(result >= VK_SUCCESS);
        }
    }

    ImFont* LoadFontOrFallback(ImFontAtlas& atlas, const std::filesystem::path& file_path, float size_pixels,
                               float extra_advance_x, ImFont* fallback)
    {
        std::error_code error;
        if (file_path.empty() || !std::filesystem::exists(file_path, error))
        {
            return fallback;
        }

        ImFontConfig config{};
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        config.GlyphExtraAdvanceX = extra_advance_x;

        const std::string pathString = file_path.string();
        ImFont* loaded = atlas.AddFontFromFileTTF(pathString.c_str(), size_pixels, &config);
        if (loaded == nullptr)
        {
            return fallback;
        }

        return loaded;
    }

    ImGuiFontSet LoadFontSet(const std::filesystem::path& font_directory)
    {
        ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
        ImGuiFontSet set{};

        if (!font_directory.empty())
        {
            // the first loaded font becomes ImGui's default font
            set.Body = LoadFontOrFallback(atlas, font_directory / "Inria_Sans" / "InriaSans-Regular.ttf", 16.0f, 0.0f, nullptr);
        }

        if (set.Body == nullptr)
        {
            set.Body = atlas.AddFontDefault();
        }

        set.BodyBold = LoadFontOrFallback(atlas, font_directory / "Inria_Sans" / "InriaSans-Bold.ttf", 15.0f, 0.0f, set.Body);
        set.BodyItalic = LoadFontOrFallback(atlas, font_directory / "Inria_Sans" / "InriaSans-Italic.ttf", 14.0f, 0.0f, set.Body);
        set.HeaderBlinker = LoadFontOrFallback(atlas, font_directory / "Blinker" / "Blinker-Light.ttf", 22.0f, 1.5f, set.BodyBold);
        set.SectionBlinker = LoadFontOrFallback(atlas, font_directory / "Blinker" / "Blinker-SemiBold.ttf", 14.0f, 1.8f, set.BodyBold);
        set.HeaderMichroma = LoadFontOrFallback(atlas, font_directory / "Michroma" / "Michroma-Regular.ttf", 15.0f, 1.2f, set.HeaderBlinker);
        set.SectionMichroma = LoadFontOrFallback(atlas, font_directory / "Michroma" / "Michroma-Regular.ttf", 11.0f, 1.5f, set.SectionBlinker);
        return set;
    }

    void ApplyTelemetryTheme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;

        style.WindowPadding = ImVec2{ 16.0f, 14.0f };
        style.FramePadding = ImVec2{ 8.0f, 5.0f };
        style.ItemSpacing = ImVec2{ 10.0f, 7.0f };
        style.ItemInnerSpacing = ImVec2{ 6.0f, 4.0f };
        style.CellPadding = ImVec2{ 6.0f, 4.0f };
        style.IndentSpacing = 14.0f;
        style.ScrollbarSize = 6.0f;
        style.GrabMinSize = 8.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4{ 0.89f, 0.93f, 0.97f, 1.00f };
        colors[ImGuiCol_TextDisabled] = ImVec4{ 0.45f, 0.53f, 0.62f, 1.00f };
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.04f, 0.06f, 0.10f, 0.90f };
        colors[ImGuiCol_ChildBg] = ImVec4{ 0.06f, 0.09f, 0.14f, 0.65f };
        colors[ImGuiCol_PopupBg] = ImVec4{ 0.05f, 0.08f, 0.13f, 0.96f };
        colors[ImGuiCol_Border] = ImVec4{ 0.58f, 0.74f, 0.90f, 0.16f };
        colors[ImGuiCol_BorderShadow] = ImVec4{ 0.00f, 0.00f, 0.00f, 0.00f };
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.07f, 0.11f, 0.17f, 0.85f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.10f, 0.17f, 0.26f, 0.95f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.13f, 0.23f, 0.35f, 1.00f };
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.04f, 0.06f, 0.10f, 0.95f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.05f, 0.09f, 0.15f, 0.98f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.04f, 0.06f, 0.10f, 0.75f };
        colors[ImGuiCol_MenuBarBg] = ImVec4{ 0.05f, 0.08f, 0.13f, 0.90f };
        colors[ImGuiCol_ScrollbarBg] = ImVec4{ 0.03f, 0.05f, 0.08f, 0.50f };
        colors[ImGuiCol_ScrollbarGrab] = ImVec4{ 0.24f, 0.36f, 0.48f, 0.65f };
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{ 0.00f, 0.78f, 0.98f, 0.80f };
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4{ 0.00f, 0.85f, 1.00f, 1.00f };
        colors[ImGuiCol_CheckMark] = ImVec4{ 0.00f, 0.82f, 1.00f, 1.00f };
        colors[ImGuiCol_SliderGrab] = ImVec4{ 0.00f, 0.78f, 0.98f, 0.90f };
        colors[ImGuiCol_SliderGrabActive] = ImVec4{ 0.25f, 0.90f, 1.00f, 1.00f };
        colors[ImGuiCol_Button] = ImVec4{ 0.08f, 0.13f, 0.20f, 0.85f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.12f, 0.22f, 0.34f, 0.95f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.00f, 0.55f, 0.75f, 1.00f };
        colors[ImGuiCol_Header] = ImVec4{ 0.08f, 0.14f, 0.22f, 0.80f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.12f, 0.22f, 0.34f, 0.90f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.28f, 0.42f, 1.00f };
        colors[ImGuiCol_Separator] = ImVec4{ 0.58f, 0.74f, 0.90f, 0.15f };
        colors[ImGuiCol_SeparatorHovered] = ImVec4{ 0.00f, 0.82f, 1.00f, 0.75f };
        colors[ImGuiCol_SeparatorActive] = ImVec4{ 0.00f, 0.82f, 1.00f, 1.00f };
        colors[ImGuiCol_ResizeGrip] = ImVec4{ 0.00f, 0.78f, 0.98f, 0.20f };
        colors[ImGuiCol_ResizeGripHovered] = ImVec4{ 0.00f, 0.78f, 0.98f, 0.65f };
        colors[ImGuiCol_ResizeGripActive] = ImVec4{ 0.00f, 0.85f, 1.00f, 0.95f };
        colors[ImGuiCol_TableHeaderBg] = ImVec4{ 0.06f, 0.10f, 0.16f, 0.90f };
        colors[ImGuiCol_TableBorderStrong] = ImVec4{ 0.58f, 0.74f, 0.90f, 0.18f };
        colors[ImGuiCol_TableBorderLight] = ImVec4{ 0.58f, 0.74f, 0.90f, 0.08f };
        colors[ImGuiCol_TableRowBg] = ImVec4{ 0.00f, 0.00f, 0.00f, 0.00f };
        colors[ImGuiCol_TableRowBgAlt] = ImVec4{ 0.58f, 0.74f, 0.90f, 0.03f };
    }
}
