#include "BootResult.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "FrameLimit.hpp"
#include "ImGuiModule.hpp"
#include "ImGuiScene.hpp"
#include "ModuleDescriptor.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "ResizeTest.hpp"
#include "RhiSystem.hpp"
#include "WindowedBoot.hpp"
#include <cstdint>
#include <span>
#include <vector>

// Opens a window and draws the Dear ImGui demo through ImGuiModule (official GLFW and Vulkan backends, dynamic
// rendering). Pass --frames=N for an unattended run, and --resize to resize the window and recreate the swapchain.
int main(int argc, char* argv[])
{
    const PlatformWindowCreateInfo windowInfo
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
    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::Full,
        .ApplicationName = "ImGuiTest",
    };
    const std::vector<bootstrap::Need> appNeeds = bootstrap::FeatureNeeds(k_ImGuiModuleFeatures, bootstrap::NeedLevel::Required);
    WindowedBoot boot;

    if (!StartWindowedBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, std::span<const bootstrap::BundleRequest>{}, appNeeds, config, windowInfo))
    {
        return 1;
    }

    PlatformWindowSystem& platformSystem = boot.Window.Platform();
    ImGuiScene scene(&boot.Device.Rhi(), &platformSystem);

    scene.Initialize(nullptr);

    const int32_t frameLimit = ParseFrameLimit(argc, argv);
    const bool resizeTest = ParseResizeTest(argc, argv);
    int32_t framesRendered = 0;

    while (!platformSystem.ShouldWindowClose() && (frameLimit < 0 || framesRendered < frameLimit))
    {
        if (resizeTest)
        {
            ApplyScheduledResize(platformSystem, framesRendered);
        }

        platformSystem.Update();
        scene.Render(nullptr);
        ++framesRendered;
    }

    int exitCode = 0;

    if (resizeTest)
    {
        exitCode = VerifyScheduledResize(platformSystem, scene.SwapchainRecreationCount());
    }

    // the GPU stage destroys the swapchain
    scene.Destroy();
    return exitCode;
}
