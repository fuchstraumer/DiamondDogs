#include "RhiSystem.hpp"
#include "Instance.hpp"
#include "PhysicalDevice.hpp"
#include "LogicalDevice.hpp"
#include "Swapchain.hpp"
#include "SurfaceKHR.hpp"
#include "VkDebugUtils.hpp"
#include "vkAssert.hpp"
#include "ExtensionWrangler.hpp"
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <atomic>
#include <vector>
#include "nlohmann/json.hpp"

static void* usedNextPtr = nullptr;
static VkPhysicalDeviceFeatures* enabledDeviceFeatures = nullptr;
static std::vector<std::string> extensionsBuffer;
static std::string windowingModeBuffer;
static bool validationEnabled{ false };
// static instance of object name function, so that we can use it for static debug callbacks
static PFN_vkSetDebugUtilsObjectNameEXT s_SetObjectNameFn{ nullptr };
// set by first context to create, currently because we assume we'll only have one context
static VkDevice s_DebugLogicalDeviceHandle{ VK_NULL_HANDLE };

inline void RecreateSwapchain();

std::string objectTypeToString(const VkObjectType type);
VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagBitsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data);
void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version);
void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version);
void AddDependenciesForSetOfExtensions(std::vector<std::string>& extensions);

void GetPhysicalDeviceFeatures(VkInstance instance, const uint32_t apiVersion);



static std::atomic<bool>& GetShouldResizeFlag()
{
    static std::atomic<bool> should_resize{ false };
    return should_resize;
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
    
    vpr::VprExtensionPack extensionPack;

    createInstance(json_file, extensionPack);

    {
        size_t num_instance_extensions = 0;
        vulkanInstance->GetEnabledExtensions(&num_instance_extensions, nullptr);
        if (num_instance_extensions != 0)
        {
            std::vector<char*> extensions_buffer(num_instance_extensions);
            vulkanInstance->GetEnabledExtensions(&num_instance_extensions, extensions_buffer.data());
            for (auto& str : extensions_buffer)
            {
                instanceExtensions.emplace_back(str);
                free(str);
            }
        }
    }

    createLogicalDevice(json_file, extensionPack);

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

    {
        size_t num_device_extensions = 0;
        logicalDevice->GetEnabledExtensions(&num_device_extensions, nullptr);
        if (num_device_extensions != 0)
        {
            std::vector<char*> extensions_buffer(num_device_extensions);
            logicalDevice->GetEnabledExtensions(&num_device_extensions, extensions_buffer.data());
            for (auto& str : extensions_buffer)
            {
                deviceExtensions.emplace_back(str);
                free(str);
            }
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

vpr::Instance* RhiSystem::Instance() noexcept
{
    return vulkanInstance.get();
}

vpr::PhysicalDevice* RhiSystem::PhysicalDevice(const size_t idx) noexcept
{
    return physicalDevices[idx].get();
}

vpr::Device* RhiSystem::Device() noexcept
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

void RhiSystem::createInstance(const nlohmann::json& json_file, vpr::VprExtensionPack& extensionPack)
{
    const std::string app_name = json_file.at("ApplicationName");
    
    const std::string engine_name = json_file.at("EngineName");
    const bool using_validation = json_file.at("EnableValidation");
    validationEnabled = using_validation;

    uint32_t app_version = 0;
    uint32_t engine_version = 0;
    uint32_t api_version = 0;
    GetVersions(json_file, app_version, engine_version, api_version);
    vkApiVersion = api_version;

    // create extension wrangler, passing VK_NULL_HANDLE for physicalDevice to make it run in instance mode
    extensionWrangler = std::make_unique<ExtensionWrangler>(api_version, VK_NULL_HANDLE);

    std::vector<std::string> required_extensions_strs;
    {
        nlohmann::json req_ext_json = json_file.at("RequiredInstanceExtensions");
        for (auto& entry : req_ext_json)
        {
            required_extensions_strs.emplace_back(entry);
        }
    }

    std::vector<std::string> requested_extensions_strs;
    {
        nlohmann::json ext_json = json_file.at("RequestedInstanceExtensions");
        for (auto& entry : ext_json)
        {
            requested_extensions_strs.emplace_back(entry);
        }
    }

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

    logicalDevice = std::make_unique<vpr::Device>(vulkanInstance.get(), physicalDevices.front().get(), windowSurface->vkHandle(), &extensionPack, nullptr, 0);

    if (postLogicalDeviceFunction != nullptr)
    {
        postLogicalDeviceFunction(usedNextPtr);
    }
}

std::string objectTypeToString(const VkObjectType type)
{
    switch (type)
    {
    case VK_OBJECT_TYPE_INSTANCE:
        return "VkInstance";
    case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
        return "VkPhysicalDevice";
    case VK_OBJECT_TYPE_DEVICE:
        return "VkDevice";
    case VK_OBJECT_TYPE_QUEUE:
        return "VkQueue";
    case VK_OBJECT_TYPE_SEMAPHORE:
        return "VkSemaphore";
    case VK_OBJECT_TYPE_COMMAND_BUFFER:
        return "VkCommandBuffer";
    case VK_OBJECT_TYPE_FENCE:
        return "VkFence";
    case VK_OBJECT_TYPE_DEVICE_MEMORY:
        return "VkDeviceMemory";
    case VK_OBJECT_TYPE_BUFFER:
        return "VkBuffer";
    case VK_OBJECT_TYPE_IMAGE:
        return "VkImage";
    case VK_OBJECT_TYPE_EVENT:
        return "VkEvent";
    case VK_OBJECT_TYPE_QUERY_POOL:
        return "VkQueryPool";
    case VK_OBJECT_TYPE_BUFFER_VIEW:
        return "VkBufferView";
    case VK_OBJECT_TYPE_IMAGE_VIEW:
        return "VkImageView";
    case VK_OBJECT_TYPE_SHADER_MODULE:
        return "VkShaderModule";
    case VK_OBJECT_TYPE_PIPELINE_CACHE:
        return "VkPipelineCache";
    case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
        return "VkPipelineLayout";
    case VK_OBJECT_TYPE_RENDER_PASS:
        return "VkRenderPass";
    case VK_OBJECT_TYPE_PIPELINE:
        return "VkPipeline";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
        return "VkDescriptorSetLayout";
    case VK_OBJECT_TYPE_SAMPLER:
        return "VkSampler";
    case VK_OBJECT_TYPE_DESCRIPTOR_POOL:
        return "VkDescriptorPool";
    case VK_OBJECT_TYPE_DESCRIPTOR_SET:
        return "VkDescriptorSet";
    case VK_OBJECT_TYPE_FRAMEBUFFER:
        return "VkFramebuffer";
    case VK_OBJECT_TYPE_COMMAND_POOL:
        return "VkCommandPool";
    case VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION:
        return "VkSamplerYcbcrConversion";
    case VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE:
        return "VkDescriptorUpdateTemplate";
    case VK_OBJECT_TYPE_PRIVATE_DATA_SLOT:
        return "VkPrivateDataSlot";
    case VK_OBJECT_TYPE_SURFACE_KHR:
        return "VkSurfaceKHR";
    case VK_OBJECT_TYPE_SWAPCHAIN_KHR:
        return "VkSwapchainKHR";
    case VK_OBJECT_TYPE_DISPLAY_KHR:
        return "VkDisplayKHR";
    case VK_OBJECT_TYPE_DISPLAY_MODE_KHR:
        return "VkDisplayModeKHR";
    case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT:
        return "VkDebugReportCallbackEXT";
    case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT:
        return "VkDebugUtilsMessengerEXT";
    case VK_OBJECT_TYPE_VALIDATION_CACHE_EXT:
        return "VkValidationCacheEXT";
    case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV:
        return "VkAccelerationStructureNV";
    case VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR:
        return "VkDeferredOperationKHR";
    case VK_OBJECT_TYPE_SHADER_EXT:
        return "VkShaderEXT";
    case VK_OBJECT_TYPE_PIPELINE_BINARY_KHR:
        return "VkPipelineBinaryKHR";
    case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT:
        return "VkIndirectCommandsLayoutEXT";
    case VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT:
        return "VkIndirectExecutionSetEXT";
    default:
        return std::string("TYPE_UNKNOWN:" + std::to_string(size_t(type)));
    };
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                           VkDebugUtilsMessageTypeFlagBitsEXT message_type,
                                                           const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                           void* user_data)
{

    std::stringstream output_string_stream;
    if (callback_data->messageIdNumber != 0u)
    {
        output_string_stream << "VUID:" << callback_data->messageIdNumber << ":VUID_NAME:" << callback_data->pMessageIdName << "\n";
    }

    const static std::string SKIP_STR{ "CREATE" };
    const std::string message_str{ callback_data->pMessage };
    size_t found_skippable = message_str.find(SKIP_STR);

    if (found_skippable != std::string::npos)
    {
        return VK_FALSE;
    }

    output_string_stream << "    Message: " << message_str.c_str() << "\n";
    if (callback_data->queueLabelCount != 0u)
    {
        output_string_stream << "    Error occured in queue: " << callback_data->pQueueLabels[0].pLabelName << "\n";
    }

    if (callback_data->cmdBufLabelCount != 0u)
    {
        output_string_stream << "    Error occured executing command buffer(s): \n";
        for (uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i)
        {
            output_string_stream << "    " << callback_data->pCmdBufLabels[i].pLabelName << "\n";
        }
    }
    if (callback_data->objectCount != 0u)
    {
        auto& p_objects = callback_data->pObjects;
        output_string_stream << "    Object(s) involved: \n";
        for (uint32_t i = 0; i < callback_data->objectCount; ++i)
        {
            if (p_objects[i].pObjectName)
            {
                output_string_stream << "        ObjectName: " << p_objects[i].pObjectName << "\n";
            }
            else
            {
                output_string_stream << "        UNNAMED_OBJECT\n";
            }
            output_string_stream << "            ObjectType: " << objectTypeToString(p_objects[i].objectType) << "\n";
            output_string_stream << "            ObjectHandle: " << std::hex << std::to_string(p_objects[i].objectHandle) << "\n";
        }
    }

    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        std::cerr << output_string_stream.str() << "\n";
    }
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << output_string_stream.str() << "\n";
    }
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        std::cout << output_string_stream.str() << "\n";
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

void AddDependenciesForSetOfExtensions(std::vector<std::string>& extensions)
{
    std::vector<std::string> extensions_dependencies_strs;

    if (!extensions_dependencies_strs.empty())
    {
        for (auto iter = extensions_dependencies_strs.begin(); iter != extensions_dependencies_strs.end();)
        {
            auto alreadyHaveExtensionInVector = std::find(extensions.cbegin(), extensions.cend(), *iter);
            // VK_KHR_surface does show up as a dependency, but how we wrangle that is platform-specific - so it shouldn't actually
            // be counted as required. Just a special edge case for something already handled as such by the API!
            if (alreadyHaveExtensionInVector != extensions.end() || (*iter == "VK_KHR_surface"))
            {
                iter = extensions_dependencies_strs.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        if (!extensions_dependencies_strs.empty())
        {
            extensions.insert(extensions.end(), extensions_dependencies_strs.cbegin(), extensions_dependencies_strs.cend());
        }
    }
}
