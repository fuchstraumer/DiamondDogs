#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "utility/delegate.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <thread>
#include <chrono>

constexpr static PlatformWindowCreateInfo k_SDefaultCreateInfo
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

    PlatformWindowCreateInfo getTestCreateInfo()
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
    auto createInfo = getTestCreateInfo();
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
        auto createInfo = getTestCreateInfo();
        createInfo.DesiredWindowMode = mode;

        auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
        DisplayInfo hardwareDisplay = PlatformWindowSystem::GetPrimaryDisplayInfo();
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
            EXPECT_GT(static_cast<float>(width), static_cast<float>(hardwareDisplay.Width) * 0.8f);
            EXPECT_GT(static_cast<float>(height), static_cast<float>(hardwareDisplay.Height) * 0.8f);
            break;
        case PlatformWindowMode::FullScreenWindowed:
            EXPECT_EQ(width, hardwareDisplay.Width);
            EXPECT_EQ(height, hardwareDisplay.Height);
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
        auto createInfo = getTestCreateInfo();
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
        auto createInfo = getTestCreateInfo();
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
    auto createInfo = getTestCreateInfo();
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
    auto createInfo = getTestCreateInfo();
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
    auto createInfo = getTestCreateInfo();
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


static size_t k_SShouldCloseCallbackCount = 0;
void TestShouldCloseCallback(void* user_data)
{
    k_SShouldCloseCallbackCount++;
}

// Test should close behavior
TEST_F(PlatformSystemTest, ShouldCloseBehavior)
{
    auto createInfo = getTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);
    
    platformSystem->AddShouldCloseEventListener(ShouldCloseEvent::Create<TestShouldCloseCallback>(), nullptr);
    platformSystem->Update();
    platformSystem->SetWindowShouldClose(true);
    platformSystem->Update();
    EXPECT_EQ(k_SShouldCloseCallbackCount, 1);
}

// Static callback functions for event testing
static std::atomic<int> k_SResizeEvents{0};
static std::atomic<int> k_SCloseEvents{0};
static std::atomic<int> k_SLastResizeWidth{0};
static std::atomic<int> k_SLastResizeHeight{0};

static void TestResizeCallback(const Events::ShouldResizeEventData& event_data, void*)
{
    k_SResizeEvents++;
    k_SLastResizeWidth = event_data.Width;
    k_SLastResizeHeight = event_data.Height;
}

static void TestCloseCallback(void*)
{
    k_SCloseEvents++;
}

// The resize listener fires when the window changes size, and the close listener fires once on close
TEST_F(PlatformSystemTest, ResizeAndCloseListeners)
{
    auto createInfo = getTestCreateInfo();
    auto platformSystem = std::make_unique<PlatformWindowSystem>(createInfo);

    k_SResizeEvents = 0;
    k_SCloseEvents = 0;

    platformSystem->AddShouldResizeEventListener(ShouldResizeEvent::Create<TestResizeCallback>(), nullptr);
    platformSystem->AddShouldCloseEventListener(ShouldCloseEvent::Create<TestCloseCallback>(), nullptr);

    constexpr int k_NewWidth = 640;
    constexpr int k_NewHeight = 480;
    platformSystem->SetWindowSize(k_NewWidth, k_NewHeight);
    platformSystem->Update();

    EXPECT_GE(k_SResizeEvents.load(), 1);
    EXPECT_EQ(k_SLastResizeWidth.load(), k_NewWidth);
    EXPECT_EQ(k_SLastResizeHeight.load(), k_NewHeight);
    EXPECT_EQ(k_SCloseEvents.load(), 0);

    platformSystem->SetWindowShouldClose(true);
    platformSystem->Update();

    EXPECT_EQ(k_SCloseEvents.load(), 1);
}

// Test window attribute manipulation
TEST_F(PlatformSystemTest, WindowAttributeManipulation)
{
    auto createInfo = getTestCreateInfo();
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

// Test cursor position management
TEST_F(PlatformSystemTest, CursorPositionManagement)
{
    auto createInfo = getTestCreateInfo();
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
    auto createInfo1 = getTestCreateInfo();
    createInfo1.WindowName = "PlatformTest1";
    createInfo1.InitialPosX = 0;
    createInfo1.InitialPosY = 0;

    auto createInfo2 = getTestCreateInfo();
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
