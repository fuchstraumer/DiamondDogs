#include "PlatformSystem.hpp"
#include "PlatformSystemImpl.hpp"

// will include linux or windows version based on CMake configuration
#include "PlatformDisplayInfo.hpp"
#include "Swapchain.hpp"
#include "PlatformSurface.hpp"

// rhi
#include "Instance.hpp"
#include "PhysicalDevice.hpp"
#include "Device.hpp"

#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
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
constexpr static ImageFormat s_DefaultSDRFormat = CommonFormats::RGBA8_SRGB;
// for now, this format seems to be common in retrieved metadata from the system APIs
constexpr static ImageFormat s_DefaultHDRFormat = CommonFormats::HDR_A2R10G10B10;

PlatformWindowSystem::PlatformWindowSystem(const char* jsonPath, void* RhiInstance, void* RhiDevice)
{

    // First, let's query the platform system to get display info. We can use this as fallbacks, and to know if HDR is supported and toggled on the system
    const bool hdrEnabledOnSystem = IsHDREnabledOnSystem();
    const DisplayInfo systemDisplayInfo = RetrievePlatformPrimaryDisplayInfo();

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
    // ignore JSON configuration if system has a valid detected color space to use and HDR is enabled, since that is more likely to be correct
    if (systemDisplayInfo.ColorCapabilities.DetectedColorSpace != ColorSpace::Invalid && hdrEnabledOnSystem)
    {
        desiredColorSpace = systemDisplayInfo.ColorCapabilities.DetectedColorSpace;
    }
    else if (platformJson.contains("ColorSpace"))
    {
        const std::string colorSpaceStr = platformJson.at("ColorSpace");
        auto iter = s_ColorSpaceFromStrMap.find(colorSpaceStr);
        if (iter != s_ColorSpaceFromStrMap.end())
        {
            desiredColorSpace = iter->second;
        }
    }

    bool enableHDR = hdrEnabledOnSystem;
    if (platformJson.contains("EnableHDR"))
    {
        enableHDR = platformJson.at("EnableHDR");
    }

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

    rhi::Instance* instance = reinterpret_cast<rhi::Instance*>(RhiInstance);
    const uint64_t vkInstanceHandle = (uint64_t)instance->vkHandle();
    rhi::Device* device = reinterpret_cast<rhi::Device*>(RhiDevice);
    const uint64_t physicalDeviceHandle = (uint64_t)device->GetPhysicalDevice().vkHandle();

    // Now continue to create default surface + swapchain, since we have all the info we need and I don't see a situation in which we wouldn't do this together (yet)
    impl->ActiveDisplay = PlatformWindowSystem::GetPrimaryDisplayInfo(); // for now, just use primary display. In future could allow config of this
    impl->ActiveSurface = std::make_unique<PlatformSurface>(vkInstanceHandle,
                                                            physicalDeviceHandle,
                                                            impl->Window);

    SwapchainCreateInfo swapchainInfo{};
    swapchainInfo.RhiDevice = (void*)RhiDevice;
    swapchainInfo.PlatformWindowHandle = impl->Window;
    swapchainInfo.VkSurfaceHandle = impl->ActiveSurface->GetVkSurface();
    swapchainInfo.MinImageCount = swapchainImageCount;
    swapchainInfo.SwapchainPresentMode = presentMode;
    swapchainInfo.TryEnableHDR = enableHDR;
    if (enableHDR)
    {
        swapchainInfo.SwapchainFormat = s_DefaultHDRFormat;
        swapchainInfo.DesiredColorSpace = desiredColorSpace;
        swapchainInfo.HdrColorCapabilities = systemDisplayInfo.ColorCapabilities;
    }
    else
    {
        swapchainInfo.SwapchainFormat = s_DefaultSDRFormat;
        swapchainInfo.DesiredColorSpace = desiredColorSpace;
    }
    swapchainInfo.PlatformSystemPtr = this;
    swapchainInfo.DisplayIndex = 0;

    impl->ActiveSwapchain = std::make_unique<Swapchain>(swapchainInfo);
}

// Factory function
PlatformWindowSystem::PlatformWindowSystem(const PlatformWindowCreateInfo& createInfo) : impl(std::make_unique<PlatformSystemImpl>(createInfo))
{
}

PlatformWindowSystem::~PlatformWindowSystem()
{
    Destroy();
}

void PlatformWindowSystem::Destroy()
{
    if (!impl)
    {
        return;
    }
    // make sure swapchain is destroyed before surface, because otherwise validation layers will complain
    impl->ActiveSwapchain.reset();
    impl->ActiveSurface.reset();
    // dtor will get the rest
    impl.reset();
}

void PlatformWindowSystem::CreateDefaultSwapchain(void* rhiInstance, void* rhiDevice)
{
    rhi::Instance* instance = reinterpret_cast<rhi::Instance*>(rhiInstance);
    const uint64_t vkInstanceHandle = (uint64_t)instance->vkHandle();
    rhi::Device* device = reinterpret_cast<rhi::Device*>(rhiDevice);
    const uint64_t vkPhysicalDeviceHandle = (uint64_t)device->GetPhysicalDevice().vkHandle();
    
    if (!impl->ActiveSurface)
    {
        // Create default platform surface
        impl->ActiveSurface = std::make_unique<PlatformSurface>(vkInstanceHandle, vkPhysicalDeviceHandle, impl->Window);
    }

    if (impl->ActiveSwapchain)
    {
        DestroySwapchain();
    }

    const DisplayInfo& displayInfo = impl->ActiveDisplay;
    const bool enableHDR = displayInfo.ColorCapabilities.hdrEnabled;

    SwapchainCreateInfo createInfo{};
    createInfo.RhiDevice = rhiDevice;
    createInfo.PlatformWindowHandle = impl->Window;
    createInfo.VkSurfaceHandle = impl->ActiveSurface->GetVkSurface();
    createInfo.PlatformSystemPtr = this;
    createInfo.DisplayIndex = 0u;
    createInfo.MinImageCount = s_DefaultSwapchainImageCount;
    createInfo.SwapchainPresentMode = s_DefaultPresentMode;
    createInfo.TryEnableHDR = enableHDR;
    createInfo.SwapchainFormat = enableHDR ? s_DefaultHDRFormat : s_DefaultSDRFormat;
    createInfo.DesiredColorSpace = enableHDR ? displayInfo.ColorCapabilities.DetectedColorSpace : s_DefaultColorSpace;
    createInfo.HdrColorCapabilities = displayInfo.ColorCapabilities;
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
    return impl->ActiveDisplay;
}

const void* PlatformWindowSystem::GetWindowHandle() const noexcept
{
    return impl->Window;
}

PresentMode PlatformWindowSystem::GetPresentMode() const noexcept
{
    return impl->ActiveSwapchain->GetPresentMode();
}

PlatformWindowMode PlatformWindowSystem::GetWindowMode() const noexcept
{
    return PlatformWindowMode();
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

void PlatformWindowSystem::GetWindowPos(int& x, int& y) const
{
    glfwGetWindowPos(impl->Window, &x, &y);
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

void PlatformWindowSystem::SetWindowShouldClose(bool shouldClose)
{
    glfwSetWindowShouldClose(impl->Window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
}

DisplayInfo PlatformWindowSystem::GetPrimaryDisplayInfo() noexcept
{
    return RetrievePlatformPrimaryDisplayInfo();
}