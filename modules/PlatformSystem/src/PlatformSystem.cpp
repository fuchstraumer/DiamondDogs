#include "PlatformSystem.hpp"
#include "ImageDataFormats.hpp"
#include "PlatformSystemImpl.hpp"
// will include linux or windows version based on CMake configuration
#include "PlatformDisplayInfo.hpp"
#include "PlatformTypes.hpp"
#include "Swapchain.hpp"
#include "PlatformSurface.hpp"
// rhi
#include "Instance.hpp"
#include "Device.hpp"
#include "nlohmann/json_fwd.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <fstream>
#include <string_view>
#include <utility>
#include <utility>

#include "nlohmann/json.hpp"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

static const std::unordered_map<std::string, PlatformWindowMode> k_WindowModeFromStrMap
{
    { "Windowed", PlatformWindowMode::Windowed },
    { "Fullscreen", PlatformWindowMode::Fullscreen },
    { "FullScreenWindowed", PlatformWindowMode::FullScreenWindowed },
    { "MaximizedWindowed", PlatformWindowMode::MaximizedWindowed }
};

static const std::unordered_map<std::string, ColorSpace> k_ColorSpaceFromStrMap
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

static const std::unordered_map<std::string, PresentMode> k_PresentModeFromStrMap
{
    { "Immediate", PresentMode::Immediate },
    { "VerticalSync", PresentMode::VerticalSync },
    { "VerticalSyncRelaxed", PresentMode::VerticalSyncRelaxed },
    { "VerticalSyncMailbox", PresentMode::VerticalSyncMailbox },
    { "SharedDemandRefresh", PresentMode::SharedDemandRefresh },
    { "SharedContinuousRefresh", PresentMode::SharedContinuousRefresh },
    { "VerticalSyncLatestReady", PresentMode::VerticalSyncLatestReady }
};

static ImageFormat ImageFormatFromString(const std::string_view& image_format_str)
{
    return ImageFormat{};
}

// Pile of defaults for various values, used as fallback when config doesn't contain them
constexpr static uint32_t k_DefaultInitialWidth = 800;
constexpr static uint32_t k_DefaultInitialHeight = 600;
constexpr static PlatformWindowMode k_DefaultWindowMode = PlatformWindowMode::Windowed;
constexpr static ColorSpace k_DefaultColorSpace = ColorSpace::sRGB_Nonlinear;
constexpr static PresentMode k_DefaultPresentMode = PresentMode::VerticalSync;
constexpr static uint32_t k_DefaultSwapchainImageCount = 2;
constexpr static ImageFormat k_DefaultSDRFormat = CommonFormats::RGBA8_SRGB;
// for now, this format seems to be common in retrieved metadata from the system APIs
constexpr static ImageFormat k_DefaultHDRFormat = CommonFormats::HDR_A2R10G10B10;

PlatformWindowSystem::PlatformWindowSystem(const char* json_path, void* rhi_instance, void* rhi_device)
{

    // First, let's query the platform system to get display info. We can use this as fallbacks, and to know if HDR is supported and toggled on the system
    const bool hdrEnabledOnSystem = IsHDREnabledOnSystem();
    const DisplayInfo systemDisplayInfo = RetrievePlatformPrimaryDisplayInfo();

    // Open up the JSON file
    std::ifstream jsonFile(json_path);
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

    uint32_t initialWidth = k_DefaultInitialWidth;
    if (platformJson.contains("InitialWindowWidth"))
    {
        initialWidth = platformJson.at("InitialWindowWidth");
    }

    uint32_t initialHeight = k_DefaultInitialHeight;
    if (platformJson.contains("InitialWindowHeight"))
    {
        initialHeight = platformJson.at("InitialWindowHeight");
    }

    PlatformWindowMode windowMode = k_DefaultWindowMode;
    if (platformJson.contains("InitialWindowMode"))
    {
        const std::string windowModeStr = platformJson.at("InitialWindowMode");
        auto iter = k_WindowModeFromStrMap.find(windowModeStr);
        if (iter != k_WindowModeFromStrMap.end())
        {
            windowMode = iter->second;
        }
    }

    PresentMode presentMode = k_DefaultPresentMode;
    if (platformJson.contains("PresentationMode"))
    {
        const std::string presentModeStr = platformJson.at("PresentationMode");
        auto iter = k_PresentModeFromStrMap.find(presentModeStr);
        if (iter != k_PresentModeFromStrMap.end())
        {
            presentMode = iter->second;
        }
    }

    uint32_t swapchainImageCount = k_DefaultSwapchainImageCount;
    if (platformJson.contains("SwapchainImageCount"))
    {
        uint32_t configImageCount = platformJson.at("SwapchainImageCount");
        swapchainImageCount = std::max(2u, configImageCount);
    }

    ColorSpace desiredColorSpace = k_DefaultColorSpace;
    // ignore JSON configuration if system has a valid detected color space to use and HDR is enabled, since that is more likely to be correct
    if (systemDisplayInfo.ColorCapabilities.DetectedColorSpace != ColorSpace::Invalid && hdrEnabledOnSystem)
    {
        desiredColorSpace = systemDisplayInfo.ColorCapabilities.DetectedColorSpace;
    }
    else if (platformJson.contains("ColorSpace"))
    {
        const std::string colorSpaceStr = platformJson.at("ColorSpace");
        auto iter = k_ColorSpaceFromStrMap.find(colorSpaceStr);
        if (iter != k_ColorSpaceFromStrMap.end())
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

    rhi::Instance* instance = reinterpret_cast<rhi::Instance*>(rhi_instance);
    const uint64_t vkInstanceHandle = instance->Handle().As<uint64_t>();
    rhi::Device* device = reinterpret_cast<rhi::Device*>(rhi_device);
    const uint64_t physicalDeviceHandle = device->GetPhysicalDevice().As<uint64_t>();

    // Now continue to create default surface + swapchain, since we have all the info we need and I don't see a situation in which we wouldn't do this together (yet)
    impl->ActiveDisplay = PlatformWindowSystem::GetPrimaryDisplayInfo(); // for now, just use primary display. In future could allow config of this
    impl->ActiveSurface = std::make_unique<PlatformSurface>(vkInstanceHandle,
                                                            physicalDeviceHandle,
                                                            impl->Window);

    SwapchainCreateInfo swapchainInfo{};
    swapchainInfo.RhiDevice = rhi_device;
    swapchainInfo.PlatformWindowHandle = impl->Window;
    swapchainInfo.VkSurfaceHandle = impl->ActiveSurface->GetVkSurface();
    swapchainInfo.MinImageCount = swapchainImageCount;
    swapchainInfo.SwapchainPresentMode = presentMode;
    swapchainInfo.TryEnableHDR = enableHDR;
    if (enableHDR)
    {
        swapchainInfo.SwapchainFormat = k_DefaultHDRFormat;
        swapchainInfo.DesiredColorSpace = desiredColorSpace;
        swapchainInfo.HdrColorCapabilities = systemDisplayInfo.ColorCapabilities;
    }
    else
    {
        swapchainInfo.SwapchainFormat = k_DefaultSDRFormat;
        swapchainInfo.DesiredColorSpace = desiredColorSpace;
    }
    swapchainInfo.PlatformSystemPtr = this;
    swapchainInfo.DisplayIndex = 0;

    impl->ActiveSwapchain = std::make_unique<Swapchain>(swapchainInfo);
}

// Factory function
PlatformWindowSystem::PlatformWindowSystem(const PlatformWindowCreateInfo& create_info) : impl(std::make_unique<PlatformSystemImpl>(create_info))
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

void PlatformWindowSystem::CreateDefaultSwapchain(void* rhi_instance, void* rhi_device)
{
    rhi::Instance* instance = reinterpret_cast<rhi::Instance*>(rhi_instance);
    const uint64_t vkInstanceHandle = instance->Handle().Get();
    rhi::Device* device = reinterpret_cast<rhi::Device*>(rhi_device);
    const uint64_t vkPhysicalDeviceHandle = device->GetPhysicalDevice().Get();
    
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
    createInfo.RhiDevice = rhi_device;
    createInfo.PlatformWindowHandle = impl->Window;
    createInfo.VkSurfaceHandle = impl->ActiveSurface->GetVkSurface();
    createInfo.PlatformSystemPtr = this;
    createInfo.DisplayIndex = 0u;
    createInfo.MinImageCount = k_DefaultSwapchainImageCount;
    createInfo.SwapchainPresentMode = k_DefaultPresentMode;
    createInfo.TryEnableHDR = enableHDR;
    createInfo.SwapchainFormat = enableHDR ? k_DefaultHDRFormat : k_DefaultSDRFormat;
    createInfo.DesiredColorSpace = enableHDR ? displayInfo.ColorCapabilities.DetectedColorSpace : k_DefaultColorSpace;
    createInfo.HdrColorCapabilities = displayInfo.ColorCapabilities;
    impl->ActiveSwapchain = std::make_unique<Swapchain>(createInfo);
}

void PlatformWindowSystem::CreateSwapchain(const SwapchainCreateInfo& create_info)
{
    if (impl->ActiveSwapchain)
    {
        DestroySwapchain();
    }
    impl->ActiveSwapchain = std::make_unique<Swapchain>(create_info);
}

void PlatformWindowSystem::DestroySwapchain()
{
    if (impl->ActiveSwapchain)
    {
        impl->ActiveSwapchain.reset();
    }
}

SwapchainRecreateResult PlatformWindowSystem::RecreateSwapchain()
{
    return impl->ActiveSwapchain->Recreate();
}

const Swapchain* PlatformWindowSystem::GetActiveSwapchain() const noexcept
{
    return impl->ActiveSwapchain.get();
}

// Event listener registration methods
void PlatformWindowSystem::AddCursorPosEventListener(CursorPosEvent listener, void* user_data)
{
    impl->AddCursorPosEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddCursorEnterEventListener(CursorEnterEvent listener, void* user_data)
{
    impl->AddCursorEnterEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddScrollEventListener(ScrollEvent listener, void* user_data)
{
    impl->AddScrollEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddCharEventListener(CharEvent listener, void* user_data)
{
    impl->AddCharEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddPathDropEventListener(PathDropEvent listener, void* user_data)
{ 
    impl->AddPathDropEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddMouseButtonEventListener(MouseButtonEvent listener, void* user_data)
{ 
    impl->AddMouseButtonEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddKeyboardKeyEventListener(KeyboardKeyEvent listener, void* user_data)
{ 
    impl->AddKeyboardKeyEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddShouldResizeEventListener(ShouldResizeEvent listener, void* user_data)
{
    impl->AddShouldResizeEventListener(std::move(listener), user_data);
}

void PlatformWindowSystem::AddShouldCloseEventListener(ShouldCloseEvent listener, void* user_data)
{
    impl->AddShouldCloseEventListener(std::move(listener), user_data);
}

// Accessors
const DisplayInfo& PlatformWindowSystem::GetActiveDisplayInfo() const noexcept
{
    return impl->ActiveDisplay;
}

void* PlatformWindowSystem::GetWindowHandle() const noexcept
{
    return impl->Window;
}

PresentMode PlatformWindowSystem::GetPresentMode() const noexcept
{
    return impl->ActiveSwapchain->PresentMode();
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

void PlatformWindowSystem::SetWindowSize(int w, int h)
{
    glfwSetWindowSize(impl->Window, w, h);
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

void PlatformWindowSystem::SetWindowShouldClose(bool should_close)
{
    glfwSetWindowShouldClose(impl->Window, should_close ? GLFW_TRUE : GLFW_FALSE);
}

DisplayInfo PlatformWindowSystem::GetPrimaryDisplayInfo() noexcept
{
    return RetrievePlatformPrimaryDisplayInfo();
}