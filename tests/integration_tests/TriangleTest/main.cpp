#include "BootResult.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "FrameLimit.hpp"
#include "ModuleDescriptor.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "ResizeTest.hpp"
#include "RhiSystem.hpp"
#include "ShaderProgram.hpp"
#include "Swapchain.hpp"
#include "TriangleTest.hpp"
#include "WindowedBoot.hpp"
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <span>
#include <vector>

namespace
{
    // the metadata of a 600 nit display with Rec.2020 primaries: any valid values, since this only checks that the call is accepted
    constexpr SwapchainHdrMetadata k_TestMetadata{ 0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f, 600.0f, 0.0f, 600.0f, 348.0f };

    [[nodiscard]] bool IsHdrColorSpace(ColorSpace space) noexcept
    {
        return space == ColorSpace::HDR10_ST2084 || space == ColorSpace::Extended_sRGB_Linear || space == ColorSpace::HDR10_HLG || space == ColorSpace::BT2020_Linear;
    }

    // --surface-pairs: lists the pairs that the surface offers before any swapchain is in use, creates a swapchain with
    // each one, and checks that the swapchain got that pair. A pair of an HDR color space also takes the HDR metadata. The
    // default swapchain comes back afterwards. Returns the number of pairs that failed.
    [[nodiscard]] int32_t CheckSurfacePairs(rhi::RhiSystem& rhi_system, PlatformWindowSystem& platform_system)
    {
        const std::vector<SurfaceFormatPair> pairs = platform_system.QuerySurfaceFormats(rhi_system.GetInstance(), rhi_system.GetDevice());
        std::cout << std::format("surface pairs: {}\n", pairs.size());
        int32_t failures = pairs.empty() ? 1 : 0;

        for (const SurfaceFormatPair& pair : pairs)
        {
            platform_system.CreateSwapchainWithSurfaceFormat(rhi_system.GetInstance(), rhi_system.GetDevice(), pair);
            Swapchain* swapchain = platform_system.GetActiveSwapchain();
            const bool gotPair = swapchain->ImageFormat() == pair.Format && swapchain->ColorSpace() == pair.Space;

            if (gotPair && IsHdrColorSpace(pair.Space))
            {
                swapchain->SetHdrMetadata(k_TestMetadata);
            }

            std::cout << std::format("[{}] components {} data type {} color space {}\n", gotPair ? "pass" : "FAIL", static_cast<int>(pair.Format.ComponentFormat),
                static_cast<int>(pair.Format.DataType), static_cast<int>(pair.Space));
            failures += gotPair ? 0 : 1;
        }

        platform_system.CreateDefaultSwapchain(rhi_system.GetInstance(), rhi_system.GetDevice());

        return failures;
    }

    // the triangle is drawn with shader objects and dynamic rendering. Device fault reports help after a device loss
    constexpr bootstrap::Need k_AppNeeds[]
    {
        bootstrap::Need
        {
            .Name = rhi::k_ShaderObjectFeature,
            .Kind = bootstrap::NeedKind::DeviceFeature,
            .Level = bootstrap::NeedLevel::Required,
        },
        bootstrap::Need
        {
            .Name = "VkPhysicalDeviceVulkan13Features.dynamicRendering",
            .Kind = bootstrap::NeedKind::DeviceFeature,
            .Level = bootstrap::NeedLevel::Required,
        },
        bootstrap::Need
        {
            .Name = "VK_KHR_device_fault",
            .Kind = bootstrap::NeedKind::DeviceExtension,
            .Level = bootstrap::NeedLevel::Wanted,
        },
    };

    [[nodiscard]] bool HasFlag(int argc, char* argv[], const char* flag) noexcept
    {
        for (int32_t index = 1; index < argc; ++index)
        {
            if (std::strcmp(argv[index], flag) == 0)
            {
                return true;
            }
        }

        return false;
    }
}

int main(int argc, char* argv[])
{
    const PlatformWindowCreateInfo windowInfo
    {
        "DiamondDogs - Triangle Test",
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
        .ApplicationName = "TriangleTest",
    };
    WindowedBoot boot;

    if (!StartWindowedBoot(boot, std::span{ argv, static_cast<size_t>(argc) }, std::span<const bootstrap::BundleRequest>{}, k_AppNeeds, config, windowInfo))
    {
        return 1;
    }

    rhi::RhiSystem* rhiSystem = &boot.Device.Rhi();
    PlatformWindowSystem* platformSystem = &boot.Window.Platform();

    if (HasFlag(argc, argv, "--surface-pairs") && CheckSurfacePairs(*rhiSystem, *platformSystem) != 0)
    {
        std::cerr << "TriangleTest: a surface pair did not give a swapchain with that pair\n";
        return 1;
    }

    // Create triangle renderer
    VulkanTriangle triangle(rhiSystem, platformSystem);
    triangle.Initialize(nullptr);

    // --frames=N exits after N frames, so that an unattended run (an agent, or CI) can use this as a smoke test
    const int32_t frameLimit = ParseFrameLimit(argc, argv);
    // --resize resizes the window on a schedule, so that the run exercises swapchain recreation
    const bool resizeTest = ParseResizeTest(argc, argv);
    int32_t framesRendered = 0;
    while (!platformSystem->ShouldWindowClose() && (frameLimit < 0 || framesRendered < frameLimit))
    {
        if (resizeTest)
        {
            ApplyScheduledResize(*platformSystem, framesRendered);
        }
        platformSystem->Update();
        triangle.Render(nullptr);
        ++framesRendered;
    }

    int exitCode = 0;
    if (resizeTest)
    {
        exitCode = VerifyScheduledResize(*platformSystem, triangle.SwapchainRecreationCount());
    }

    // the GPU stage destroys the swapchain
    triangle.Destroy();
    return exitCode;
}
