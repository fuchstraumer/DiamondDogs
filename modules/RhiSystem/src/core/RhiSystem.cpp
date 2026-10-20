#include "RhiSystem.hpp"

#include "Device.hpp"
#include "ExtensionPack.hpp"
#include "ExtensionWrangler.hpp"
#include "Instance.hpp"

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

    static bool k_ValidationEnabled{ false };
    // static instance of object name function, so that we can use it for static debug callbacks
    static PFN_vkSetDebugUtilsObjectNameEXT k_SSetObjectNameFn{ nullptr };
    // set by first context to create, currently because we assume we'll only have one context
    static VkDevice k_SDebugLogicalDeviceHandle{ VK_NULL_HANDLE };

    #ifdef _WIN32
    constexpr const char* k_SurfaceExtensionName = "VK_KHR_win32_surface";
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

        std::ifstream inputFile(file_path);

        if (!inputFile.is_open())
        {
            throw std::runtime_error("Couldn't open input file.");
        }

        nlohmann::json jsonFile;
        inputFile >> jsonFile;
        nlohmann::json rhiConfig;
        
        // first step: see if this is categorized JSON or flat
        if (jsonFile.contains("RHISystemConfig"))
        {
            rhiConfig = jsonFile["RHISystemConfig"];
        }
        else
        {
            rhiConfig = jsonFile;
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
        if (jsonFile.contains("EngineConfig"))
        {
            engineConfig = jsonFile["EngineConfig"];
        }
        else
        {
            engineConfig = jsonFile;
        }

        createInstance(rhiConfig, engineConfig);
        gatherDeviceExtensions(jsonFile, rhiConfig);
        createLogicalDevice();

        if (vulkanInstance->HasValidation() && vulkanInstance->HasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            createDebugUtilsMessenger();
        }

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

    
    RhiSystem::RhiSystem(const RhiSystemCreateInfo& create_info)
    {
        extensionPack = std::make_unique<ExtensionPack>(create_info.VkVersion);

        if (create_info.RequiredInstanceExtensions.size() > 0)
        {
            extensionPack->AddRequiredInstanceExtensions(create_info.RequiredInstanceExtensions);
        }

        if (create_info.RequestedInstanceExtensions.size() > 0)
        {
            extensionPack->AddOptionalInstanceExtensions(create_info.RequestedInstanceExtensions);
        }

        extensionPack->ResolveInstanceDependencies();

        createInstance(create_info);

        if (create_info.RequiredDeviceExtensions.size() > 0)
        {
            extensionPack->AddRequiredDeviceExtensions(create_info.RequiredDeviceExtensions);
        }

        if (create_info.RequestedDeviceExtensions.size() > 0)
        {
            extensionPack->AddOptionalDeviceExtensions(create_info.RequestedDeviceExtensions);
        }

        createLogicalDevice();

        shaderCacheDir = create_info.ShaderCacheDir;

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
                debugUtilsFns.vkDestroyDebugUtilsMessenger(vulkanInstance->Handle().As<VkInstance>(), DebugUtilsMessenger, nullptr);
            }
        }
        logicalDevice.reset();
        vulkanInstance.reset();
        extensionPack.reset();
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

            if (!k_SSetObjectNameFn)
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

                return k_SSetObjectNameFn(k_SDebugLogicalDeviceHandle, &nameInfo);
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
                return k_SSetObjectNameFn(k_SDebugLogicalDeviceHandle, &nameInfo);
            }
        }
        else
        {
            return VK_SUCCESS;
        }
    }

    void RhiSystem::gatherAndResolveInstanceExtensions(const nlohmann::json& rhi_config)
    {
        // Now, grab the extension lists first so we can initialize the extension pack and resolve instance extensions
        std::vector<std::string> requiredInstanceExts;
        std::vector<std::string> requestedInstanceExts;

        {
            nlohmann::json reqExtJson = rhi_config.at("RequiredInstanceExtensions");
            for (auto& entry : reqExtJson)
            {
                requiredInstanceExts.emplace_back(entry);
            }
        }

        // quick check: do we need presentation support? are said extensions present (lol) in the required list?
        const bool needsPresentationSupport = rhi_config.value("NeedsPresentationSupport", false); // this is a required value
        if (needsPresentationSupport)
        {
            const bool hasSurfaceExt = std::find(requiredInstanceExts.begin(), requiredInstanceExts.end(), k_SurfaceExtensionName) != requiredInstanceExts.end();
            if (!hasSurfaceExt)
            {
                // user didn't include the extensions, so add them
                requiredInstanceExts.push_back(k_SurfaceExtensionName);
            }
        }

        extensionPack->AddRequiredInstanceExtensions(requiredInstanceExts);

        {
            nlohmann::json extJson = rhi_config.at("RequestedInstanceExtensions");
            for (auto& entry : extJson)
            {
                requestedInstanceExts.emplace_back(entry);
            }
        }

        extensionPack->AddOptionalInstanceExtensions(requestedInstanceExts);
        extensionPack->ResolveInstanceDependencies();
    }

    void RhiSystem::createInstance(const nlohmann::json& rhi_config, const nlohmann::json& engine_config)
    {
        ValidationLayers layers = ValidationLayers::None;
        if (rhi_config.contains("ValidationLayers"))
        {
            layers = ValidationLayersFromString(rhi_config.at("ValidationLayers").get<std::string>());
        }

        uint32_t appVersion = 0;
        uint32_t engineVersion = 0;
        uint32_t apiVersion = 0;
        GetVersions(engine_config, appVersion, engineVersion, apiVersion);

        std::string applicationName = "DiamondDogs Application";
        if (engine_config.contains("ApplicationName"))
        {
            applicationName = engine_config.at("ApplicationName").get<std::string>();
        }

        static const std::string engineName = "DiamondDogsEngine";
   
        vulkanInstance = std::make_unique<Instance>(applicationName,
                                                    engineName,
                                                    appVersion,
                                                    engineVersion,
                                                    layers,
                                                    *extensionPack);

    }

    void RhiSystem::createInstance(const RhiSystemCreateInfo& create_info)
    {
        vulkanInstance = std::make_unique<Instance>(create_info.ApplicationName,
                                                    create_info.EngineName,
                                                    create_info.AppVersion,
                                                    create_info.EngineVersion,
                                                    create_info.ValidationLevel,
                                                    *extensionPack);
    }

    void RhiSystem::gatherDeviceExtensions(const nlohmann::json& all_config, const nlohmann::json& rhi_config)
    {
        std::vector<std::string> requiredDeviceExts;
        std::vector<std::string> requestedDeviceExts;

        {
            nlohmann::json reqExtJson = rhi_config.at("RequiredDeviceExtensions");
            for (auto& entry : reqExtJson)
            {
                requiredDeviceExts.emplace_back(entry);
            }
        }


        const bool hasSwapchainExt = std::find(requiredDeviceExts.begin(), requiredDeviceExts.end(), "VK_KHR_swapchain") != requiredDeviceExts.end();
        if (!hasSwapchainExt && rhi_config.value("NeedsPresentationSupport", false))
        {
            requiredDeviceExts.push_back("VK_KHR_swapchain");
            // if we are enabling presentation support, we should also do a "horizontal" query of sorts to see if we're requesting HDR
            // if we are, we should also enable the HDR extension. we need to use the "allConfig" because rhiConfig doesn't store surface/platform config data
            const bool hasPlatformConfig = all_config.contains("PlatformSystemConfig");
            if (hasPlatformConfig)
            {
                // now check for HDR request
                const nlohmann::json& platformConfig = all_config.at("PlatformSystemConfig");
                const bool wantsHDR = platformConfig.value("EnableHDR", false);
                if (wantsHDR)
                {
                    const bool hasHDRExt = std::find(requiredDeviceExts.begin(), requiredDeviceExts.end(), VK_EXT_HDR_METADATA_EXTENSION_NAME) != requiredDeviceExts.end();
                    if (!hasHDRExt)
                    {
                        // put this in requested, because it's not critical we have it but it is really nice to have! (and builds on the required swapchain ext)
                        requestedDeviceExts.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
                    }
                }
            }
        }

        extensionPack->AddRequiredDeviceExtensions(requiredDeviceExts);
        
        {
            nlohmann::json extJson = rhi_config.at("RequestedDeviceExtensions");
            for (auto& entry : extJson)
            {
                requestedDeviceExts.emplace_back(entry);
            }
        }

        extensionPack->AddOptionalDeviceExtensions(requestedDeviceExts);
    }

    void RhiSystem::createLogicalDevice()
    {
        logicalDevice = std::make_unique<Device>(vulkanInstance.get(), *extensionPack);
    }

    void RhiSystem::createDebugUtilsMessenger()
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            VkDebugUtilsFunctions debugUtilsFns = logicalDevice->GetDebugUtilFns();
            k_SSetObjectNameFn = debugUtilsFns.vkSetDebugUtilsObjectName;
            k_SDebugLogicalDeviceHandle = logicalDevice->Handle().As<VkDevice>();

            const VkDebugUtilsMessengerCreateInfoEXT messengerInfo
            {
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                nullptr,
                0,
                // capture warnings and info that the current one does not
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(DebugUtilsMessengerCallback),
                nullptr
            };

            if (!debugUtilsFns.vkCreateDebugUtilsMessenger)
            {
                std::cerr << "Debug utils function pointers struct doesn't have function pointer for debug utils messenger creation!\n";
                throw std::runtime_error("Failed to create debug utils messenger: function pointer not loaded!");
            }

            VkResult result = debugUtilsFns.vkCreateDebugUtilsMessenger(vulkanInstance->Handle().As<VkInstance>(), &messengerInfo, nullptr, &DebugUtilsMessenger);
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
            auto& pObjects = callback_data->pObjects;
            outputMessage += "    Object(s) involved: \n";
            for (uint32_t i = 0; i < callback_data->objectCount; ++i)
            {
                if (pObjects[i].pObjectName)
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

        if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            std::cerr << outputMessage << "\n";
        }
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            std::cerr << outputMessage << "\n";
        }
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            std::cout << outputMessage << "\n";
        }

        return VK_FALSE;
    }

    void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version)
    {
        const size_t minorDotPos = version_string.find('.');
        const size_t patchDotPos = version_string.rfind('.');
        if (patchDotPos == std::string::npos)
        {
            patch_version = 0;
            if (minorDotPos == std::string::npos)
            {
                minor_version = 0;
                major_version = static_cast<uint32_t>(strtod(version_string.c_str(), nullptr));
            }
            else
            {
                minor_version = static_cast<uint32_t>(strtod(version_string.substr(minorDotPos).c_str(), nullptr));
                major_version = static_cast<uint32_t>(strtod(version_string.substr(0, minorDotPos).c_str(), nullptr));
            }
        }
        else
        {
            if (minorDotPos == std::string::npos)
            {
                major_version = static_cast<uint32_t>(strtod(version_string.c_str(), nullptr));
                minor_version = 0;
                patch_version = 0;
                return;
            }
            else
            {
                major_version = static_cast<uint32_t>(strtod(version_string.substr(0, minorDotPos + 1).c_str(), nullptr));
                minor_version = static_cast<uint32_t>(strtod(version_string.substr(minorDotPos + 1, patchDotPos - minorDotPos - 1).c_str(), nullptr));
                patch_version = static_cast<uint32_t>(strtod(version_string.substr(patchDotPos).c_str(), nullptr));
            }
        }
    }

    void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version)
    {
        if (json_file.contains("ApplicationVersion"))
        {
            uint32_t appVersionMajor = 0;
            uint32_t appVersionMinor = 0;
            uint32_t appVersionPatch = 0;
            const std::string appVersionStr = json_file.at("ApplicationVersion");
            SplitVersionString(appVersionStr, appVersionMajor, appVersionMinor, appVersionPatch);
            app_version = VK_MAKE_VERSION(appVersionMajor, appVersionMinor, appVersionPatch);
        }
        else
        {
            app_version = VK_MAKE_VERSION(0, 1, 0);
        }

        if (json_file.contains("EngineVersion"))
        {
            uint32_t engineVersionMajor = 0;
            uint32_t engineVersionMinor = 0;
            uint32_t engineVersionPatch = 0;
            const std::string engineVersionStr = json_file.at("EngineVersion");
            SplitVersionString(engineVersionStr, engineVersionMajor, engineVersionMinor, engineVersionPatch);
            engine_version = VK_MAKE_VERSION(engineVersionMajor, engineVersionMinor, engineVersionPatch);
        }
        else
        {
            engine_version = VK_MAKE_VERSION(0, 1, 0);
        }

        if (json_file.contains("VulkanVersion"))
        {
            uint32_t apiVersionMajor = 0;
            uint32_t apiVersionMinor = 0;
            uint32_t apiVersionPatch = 0;
            const std::string apiVersionStr = json_file.at("VulkanVersion");
            if (apiVersionStr == "Latest")
            {
                vkEnumerateInstanceVersion(&api_version);
            }
            else
            {
                SplitVersionString(apiVersionStr, apiVersionMajor, apiVersionMinor, apiVersionPatch);
                api_version = VK_MAKE_VERSION(apiVersionMajor, apiVersionMinor, apiVersionPatch);
            }
        }
        else
        {
            vkEnumerateInstanceVersion(&api_version);
        }
    }

}
