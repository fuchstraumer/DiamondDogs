#include "RhiSystem.hpp"
#include "DebugUtilFns.hpp"
#include "Device.hpp"
#include "DevicePlanner.hpp"
#include "ExtensionPack.hpp"
#include "ExtensionWrangler.hpp"
#include "Instance.hpp"
#include "RhiSystemStart.hpp"
#include "VulkanCapabilityTable.hpp"
#include <algorithm>
#include <exception>
#include <functional>
#include <span>
#include <string_view>
#include <utility>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread> // for std::this_thread::get_id() for debug info
#include <format>
#include <iostream>
#include <vector>

#include "RhiFlags.hpp"
#include "vulkan/vk_platform.h"
#include "vulkan/vulkan_core.h"
#include <vulkan/vk_enum_string_helper.h>

namespace
{
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                               VkDebugUtilsMessageTypeFlagBitsEXT message_type,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                               void* user_data);

    [[nodiscard]] rhi::RhiSystemResult StartResult(rhi::RhiSystemResultCode code, std::string_view subject, std::string detail);
    // the best-scored plan, among the plans that meet every Required need when `only_complete` is true
    [[nodiscard]] const rhi::DevicePlan* BestPlan(const std::vector<rhi::DevicePlan>& plans, bool only_complete) noexcept;

    VkDebugUtilsMessengerCreateInfoEXT MessengerCreateInfo() noexcept
    {
        return VkDebugUtilsMessengerCreateInfoEXT
        {
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            nullptr,
            0,
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(DebugUtilsMessengerCallback),
            nullptr
        };
    }
}

namespace rhi
{
    // static instance of object name function, so that we can use it for static debug callbacks
    static PFN_vkSetDebugUtilsObjectNameEXT k_SetObjectNameFn{ nullptr };
    // set by first context to create, currently because we assume we'll only have one context
    static VkDevice k_DebugLogicalDeviceHandle{ VK_NULL_HANDLE };

    RhiSystem::RhiSystem(const RhiSystemCreateInfo& create_info)
    {
        extensionPack = std::make_unique<ExtensionPack>(create_info.VkVersion);

        if (!create_info.RequiredInstanceExtensions.empty())
        {
            extensionPack->AddRequiredInstanceExtensions(create_info.RequiredInstanceExtensions);
        }

        if (!create_info.RequestedInstanceExtensions.empty())
        {
            extensionPack->AddOptionalInstanceExtensions(create_info.RequestedInstanceExtensions);
        }

        extensionPack->ResolveInstanceDependencies();

        createInstance(create_info);
        // before the device, so that device creation messages reach our callback
        createDebugUtilsMessenger();

        if (!create_info.RequiredDeviceExtensions.empty())
        {
            extensionPack->AddRequiredDeviceExtensions(create_info.RequiredDeviceExtensions);
        }

        if (!create_info.RequestedDeviceExtensions.empty())
        {
            extensionPack->AddOptionalDeviceExtensions(create_info.RequestedDeviceExtensions);
        }

        createLogicalDevice();
        setupObjectNaming();

        shaderCacheDir = create_info.ShaderCacheDir;
    }

    RhiSystem::RhiSystem() noexcept = default;

    RhiSystem::~RhiSystem()
    {
        Destroy();
    }

    RhiSystemResult RhiSystem::StartInstance(const InstanceStartInfo& start_info)
    {
        if (vulkanInstance)
        {
            return StartResult(RhiSystemResultCode::AlreadyStarted, "", "StartInstance ran twice without Destroy");
        }

        extensionPack = std::make_unique<ExtensionPack>(start_info.VkVersion);

        // a wrangler with no physical device answers for instance extensions
        const ExtensionWrangler wrangler(extensionPack->GetVulkanApiVersion(), VK_NULL_HANDLE);
        std::vector<std::string> required;
        std::vector<std::string> wanted;

        for (const DeviceNeed& need : start_info.Extensions)
        {
            if (need.Level == DeviceNeedLevel::Wanted)
            {
                wanted.emplace_back(need.Name);
                continue;
            }

            if (!wrangler.IsExtensionSupported(need.Name))
            {
                extensionPack.reset();
                return StartResult(RhiSystemResultCode::InstanceExtensionMissing,
                                    need.Name,
                                    "neither the loader nor an enabled layer offers it");
            }

            required.emplace_back(need.Name);
        }

        extensionPack->AddRequiredInstanceExtensions(std::move(required));
        extensionPack->AddOptionalInstanceExtensions(std::move(wanted));
        extensionPack->ResolveInstanceDependencies();

        const VkDebugUtilsMessengerCreateInfoEXT messengerInfo = MessengerCreateInfo();

        vulkanInstance = std::make_unique<Instance>(std::string{ start_info.ApplicationName },
                                                    std::string{ start_info.EngineName },
                                                    start_info.AppVersion,
                                                    start_info.EngineVersion,
                                                    start_info.ValidationLevel,
                                                    *extensionPack,
                                                    RHI_SYSTEM_VALIDATION_ENABLED ? &messengerInfo : nullptr);

        if (!vulkanInstance->Handle().IsValid())
        {
            Destroy();
            return StartResult(RhiSystemResultCode::InstanceCreateFailed, "", "vkCreateInstance failed");
        }

        // before the device, so that device creation messages reach our callback
        createDebugUtilsMessenger();
        return StartResult(RhiSystemResultCode::Success, "", "");
    }

    RhiSystemResult RhiSystem::StartDevice(const DeviceStartInfo& start_info)
    {
        if (!vulkanInstance)
        {
            return StartResult(RhiSystemResultCode::InstanceNotStarted, "", "StartDevice needs StartInstance first");
        }

        if (logicalDevice)
        {
            return StartResult(RhiSystemResultCode::AlreadyStarted, "", "StartDevice ran twice without Destroy");
        }

        const VkInstance instance = vulkanInstance->Handle().As<VkInstance>();
        uint32_t physicalDeviceCount = 0u;

        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);

        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);

        vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

        if (physicalDevices.empty())
        {
            return StartResult(RhiSystemResultCode::NoPhysicalDevice, "", "the instance reports no physical device");
        }

        std::vector<DevicePlan> plans;

        for (const VkPhysicalDevice physicalDevice : physicalDevices)
        {
            plans.push_back(PlanDevice(physicalDevice, extensionPack->GetVulkanApiVersion(), start_info));
        }

        const DevicePlan* chosen = BestPlan(plans, true);

        if (chosen == nullptr)
        {
            const DevicePlan* closest = BestPlan(plans, false);

            return StartResult(RhiSystemResultCode::NoDeviceMeetsRequirements,
                                closest->UnmetNeed,
                                std::format("{}: {}", closest->DeviceName, closest->UnmetReason));
        }

        // the dependencies of the planned extensions, and the split into enabled-by-name and core
        extensionPack->SetPhysicalDevice(chosen->PhysicalDevice);
        extensionPack->AddRequiredDeviceExtensions(chosen->Extensions);

        try
        {
            extensionPack->ResolveDeviceDependencies();
        }
        catch (const std::exception& error)
        {
            return StartResult(RhiSystemResultCode::DeviceCreateFailed, "", error.what());
        }

        // the plan is not const: the device reads its feature chain, which Head returns as a mutable pointer
        DevicePlan& plan = plans[static_cast<size_t>(chosen - plans.data())];
        const PhysicalDeviceHandle physicalDevice{ reinterpret_cast<uint64_t>(plan.PhysicalDevice) };

        logicalDevice = std::make_unique<Device>(vulkanInstance.get(),
                                                 plan.ApiVersion,
                                                 physicalDevice,
                                                 extensionPack->GetDeviceExtensions(),
                                                 extensionPack->GetCoreDeviceExtensions(),
                                                 plan.Enabled.Head());

        if (!logicalDevice->Handle().IsValid())
        {
            logicalDevice.reset();
            return StartResult(RhiSystemResultCode::DeviceCreateFailed, "", std::format("vkCreateDevice failed on {}", plan.DeviceName));
        }

        setupObjectNaming();
        enabledCapabilities = std::move(plan.EnabledCapabilities);
        enabledFeatures = std::move(plan.EnabledFeatures);
        deviceLimits = plan.Limits;
        return StartResult(RhiSystemResultCode::Success, "", "");
    }

    bool RhiSystem::SupportsCapability(std::string_view capability_name) const noexcept
    {
        const std::string_view primaryName = lodestone::vulkan::PrimaryCapabilityName(capability_name);

        return std::ranges::binary_search(enabledCapabilities, primaryName, std::less<>{});
    }

    bool RhiSystem::SupportsCapabilityFn(void* user, std::string_view capability_name) noexcept
    {
        return static_cast<const RhiSystem*>(user)->SupportsCapability(capability_name);
    }

    std::span<const std::string> RhiSystem::EnabledCapabilities() const noexcept
    {
        return enabledCapabilities;
    }

    std::span<const std::string> RhiSystem::EnabledFeatures() const noexcept
    {
        return enabledFeatures;
    }

    const lodestone::manifest::DeviceLimits& RhiSystem::Limits() const noexcept
    {
        return deviceLimits;
    }

    bool RhiSystemResult::Succeeded() const noexcept
    {
        return Code == RhiSystemResultCode::Success;
    }

    std::string_view ToString(RhiSystemResultCode code) noexcept
    {
        switch (code)
        {
        case RhiSystemResultCode::Success: return "Success";
        case RhiSystemResultCode::AlreadyStarted: return "AlreadyStarted";
        case RhiSystemResultCode::InstanceNotStarted: return "InstanceNotStarted";
        case RhiSystemResultCode::InstanceExtensionMissing: return "InstanceExtensionMissing";
        case RhiSystemResultCode::InstanceCreateFailed: return "InstanceCreateFailed";
        case RhiSystemResultCode::NoPhysicalDevice: return "NoPhysicalDevice";
        case RhiSystemResultCode::NoDeviceMeetsRequirements: return "NoDeviceMeetsRequirements";
        case RhiSystemResultCode::DeviceCreateFailed: return "DeviceCreateFailed";
        default: return "Invalid";
        }
    }

    void RhiSystem::Update()
    {

    }

    void RhiSystem::Destroy()
    {
        // safe to call twice: the destructor calls it after an explicit Destroy
        if (logicalDevice && k_DebugLogicalDeviceHandle == logicalDevice->Handle().As<VkDevice>())
        {
            k_SetObjectNameFn = nullptr;
            k_DebugLogicalDeviceHandle = VK_NULL_HANDLE;
        }
        logicalDevice.reset();
        // after the device, so that device destruction messages still reach our callback
        if (debugUtilsMessenger != VK_NULL_HANDLE && vulkanInstance && destroyDebugUtilsMessenger != nullptr)
        {
            destroyDebugUtilsMessenger(vulkanInstance->Handle().As<VkInstance>(), debugUtilsMessenger, nullptr);
        }
        debugUtilsMessenger = VK_NULL_HANDLE;
        destroyDebugUtilsMessenger = nullptr;
        vulkanInstance.reset();
        extensionPack.reset();
        enabledCapabilities.clear();
        enabledFeatures.clear();
        deviceLimits = lodestone::manifest::DeviceLimits{};
    }

    Instance* RhiSystem::GetInstance() noexcept
    {
        return vulkanInstance.get();
    }

    Device* RhiSystem::GetDevice() noexcept
    {
        return logicalDevice.get();
    }

    uint32_t RhiSystem::GetVulkanApiVersion() const noexcept
    {
        return extensionPack->GetVulkanApiVersion();
    }

    VkResult RhiSystem::SetObjectName(VkObjectType object_type, uint64_t handle, const char* name)
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED && RHI_SYSTEM_USE_DEBUG_INFO)
        {

            if (!k_SetObjectNameFn)
            {
                // unlikely we'll introspect on this, but this error value is the only one that makes sense
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }

            if constexpr (RHI_SYSTEM_DEBUG_INFO_THREAD_ID || RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS)
            {

                std::string objectNameStr{ name };

                if constexpr (RHI_SYSTEM_DEBUG_INFO_THREAD_ID)
                {
                    std::string threadInfoStr = std::format("_ThreadID:{}", std::this_thread::get_id());
                    objectNameStr += threadInfoStr;
                }

                const VkDebugUtilsObjectNameInfoEXT nameInfo
                {
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    nullptr,
                    object_type,
                    handle,
                    objectNameStr.c_str()
                };

                return k_SetObjectNameFn(k_DebugLogicalDeviceHandle, &nameInfo);
            }
            else
            {
                const VkDebugUtilsObjectNameInfoEXT nameInfo
                {
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    nullptr,
                    object_type,
                    handle,
                    name
                };
                return k_SetObjectNameFn(k_DebugLogicalDeviceHandle, &nameInfo);
            }
        }
        else
        {
            return VK_SUCCESS;
        }
    }

    void RhiSystem::createInstance(const RhiSystemCreateInfo& create_info)
    {
        const VkDebugUtilsMessengerCreateInfoEXT messengerInfo = MessengerCreateInfo();
        vulkanInstance = std::make_unique<Instance>(create_info.ApplicationName,
                                                    create_info.EngineName,
                                                    create_info.AppVersion,
                                                    create_info.EngineVersion,
                                                    create_info.ValidationLevel,
                                                    *extensionPack,
                                                    RHI_SYSTEM_VALIDATION_ENABLED ? &messengerInfo : nullptr);
    }

    void RhiSystem::createLogicalDevice()
    {
        logicalDevice = std::make_unique<Device>(vulkanInstance.get(), *extensionPack);
    }

    void RhiSystem::createDebugUtilsMessenger()
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            if (!vulkanInstance->HasValidation() ||
                !vulkanInstance->HasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            {
                return;
            }

            const VkInstance instance = vulkanInstance->Handle().As<VkInstance>();
            const auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            destroyDebugUtilsMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (createFn == nullptr || destroyDebugUtilsMessenger == nullptr)
            {
                throw std::runtime_error("Failed to create debug utils messenger: function pointer not loaded!");
            }

            const VkDebugUtilsMessengerCreateInfoEXT messengerInfo = MessengerCreateInfo();
            const VkResult result = createFn(instance, &messengerInfo, nullptr, &debugUtilsMessenger);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create debug utils messenger.");
            }
        }
    }

    void RhiSystem::setupObjectNaming()
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            if (debugUtilsMessenger == VK_NULL_HANDLE)
            {
                return;
            }
            const VkDebugUtilsFunctions debugUtilsFns = logicalDevice->GetDebugUtilFns();
            k_SetObjectNameFn = debugUtilsFns.vkSetDebugUtilsObjectName;
            k_DebugLogicalDeviceHandle = logicalDevice->Handle().As<VkDevice>();
        }
    }

} // namespace rhi

namespace
{
    rhi::RhiSystemResult StartResult(rhi::RhiSystemResultCode code, std::string_view subject, std::string detail)
    {
        return rhi::RhiSystemResult{ .Code = code, .Subject = std::string{ subject }, .Detail = std::move(detail) };
    }

    const rhi::DevicePlan* BestPlan(const std::vector<rhi::DevicePlan>& plans, bool only_complete) noexcept
    {
        const rhi::DevicePlan* best = nullptr;

        for (const rhi::DevicePlan& plan : plans)
        {
            if (only_complete && !plan.UnmetNeed.empty())
            {
                continue;
            }

            if (best == nullptr || plan.Score > best->Score)
            {
                best = &plan;
            }
        }

        return best;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                               VkDebugUtilsMessageTypeFlagBitsEXT message_type,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                               void* user_data)
    {
        std::string outputMessage;
        
        if (callback_data->messageIdNumber != 0u)
        {
            outputMessage += std::format("VUID:{}:VUID_NAME:{}\n", callback_data->messageIdNumber, callback_data->pMessageIdName);
        }

        // fixed: this used to drop every message containing "CREATE", which hid all vkCreate* validation errors
        const std::string messageStr{ callback_data->pMessage };
        outputMessage += std::format("    Message: {}\n", messageStr);
        
        if (callback_data->queueLabelCount != 0u)
        {
            outputMessage += std::format("    Error occured in queue: {}\n", callback_data->pQueueLabels[0].pLabelName);
        }

        if (callback_data->cmdBufLabelCount != 0u)
        {
            outputMessage += "    Error occured executing command buffer(s): \n";
            for (uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i)
            {
                outputMessage += std::format("    {}\n", callback_data->pCmdBufLabels[i].pLabelName);
            }
        }
        
        if (callback_data->objectCount != 0u)
        {
            const auto& pObjects = callback_data->pObjects;
            outputMessage += "    Object(s) involved: \n";
            for (uint32_t i = 0; i < callback_data->objectCount; ++i)
            {
                if (pObjects[i].pObjectName != nullptr)
                {
                    outputMessage += std::format("        ObjectName: {}\n", pObjects[i].pObjectName);
                }
                else
                {
                    outputMessage += "        UNNAMED_OBJECT\n";
                }
                outputMessage += std::format("            ObjectType: {}\n", string_VkObjectType(pObjects[i].objectType));
                outputMessage += std::format("            ObjectHandle: {:x}\n", pObjects[i].objectHandle);
            }
        }

        if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
        {
            std::cerr << outputMessage << "\n";
        }
        else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
        {
            std::cerr << outputMessage << "\n";
        }
        else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
        {
            std::cout << outputMessage << "\n";
        }

        return VK_FALSE;
    }

}
