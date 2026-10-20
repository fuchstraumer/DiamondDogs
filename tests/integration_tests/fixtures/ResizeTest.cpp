#include "ResizeTest.hpp"
#include "Math.hpp"
#include "PlatformSystem.hpp"
#include "Swapchain.hpp"
#include <array>
#include <iostream>
#include <string_view>

namespace
{
    struct ScheduledResize
    {
        int32_t Frame;
        int Width;
        int Height;
    };

    // the last entry returns to the size that ImGuiTest and TriangleTest open with
    constexpr std::array<ScheduledResize, 3> k_SResizeSchedule
    {
        ScheduledResize{ 30, 960, 540 },
        ScheduledResize{ 60, 1600, 900 },
        ScheduledResize{ 90, 1280, 720 },
    };
}

bool ParseResizeTest(int argc, char* argv[])
{
    constexpr std::string_view k_ResizeFlag{ "--resize" };
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view{ argv[i] } == k_ResizeFlag)
        {
            return true;
        }
    }
    return false;
}

void ApplyScheduledResize(PlatformWindowSystem& platform_system, int32_t frame)
{
    for (const ScheduledResize& resize : k_SResizeSchedule)
    {
        if (resize.Frame == frame)
        {
            platform_system.SetWindowSize(resize.Width, resize.Height);
        }
    }
}

int VerifyScheduledResize(const PlatformWindowSystem& platform_system, uint32_t swapchain_recreation_count)
{
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    platform_system.GetFramebufferSize(framebufferWidth, framebufferHeight);
    const math::Float2 extent = platform_system.GetActiveSwapchain()->GetExtent();
    const bool extentMatches = static_cast<int>(extent.x) == framebufferWidth && static_cast<int>(extent.y) == framebufferHeight;
    const bool recreatedPerResize = swapchain_recreation_count >= k_SResizeSchedule.size();

    std::cout << "Resize test: " << swapchain_recreation_count << " swapchain recreations for " << k_SResizeSchedule.size()
              << " resizes, swapchain " << extent.x << "x" << extent.y << ", framebuffer " << framebufferWidth << "x"
              << framebufferHeight << "\n";
    if (!extentMatches || !recreatedPerResize)
    {
        std::cerr << "Resize test failed\n";
        return 1;
    }
    return 0;
}
