#include "PlatformSystem.hpp"
#include "PlatformSystemImpl.hpp"
// will include linux or windows version based on CMake configuration
#include "HDRSupport.hpp"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#include <unordered_map>

// Static display query functions - available before initialization
size_t PlatformWindowSystem::GetNumDisplays() noexcept
{
    if (!glfwInit())
    {
        return 0;
    }
    
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    glfwTerminate(); // Clean up since we're just querying
    
    return static_cast<size_t>(count);
}

DisplayInfo PlatformWindowSystem::GetDisplayInfo(const size_t displayIndex) noexcept
{
    if (!glfwInit())
    {
        return DisplayInfo{};
    }
    
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    
    if (displayIndex >= static_cast<size_t>(count) || !monitors)
    {
        glfwTerminate();
        return DisplayInfo{};
    }
    
    GLFWmonitor* monitor = monitors[displayIndex];
    if (!monitor)
    {
        glfwTerminate();
        return DisplayInfo{};
    }
    
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode)
    {
        glfwTerminate();
        return DisplayInfo{};
    }
    
    float xScale, yScale;
    glfwGetMonitorContentScale(monitor, &xScale, &yScale);
    
    DisplayInfo info{};
    info.Width = static_cast<uint32_t>(mode->width);
    info.Height = static_cast<uint32_t>(mode->height);
    info.BitDepthRed = static_cast<uint8_t>(mode->redBits);
    info.BitDepthGreen = static_cast<uint8_t>(mode->greenBits);
    info.BitDepthBlue = static_cast<uint8_t>(mode->blueBits);
    info.DisplayScaleX = xScale;
    info.DisplayScaleY = yScale;
    info.RefreshRate = static_cast<float>(mode->refreshRate);
    info.MonitorIdx = static_cast<int>(displayIndex);
    
    glfwTerminate();
    return info;
}

// Factory function
std::unique_ptr<PlatformWindowSystem>PlatformWindowSystem::CreatePlatformWindowSystem(const PlatformWindowCreateInfo& createInfo, const void* vkInstancePtr)
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

bool PlatformWindowSystem::IsHDRCapable() const noexcept
{
    // queries platform API to determine if HDR is enabled or supported on current config/hardware etc
    // note this does not mean the display or application is currently in HDR mode, just that platform support is enabled for it
    return IsHDREnabled();
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
