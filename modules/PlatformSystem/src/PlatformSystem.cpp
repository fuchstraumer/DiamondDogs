#include "PlatformSystem.hpp"
#include "PlatformSystemImpl.hpp"
// will include linux or windows version based on CMake configuration
#include "HDRSupport.hpp"
#include "Swapchain.hpp"
#include "PlatformSurface.hpp"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <string_view>
#include "nlohmann/json.hpp"

static const std::unordered_map<std::string, PlatformWindowMode> s_WindowModeFromStrMap
{
    { "Windowed", PlatformWindowMode::Windowed },
    { "Fullscreen", PlatformWindowMode::Fullscreen },
    { "FullScreenWindowed", PlatformWindowMode::FullScreenWindowed },
    { "MaximizedWindowed", PlatformWindowMode::MaximizedWindowed }
};

static const std::unordered_map<std::string, ColorSpace> s_ColorSpaceFromStrMap
{
    { "sRGB_Nonlinear", ColorSpace::sRGB_Nonlinear },
    { "Display_P3_Nonlinear", ColorSpace::Display_P3_Nonlinear },
    { "Extended_sRGB_Linear", ColorSpace::Extended_sRGB_Linear },
    { "Display_P3_Linear", ColorSpace::Display_P3_Linear },
    { "DCI_P3_Nonlinear", ColorSpace::DCI_P3_Nonlinear },
    { "BT709_Linear", ColorSpace::BT709_Linear },
    { "BT709_Nonlinear", ColorSpace::BT709_Nonlinear },
    { "BT2020_Linear", ColorSpace::BT2020_Linear },
    { "HDR10_ST2084", ColorSpace::HDR10_ST2084 },
    { "HDR10_HLG", ColorSpace::HDR10_HLG },
    { "Extended_sRGB_Nonlinear", ColorSpace::Extended_sRGB_Nonlinear },
    { "PassThrough", ColorSpace::PassThrough },
    { "DisplayNativeAMD", ColorSpace::DisplayNativeAMD }
};

static const std::unordered_map<std::string, PresentMode> s_PresentModeFromStrMap
{
    { "Immediate", PresentMode::Immediate },
    { "VerticalSync", PresentMode::VerticalSync },
    { "VerticalSyncRelaxed", PresentMode::VerticalSyncRelaxed },
    { "VerticalSyncMailbox", PresentMode::VerticalSyncMailbox },
    { "SharedDemandRefresh", PresentMode::SharedDemandRefresh },
    { "SharedContinuousRefresh", PresentMode::SharedContinuousRefresh },
    { "VerticalSyncLatestReady", PresentMode::VerticalSyncLatestReady }
};

ImageFormat ImageFormatFromString(const std::string_view& imageFormatStr)
{
    return ImageFormat{};
}

// Pile of defaults for various values, used as fallback when config doesn't contain them
constexpr static uint32_t s_DefaultInitialWidth = 800;
constexpr static uint32_t s_DefaultInitialHeight = 600;
constexpr static PlatformWindowMode s_DefaultWindowMode = PlatformWindowMode::Windowed;
constexpr static ColorSpace s_DefaultColorSpace = ColorSpace::sRGB_Nonlinear;
constexpr static PresentMode s_DefaultPresentMode = PresentMode::VerticalSync;
constexpr static uint32_t s_DefaultSwapchainImageCount = 2;
constexpr static ColorSpace s_DefaultSDRColorSpace = s_DefaultColorSpace;
constexpr static ColorSpace s_DefaultHDRColorSpace = ColorSpace::HDR10_ST2084;
constexpr static ImageFormat s_DefaultSDRFormat = CommonFormats::RGBA8_SRGB;
constexpr static ImageFormat s_DefaultHDRFormat = CommonFormats::HDR_RGBA16;

PlatformWindowSystem::PlatformWindowSystem(const char* jsonPath,
                                           const uint64_t vkInstanceHandle,
                                           const uint64_t vkDeviceHandle,
                                           const uint64_t vkPhysicalDeviceHandle)
{
    // Open up the JSON file
    std::ifstream jsonFile(jsonPath);
    if (!jsonFile.is_open())
    {
        throw std::runtime_error("Failed to open PlatformWindowSystem configuration JSON file.");
    }

    // Parse the JSON file
    nlohmann::json jsonConfig;
    jsonFile >> jsonConfig;
    nlohmann::json platformJson;

    // First check: is this a composite categorized configuration file, or a direct one?
    if (jsonConfig.contains("PlatformWindowConfig"))
    {
        // alias to the actual config section we want
        platformJson = jsonConfig["PlatformWindowConfig"];
    }
    else
    {
        platformJson = jsonConfig;
    }

    uint32_t initialWidth = s_DefaultInitialWidth;
    if (platformJson.contains("InitialWindowWidth"))
    {
        initialWidth = jsonConfig.at("InitialWindowWidth");
    }

    uint32_t initialHeight = s_DefaultInitialHeight;
    if (platformJson.contains("InitialWindowHeight"))
    {
        initialHeight = platformJson.at("InitialWindowHeight");
    }

    PlatformWindowMode windowMode = s_DefaultWindowMode;
    if (platformJson.contains("InitialWindowMode"))
    {
        const std::string windowModeStr = platformJson.at("InitialWindowMode");
        auto iter = s_WindowModeFromStrMap.find(windowModeStr);
        if (iter != s_WindowModeFromStrMap.end())
        {
            windowMode = iter->second;
        }
    }

    PresentMode presentMode = s_DefaultPresentMode;
    if (platformJson.contains("PresentationMode"))
    {
        const std::string presentModeStr = platformJson.at("PresentationMode");
        auto iter = s_PresentModeFromStrMap.find(presentModeStr);
        if (iter != s_PresentModeFromStrMap.end())
        {
            presentMode = iter->second;
        }
    }

    uint32_t swapchainImageCount = s_DefaultSwapchainImageCount;
    if (platformJson.contains("SwapchainImageCount"))
    {
        uint32_t configImageCount = platformJson.at("SwapchainImageCount");
        swapchainImageCount = std::max(2u, configImageCount);
    }

    ColorSpace desiredColorSpace = s_DefaultColorSpace;
    if (platformJson.contains("ColorSpace"))
    {
        const std::string colorSpaceStr = platformJson.at("ColorSpace");
        auto iter = s_ColorSpaceFromStrMap.find(colorSpaceStr);
        if (iter != s_ColorSpaceFromStrMap.end())
        {
            desiredColorSpace = iter->second;
        }
    }

    bool enableHDR = false;
    if (platformJson.contains("EnableHDR"))
    {
        enableHDR = platformJson.at("EnableHDR");
    }
    // make sure that even if config tries to enable it, system supports it and has it on as well
    enableHDR = enableHDR && IsHDREnabledOnSystem();

    // now grab EngineConfig to get the application name we'll use for the window title
    std::string applicationName = "DiamondDogs Application";
    if (jsonConfig.contains("EngineConfig"))
    {
        const nlohmann::json& engineJson = jsonConfig["EngineConfig"];
        if (engineJson.contains("ApplicationName"))
        {
            applicationName = engineJson.at("ApplicationName");
        }
    }
    else if (platformJson.contains("ApplicationName"))
    {
        applicationName = platformJson.at("ApplicationName");
    }

    PlatformWindowCreateInfo createInfo{};
    createInfo.WindowName = applicationName.c_str();
    createInfo.InitialWidth = initialWidth;
    createInfo.InitialHeight = initialHeight;
    createInfo.DesiredWindowMode = windowMode;
    createInfo.BehaviorFlags.Resizable = true;
    createInfo.BehaviorFlags.Moveable = true;
    createInfo.BehaviorFlags.Decorated = true;
    createInfo.BehaviorFlags.FocusOnShow = false;
    createInfo.BehaviorFlags.CenterMouse = false;
    
    // Create the platform window system
    impl = std::make_unique<PlatformSystemImpl>(createInfo);

    // Now continue to create default surface + swapchain, since we have all the info we need and I don't see a situation in which we wouldn't do this together (yet)
    impl->ActiveDisplay = &s_PrimaryDisplay; // for now, just use primary display. In future could allow config of this
    impl->ActiveSurface = std::make_unique<PlatformSurface>(vkInstanceHandle,
                                                            vkPhysicalDeviceHandle,
                                                            impl->Window);

    SwapchainCreateInfo swapchainInfo{};
    swapchainInfo.VkDeviceHandle = vkDeviceHandle;
    swapchainInfo.VkPhysicalDeviceHandle = vkPhysicalDeviceHandle;
    swapchainInfo.PlatformWindowHandle = impl->Window;
    swapchainInfo.VkSurfaceHandle = impl->ActiveSurface->GetVkSurface();
    swapchainInfo.MinImageCount = swapchainImageCount;
    swapchainInfo.SwapchainPresentMode = presentMode;
    swapchainInfo.TryEnableHDR = enableHDR;
    if (enableHDR)
    {
        swapchainInfo.SwapchainFormat = s_DefaultHDRFormat;
        swapchainInfo.HdrColorSpace = desiredColorSpace;
        swapchainInfo.SdrColorSpace = s_DefaultSDRColorSpace;
    }
    else
    {
        swapchainInfo.SwapchainFormat = s_DefaultSDRFormat;
        swapchainInfo.SdrColorSpace = desiredColorSpace;
        swapchainInfo.HdrColorSpace = s_DefaultHDRColorSpace;
    }
    swapchainInfo.PlatformSystemPtr = this;
    swapchainInfo.DisplayIndex = 0;

    impl->ActiveSwapchain = std::make_unique<Swapchain>(swapchainInfo);
}

// Factory function
PlatformWindowSystem::PlatformWindowSystem(const PlatformWindowCreateInfo& createInfo) : impl(std::make_unique<PlatformSystemImpl>(createInfo))
{
    // Set active display to the requested display, or primary display if none specified
    if (createInfo.DisplayToUse != nullptr)
    {
        impl->ActiveDisplay = createInfo.DisplayToUse;
    }
    else
    {
        // Fallback to primary display buffer if no displays were enumerated
        impl->ActiveDisplay = &s_PrimaryDisplay;
    }
}

PlatformWindowSystem::~PlatformWindowSystem()
{
}

void PlatformWindowSystem::Destroy()
{
    // make sure swapchain is destroyed before surface, because otherwise validation layers will complain
    impl->ActiveSwapchain.reset();
    impl->ActiveSurface.reset();
    // dtor will get the rest
    impl.reset();
}

void PlatformWindowSystem::CreateDefaultSwapchain(
    const uint64_t vkInstanceHandle,
    const uint64_t vkDeviceHandle,
    const uint64_t vkPhysicalDeviceHandle)
{
    
    if (!impl->ActiveSurface)
    {
        // Create default platform surface
        impl->ActiveSurface = std::make_unique<PlatformSurface>(vkInstanceHandle, vkPhysicalDeviceHandle, impl->Window);
    }

    if (impl->ActiveSwapchain)
    {
        DestroySwapchain();
    }

    SwapchainCreateInfo createInfo{};
    createInfo.VkDeviceHandle = vkDeviceHandle;
    createInfo.VkPhysicalDeviceHandle = vkPhysicalDeviceHandle;
    createInfo.PlatformWindowHandle = impl->Window;
    createInfo.VkSurfaceHandle = impl->ActiveSurface->GetVkSurface();
    createInfo.PlatformSystemPtr = this;
    createInfo.DisplayIndex = 0u;
    createInfo.MinImageCount = s_DefaultSwapchainImageCount;
    createInfo.SwapchainPresentMode = s_DefaultPresentMode;
    createInfo.SwapchainFormat = s_DefaultSDRFormat;
    createInfo.SdrColorSpace = s_DefaultSDRColorSpace;
    createInfo.HdrColorSpace = s_DefaultHDRColorSpace;
    createInfo.TryEnableHDR = false;
    impl->ActiveSwapchain = std::make_unique<Swapchain>(createInfo);
}

void PlatformWindowSystem::CreateSwapchain(const SwapchainCreateInfo& createInfo)
{
    if (impl->ActiveSwapchain)
    {
        DestroySwapchain();
    }
    impl->ActiveSwapchain = std::make_unique<Swapchain>(createInfo);
}

void PlatformWindowSystem::DestroySwapchain()
{
    if (impl->ActiveSwapchain)
    {
        impl->ActiveSwapchain.reset();
    }
}

// Event listener registration methods
void PlatformWindowSystem::AddCursorPosEventListener(CursorPosEvent listener, void* userData)
{
    impl->AddCursorPosEventListener(listener, userData);
}

void PlatformWindowSystem::AddCursorEnterEventListener(CursorEnterEvent listener, void* userData)
{
    impl->AddCursorEnterEventListener(listener, userData);
}

void PlatformWindowSystem::AddScrollEventListener(ScrollEvent listener, void* userData)
{
    impl->AddScrollEventListener(listener, userData);
}

void PlatformWindowSystem::AddCharEventListener(CharEvent listener, void* userData)
{
    impl->AddCharEventListener(listener, userData);
}

void PlatformWindowSystem::AddPathDropEventListener(PathDropEvent listener, void* userData)
{ 
    impl->AddPathDropEventListener(listener, userData);
}

void PlatformWindowSystem::AddMouseButtonEventListener(MouseButtonEvent listener, void* userData)
{ 
    impl->AddMouseButtonEventListener(listener, userData);
}

void PlatformWindowSystem::AddKeyboardKeyEventListener(KeyboardKeyEvent listener, void* userData)
{ 
    impl->AddKeyboardKeyEventListener(listener, userData);
}

void PlatformWindowSystem::AddShouldResizeEventListener(ShouldResizeEvent listener, void* userData)
{
    impl->AddShouldResizeEventListener(listener, userData);
}

void PlatformWindowSystem::AddShouldCloseEventListener(ShouldCloseEvent listener, void* userData)
{
    impl->AddShouldCloseEventListener(listener, userData);
}

// Accessors
const DisplayInfo& PlatformWindowSystem::GetActiveDisplayInfo() const noexcept
{
    return *impl->ActiveDisplay;
}

const void* PlatformWindowSystem::GetWindowHandle() const noexcept
{
    return impl->Window;
}

// Lifecycle methods
void PlatformWindowSystem::Update()
{
    impl->Update();
}

void PlatformWindowSystem::WaitForEvents()
{
    impl->WaitForEvents();
}

bool PlatformWindowSystem::ShouldWindowClose() const
{
    return glfwWindowShouldClose(impl->Window) != 0;
}

bool PlatformWindowSystem::IsHDREnabledOnSystem() noexcept
{
    // queries platform API to determine if HDR is enabled or supported on current config/hardware etc
    // note this does not mean the display or application is currently in HDR mode, just that platform support is enabled for it
    return IsHDRSupportedAndEnabled();
}

// Window and input management methods (mirroring deprecated RhiSystem functionality)
void PlatformWindowSystem::GetWindowSize(int& w, int& h) const
{
    glfwGetWindowSize(impl->Window, &w, &h);
}

void PlatformWindowSystem::GetFramebufferSize(int& w, int& h) const
{
    glfwGetFramebufferSize(impl->Window, &w, &h);
}

int PlatformWindowSystem::GetMouseButton(int button) const
{
    return glfwGetMouseButton(impl->Window, button);
}

void PlatformWindowSystem::GetCursorPosition(double& x, double& y) const
{
    glfwGetCursorPos(impl->Window, &x, &y);
}

void PlatformWindowSystem::SetCursorPosition(double x, double y)
{
    glfwSetCursorPos(impl->Window, x, y);
}

int PlatformWindowSystem::GetWindowAttribute(int attrib) const
{
    return glfwGetWindowAttrib(impl->Window, attrib);
}

void PlatformWindowSystem::SetWindowAttribute(int attrib, int value)
{
    glfwSetWindowAttrib(impl->Window, attrib, value);
}

int PlatformWindowSystem::GetInputMode(int mode) const
{
    return glfwGetInputMode(impl->Window, mode);
}

void PlatformWindowSystem::SetInputMode(int mode, int val)
{
    glfwSetInputMode(impl->Window, mode, val);
}
