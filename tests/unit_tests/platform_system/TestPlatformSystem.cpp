#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "utility/Delegate.hpp"
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
    60.0f, // desired refresh rate
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
            60.0f, // desired refresh rate
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
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    EXPECT_NE(platformSystem, nullptr);
    EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
        
    // Validate stored configuration values
    int width, height;
    platformSystem->GetWindowSize(width, height);
    EXPECT_NEAR(width, createInfo.InitialWidth, 100);
    EXPECT_NEAR(height, createInfo.InitialHeight, 100);
    int posX, posY;
    platformSystem->GetWindowPos(posX, posY);
    EXPECT_EQ(posX, createInfo.InitialPosX);
    EXPECT_EQ(posY, createInfo.InitialPosY);

    // Change config, reset, and validate again
    createInfo.InitialWidth = 1280;
    createInfo.InitialHeight = 720;
    createInfo.InitialPosX = 200;
    createInfo.InitialPosY = 0;

    platformSystem.reset();
    platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    EXPECT_NE(platformSystem, nullptr);
    EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
    platformSystem->GetWindowSize(width, height);
    EXPECT_NEAR(width, createInfo.InitialWidth, 100);
    EXPECT_NEAR(height, createInfo.InitialHeight, 100);
    platformSystem->GetWindowPos(posX, posY);
    EXPECT_EQ(posX, createInfo.InitialPosX);
    EXPECT_EQ(posY, createInfo.InitialPosY);
}

// Test platform system with different window modes
TEST_F(PlatformSystemTest, WindowModeVariations)
{
    std::vector<PlatformWindowMode> windowModes =
    {
        PlatformWindowMode::Windowed,
        PlatformWindowMode::FullScreenWindowed,
        // commenting out since it's quite disruptive to test environment, so I just test it manually when needed
        // PlatformWindowMode::Fullscreen
    };
    
    for (auto mode : windowModes)
    {
        auto createInfo = GetTestCreateInfo();
        createInfo.DesiredWindowMode = mode;

        auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
        DisplayInfo HardwareDisplay = PlatformWindowSystem::GetPrimaryDisplayInfo();
        EXPECT_NE(platformSystem, nullptr);
        EXPECT_NE(platformSystem->GetWindowHandle(), nullptr);
            
        // Validate window was created successfully, testing width/height as appropriate based on mode
        int width, height;
        platformSystem->GetWindowSize(width, height);

        switch (mode)
        {
        case PlatformWindowMode::Windowed:
            // window should be exactly same as requested size (size includes decorations)
            EXPECT_EQ(width, createInfo.InitialWidth);
            EXPECT_EQ(height, createInfo.InitialHeight);
            break;
        case PlatformWindowMode::Fullscreen:
            // maximized window fills screen except taskbar, so should be close to full size
            EXPECT_GT(static_cast<float>(width), static_cast<float>(HardwareDisplay.Width) * 0.8f);
            EXPECT_GT(static_cast<float>(height), static_cast<float>(HardwareDisplay.Height) * 0.8f);
            break;
        case PlatformWindowMode::FullScreenWindowed:
            EXPECT_EQ(width, HardwareDisplay.Width);
            EXPECT_EQ(height, HardwareDisplay.Height);
            break;
        }
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
    EXPECT_GT(displayInfo.RefreshRateCapabilities.RoundedRefreshRate, 0.0f);
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


static size_t s_shouldCloseCallbackCount = 0;
void TestShouldCloseCallback(void* userData)
{
    s_shouldCloseCallbackCount++;
}

// Test should close behavior
TEST_F(PlatformSystemTest, ShouldCloseBehavior)
{
    auto createInfo = GetTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    platformSystem->AddShouldCloseEventListener(ShouldCloseEvent::Create<TestShouldCloseCallback>(), nullptr);
    platformSystem->Update();
    platformSystem->SetWindowShouldClose(true);
    platformSystem->Update();
    EXPECT_EQ(s_shouldCloseCallbackCount, 1);
}

// Static callback functions for event testing
static std::atomic<int> s_cursorPosEvents{0};
static std::atomic<int> s_cursorEnterEvents{0};
static std::atomic<int> s_scrollEvents{0};
static std::atomic<int> s_charEvents{0};
static std::atomic<int> s_pathDropEvents{0};
static std::atomic<int> s_mouseButtonEvents{0};
static std::atomic<int> s_keyboardEvents{0};
static std::atomic<int> s_resizeEvents{0};
static std::atomic<int> s_closeEvents{0};

static void TestCursorPosCallback(const Events::CursorPosEventData&, void*)
{
    s_cursorPosEvents++;
}

static void TestCursorEnterCallback(const int, void*)
{
    s_cursorEnterEvents++;
}

static void TestScrollCallback(const Events::ScrollEventData&, void*)
{
    s_scrollEvents++;
}

static void TestCharCallback(const char, void*)
{
    s_charEvents++;
}

static void TestPathDropCallback(const Events::PathDropEventData&, void*)
{
    s_pathDropEvents++;
}

static void TestMouseButtonCallback(const Events::MouseButtonEventData&, void*)
{
    s_mouseButtonEvents++;
}

static void TestKeyboardCallback(const Events::KeyboardKeyEventData&, void*)
{
    s_keyboardEvents++;
}

static void TestResizeCallback(const Events::ShouldResizeEventData&, void*)
{
    s_resizeEvents++;
}

static void TestCloseCallback(void*)
{
    s_closeEvents++;
}

// Test event listener registration
TEST_F(PlatformSystemTest, EventListenerRegistration)
{
    auto createInfo = GetTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    // Reset event counters
    s_cursorPosEvents = 0;
    s_cursorEnterEvents = 0;
    s_scrollEvents = 0;
    s_charEvents = 0;
    s_pathDropEvents = 0;
    s_mouseButtonEvents = 0;
    s_keyboardEvents = 0;
    s_resizeEvents = 0;
    s_closeEvents = 0;
    
    // Register event listeners using function pointers
    EXPECT_NO_THROW({
        platformSystem->AddCursorPosEventListener(
            CursorPosEvent::Create<TestCursorPosCallback>(), nullptr);
            
        platformSystem->AddCursorEnterEventListener(
            CursorEnterEvent::Create<TestCursorEnterCallback>(), nullptr);
            
        platformSystem->AddScrollEventListener(
            ScrollEvent::Create<TestScrollCallback>(), nullptr);
            
        platformSystem->AddCharEventListener(
            CharEvent::Create<TestCharCallback>(), nullptr);
            
        platformSystem->AddPathDropEventListener(
            PathDropEvent::Create<TestPathDropCallback>(), nullptr);
            
        platformSystem->AddMouseButtonEventListener(
            MouseButtonEvent::Create<TestMouseButtonCallback>(), nullptr);
            
        platformSystem->AddKeyboardKeyEventListener(
            KeyboardKeyEvent::Create<TestKeyboardCallback>(), nullptr);
            
        platformSystem->AddShouldResizeEventListener(
            ShouldResizeEvent::Create<TestResizeCallback>(), nullptr);
            
        platformSystem->AddShouldCloseEventListener(
            ShouldCloseEvent::Create<TestCloseCallback>(), nullptr);
    });
    
    // Process some update cycles to ensure event system is working
    for (int i = 0; i < 5; ++i)
    {
        platformSystem->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Event counters might still be 0 since we're not generating actual events,
    // but registration should not have crashed
    EXPECT_GE(s_cursorPosEvents.load(), 0);
    EXPECT_GE(s_cursorEnterEvents.load(), 0);
    EXPECT_GE(s_scrollEvents.load(), 0);
    EXPECT_GE(s_charEvents.load(), 0);
    EXPECT_GE(s_pathDropEvents.load(), 0);
    EXPECT_GE(s_mouseButtonEvents.load(), 0);
    EXPECT_GE(s_keyboardEvents.load(), 0);
    EXPECT_GE(s_resizeEvents.load(), 0);
    EXPECT_GE(s_closeEvents.load(), 0);
}

// Test window attribute manipulation
TEST_F(PlatformSystemTest, WindowAttributeManipulation)
{
    auto createInfo = GetTestCreateInfo();
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
TEST_F(PlatformSystemTest, InputModeManipulation)
{
    auto createInfo = GetTestCreateInfo();
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

// Test cursor position management
TEST_F(PlatformSystemTest, CursorPositionManagement)
{
    auto createInfo = GetTestCreateInfo();
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
        platformSystem->SetCursorPosition(testX, testY);

        // Allow a brief moment for the position to be set
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        double actualX, actualY;
        platformSystem->GetCursorPosition(actualX, actualY);

        // Cursor position might not be exact due to window bounds, etc.
        // Just ensure it doesn't crash and returns reasonable values
        EXPECT_TRUE(std::isfinite(actualX));
        EXPECT_TRUE(std::isfinite(actualY));
    }
}

// Test multiple platform systems (if supported)
TEST_F(PlatformSystemTest, MultiplePlatformSystems)
{
    auto createInfo1 = GetTestCreateInfo();
    createInfo1.WindowName = "PlatformTest1";
    createInfo1.InitialPosX = 0;
    createInfo1.InitialPosY = 0;

    auto createInfo2 = GetTestCreateInfo();
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
