#include "Device.hpp"
#include "FrameLimit.hpp"
#include "OceanScene.hpp"
#include "PlatformSystem.hpp"
#include "RhiSystem.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

// The OceanFft demo: a square of FFT ocean over a seabed, with an orbit camera and an ImGui panel.
//   --frames=N      render N frames and exit (for unattended runs)
//   --size=N        IFFT size: 128, 256, 512 (default) or 1024
//   --radix2        use the radix-2 IFFT kernel instead of radix-4
//   --no-wave-ops   use the variant without subgroup operations, even when the device supports them
namespace
{
    void ParseArguments(int argc, char* argv[], OceanSceneSettings& settings, bool& no_wave_ops)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument{ argv[i] };
            if (argument == "--radix2")
            {
                settings.Simulation.Kernel = ocean::IfftKernel::Radix2;
            }
            else if (argument == "--no-wave-ops")
            {
                no_wave_ops = true;
            }
            else if (argument.starts_with("--size="))
            {
                const std::string_view value = argument.substr(7);
                uint32_t size = 0u;
                if (std::from_chars(value.data(), value.data() + value.size(), size).ec == std::errc{} &&
                    (size == 128u || size == 256u || size == 512u || size == 1024u))
                {
                    settings.Simulation.Size = size;
                }
                else
                {
                    std::cerr << "--size must be 128, 256, 512 or 1024\n";
                }
            }
        }
    }
}

int main(int argc, char* argv[])
{
    rhi::RhiSystemCreateInfo rhiCreateInfo{};
    rhiCreateInfo.ApplicationName = "OceanFft";
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
        "VK_KHR_swapchain",
        "VK_EXT_shader_object"
    };
    std::unique_ptr<rhi::RhiSystem> rhiSystem = std::make_unique<rhi::RhiSystem>(rhiCreateInfo);

    const PlatformWindowCreateInfo platformCreateInfo
    {
        "DiamondDogs - OceanFft",
        nullptr, // use primary display
        PlatformWindowMode::Windowed,
        1600,
        900,
        100,
        100,
        60.0f,
        PlatformWindowBehaviorFlags{ true, true, true, false, false }
    };
    std::unique_ptr<PlatformWindowSystem> platformSystem = std::make_unique<PlatformWindowSystem>(platformCreateInfo);
    platformSystem->CreateDefaultSwapchain(rhiSystem->GetInstance(), rhiSystem->GetDevice());

    OceanSceneSettings settings{};
    bool noWaveOps = false;
    ParseArguments(argc, argv, settings, noWaveOps);
    settings.Simulation.WaveSize = noWaveOps ? 0u : ocean::PickWaveSize(*rhiSystem->GetDevice());

    int exitCode = 0;
    {
        OceanScene scene(rhiSystem.get(), platformSystem.get(), settings);
        scene.Initialize(nullptr);
        const int32_t frameLimit = ParseFrameLimit(argc, argv);
        int32_t framesRendered = 0;
        while (!platformSystem->ShouldWindowClose() && (frameLimit < 0 || framesRendered < frameLimit))
        {
            platformSystem->Update();
            scene.Render(nullptr);
            ++framesRendered;
        }
        scene.Destroy();
    }
    platformSystem->DestroySwapchain();
    return exitCode;
}
