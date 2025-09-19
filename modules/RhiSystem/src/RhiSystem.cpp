#include "RhiSystem.hpp"

#include "Device.hpp"
#include "ExtensionPack.hpp"
#include "ExtensionWrangler.hpp"
#include "Instance.hpp"
#include "PhysicalDevice.hpp"

#include <thread> // for std::this_thread::get_id() for debug info
#include <chrono>
#include <format>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <vulkan/vk_enum_string_helper.h>
#include "nlohmann/json.hpp"

namespace
{
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                               VkDebugUtilsMessageTypeFlagBitsEXT message_type,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                               void* user_data);
    void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version);
    void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version); 
}

namespace rhi
{

    static bool validationEnabled{ false };
    // static instance of object name function, so that we can use it for static debug callbacks
    static PFN_vkSetDebugUtilsObjectNameEXT s_SetObjectNameFn{ nullptr };
    // set by first context to create, currently because we assume we'll only have one context
    static VkDevice s_DebugLogicalDeviceHandle{ VK_NULL_HANDLE };

    #ifdef _WIN32
    constexpr const char* SurfaceExtensionName = "VK_KHR_win32_surface";
    #elif defined(__linux__)
    constexpr const char* SurfaceExtensionName = "VK_KHR_xcb_surface";
    #endif

    ApiVersion ApiVersionFromString(const std::string& version_string)
    {
        if (version_string == "1.0" || version_string == "Vulkan10")
        {
            return ApiVersion::Vulkan10;
        }
        else if (version_string == "1.1" || version_string == "Vulkan11")
        {
            return ApiVersion::Vulkan11;
        }
        else if (version_string == "1.2" || version_string == "Vulkan12")
        {
            return ApiVersion::Vulkan12;
        }
        else if (version_string == "1.3" || version_string == "Vulkan13")
        {
            return ApiVersion::Vulkan13;
        }
        else if (version_string == "1.4" || version_string == "Vulkan14")
        {
            return ApiVersion::Vulkan14;
        }
        else if (version_string == "Latest" || version_string == "latest")
        {
            return ApiVersion::Latest;
        }
        else
        {
            throw std::runtime_error("Unknown Vulkan API version string: " + version_string);
        }
    }

    ApiVersion ApiVersionFromUint32(const uint32_t version)
    {
        switch (version)
        {
            case VK_API_VERSION_1_0:
                return ApiVersion::Vulkan10;
            case VK_API_VERSION_1_1:
                return ApiVersion::Vulkan11;
            case VK_API_VERSION_1_2:
                return ApiVersion::Vulkan12;
            case VK_API_VERSION_1_3:
                return ApiVersion::Vulkan13;
            case VK_API_VERSION_1_4:
                return ApiVersion::Vulkan14;
            default:
                return ApiVersion::Latest;
        }
    }

    ValidationLayers ValidationLayersFromString(const std::string& layer_string)
    {
        if (layer_string == "None" || layer_string == "none")
        {
            return ValidationLayers::None;
        }
        else if (layer_string == "Base" || layer_string == "base")
        {
            return ValidationLayers::BaseOnly;
        }
        else if (layer_string == "Synchronization" || layer_string == "synchronization")
        {
            return ValidationLayers::WithSynchronization;
        }
        else if (layer_string == "All" || layer_string == "all")
        {
            return ValidationLayers::Full;
        }
        else
        {
            std::cerr << std::format("Unknown validation layers string: {}, defaulting to no validation layers.\n", layer_string);
            return ValidationLayers::None;
        }
    }

    RhiSystem::RhiSystem(const char* file_path)
    {

        std::ifstream input_file(file_path);

        if (!input_file.is_open())
        {
            throw std::runtime_error("Couldn't open input file.");
        }

        nlohmann::json json_file;
        input_file >> json_file;
        nlohmann::json rhiConfig;
        
        // first step: see if this is categorized JSON or flat
        if (json_file.contains("RHISystemConfig"))
        {
            rhiConfig = json_file["RHISystemConfig"];
        }
        else
        {
            rhiConfig = json_file;
        }

        ApiVersion preferredApiVersion = ApiVersion::Latest;
        if (rhiConfig.contains("ApiVersion"))
        {
            preferredApiVersion = ApiVersionFromString(rhiConfig.at("ApiVersion").get<std::string>());
        }
        else if (rhiConfig.contains("VulkanVersion"))
        {
            preferredApiVersion = ApiVersionFromString(rhiConfig.at("VulkanVersion").get<std::string>());
        }

        extensionPack = std::make_unique<ExtensionPack>(preferredApiVersion);

        gatherAndResolveInstanceExtensions(rhiConfig);

        nlohmann::json engineConfig;
        if (json_file.contains("EngineConfig"))
        {
            engineConfig = json_file["EngineConfig"];
        }
        else
        {
            engineConfig = json_file;
        }

        createInstance(rhiConfig, engineConfig);
        createPhysicalDevice();
        extensionPack->SetPhysicalDevice(physicalDevices.front()->vkHandle());
        gatherAndResolveDeviceExtensions(rhiConfig);
        createLogicalDevice();

        createDebugUtilsMessenger();

        std::filesystem::path shaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogs" / "ShaderCache";
        if (rhiConfig.contains("ShaderCacheDir"))
        {
            shaderCacheDir = rhiConfig.at("ShaderCacheDir").get<std::string>();
            if (!std::filesystem::exists(shaderCacheDir))
            {
                std::filesystem::create_directories(shaderCacheDir);
            }
            shaderCacheDir = std::filesystem::absolute(shaderCacheDir);
        }
        
    }

    
    RhiSystem::RhiSystem(const RhiSystemCreateInfo& createInfo)
    {
        extensionPack = std::make_unique<ExtensionPack>(createInfo.VkVersion);

        if (createInfo.RequiredInstanceExtensions.size() > 0)
        {
            extensionPack->AddRequiredInstanceExtensions(createInfo.RequiredInstanceExtensions);
        }

        if (createInfo.RequestedInstanceExtensions.size() > 0)
        {
            extensionPack->AddOptionalInstanceExtensions(createInfo.RequestedInstanceExtensions);
        }

        extensionPack->ResolveInstanceDependencies();

        createInstance(createInfo);

        createPhysicalDevice();
        extensionPack->SetPhysicalDevice(physicalDevices.front()->vkHandle());

        if (createInfo.RequiredDeviceExtensions.size() > 0)
        {
            extensionPack->AddRequiredDeviceExtensions(createInfo.RequiredDeviceExtensions);
        }

        if (createInfo.RequestedDeviceExtensions.size() > 0)
        {
            extensionPack->AddOptionalDeviceExtensions(createInfo.RequestedDeviceExtensions);
        }

        extensionPack->ResolveDeviceDependencies();

        createLogicalDevice();

        shaderCacheDir = createInfo.ShaderCacheDir;

        if (vulkanInstance->HasValidation() && vulkanInstance->HasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            createDebugUtilsMessenger();
        }
        
    }

    RhiSystem::~RhiSystem()
    {
        Destroy();
    }

    void RhiSystem::Update()
    {

    }

    void RhiSystem::Destroy()
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            VkDebugUtilsFunctions debugUtilsFns = logicalDevice->GetDebugUtilFns();
            if (DebugUtilsMessenger != VK_NULL_HANDLE && debugUtilsFns.vkDestroyDebugUtilsMessenger)
            {
                debugUtilsFns.vkDestroyDebugUtilsMessenger(vulkanInstance->vkHandle(), DebugUtilsMessenger, nullptr);
            }
        }
        logicalDevice.reset();
        physicalDevices.clear();
        vulkanInstance.reset();
        extensionPack.reset();
    }

    Instance* RhiSystem::GetInstance() noexcept
    {
        return vulkanInstance.get();
    }

    PhysicalDevice* RhiSystem::GetPhysicalDevice(const size_t idx) noexcept
    {
        return physicalDevices[idx].get();
    }

    Device* RhiSystem::GetDevice() noexcept
    {
        return logicalDevice.get();
    }

    VkResult RhiSystem::SetObjectName(VkObjectType object_type, uint64_t handle, const char* name)
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED && RHI_SYSTEM_USE_DEBUG_INFO)
        {

            if (!s_SetObjectNameFn)
            {
                // unlikely we'll introspect on this, but this error value is the only one that makes sense
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }

            if constexpr (RHI_SYSTEM_DEBUG_INFO_THREAD_ID || RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS)
            {

                std::string object_name_str{ name };

                if constexpr (RHI_SYSTEM_DEBUG_INFO_THREAD_ID)
                {
                    std::string threadInfoStr = std::format("_ThreadID:{}", std::this_thread::get_id());
                    object_name_str += threadInfoStr;
                }

                const VkDebugUtilsObjectNameInfoEXT name_info
                {
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    nullptr,
                    object_type,
                    handle,
                    object_name_str.c_str()
                };

                return s_SetObjectNameFn(s_DebugLogicalDeviceHandle, &name_info);
            }
            else
            {
                const VkDebugUtilsObjectNameInfoEXT name_info
                {
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    nullptr,
                    object_type,
                    handle,
                    name
                };
                return s_SetObjectNameFn(s_DebugLogicalDeviceHandle, &name_info);
            }
        }
        else
        {
            return VK_SUCCESS;
        }
    }

    void RhiSystem::gatherAndResolveInstanceExtensions(const nlohmann::json& rhiConfig)
    {
        // Now, grab the extension lists first so we can initialize the extension pack and resolve instance extensions
        std::vector<std::string> requiredInstanceExts;
        std::vector<std::string> requestedInstanceExts;

        {
            nlohmann::json req_ext_json = rhiConfig.at("RequiredInstanceExtensions");
            for (auto& entry : req_ext_json)
            {
                requiredInstanceExts.emplace_back(entry);
            }
        }

        // quick check: do we need presentation support? are said extensions present (lol) in the required list?
        const bool needsPresentationSupport = rhiConfig.value("NeedsPresentationSupport", false); // this is a required value
        if (needsPresentationSupport)
        {
            const bool hasSurfaceExt = std::find(requiredInstanceExts.begin(), requiredInstanceExts.end(), SurfaceExtensionName) != requiredInstanceExts.end();
            if (!hasSurfaceExt)
            {
                // user didn't include the extensions, so add them
                requiredInstanceExts.push_back(SurfaceExtensionName);
            }
        }

        extensionPack->AddRequiredInstanceExtensions(requiredInstanceExts);

        {
            nlohmann::json ext_json = rhiConfig.at("RequestedInstanceExtensions");
            for (auto& entry : ext_json)
            {
                requestedInstanceExts.emplace_back(entry);
            }
        }

        extensionPack->AddOptionalInstanceExtensions(requestedInstanceExts);
        extensionPack->ResolveInstanceDependencies();
    }

    void RhiSystem::createInstance(const nlohmann::json& rhiConfig, const nlohmann::json& engineConfig)
    {
        ValidationLayers layers = ValidationLayers::None;
        if (rhiConfig.contains("ValidationLayers"))
        {
            layers = ValidationLayersFromString(rhiConfig.at("ValidationLayers").get<std::string>());
        }

        uint32_t app_version = 0;
        uint32_t engine_version = 0;
        uint32_t api_version = 0;
        GetVersions(engineConfig, app_version, engine_version, api_version);

        std::string applicationName = "DiamondDogs Application";
        if (engineConfig.contains("ApplicationName"))
        {
            applicationName = engineConfig.at("ApplicationName").get<std::string>();
        }

        static const std::string engineName = "DiamondDogsEngine";
   
        vulkanInstance = std::make_unique<Instance>(applicationName,
                                                    engineName,
                                                    app_version,
                                                    engine_version,
                                                    layers,
                                                    *extensionPack);

    }

    void RhiSystem::createInstance(const RhiSystemCreateInfo& createInfo)
    {
        vulkanInstance = std::make_unique<Instance>(createInfo.ApplicationName,
                                                    createInfo.EngineName,
                                                    createInfo.AppVersion,
                                                    createInfo.EngineVersion,
                                                    createInfo.ValidationLevel,
                                                    *extensionPack);
    }

    void RhiSystem::createPhysicalDevice()
    {
        physicalDevices.emplace_back(std::make_unique<PhysicalDevice>(vulkanInstance->vkHandle(), extensionPack->GetVulkanApiVersion()));
    }

    void RhiSystem::gatherAndResolveDeviceExtensions(const nlohmann::json& rhiConfig)
    {
        std::vector<std::string> requiredDeviceExts;
        {
            nlohmann::json req_ext_json = rhiConfig.at("RequiredDeviceExtensions");
            for (auto& entry : req_ext_json)
            {
                requiredDeviceExts.emplace_back(entry);
            }
        }


        const bool hasSwapchainExt = std::find(requiredDeviceExts.begin(), requiredDeviceExts.end(), "VK_KHR_swapchain") != requiredDeviceExts.end();
        if (!hasSwapchainExt && rhiConfig.value("NeedsPresentationSupport", false))
        {
            requiredDeviceExts.push_back("VK_KHR_swapchain");
        }

        extensionPack->AddRequiredDeviceExtensions(requiredDeviceExts);
        
        std::vector<std::string> requestedDeviceExts;
        {
            nlohmann::json ext_json = rhiConfig.at("RequestedDeviceExtensions");
            for (auto& entry : ext_json)
            {
                requestedDeviceExts.emplace_back(entry);
            }
        }

        extensionPack->AddOptionalDeviceExtensions(requestedDeviceExts);
        extensionPack->ResolveDeviceDependencies();
    }

    void RhiSystem::createLogicalDevice()
    {
        const VkPhysicalDeviceFeatures2* all_extensions_features = extensionPack->GetDeviceFeatures();
        logicalDevice = std::make_unique<Device>(vulkanInstance.get(), physicalDevices.front().get(), *extensionPack);
    }

    void RhiSystem::createDebugUtilsMessenger()
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            VkDebugUtilsFunctions debugUtilsFns = logicalDevice->GetDebugUtilFns();
            s_SetObjectNameFn = debugUtilsFns.vkSetDebugUtilsObjectName;
            s_DebugLogicalDeviceHandle = logicalDevice->vkHandle();

            const VkDebugUtilsMessengerCreateInfoEXT messenger_info
            {
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                nullptr,
                0,
                // capture warnings and info that the current one does not
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                (PFN_vkDebugUtilsMessengerCallbackEXT)DebugUtilsMessengerCallback,
                nullptr
            };

            if (!debugUtilsFns.vkCreateDebugUtilsMessenger)
            {
                std::cerr << "Debug utils function pointers struct doesn't have function pointer for debug utils messenger creation!\n";
                throw std::runtime_error("Failed to create debug utils messenger: function pointer not loaded!");
            }

            VkResult result = debugUtilsFns.vkCreateDebugUtilsMessenger(vulkanInstance->vkHandle(), &messenger_info, nullptr, &DebugUtilsMessenger);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create debug utils messenger.");
            }
        }   
    }

} // namespace rhi

namespace
{

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                               VkDebugUtilsMessageTypeFlagBitsEXT message_type,
                                                               const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                               void* user_data)
    {
        std::string output_message;
        
        if (callback_data->messageIdNumber != 0u)
        {
            output_message += std::format("VUID:{}:VUID_NAME:{}\n", callback_data->messageIdNumber, callback_data->pMessageIdName);
        }

        const static std::string SKIP_STR{ "CREATE" };
        const std::string message_str{ callback_data->pMessage };
        size_t found_skippable = message_str.find(SKIP_STR);

        if (found_skippable != std::string::npos)
        {
            return VK_FALSE;
        }

        output_message += std::format("    Message: {}\n", message_str);
        
        if (callback_data->queueLabelCount != 0u)
        {
            output_message += std::format("    Error occured in queue: {}\n", callback_data->pQueueLabels[0].pLabelName);
        }

        if (callback_data->cmdBufLabelCount != 0u)
        {
            output_message += "    Error occured executing command buffer(s): \n";
            for (uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i)
            {
                output_message += std::format("    {}\n", callback_data->pCmdBufLabels[i].pLabelName);
            }
        }
        
        if (callback_data->objectCount != 0u)
        {
            auto& p_objects = callback_data->pObjects;
            output_message += "    Object(s) involved: \n";
            for (uint32_t i = 0; i < callback_data->objectCount; ++i)
            {
                if (p_objects[i].pObjectName)
                {
                    output_message += std::format("        ObjectName: {}\n", p_objects[i].pObjectName);
                }
                else
                {
                    output_message += "        UNNAMED_OBJECT\n";
                }
                output_message += std::format("            ObjectType: {}\n", string_VkObjectType(p_objects[i].objectType));
                output_message += std::format("            ObjectHandle: {:x}\n", p_objects[i].objectHandle);
            }
        }

        if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            std::cerr << output_message << "\n";
        }
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            std::cerr << output_message << "\n";
        }
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            std::cout << output_message << "\n";
        }

        return VK_FALSE;
    }

    void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version)
    {
        const size_t minor_dot_pos = version_string.find('.');
        const size_t patch_dot_pos = version_string.rfind('.');
        if (patch_dot_pos == std::string::npos)
        {
            patch_version = 0;
            if (minor_dot_pos == std::string::npos)
            {
                minor_version = 0;
                major_version = static_cast<uint32_t>(strtod(version_string.c_str(), nullptr));
            }
            else
            {
                minor_version = static_cast<uint32_t>(strtod(version_string.substr(minor_dot_pos).c_str(), nullptr));
                major_version = static_cast<uint32_t>(strtod(version_string.substr(0, minor_dot_pos).c_str(), nullptr));
            }
        }
        else
        {
            if (minor_dot_pos == std::string::npos)
            {
                major_version = static_cast<uint32_t>(strtod(version_string.c_str(), nullptr));
                minor_version = 0;
                patch_version = 0;
                return;
            }
            else
            {
                major_version = static_cast<uint32_t>(strtod(version_string.substr(0, minor_dot_pos + 1).c_str(), nullptr));
                minor_version = static_cast<uint32_t>(strtod(version_string.substr(minor_dot_pos + 1, patch_dot_pos - minor_dot_pos - 1).c_str(), nullptr));
                patch_version = static_cast<uint32_t>(strtod(version_string.substr(patch_dot_pos).c_str(), nullptr));
            }
        }
    }

    void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version)
    {
        if (json_file.contains("ApplicationVersion"))
        {
            uint32_t app_version_major = 0;
            uint32_t app_version_minor = 0;
            uint32_t app_version_patch = 0;
            const std::string app_version_str = json_file.at("ApplicationVersion");
            SplitVersionString(app_version_str, app_version_major, app_version_minor, app_version_patch);
            app_version = VK_MAKE_VERSION(app_version_major, app_version_minor, app_version_patch);
        }
        else
        {
            app_version = VK_MAKE_VERSION(0, 1, 0);
        }

        if (json_file.contains("EngineVersion"))
        {
            uint32_t engine_version_major = 0;
            uint32_t engine_version_minor = 0;
            uint32_t engine_version_patch = 0;
            const std::string engine_version_str = json_file.at("EngineVersion");
            SplitVersionString(engine_version_str, engine_version_major, engine_version_minor, engine_version_patch);
            engine_version = VK_MAKE_VERSION(engine_version_major, engine_version_minor, engine_version_patch);
        }
        else
        {
            engine_version = VK_MAKE_VERSION(0, 1, 0);
        }

        if (json_file.contains("VulkanVersion"))
        {
            uint32_t api_version_major = 0;
            uint32_t api_version_minor = 0;
            uint32_t api_version_patch = 0;
            const std::string api_version_str = json_file.at("VulkanVersion");
            if (api_version_str == "Latest")
            {
                vkEnumerateInstanceVersion(&api_version);
            }
            else
            {
                SplitVersionString(api_version_str, api_version_major, api_version_minor, api_version_patch);
                api_version = VK_MAKE_VERSION(api_version_major, api_version_minor, api_version_patch);
            }
        }
        else
        {
            vkEnumerateInstanceVersion(&api_version);
        }
    }

}
