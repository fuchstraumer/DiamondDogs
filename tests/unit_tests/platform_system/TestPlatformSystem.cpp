#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include <filesystem>
#include <memory>

constexpr static PlatformWindowCreateInfo s_DefaultCreateInfo
{
    "UnitTestWindow", // window name
    nullptr, // use primary display
    PlatformWindowMode::Windowed,
    800, // initial width
    600, // initial height
    0,   // initial pos x
    0,   // initial pos y
    PlatformWindowBehaviorFlags
    {
        true,   // Resizable
        true,   // Moveable
        true,   // Decorated
        false,  // FocusOnShow
        false   // CenterMouse
    }   // default behavior flags
};

class PlatformSystemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temporary directories for test configs
        tempConfigDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "PlatformConfigs";
        std::filesystem::create_directories(tempConfigDir);
    }
    
    void TearDown() override
    {
        // Cleanup temporary directory
        if (std::filesystem::exists(tempConfigDir))
        {
            std::filesystem::remove_all(tempConfigDir);
        }
    }

    std::filesystem::path tempConfigDir;

    PlatformWindowCreateInfo GetTestCreateInfo()
    {
        return PlatformWindowCreateInfo
        {
            "PlatformSystemTest",
            nullptr, // use primary display
            PlatformWindowMode::Windowed,
            1024, // test different size
            768,
            100, // test different position
            100,
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

// Test basic platform system construction with CreateInfo
TEST_F(PlatformSystemTest, BasicCreateInfoConstruction)
{
    auto createInfo = GetTestCreateInfo();
    
    EXPECT_NO_THROW({
        auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
        EXPECT_NE(platformSystem, nullptr);
        EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
        
        // Validate stored configuration values
        int width, height;
        platformSystem->GetWindowSize(width, height);
        EXPECT_GE(width, 0);
        EXPECT_GE(height, 0);
        
        // Test window mode is accessible (even if we can't directly get it without getter)
        EXPECT_FALSE(platformSystem->ShouldWindowClose());
    });
}

// Test platform system with different window modes
TEST_F(PlatformSystemTest, WindowModeVariations)
{
    std::vector<PlatformWindowMode> windowModes = {
        PlatformWindowMode::Windowed,
        PlatformWindowMode::MaximizedWindowed
        // Note: Fullscreen modes might not be suitable for automated testing
    };
    
    for (auto mode : windowModes)
    {
        auto createInfo = GetTestCreateInfo();
        createInfo.DesiredWindowMode = mode;
        
        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
            
            // Validate window was created successfully
            int width, height;
            platformSystem->GetWindowSize(width, height);
            EXPECT_GT(width, 0);
            EXPECT_GT(height, 0);
        });
    }
}

// Test platform system with different window sizes
TEST_F(PlatformSystemTest, WindowSizeVariations)
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
        auto createInfo = GetTestCreateInfo();
        createInfo.InitialWidth = width;
        createInfo.InitialHeight = height;
        
        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            
            // Validate window size (may not be exact due to window decorations)
            int actualWidth, actualHeight;
            platformSystem->GetWindowSize(actualWidth, actualHeight);
            EXPECT_GT(actualWidth, 0);
            EXPECT_GT(actualHeight, 0);
            
            // Should be reasonably close to requested size
            EXPECT_NEAR(actualWidth, static_cast<int>(width), 100);
            EXPECT_NEAR(actualHeight, static_cast<int>(height), 100);
        });
    }
}

// Test platform system behavior flags
TEST_F(PlatformSystemTest, BehaviorFlags)
{
    std::vector<PlatformWindowBehaviorFlags> flagVariations = {
        {true, true, true, false, false},    // default
        {false, true, true, false, false},   // non-resizable
        {true, false, true, false, false},   // non-moveable
        {true, true, false, false, false},   // undecorated
        {true, true, true, true, false},     // focus on show
        {true, true, true, false, true}      // center mouse
    };
    
    for (const auto& flags : flagVariations)
    {
        auto createInfo = GetTestCreateInfo();
        createInfo.BehaviorFlags = flags;
        
        EXPECT_NO_THROW({
            auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
            EXPECT_NE(platformSystem, nullptr);
            EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
        });
    }
}

// Test display info access
TEST_F(PlatformSystemTest, DisplayInfoAccess)
{
    auto createInfo = GetTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    const DisplayInfo& displayInfo = platformSystem->GetActiveDisplayInfo();
    
    // Validate display info contains reasonable values
    EXPECT_GT(displayInfo.Width, 0);
    EXPECT_GT(displayInfo.Height, 0);
    EXPECT_GE(displayInfo.BitDepthRed, 8);
    EXPECT_GE(displayInfo.BitDepthGreen, 8);
    EXPECT_GE(displayInfo.BitDepthBlue, 8);
    EXPECT_GT(displayInfo.DisplayScaleX, 0.0f);
    EXPECT_GT(displayInfo.DisplayScaleY, 0.0f);
    EXPECT_GT(displayInfo.RefreshRate, 0.0f);
    EXPECT_GE(displayInfo.MonitorIdx, -1); // -1 is valid for primary monitor
}

// Test window and input management methods
TEST_F(PlatformSystemTest, WindowAndInputManagement)
{
    auto createInfo = GetTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Test window size getter
    int width, height;
    platformSystem->GetWindowSize(width, height);
    EXPECT_GT(width, 0);
    EXPECT_GT(height, 0);
    
    // Test framebuffer size getter
    int fbWidth, fbHeight;
    platformSystem->GetFramebufferSize(fbWidth, fbHeight);
    EXPECT_GT(fbWidth, 0);
    EXPECT_GT(fbHeight, 0);
    
    // Test cursor position getter
    double cursorX, cursorY;
    platformSystem->GetCursorPosition(cursorX, cursorY);
    // Cursor position can be any value, just ensure it doesn't crash
    
    // Test cursor position setter
    EXPECT_NO_THROW({
        platformSystem->SetCursorPosition(100.0, 100.0);
    });
    
    // Test mouse button state (should not crash)
    EXPECT_NO_THROW({
        int buttonState = platformSystem->GetMouseButton(0); // Left mouse button
        (void)buttonState; // Suppress unused variable warning
    });
}

// Test HDR support query
TEST_F(PlatformSystemTest, HDRSupportQuery)
{
    // This is a static method, should not crash
    EXPECT_NO_THROW({
        bool hdrSupported = PlatformWindowSystem::IsHDREnabledOnSystem();
        (void)hdrSupported; // Suppress unused variable warning
    });
}

// Test lifecycle management
TEST_F(PlatformSystemTest, LifecycleManagement)
{
    auto createInfo = GetTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Test update (should not crash)
    EXPECT_NO_THROW({
        platformSystem->Update();
    });
    
    // Test should close query
    EXPECT_NO_THROW({
        bool shouldClose = platformSystem->ShouldWindowClose();
        (void)shouldClose;
    });
    
    // Test explicit destroy
    EXPECT_NO_THROW({
        platformSystem->Destroy();
    });
}
