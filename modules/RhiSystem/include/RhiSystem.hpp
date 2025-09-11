#pragma once
#ifndef DIAMOND_DOGS_RHI_SYSTEM_HPP
#define DIAMOND_DOGS_RHI_SYSTEM_HPP
#include "utility/Delegate.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <vulkan/vulkan_core.h>
#include <nlohmann/json_fwd.hpp>

#ifdef RHI_SYSTEM_USE_DEBUG_INFO_CONF
constexpr static bool RHI_SYSTEM_USE_DEBUG_INFO = true;
#else
constexpr static bool RHI_SYSTEM_USE_DEBUG_INFO = false;
#endif

#ifdef RHI_SYSTEM_VALIDATION_ENABLED_CONF
constexpr static bool RHI_SYSTEM_VALIDATION_ENABLED = true;
#else
constexpr static bool RHI_SYSTEM_VALIDATION_ENABLED = false;
#endif

#ifdef RHI_SYSTEM_DEBUG_INFO_THREAD_ID_CONF
constexpr static bool RHI_SYSTEM_DEBUG_INFO_THREAD_ID = true;
#else
constexpr static bool RHI_SYSTEM_DEBUG_INFO_THREAD_ID = false;
#endif

#ifdef RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS_CONF
constexpr static bool RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS = true;
#else
constexpr static bool RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS = false;
#endif

#ifdef RHI_SYSTEM_DEBUG_INFO_CALLING_FN_CONF
#include <source_location>
#include <format>
// In current configuration, this macro adds the calling function name and line to the objects name
#define RHI_SYSTEM_DEBUG_OBJECT_NAME(name) std::format("{}_{}_{}", name, std::source_location::current().function_name(), std::source_location::current().line())
#else
// In current configuration, this macro just returns the name without modification
#define RHI_SYSTEM_DEBUG_OBJECT_NAME(name) name
#endif

namespace vpr
{
    class Instance;
    class PhysicalDevice;
    class Device;
    class Swapchain;
    class SurfaceKHR;
    struct VprExtensionPack;
}

class ExtensionWrangler;
class PlatformWindow;
struct GLFWwindow;
struct GLFWcursor;
struct GLFWimage;

struct RhiSystemImpl;

class RhiSystem
{
    RhiSystem() noexcept;
    ~RhiSystem();
    RhiSystem(const RhiSystem&) = delete;
    RhiSystem& operator=(const RhiSystem&) = delete;
public:

    static RhiSystem& Get() noexcept;
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

    static void GetWindowSize(int& w, int& h);
    static void GetFramebufferSize(int& w, int& h);
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

    void createInstanceAndWindow(const nlohmann::json& json_file, std::string& _window_mode, vpr::VprExtensionPack& extensionPack);
    void createLogicalDevice(const nlohmann::json& json_file, vpr::VprExtensionPack& extensionPack);

    std::unique_ptr<PlatformWindow> window;
    std::unique_ptr<vpr::Instance> vulkanInstance;
    std::vector<std::unique_ptr<vpr::PhysicalDevice>> physicalDevices;
    std::unique_ptr<vpr::Device> logicalDevice;
    std::unique_ptr<vpr::Swapchain> swapchain;
    std::unique_ptr<vpr::SurfaceKHR> windowSurface;

    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::string windowMode;
    uint32_t syncMode{ uint32_t(0u) };
    std::string syncModeStr;
    std::string shaderCacheDir;
    PFN_vkSetDebugUtilsObjectNameEXT SetObjectNameFn{ nullptr };
    VkDebugUtilsMessengerEXT DebugUtilsMessenger{ VK_NULL_HANDLE };
    std::unique_ptr<::ExtensionWrangler> extensionWrangler;
    uint32_t vkApiVersion{ 0u };


};

#endif //!DIAMOND_DOGS_RHI_SYSTEM_HPP
