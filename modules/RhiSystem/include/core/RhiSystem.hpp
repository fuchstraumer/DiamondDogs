#pragma once
#ifndef DIAMOND_DOGS_RHI_SYSTEM_HPP
#define DIAMOND_DOGS_RHI_SYSTEM_HPP
#include "RhiFlags.hpp"
#include "ShaderManifestDevice.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

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
struct InstanceStartInfo;
struct DeviceStartInfo;
struct RhiSystemResult;

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
public:

    /** @brief The start path: construct empty, then StartInstance and StartDevice. Nothing throws, and the device
     *  enables only what DeviceStartInfo asks for. */
    RhiSystem() noexcept;
    /** @brief The old path: enables every supported feature. Step 5 of the boot work moves its callers to the start
     *  path and deletes it. */
    RhiSystem(const RhiSystemCreateInfo& create_info);
    ~RhiSystem();
    RhiSystem(const RhiSystem&) = delete;
    RhiSystem& operator=(const RhiSystem&) = delete;

    /** @brief Creates the instance and, with validation, the debug messenger. Fails with InstanceExtensionMissing
     *  before vkCreateInstance when a Required extension is not offered. */
    [[nodiscard]] RhiSystemResult StartInstance(const InstanceStartInfo& start_info);
    /** @brief Plans every physical device, chooses the best one that meets every Required need, and creates the
     *  device with only the planned extensions and feature members. Fails with NoDeviceMeetsRequirements, naming the
     *  first unmet need of the best device, when none meets them. */
    [[nodiscard]] RhiSystemResult StartDevice(const DeviceStartInfo& start_info);

    /** @brief Answers from the enabled set, never from the physical device, so that a variant filter cannot pick
     *  code that needs a capability the device was created without. Aliases count as their primary name. */
    [[nodiscard]] bool SupportsCapability(std::string_view capability_name) const noexcept;
    /** @brief The shape of lodestone::manifest::CapabilitySupportFn. `user` is the RhiSystem. */
    [[nodiscard]] static bool SupportsCapabilityFn(void* user, std::string_view capability_name) noexcept;
    /** @brief The primary names of the enabled capabilities, sorted. Empty on the old path. */
    [[nodiscard]] std::span<const std::string> EnabledCapabilities() const noexcept;
    /** @brief The "Struct.member" features that StartDevice enabled, sorted. */
    [[nodiscard]] std::span<const std::string> EnabledFeatures() const noexcept;
    /** @brief The compute and push constant limits of the device, for the variant filter. */
    [[nodiscard]] const lodestone::manifest::DeviceLimits& Limits() const noexcept;

    void Update();
    void Destroy();

    [[nodiscard]] Instance* GetInstance() noexcept;
    [[nodiscard]] Device* GetDevice() noexcept;
    [[nodiscard]] uint32_t GetVulkanApiVersion() const noexcept;

    static VkResult SetObjectName(VkObjectType object_type, uint64_t handle, const char* name);

private:

    void createInstance(const RhiSystemCreateInfo& create_info);
    void createDebugUtilsMessenger();
    void createLogicalDevice();
    void setupObjectNaming();

    std::unique_ptr<Instance> vulkanInstance;
    std::unique_ptr<Device> logicalDevice;
    std::unique_ptr<ExtensionPack> extensionPack;
    std::string shaderCacheDir;
    VkDebugUtilsMessengerEXT debugUtilsMessenger{ VK_NULL_HANDLE };
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessenger{ nullptr };
    std::vector<std::string> enabledCapabilities;
    std::vector<std::string> enabledFeatures;
    lodestone::manifest::DeviceLimits deviceLimits;

};

} // namespace rhi

#endif //!DIAMOND_DOGS_RHI_SYSTEM_HPP
