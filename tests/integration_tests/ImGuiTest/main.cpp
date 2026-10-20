#include "FrameLimit.hpp"
#include "ImGuiScene.hpp"
#include "PlatformSystem.hpp"
#include "ResizeTest.hpp"
#include "RhiSystem.hpp"
#include <cstdint>
#include <memory>

// Opens a window and draws the Dear ImGui demo through ImGuiModule (official GLFW and Vulkan backends, dynamic
// rendering). Pass --frames=N for an unattended run, and --resize to resize the window and recreate the swapchain.
int main(int argc, char* argv[])
{
    rhi::RhiSystemCreateInfo rhiCreateInfo{};
    rhiCreateInfo.ApplicationName = "ImGuiTest";
    rhiCreateInfo.VkVersion = rhi::ApiVersion::Latest;
    rhiCreateInfo.ValidationLevel = rhi::ValidationLayers::Full;
    rhiCreateInfo.RequiredInstanceExtensions =
    {
        "VK_EXT_debug_utils",
        "VK_KHR_surface",
        "VK_KHR_win32_surface",
        "VK_KHR_get_surface_capabilities2",
        "VK_EXT_swapchain_colorspace"
    };
    rhiCreateInfo.RequiredDeviceExtensions =
    {
        "VK_KHR_swapchain"
    };
    std::unique_ptr<rhi::RhiSystem> rhiSystem = std::make_unique<rhi::RhiSystem>(rhiCreateInfo);

    const PlatformWindowCreateInfo platformCreateInfo
    {
        "DiamondDogs - ImGui Test",
        nullptr, // use primary display
        PlatformWindowMode::Windowed,
        1280,
        720,
        100,
        100,
        60.0f,
        PlatformWindowBehaviorFlags{ true, true, true, false, false }
    };
    std::unique_ptr<PlatformWindowSystem> platformSystem = std::make_unique<PlatformWindowSystem>(platformCreateInfo);
    platformSystem->CreateDefaultSwapchain(rhiSystem->GetInstance(), rhiSystem->GetDevice());

    ImGuiScene scene(rhiSystem.get(), platformSystem.get());
    scene.Initialize(nullptr);

    const int32_t frameLimit = ParseFrameLimit(argc, argv);
    const bool resizeTest = ParseResizeTest(argc, argv);
    int32_t framesRendered = 0;
    while (!platformSystem->ShouldWindowClose() && (frameLimit < 0 || framesRendered < frameLimit))
    {
        if (resizeTest)
        {
            ApplyScheduledResize(*platformSystem, framesRendered);
        }
        platformSystem->Update();
        scene.Render(nullptr);
        ++framesRendered;
    }

    int exitCode = 0;
    if (resizeTest)
    {
        exitCode = VerifyScheduledResize(*platformSystem, scene.SwapchainRecreationCount());
    }
    scene.Destroy();
    platformSystem->DestroySwapchain();
    return exitCode;
}
