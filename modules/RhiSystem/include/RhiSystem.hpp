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
    struct VprExtensionPack;
}

struct RhiSystemCreateInfo
{
    uint32_t VkVersion{ 0u };
    uint32_t EngineVersion{ 0u };
    uint32_t AppVersion{ 0u };
    bool EnableValidationLayers{ false };
    std::string ApplicationName{ "DiamondDogs Application" };
    std::string EngineName{ "DiamondDogs" };
    std::vector<std::string> RequiredInstanceExtensions;
    std::vector<std::string> RequiredDeviceExtensions;
    std::vector<std::string> RequestedInstanceExtensions;
    std::vector<std::string> RequestedDeviceExtensions;
    std::string ShaderCacheDir;
};

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

    void Construct(const char* cfg_file_path);
    void Update();
    void Destroy();

    vpr::Instance* Instance() noexcept;
    vpr::PhysicalDevice* PhysicalDevice(const size_t idx = 0) noexcept;
    vpr::Device* Device() noexcept;

    const char* GetShaderCacheDir();
    void SetShaderCacheDir(const char* dir);
    static VkResult SetObjectName(VkObjectType object_type, uint64_t handle, const char* name);

private:

    void createInstance(const nlohmann::json& json_file, vpr::VprExtensionPack& extensionPack);
    void createLogicalDevice(const nlohmann::json& json_file, vpr::VprExtensionPack& extensionPack);

    std::unique_ptr<vpr::Instance> vulkanInstance;
    std::vector<std::unique_ptr<vpr::PhysicalDevice>> physicalDevices;
    std::unique_ptr<vpr::Device> logicalDevice;

    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::string shaderCacheDir;
    VkDebugUtilsMessengerEXT DebugUtilsMessenger{ VK_NULL_HANDLE };
    std::unique_ptr<::ExtensionWrangler> extensionWrangler;
    uint32_t vkApiVersion{ 0u };


};

#endif //!DIAMOND_DOGS_RHI_SYSTEM_HPP
