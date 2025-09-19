#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include <memory>

class PlatformWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Common setup for platform window tests
    }

    void TearDown() override
    {
        // Common cleanup
    }

    PlatformWindowCreateInfo GetDefaultCreateInfo()
    {
        return PlatformWindowCreateInfo
        {
            "PlatformWindowTest",
            nullptr, // use primary display
            PlatformWindowMode::Windowed,
            800,
            600,
            0,
            0,
            PlatformWindowBehaviorFlags
            {
                true,   // Resizable
                true,   // Moveable
                true,   // Decorated
                false,  // FocusOnShow
                false   // CenterMouse
            }
        };
    }
};

// Test window creation with different modes
TEST_F(PlatformWindowTest, WindowModeCreation)
{
    std::vector<PlatformWindowMode> modes = {
        PlatformWindowMode::Windowed,
        PlatformWindowMode::MaximizedWindowed
        // Fullscreen modes might interfere with automated testing
    };

    for (auto mode : modes)
    {
        auto createInfo = GetDefaultCreateInfo();
        createInfo.DesiredWindowMode = mode;

        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);

            // Test that window was created with correct properties
            int width, height;
            platformSystem->GetWindowSize(width, height);
            EXPECT_GT(width, 0);
            EXPECT_GT(height, 0);
        });
    }
}

// Test window behavior flags
TEST_F(PlatformWindowTest, BehaviorFlags)
{
    struct FlagTest
    {
        PlatformWindowBehaviorFlags flags;
        std::string description;
    };

    std::vector<FlagTest> flagTests = {
        {{true, true, true, false, false}, "Default flags"},
        {{false, true, true, false, false}, "Non-resizable"},
        {{true, false, true, false, false}, "Non-moveable"},
        {{true, true, false, false, false}, "Undecorated"},
        {{true, true, true, true, false}, "Focus on show"},
        {{true, true, true, false, true}, "Center mouse"}
    };

    for (const auto& test : flagTests)
    {
        auto createInfo = GetDefaultCreateInfo();
        createInfo.BehaviorFlags = test.flags;

        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
        }) << "Failed with " << test.description;
    }
}

// Test window positioning
TEST_F(PlatformWindowTest, WindowPositioning)
{
    std::vector<std::pair<uint32_t, uint32_t>> positions = {
        {0, 0},
        {100, 100},
        {200, 150},
        {50, 300}
    };

    for (const auto& [x, y] : positions)
    {
        auto createInfo = GetDefaultCreateInfo();
        createInfo.InitialPosX = x;
        createInfo.InitialPosY = y;

        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
        });
    }
}

// Test window sizing
TEST_F(PlatformWindowTest, WindowSizing)
{
    std::vector<std::pair<uint32_t, uint32_t>> sizes = {
        {640, 480},
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1920, 1080}
    };

    for (const auto& [width, height] : sizes)
    {
        auto createInfo = GetDefaultCreateInfo();
        createInfo.InitialWidth = width;
        createInfo.InitialHeight = height;

        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);

            // Verify window size is approximately correct
            int actualWidth, actualHeight;
            platformSystem->GetWindowSize(actualWidth, actualHeight);
            
            // Allow some tolerance for window decorations
            EXPECT_NEAR(actualWidth, static_cast<int>(width), 100);
            EXPECT_NEAR(actualHeight, static_cast<int>(height), 100);
        });
    }
}

// Test window name setting
TEST_F(PlatformWindowTest, WindowNaming)
{
    std::vector<std::string> windowNames = {
        "TestWindow",
        "DiamondDogs Test",
        "Platform System Test",
        "Window with Spaces",
        "SpecialChars!@#$%"
    };

    for (const auto& name : windowNames)
    {
        auto createInfo = GetDefaultCreateInfo();
        createInfo.WindowName = name.c_str();

        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
        });
    }
}

// Test display info retrieval
TEST_F(PlatformWindowTest, DisplayInfoRetrieval)
{
    auto createInfo = GetDefaultCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);

    const DisplayInfo& displayInfo = platformSystem->GetActiveDisplayInfo();

    // Validate display info structure
    EXPECT_GT(displayInfo.Width, 0);
    EXPECT_GT(displayInfo.Height, 0);
    EXPECT_GE(displayInfo.BitDepthRed, 8);
    EXPECT_GE(displayInfo.BitDepthGreen, 8);
    EXPECT_GE(displayInfo.BitDepthBlue, 8);
    EXPECT_GT(displayInfo.DisplayScaleX, 0.0f);
    EXPECT_GT(displayInfo.DisplayScaleY, 0.0f);
    EXPECT_GT(displayInfo.RefreshRate, 0.0f);
    EXPECT_GE(displayInfo.MonitorIdx, -1);

    // Test consistency of display info across multiple queries
    for (int i = 0; i < 5; ++i)
    {
        const DisplayInfo& currentInfo = platformSystem->GetActiveDisplayInfo();
        EXPECT_EQ(displayInfo.Width, currentInfo.Width);
        EXPECT_EQ(displayInfo.Height, currentInfo.Height);
        EXPECT_EQ(displayInfo.MonitorIdx, currentInfo.MonitorIdx);
    }
}

// Test window validation with invalid parameters
TEST_F(PlatformWindowTest, InvalidParameters)
{
    // Test with zero width/height (should be handled gracefully)
    auto createInfo = GetDefaultCreateInfo();
    createInfo.InitialWidth = 0;
    createInfo.InitialHeight = 0;

    // This might work (using defaults) or might throw - either is acceptable
    // The important thing is it doesn't crash
    try
    {
        auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
        if (platformSystem)
        {
            int width, height;
            platformSystem->GetWindowSize(width, height);
            EXPECT_GT(width, 0);  // Should have fallback to reasonable size
            EXPECT_GT(height, 0);
        }
    }
    catch (const std::exception&)
    {
        // Exception is acceptable for invalid parameters
        SUCCEED() << "Exception thrown for invalid parameters (expected behavior)";
    }
}