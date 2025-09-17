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
    { "None", PresentMode::None },
    { "VerticalSync", PresentMode::VerticalSync },
    { "VerticalSyncRelaxed", PresentMode::VerticalSyncRelaxed },
    { "VerticalSyncMailbox", PresentMode::VerticalSyncMailbox }
};

ImageFormat ImageFormatFromString(const std::string_view& imageFormatStr)
{

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

std::unique_ptr<PlatformWindowSystem> PlatformWindowSystem::CreatePlatformWindowSystem(const char* jsonPath, const void* vkInstancePtr)
{
    // Open up the JSON file
    std::ifstream jsonFile(jsonPath);
    if (!jsonFile.is_open())
    {
        return nullptr;
    }

    // Parse the JSON file
    nlohmann::json jsonConfig;
    jsonFile >> jsonConfig;

    // First check: is this a composite categorized configuration file, or a direct one?
    if (jsonConfig.contains("PlatformWindowConfig"))
    {
        // alias to the actual config section we want
        jsonConfig = jsonConfig["PlatformWindowConfig"];
    }

    uint32_t initialWidth = s_DefaultInitialWidth;
    if (jsonConfig.contains("InitialWindowWidth"))
    {
        initialWidth = jsonConfig.at("InitialWindowWidth");
    }

    uint32_t initialHeight = s_DefaultInitialHeight;
    if (jsonConfig.contains("InitialWindowHeight"))
    {
        initialHeight = jsonConfig.at("InitialWindowHeight");
    }

    PlatformWindowMode windowMode = s_DefaultWindowMode;
    if (jsonConfig.contains("InitialWindowMode"))
    {
        const std::string windowModeStr = jsonConfig.at("InitialWindowMode");
        auto iter = s_WindowModeFromStrMap.find(windowModeStr);
        if (iter != s_WindowModeFromStrMap.end())
        {
            windowMode = iter->second;
        }
    }

    PresentMode presentMode = s_DefaultPresentMode;
    if (jsonConfig.contains("PresentationMode"))
    {
        const std::string presentModeStr = jsonConfig.at("PresentationMode");
        auto iter = s_PresentModeFromStrMap.find(presentModeStr);
        if (iter != s_PresentModeFromStrMap.end())
        {
            presentMode = iter->second;
        }
    }

    uint32_t swapchainImageCount = s_DefaultSwapchainImageCount;
    if (jsonConfig.contains("SwapchainImageCount"))
    {
        uint32_t configImageCount = jsonConfig.at("SwapchainImageCount");
        swapchainImageCount = std::max(2u, configImageCount);
    }

    ColorSpace desiredColorSpace = s_DefaultColorSpace;
    if (jsonConfig.contains("ColorSpace"))
    {
        const std::string colorSpaceStr = jsonConfig.at("ColorSpace");
        auto iter = s_ColorSpaceFromStrMap.find(colorSpaceStr);
        if (iter != s_ColorSpaceFromStrMap.end())
        {
            desiredColorSpace = iter->second;
        }
    }

    bool enableHDR = false;
    if (jsonConfig.contains("EnableHDR"))
    {
        enableHDR = jsonConfig.at("EnableHDR");
    }
    // make sure that even if config tries to enable it, system supports it and has it on as well
    enableHDR = enableHDR && IsHDREnabledOnSystem();

    // Create the platform window system
    auto system = std::make_unique<PlatformWindowSystem>();
    system->impl = std::make_unique<PlatformSystemImpl>(PlatformWindowCreateInfo{});

    return system;
}

// Factory function
std::unique_ptr<PlatformWindowSystem> PlatformWindowSystem::CreatePlatformWindowSystem(const PlatformWindowCreateInfo& createInfo, const void* vkInstancePtr)
{
    try
    {
        auto system = std::make_unique<PlatformWindowSystem>();
        system->impl = std::make_unique<PlatformSystemImpl>(createInfo);
        
        // Set active display to the requested display, or primary display if none specified
        if (createInfo.DisplayToUse != nullptr)
        {
            system->impl->ActiveDisplay = createInfo.DisplayToUse;
        }
        else
        {
            // Fallback to primary display buffer if no displays were enumerated
            system->impl->ActiveDisplay = &s_PrimaryDisplay;
        }
        
        // Note: vkInstancePtr is passed but not currently used in PlatformSystem
        // In the future this could be used for Vulkan surface creation
        
        return system;
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
}

// Constructor and Destructor
PlatformWindowSystem::PlatformWindowSystem()
    : impl(nullptr)
{
}

PlatformWindowSystem::~PlatformWindowSystem()
{
}

void PlatformWindowSystem::Destroy()
{
    impl.reset();
}

void PlatformWindowSystem::CreateDefaultSwapchain(
    const uint64_t vkInstanceHandle,
    const uint64_t vkDeviceHandle,
    const uint64_t vkPhysicalDeviceHandle)
{
    const VkInstance vkInstance = reinterpret_cast<VkInstance>(vkInstanceHandle);
    const VkDevice vkDevice = reinterpret_cast<VkDevice>(vkDeviceHandle);
    const VkPhysicalDevice vkPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(vkPhysicalDeviceHandle);
    
    if (!impl->ActiveSurface)
    {
        // Create default platform surface
        impl->ActiveSurface = std::make_unique<PlatformSurface>(vkInstance, vkPhysicalDevice, impl->Window);
    }

    if (impl->ActiveSwapchain)
    {
        DestroySwapchain();
    }

    SwapchainCreateInfo createInfo{};
    createInfo.DeviceHandle = vkDeviceHandle;
    createInfo.PhysicalDeviceHandle = vkPhysicalDeviceHandle;
    createInfo.PlatformWindowHandle = impl->Window;
    createInfo.SurfaceHandle = reinterpret_cast<uint64_t>(impl->ActiveSurface->GetVkSurface());
    createInfo.PlatformSystemPtr = this;
    createInfo.DisplayIndex = static_cast<uint32_t>(impl->ActiveDisplay->MonitorIdx);
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
