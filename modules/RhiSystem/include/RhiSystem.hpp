#pragma once
#ifndef DIAMOND_DOGS_RHI_SYSTEM_HPP
#define DIAMOND_DOGS_RHI_SYSTEM_HPP
#include "RhiTypes.hpp"
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

class ExtensionWrangler;
class PlatformWindow;

namespace rhi
{

class Instance;
class PhysicalDevice;
class Device;
class ExtensionPack;
struct RhiSystemImpl;

struct RhiSystemCreateInfo
{
    ApiVersion VkVersion{ ApiVersion::Latest };
    uint32_t EngineVersion{ 0u };
    uint32_t AppVersion{ 0u };
    ValidationLayers ValidationLevel{ ValidationLayers::BaseOnly };
    std::string ApplicationName{ "DiamondDogs Application" };
    std::string EngineName{ "DiamondDogs" };
    std::vector<std::string> RequiredInstanceExtensions;
    std::vector<std::string> RequiredDeviceExtensions;
    std::vector<std::string> RequestedInstanceExtensions;
    std::vector<std::string> RequestedDeviceExtensions;
    std::string ShaderCacheDir;
};

class RhiSystem
{
    RhiSystem(const RhiSystem&) = delete;
    RhiSystem& operator=(const RhiSystem&) = delete;
public:

    RhiSystem(const char* cfg_file_path);
    RhiSystem(const RhiSystemCreateInfo& create_info);
    ~RhiSystem();

    void Update();
    void Destroy();

    Instance* GetInstance() noexcept;
    PhysicalDevice* GetPhysicalDevice(const size_t idx = 0) noexcept;
    Device* GetDevice() noexcept;

    const char* GetShaderCacheDir();
    void SetShaderCacheDir(const char* dir);
    static VkResult SetObjectName(VkObjectType object_type, uint64_t handle, const char* name);

private:

    void gatherAndResolveInstanceExtensions(const nlohmann::json& json_file);
    void createInstance(const nlohmann::json& rhiConfig, const nlohmann::json& engineConfig);
    void createInstance(const RhiSystemCreateInfo& createInfo);
    void createDebugUtilsMessenger();
    void createPhysicalDevice();
    void gatherAndResolveDeviceExtensions(const nlohmann::json& json_file);
    void createLogicalDevice();

    std::unique_ptr<Instance> vulkanInstance;
    std::vector<std::unique_ptr<PhysicalDevice>> physicalDevices;
    std::unique_ptr<Device> logicalDevice;
    std::unique_ptr<ExtensionPack> extensionPack;
    std::string shaderCacheDir;
    VkDebugUtilsMessengerEXT DebugUtilsMessenger{ VK_NULL_HANDLE };

};

} // namespace rhi

#endif //!DIAMOND_DOGS_RHI_SYSTEM_HPP
