#pragma once
#ifndef DIAMOND_DOGS_RENDERING_CONTEXT_HPP
#define DIAMOND_DOGS_RENDERING_CONTEXT_HPP
#include "signal/delegate.hpp"
#include <memory>
#include <vector>
#include <string>

namespace vpr {
    class Instance;
    class PhysicalDevice;
    class Device;
    class Swapchain;
    class SurfaceKHR;
}

class PlatformWindow;
struct GLFWwindow;
struct GLFWcursor;
struct GLFWimage;
using cursor_pos_callback_t = delegate_t<void(double pos_x, double pos_y)>;
using cursor_enter_callback_t = delegate_t<void(int enter)>;
using scroll_callback_t = delegate_t<void(double scroll_x, double scroll_y)>;
using char_callback_t = delegate_t<void(unsigned int code_point)>;
using path_drop_callback_t = delegate_t<void(int count, const char** paths)>;
using mouse_button_callback_t = delegate_t<void(int button, int action, int mods)>;
using keyboard_key_callback_t = delegate_t<void(int key, int scancode, int action, int mods)>;

struct SwapchainCallbacks {
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> SwapchainCreated;
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> BeginResize;
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> CompleteResize;
    delegate_t<void(VkSwapchainKHR handle)> SwapchainDestroyed;
};

class RenderingContext {
    RenderingContext() noexcept = default;
    ~RenderingContext() noexcept = default;
    RenderingContext(const RenderingContext&) = delete;
    RenderingContext& operator=(const RenderingContext&) = delete;
public:
    
    static RenderingContext& Get() noexcept;
    static void SetShouldResize(const bool val);
    static bool ShouldResizeExchange(const bool val);

    void Construct(const char* cfg_file_path);
    void Update();
    void Destroy();

    vpr::Instance* Instance() noexcept;
    vpr::PhysicalDevice* PhysicalDevice(const size_t idx = 0) noexcept;
    vpr::Device* Device() noexcept;
    vpr::Swapchain* Swapchain() noexcept;
    vpr::SurfaceKHR* Surface() noexcept;
    PlatformWindow* Window() noexcept;
    GLFWwindow* glfwWindow() noexcept;
    
    const std::vector<std::string>& InstanceExtensions() const noexcept;
    const std::vector<std::string>& DeviceExtensions() const noexcept;

    static void AddSwapchainCallbacks(SwapchainCallbacks callbacks);
    static void GetWindowSize(int& w, int& h);
    static void GetFramebufferSize(int& w, int& h);
    static void RegisterCursorPosCallback(cursor_pos_callback_t callback_fn);
    static void RegisterCursorEnterCallback(cursor_enter_callback_t callback_fn);
    static void RegisterScrollCallback(scroll_callback_t callback_fn);
    static void RegisterCharCallback(char_callback_t callback_fn);
    static void RegisterPathDropCallback(path_drop_callback_t callback_fn);
    static void RegisterMouseButtonCallback(mouse_button_callback_t callback_fn);
    static void RegisterKeyboardKeyCallback(keyboard_key_callback_t callback_fn);
    static int GetMouseButton(int button);
    static void GetCursorPosition(double& x, double& y);
    static void SetCursorPosition(double x, double y);
    static void SetCursor(GLFWcursor* cursor);
    static GLFWcursor* CreateCursor(GLFWimage* image, int w, int h);
    static GLFWcursor* CreateStandardCursor(int type);
    static void DestroyCursor(GLFWcursor* cursor);
    static bool ShouldWindowClose();
    static int GetWindowAttribute(int attribute);
    static void SetInputMode(int mode, int value);
    static int GetInputMode(int mode);

private:

    std::unique_ptr<PlatformWindow> window;
    std::unique_ptr<vpr::Instance> vulkanInstance;
    std::vector<std::unique_ptr<vpr::PhysicalDevice>> physicalDevices;
    std::unique_ptr<vpr::Device> logicalDevice;
    std::unique_ptr<vpr::Swapchain> swapchain;
    std::unique_ptr<vpr::SurfaceKHR> windowSurface;

    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::string windowMode;
    uint32_t syncMode;
    std::string syncModeStr;
    
};

#endif //!DIAMOND_DOGS_RENDERING_CONTEXT_HPP
