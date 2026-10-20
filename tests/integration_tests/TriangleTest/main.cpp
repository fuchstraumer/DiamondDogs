#include "TriangleTest.hpp"
#include "RhiSystem.hpp"
#include "PlatformSystem.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    int32_t ParseFrameLimit(int argc, char* argv[]);
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

    // Create triangle renderer
    VulkanTriangle triangle(rhiSystem.get(), platformSystem.get());
    triangle.Initialize(nullptr);

    // --frames=N exits after N frames, so that an unattended run (an agent, or CI) can use this as a smoke test
    const int32_t frameLimit = ParseFrameLimit(argc, argv);
    int32_t framesRendered = 0;
    while (!platformSystem->ShouldWindowClose() && (frameLimit < 0 || framesRendered < frameLimit))
    {
        platformSystem->Update();
        triangle.Render(nullptr);
        ++framesRendered;
    }

    // Cleanup
    triangle.Destroy();
    platformSystem->DestroySwapchain();
    return 0;
}

namespace
{
    int32_t ParseFrameLimit(int argc, char* argv[])
    {
        constexpr std::string_view framesFlag{ "--frames=" };
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument{ argv[i] };
            if (!argument.starts_with(framesFlag))
            {
                continue;
            }
            const std::string_view value = argument.substr(framesFlag.size());
            int32_t frameLimit = -1;
            const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
            if (parsed.ec != std::errc{})
            {
                std::cerr << "Ignoring malformed " << argument << ", running until the window closes\n";
                return -1;
            }
            return frameLimit;
        }
        return -1;
    }
}
