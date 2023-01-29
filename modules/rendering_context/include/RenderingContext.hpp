#pragma once
#ifndef DIAMOND_DOGS_RENDERING_CONTEXT_HPP
#define DIAMOND_DOGS_RENDERING_CONTEXT_HPP
#include "utility/delegate.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <vulkan/vulkan_core.h>
#include <nlohmann/json_fwd.hpp>

#ifdef VTF_DEBUG_INFO_DISABLE
constexpr static bool VTF_USE_DEBUG_INFO = false;
#else
constexpr static bool VTF_USE_DEBUG_INFO = true;
#endif

#define VTF_VALIDATION_ENABLED_CONF
#ifdef VTF_VALIDATION_ENABLED_CONF
constexpr static bool VTF_VALIDATION_ENABLED = true;
#else
constexpr static bool VTF_VALIDATION_ENABLED = false;
#endif

#define VTF_DEBUG_INFO_THREADING_CONF
#ifdef VTF_DEBUG_INFO_THREADING_CONF
constexpr static bool VTF_DEBUG_INFO_THREADING = true;
#else
constexpr static bool VTF_DEBUG_INFO_THREADING = false;
#endif

// Really need C++20 for this to work ideally, I feel
#define VTF_DEBUG_INFO_THREADING_CONF
#ifdef VTF_DEBUG_INFO_TIMESTAMPS_CONF
constexpr static bool VTF_DEBUG_INFO_TIMESTAMPS = true;
#else
constexpr static bool VTF_DEBUG_INFO_TIMESTAMPS = false;
#endif

#ifdef VTF_DEBUG_INFO_CALLING_FN_CONF
// In current configuration, this macro adds the calling function name and line to the objects name

#else
// In current configuration, this macro just returns the name without modification
#define VTF_DEBUG_OBJECT_NAME(name) name
#endif

namespace vpr
{
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

using post_physical_device_pre_logical_device_function_t = void(*)(VkPhysicalDevice dvc, VkPhysicalDeviceFeatures** features, void** pNext);
using post_logical_device_function_t = void(*)(void* pNext);

struct SwapchainCallbacks
{
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> SwapchainCreated;
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> BeginResize;
    delegate_t<void(VkSwapchainKHR handle, uint32_t width, uint32_t height)> CompleteResize;
    delegate_t<void(VkSwapchainKHR handle)> SwapchainDestroyed;
};

struct DescriptorLimits
{
    DescriptorLimits(const vpr::PhysicalDevice* physicalDevice);
    uint32_t MaxSamplers;
    uint32_t MaxUniformBuffers;
    uint32_t MaxDynamicUniformBuffers;
    uint32_t MaxStorageBuffers;
    uint32_t MaxDynamicStorageBuffers;
    uint32_t MaxSampledImages;
    uint32_t MaxStorageImages;
    uint32_t MaxInputAttachments;
};

class RenderingContext
{
    RenderingContext() noexcept = default;
    ~RenderingContext();
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

    static void AddSetupFunctions(post_physical_device_pre_logical_device_function_t fn0, post_logical_device_function_t fn1);
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
    static const char* GetShaderCacheDir();
    static void SetShaderCacheDir(const char* dir);
    static VkResult SetObjectName(VkObjectType object_type, uint64_t handle, const char* name);

private:

    void createInstanceAndWindow(const nlohmann::json& json_file, std::string& _window_mode);

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
    std::string shaderCacheDir;
    PFN_vkSetDebugUtilsObjectNameEXT SetObjectNameFn{ nullptr };
    VkDebugUtilsMessengerEXT DebugUtilsMessenger{ VK_NULL_HANDLE };

};

#endif //!DIAMOND_DOGS_RENDERING_CONTEXT_HPP
