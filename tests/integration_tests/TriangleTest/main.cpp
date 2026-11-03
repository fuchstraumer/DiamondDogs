#include "FrameLimit.hpp"
#include "ResizeTest.hpp"
#include "TriangleTest.hpp"
#include "RhiSystem.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
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
    // Initialize RHI system
    rhi::RhiSystemCreateInfo rhiCreateInfo{};
    rhiCreateInfo.ApplicationName = "TriangleTest";
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
        "VK_EXT_shader_object",
        "VK_KHR_device_fault"
    };

    auto rhiSystem = std::make_unique<rhi::RhiSystem>(rhiCreateInfo);

    // Initialize platform window system with swapchain
    PlatformWindowCreateInfo platformCreateInfo
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

    auto platformSystem = std::make_unique<PlatformWindowSystem>(platformCreateInfo);
    
    // Create swapchain after window is created
    platformSystem->CreateDefaultSwapchain(rhiSystem->GetInstance(), rhiSystem->GetDevice());

    if (HasFlag(argc, argv, "--surface-pairs") && CheckSurfacePairs(*rhiSystem, *platformSystem) != 0)
    {
        std::cerr << "TriangleTest: a surface pair did not give a swapchain with that pair\n";
        platformSystem->DestroySwapchain();
        return 1;
    }

    // Create triangle renderer
    VulkanTriangle triangle(rhiSystem.get(), platformSystem.get());
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

    // Cleanup
    triangle.Destroy();
    platformSystem->DestroySwapchain();
    return exitCode;
}
