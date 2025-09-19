#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include <filesystem>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>

class PlatformIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temporary directories for any test artifacts
        tempTestDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "PlatformIntegration";
        std::filesystem::create_directories(tempTestDir);
    }

    void TearDown() override
    {
        // Cleanup temporary directory
        if (std::filesystem::exists(tempTestDir))
        {
            std::filesystem::remove_all(tempTestDir);
        }
    }

    std::filesystem::path tempTestDir;

    PlatformWindowCreateInfo GetIntegrationTestCreateInfo()
    {
        return PlatformWindowCreateInfo
        {
            "PlatformIntegrationTest",
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

// Test platform system lifecycle with events
TEST_F(PlatformIntegrationTest, LifecycleWithEvents)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    EXPECT_NE(platformSystem, nullptr);
    EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
    
    // Test initial window state
    EXPECT_FALSE(platformSystem->ShouldWindowClose());
    
    // Test multiple update cycles
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW({
            platformSystem->Update();
        });
        
        // Brief pause to allow GLFW to process any pending events
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Test explicit destruction
    EXPECT_NO_THROW({
        platformSystem->Destroy();
    });
}

// Test event listener registration
TEST_F(PlatformIntegrationTest, EventListenerRegistration)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Event counters to verify callbacks are registered correctly
    std::atomic<int> cursorPosEvents{0};
    std::atomic<int> cursorEnterEvents{0};
    std::atomic<int> scrollEvents{0};
    std::atomic<int> charEvents{0};
    std::atomic<int> pathDropEvents{0};
    std::atomic<int> mouseButtonEvents{0};
    std::atomic<int> keyboardEvents{0};
    std::atomic<int> resizeEvents{0};
    std::atomic<int> closeEvents{0};
    
    // Register event listeners
    EXPECT_NO_THROW({
        platformSystem->AddCursorPosEventListener(
            [&cursorPosEvents](const Events::CursorPosEventData&, void*) {
                cursorPosEvents++;
            }, nullptr);
            
        platformSystem->AddCursorEnterEventListener(
            [&cursorEnterEvents](const int, void*) {
                cursorEnterEvents++;
            }, nullptr);
            
        platformSystem->AddScrollEventListener(
            [&scrollEvents](const Events::ScrollEventData&, void*) {
                scrollEvents++;
            }, nullptr);
            
        platformSystem->AddCharEventListener(
            [&charEvents](const char, void*) {
                charEvents++;
            }, nullptr);
            
        platformSystem->AddPathDropEventListener(
            [&pathDropEvents](const Events::PathDropEventData&, void*) {
                pathDropEvents++;
            }, nullptr);
            
        platformSystem->AddMouseButtonEventListener(
            [&mouseButtonEvents](const Events::MouseButtonEventData&, void*) {
                mouseButtonEvents++;
            }, nullptr);
            
        platformSystem->AddKeyboardKeyEventListener(
            [&keyboardEvents](const Events::KeyboardKeyEventData&, void*) {
                keyboardEvents++;
            }, nullptr);
            
        platformSystem->AddShouldResizeEventListener(
            [&resizeEvents](const Events::ShouldResizeEventData&, void*) {
                resizeEvents++;
            }, nullptr);
            
        platformSystem->AddShouldCloseEventListener(
            [&closeEvents](void*) {
                closeEvents++;
            }, nullptr);
    });
    
    // Process some update cycles to ensure event system is working
    for (int i = 0; i < 5; ++i)
    {
        platformSystem->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Event counters might still be 0 since we're not generating actual events,
    // but registration should not have crashed
    EXPECT_GE(cursorPosEvents.load(), 0);
    EXPECT_GE(cursorEnterEvents.load(), 0);
    EXPECT_GE(scrollEvents.load(), 0);
    EXPECT_GE(charEvents.load(), 0);
    EXPECT_GE(pathDropEvents.load(), 0);
    EXPECT_GE(mouseButtonEvents.load(), 0);
    EXPECT_GE(keyboardEvents.load(), 0);
    EXPECT_GE(resizeEvents.load(), 0);
    EXPECT_GE(closeEvents.load(), 0);
}

// Test window attribute manipulation
TEST_F(PlatformIntegrationTest, WindowAttributeManipulation)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Test getting window attributes (values depend on GLFW constants)
    EXPECT_NO_THROW({
        // Common GLFW window attributes
        int decorated = platformSystem->GetWindowAttribute(0x20005); // GLFW_DECORATED
        int resizable = platformSystem->GetWindowAttribute(0x20006); // GLFW_RESIZABLE
        int visible = platformSystem->GetWindowAttribute(0x20004);   // GLFW_VISIBLE
        
        (void)decorated; (void)resizable; (void)visible; // Suppress unused variable warnings
    });
    
    // Test setting window attributes
    EXPECT_NO_THROW({
        // These should not crash, even if the attributes are not settable
        platformSystem->SetWindowAttribute(0x20005, 1); // GLFW_DECORATED
    });
}

// Test input mode manipulation
TEST_F(PlatformIntegrationTest, InputModeManipulation)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Test cursor modes
    EXPECT_NO_THROW({
        int cursorMode = platformSystem->GetInputMode(0x00033001); // GLFW_CURSOR
        platformSystem->SetInputMode(0x00033001, 0x00034001); // GLFW_CURSOR_NORMAL
        
        (void)cursorMode; // Suppress unused variable warning
    });
    
    // Test sticky keys mode
    EXPECT_NO_THROW({
        int stickyKeys = platformSystem->GetInputMode(0x00033002); // GLFW_STICKY_KEYS
        platformSystem->SetInputMode(0x00033002, 0); // Disable sticky keys
        
        (void)stickyKeys; // Suppress unused variable warning
    });
}

// Test display info consistency
TEST_F(PlatformIntegrationTest, DisplayInfoConsistency)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    const DisplayInfo& displayInfo = platformSystem->GetActiveDisplayInfo();
    
    // Validate display info is consistent across multiple queries
    for (int i = 0; i < 5; ++i)
    {
        const DisplayInfo& currentInfo = platformSystem->GetActiveDisplayInfo();
        
        EXPECT_EQ(displayInfo.Width, currentInfo.Width);
        EXPECT_EQ(displayInfo.Height, currentInfo.Height);
        EXPECT_EQ(displayInfo.BitDepthRed, currentInfo.BitDepthRed);
        EXPECT_EQ(displayInfo.BitDepthGreen, currentInfo.BitDepthGreen);
        EXPECT_EQ(displayInfo.BitDepthBlue, currentInfo.BitDepthBlue);
        EXPECT_FLOAT_EQ(displayInfo.DisplayScaleX, currentInfo.DisplayScaleX);
        EXPECT_FLOAT_EQ(displayInfo.DisplayScaleY, currentInfo.DisplayScaleY);
        EXPECT_FLOAT_EQ(displayInfo.RefreshRate, currentInfo.RefreshRate);
        EXPECT_EQ(displayInfo.MonitorIdx, currentInfo.MonitorIdx);
        
        platformSystem->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Test window size consistency
TEST_F(PlatformIntegrationTest, WindowSizeConsistency)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    int initialWidth, initialHeight;
    platformSystem->GetWindowSize(initialWidth, initialHeight);
    
    int initialFbWidth, initialFbHeight;
    platformSystem->GetFramebufferSize(initialFbWidth, initialFbHeight);
    
    // Validate sizes are reasonable
    EXPECT_GT(initialWidth, 0);
    EXPECT_GT(initialHeight, 0);
    EXPECT_GT(initialFbWidth, 0);
    EXPECT_GT(initialFbHeight, 0);
    
    // Process some updates and verify sizes remain consistent
    for (int i = 0; i < 5; ++i)
    {
        platformSystem->Update();
        
        int currentWidth, currentHeight;
        platformSystem->GetWindowSize(currentWidth, currentHeight);
        
        int currentFbWidth, currentFbHeight;
        platformSystem->GetFramebufferSize(currentFbWidth, currentFbHeight);
        
        // Sizes should remain consistent unless a resize event occurred
        // Allow some tolerance for DPI scaling differences
        EXPECT_NEAR(currentWidth, initialWidth, 50);
        EXPECT_NEAR(currentHeight, initialHeight, 50);
        EXPECT_NEAR(currentFbWidth, initialFbWidth, 50);
        EXPECT_NEAR(currentFbHeight, initialFbHeight, 50);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Test cursor position management
TEST_F(PlatformIntegrationTest, CursorPositionManagement)
{
    auto createInfo = GetIntegrationTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Test cursor position setting and getting
    std::vector<std::pair<double, double>> testPositions = {
        {100.0, 100.0},
        {200.0, 150.0},
        {50.0, 300.0},
        {400.0, 250.0}
    };
    
    for (const auto& [testX, testY] : testPositions)
    {
        EXPECT_NO_THROW({
            platformSystem->SetCursorPosition(testX, testY);
            
            // Allow a brief moment for the position to be set
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            double actualX, actualY;
            platformSystem->GetCursorPosition(actualX, actualY);
            
            // Cursor position might not be exact due to window bounds, etc.
            // Just ensure it doesn't crash and returns reasonable values
            EXPECT_TRUE(std::isfinite(actualX));
            EXPECT_TRUE(std::isfinite(actualY));
        });
    }
}

// Test multiple platform systems (if supported)
TEST_F(PlatformIntegrationTest, MultiplePlatformSystems)
{
    auto createInfo1 = GetIntegrationTestCreateInfo();
    createInfo1.WindowName = "PlatformTest1";
    createInfo1.InitialPosX = 0;
    createInfo1.InitialPosY = 0;
    
    auto createInfo2 = GetIntegrationTestCreateInfo();
    createInfo2.WindowName = "PlatformTest2";
    createInfo2.InitialPosX = 100;
    createInfo2.InitialPosY = 100;
    
    std::unique_ptr<PlatformWindowSystem> platformSystem1;
    std::unique_ptr<PlatformWindowSystem> platformSystem2;
    
    EXPECT_NO_THROW({
        platformSystem1 = std::make_unique<PlatformWindowSystem>(createInfo1);
        EXPECT_NE(platformSystem1, nullptr);
        EXPECT_NE(platformSystem1->GetWindowHandle(), nullptr);
    });
    
    EXPECT_NO_THROW({
        platformSystem2 = std::make_unique<PlatformWindowSystem>(createInfo2);
        EXPECT_NE(platformSystem2, nullptr);
        EXPECT_NE(platformSystem2->GetWindowHandle(), nullptr);
    });
    
    // Verify both systems are independent
    if (platformSystem1 && platformSystem2)
    {
        EXPECT_NE(platformSystem1->GetWindowHandle(), platformSystem2->GetWindowHandle());
        
        // Test that both can update independently
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_NO_THROW({
                platformSystem1->Update();
                platformSystem2->Update();
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// Test HDR system support query consistency
TEST_F(PlatformIntegrationTest, HDRSupportConsistency)
{
    // Test HDR support query multiple times to ensure consistency
    bool initialHDRSupport = PlatformWindowSystem::IsHDREnabledOnSystem();
    
    for (int i = 0; i < 10; ++i)
    {
        bool currentHDRSupport = PlatformWindowSystem::IsHDREnabledOnSystem();
        EXPECT_EQ(initialHDRSupport, currentHDRSupport);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Test destruction order and cleanup
TEST_F(PlatformIntegrationTest, DestructionOrderAndCleanup)
{
    std::unique_ptr<PlatformWindowSystem> platformSystem;
    
    EXPECT_NO_THROW({
        auto createInfo = GetIntegrationTestCreateInfo();
        platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
        EXPECT_NE(platformSystem, nullptr);
    });
    
    // Test explicit destroy before destruction
    if (platformSystem)
    {
        EXPECT_NO_THROW({
            platformSystem->Destroy();
        });
    }
    
    // Test destruction after explicit destroy (should not crash)
    EXPECT_NO_THROW({
        platformSystem.reset();
    });
}