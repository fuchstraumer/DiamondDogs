#include "TriangleTest.hpp"
#include "RhiSystem.hpp"
#include "PlatformSystem.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[])
{
    try
    {
        // Initialize RHI system
        rhi::RhiSystemCreateInfo rhiCreateInfo{};
        rhiCreateInfo.ApplicationName = "TriangleTest";
        rhiCreateInfo.VkVersion = rhi::ApiVersion::Latest;
        rhiCreateInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
        rhiCreateInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils", "VK_KHR_surface", "VK_KHR_win32_surface", "VK_KHR_get_surface_capabilities2", "VK_EXT_swapchain_colorspace" };
        rhiCreateInfo.RequiredDeviceExtensions = { "VK_KHR_swapchain" };

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

        // Main loop
        while (!platformSystem->ShouldWindowClose())
        {
            platformSystem->Update();
            triangle.Render(nullptr);
        }

        // Cleanup
        triangle.Destroy();
        platformSystem->DestroySwapchain();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
