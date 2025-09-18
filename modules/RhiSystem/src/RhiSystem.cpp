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

    std::string objectTypeToString(const VkObjectType type);
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagBitsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data);
    void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version);
    void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version);
    void AddDependenciesForSetOfExtensions(std::vector<std::string>& extensions);

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

    RhiSystem::~RhiSystem()
    {
        Destroy();
    }

    RhiSystem::RhiSystem() noexcept {}

    void RhiSystem::Construct(const char* file_path)
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
        createInstance(json_file);
        createLogicalDevice(json_file);

        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            s_SetObjectNameFn = logicalDevice->DebugUtilsHandler().vkSetDebugUtilsObjectName;
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

            const auto& debugUtilsFnPtrs = logicalDevice->DebugUtilsHandler();

            if (!debugUtilsFnPtrs.vkCreateDebugUtilsMessenger)
            {
                std::cerr << "Debug utils function pointers struct doesn't have function pointer for debug utils messenger creation!\n";
                throw std::runtime_error("Failed to create debug utils messenger: function pointer not loaded!");
            }

            VkResult result = debugUtilsFnPtrs.vkCreateDebugUtilsMessenger(vulkanInstance->vkHandle(), &messenger_info, nullptr, &DebugUtilsMessenger);
            if (result != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create debug utils messenger.");
            }
        }

    }

    void RhiSystem::Update()
    {

    }

    void RhiSystem::Destroy()
    {
        if constexpr (RHI_SYSTEM_VALIDATION_ENABLED)
        {
            logicalDevice->DebugUtilsHandler().vkDestroyDebugUtilsMessenger(vulkanInstance->vkHandle(), DebugUtilsMessenger, nullptr);
        }
        logicalDevice.reset();
        physicalDevices.clear();
        vulkanInstance.reset();
        extensionWrangler.reset();
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
            const bool hasSwapchainExt = std::find(requiredInstanceExts.begin(), requiredInstanceExts.end(), "VK_KHR_swapchain") != requiredInstanceExts.end();
            if (!hasSurfaceExt || !hasSwapchainExt)
            {
                // user didn't include the extensions, so add them
                requiredInstanceExts.push_back(SurfaceExtensionName);
                requiredInstanceExts.push_back("VK_KHR_swapchain");
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

    void RhiSystem::createInstance(const nlohmann::json& rhiConfig)
    {
        const bool using_validation = json_file.at("EnableValidation");
        validationEnabled = using_validation;

        uint32_t app_version = 0;
        uint32_t engine_version = 0;
        uint32_t api_version = 0;
        GetVersions(json_file, app_version, engine_version, api_version);
        vkApiVersion = api_version;


        // convert to arrays of std::string_view for the extension wrangler 

        std::vector<std::string_view> required_extensions;
        for (auto& str : required_extensions_strs)
        {
            required_extensions.emplace_back(str);
        }

        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> requiredExtensionDeps = extensionWrangler->GetExtensionDependencies(required_extensions.size(), required_extensions.data());
        if (requiredExtensionDeps.has_value())
        {
            for (size_t i = 0; i < requiredExtensionDeps->numInstanceExtensionDeps; ++i)
            {
                required_extensions.emplace_back(requiredExtensionDeps->instanceExtensionDeps[i]);
            }
        }

        std::vector<std::string_view> requested_extensions;
        for (auto& str : requested_extensions_strs)
        {
            requested_extensions.emplace_back(str);
        }

        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> requestedExtensionDeps = extensionWrangler->GetExtensionDependencies(requested_extensions.size(), requested_extensions.data());
        if (requestedExtensionDeps.has_value())
        {
            for (size_t i = 0; i < requestedExtensionDeps->numInstanceExtensionDeps; ++i)
            {
                requested_extensions.emplace_back(requestedExtensionDeps->instanceExtensionDeps[i]);
            }
        }


        // convert again to arrays of const char* for the extension pack. this is starting to get a bit absurd.
        std::vector<const char*> required_extensions_cstr;
        for (auto& str : required_extensions)
        {
            required_extensions_cstr.emplace_back(str.data());
        }

        std::vector<const char*> requested_extensions_cstr;
        for (auto& str : requested_extensions)
        {
            requested_extensions_cstr.emplace_back(str.data());
        }
        

        extensionPack.PreferredApiVersion = vpr::VprExtensionPack::ApiVersion::Vulkan13;
        extensionPack.RequiredExtensionCount = static_cast<uint32_t>(required_extensions_cstr.size());
        extensionPack.RequiredExtensionNames = reinterpret_cast<const char**>(required_extensions_cstr.data());
        extensionPack.OptionalExtensionCount = static_cast<uint32_t>(requested_extensions_cstr.size());
        extensionPack.OptionalExtensionNames = reinterpret_cast<const char**>(requested_extensions_cstr.data());

        const VkApplicationInfo application_info
        {
            VK_STRUCTURE_TYPE_APPLICATION_INFO,
            nullptr,
            app_name.c_str(),
            app_version,
            engine_name.c_str(),
            engine_version,
            api_version
        };

        auto layers = using_validation ? vpr::Instance::instance_layers::Full : vpr::Instance::instance_layers::Disabled;
        vulkanInstance = std::make_unique<vpr::Instance>(layers, &application_info, &extensionPack);

        extensionPack.featuresToEnable = nullptr;
        extensionPack.featuresToEnable2 = nullptr;// &queriedDeviceFeatures->deviceFeaturesBase;

        physicalDevices.emplace_back(std::make_unique<vpr::PhysicalDevice>(vulkanInstance->vkHandle(), &extensionPack));
        extensionWrangler.reset(); // reset because we'll need the VkPhysicalDevice handle for it's next incarnation
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

    void RhiSystem::createLogicalDevice(const nlohmann::json& json_file, vpr::VprExtensionPack& extensionPack)
    {

        extensionWrangler = std::make_unique<ExtensionWrangler>(vkApiVersion, physicalDevices.front()->vkHandle());

        std::vector<std::string> required_extensions_strs;
        {
            nlohmann::json req_ext_json = json_file.at("RequiredDeviceExtensions");
            for (auto& entry : req_ext_json)
            {
                required_extensions_strs.emplace_back(entry);
            }
        }

        std::vector<std::string> requested_extensions_strs;
        {
            nlohmann::json ext_json = json_file.at("RequestedDeviceExtensions");
            for (auto& entry : ext_json)
            {
                requested_extensions_strs.emplace_back(entry);
            }
        }

        // from string objects to std::string_view, then to query deps and add them to the list
        std::vector<std::string_view> required_extensions_strs_view;
        for (auto& str : required_extensions_strs)
        {
            required_extensions_strs_view.emplace_back(str);
        }

        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> required_extension_deps =
            extensionWrangler->GetExtensionDependencies(required_extensions_strs_view.size(), required_extensions_strs_view.data());
        if (required_extension_deps.has_value())
        {
            for (size_t i = 0; i < required_extension_deps->numDeviceExtensionDeps; ++i)
            {
                required_extensions_strs_view.emplace_back(required_extension_deps->deviceExtensionDeps[i]);
            }
        }

        std::vector<std::string_view> requested_extensions_strs_view;
        for (auto& str : requested_extensions_strs)
        {
            requested_extensions_strs_view.emplace_back(str);
        }

        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> requested_extension_deps =
            extensionWrangler->GetExtensionDependencies(requested_extensions_strs_view.size(), requested_extensions_strs_view.data());
        if (requested_extension_deps.has_value())
        {
            for (size_t i = 0; i < requested_extension_deps->numDeviceExtensionDeps; ++i)
            {
                requested_extensions_strs_view.emplace_back(requested_extension_deps->deviceExtensionDeps[i]);
            }
        }

        // from str_view to cstr
        std::vector<const char*> required_extensions;
        for (auto& str : required_extensions_strs_view)
        {
            required_extensions.emplace_back(str.data());
        }

        std::vector<const char*> requested_extensions;
        for (auto& str : requested_extensions_strs_view)
        {
            requested_extensions.emplace_back(str.data());
        }

        extensionPack.RequiredExtensionCount = static_cast<uint32_t>(required_extensions.size());
        extensionPack.RequiredExtensionNames = required_extensions.data();
        extensionPack.OptionalExtensionCount = static_cast<uint32_t>(requested_extensions.size());
        extensionPack.OptionalExtensionNames = requested_extensions.data();

        // last step: combine both pools of extensions to get the device features
        // note: we need to tear out the split between required and optional extensions, as the device features are going to be enabled for all, but if for example
        // we elect to disable any optional extensions, we need to ensure the device features are still valid
        std::vector<std::string_view> all_extensions;
        all_extensions.reserve(required_extensions_strs_view.size() + requested_extensions_strs_view.size());
        all_extensions.insert(all_extensions.end(), required_extensions_strs_view.begin(), required_extensions_strs_view.end());
        all_extensions.insert(all_extensions.end(), requested_extensions_strs_view.begin(), requested_extensions_strs_view.end());

        std::expected<VkPhysicalDeviceFeatures2, ExtensionWrangler::DependencyError> all_extensions_features_expected =
            extensionWrangler->GetExtensionFeatures(all_extensions.size(), all_extensions.data(), ExtensionWrangler::GetVersionFeatures::True, ExtensionWrangler::CollectDependencies::False);

        VkPhysicalDeviceFeatures2 all_extensions_features = all_extensions_features_expected.value_or(VkPhysicalDeviceFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr });
        extensionPack.featuresToEnable = nullptr;
        extensionPack.featuresToEnable2 = &all_extensions_features;

        logicalDevice = std::make_unique<Device>(vulkanInstance.get(), physicalDevices.front().get(), windowSurface->vkHandle(), &extensionPack, nullptr, 0);

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
        {
            uint32_t app_version_major = 0;
            uint32_t app_version_minor = 0;
            uint32_t app_version_patch = 0;
            const std::string app_version_str = json_file.at("ApplicationVersion");
            SplitVersionString(app_version_str, app_version_major, app_version_minor, app_version_patch);
            app_version = VK_MAKE_VERSION(app_version_major, app_version_minor, app_version_patch);
        }

        {
            uint32_t engine_version_major = 0;
            uint32_t engine_version_minor = 0;
            uint32_t engine_version_patch = 0;
            const std::string engine_version_str = json_file.at("EngineVersion");
            SplitVersionString(engine_version_str, engine_version_major, engine_version_minor, engine_version_patch);
            engine_version = VK_MAKE_VERSION(engine_version_major, engine_version_minor, engine_version_patch);
        }

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
    }

}
