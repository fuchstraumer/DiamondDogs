#include "RenderingContext.hpp"
#include "PlatformWindow.hpp"
#include "Instance.hpp"
#include "PhysicalDevice.hpp"
#include "LogicalDevice.hpp"
#include "Swapchain.hpp"
#include "SurfaceKHR.hpp"
#include "VkDebugUtils.hpp"
#include "vkAssert.hpp"
#include <thread>
#include <sstream>
#include <chrono>
#include <iostream>
#include <sstream>
#include <fstream>
#include <atomic>
#include <forward_list>
#include "GLFW/glfw3.h"
#ifdef APIENTRY
// re-defined by glfw on windows, then seen again by easylogging
#undef APIENTRY
#endif
#include "nlohmann/json.hpp"

#include "GeneratedExtensionHeader.hpp"

static post_physical_device_pre_logical_device_function_t postPhysicalPreLogicalSetupFunction = nullptr;
static post_logical_device_function_t postLogicalDeviceFunction = nullptr;
static void* usedNextPtr = nullptr;
static VkPhysicalDeviceFeatures* enabledDeviceFeatures = nullptr;
static std::vector<std::string> extensionsBuffer;
static std::string windowingModeBuffer;
static bool validationEnabled{ false };
struct swapchain_callbacks_storage_t
{
    std::forward_list<decltype(SwapchainCallbacks::SwapchainCreated)> CreationFns;
    std::forward_list<decltype(SwapchainCallbacks::BeginResize)> BeginFns;
    std::forward_list<decltype(SwapchainCallbacks::CompleteResize)> CompleteFns;
    std::forward_list<decltype(SwapchainCallbacks::SwapchainDestroyed)> DestroyedFns;
};
static swapchain_callbacks_storage_t SwapchainCallbacksStorage;
inline void RecreateSwapchain();

std::string objectTypeToString(const VkObjectType type);
VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagBitsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data);
void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version);
void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version);
void AddDependenciesForSetOfExtensions(std::vector<std::string>& extensions);

struct QueriedDeviceFeatures
{
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, nullptr };
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extendedDynamicState2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT, &extendedDynamicState3Features };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, &extendedDynamicState2Features };
    VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rayTracingMaintenanceFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, &extendedDynamicStateFeatures };
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &rayTracingMaintenanceFeatures };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingPipelineFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, &rayQueryFeatures };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &raytracingPipelineFeatures };
    VkPhysicalDeviceCoherentMemoryFeaturesAMD coherentMemoryFeaturesAMD{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD, &accelerationStructureFeatures };
    VkPhysicalDeviceLineRasterizationFeaturesEXT lineRasterizationFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT, &coherentMemoryFeaturesAMD };
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeaturesEXT{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT, &lineRasterizationFeatures };
    VkPhysicalDeviceMeshShaderFeaturesNV meshShaderFeaturesNV{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV, &meshShaderFeaturesEXT };
    VkPhysicalDeviceConditionalRenderingFeaturesEXT conditionalRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT, &meshShaderFeaturesNV };
    VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT blendOperationAdvancedFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT, &conditionalRenderingFeatures };
    VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR, &blendOperationAdvancedFeatures };
    VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT shaderAtomicFloat2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT, &shaderClockFeatures };
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT shaderAtomicFloatFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT, &shaderAtomicFloat2Features };
    VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &shaderAtomicFloatFeatures };
    VkPhysicalDeviceVulkan12Features vulkan12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &vulkan13Features };
    VkPhysicalDeviceVulkan11Features vulkan11Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &vulkan12Features };
    VkPhysicalDeviceFeatures2 deviceFeaturesBase{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan11Features, VkPhysicalDeviceFeatures{} };
};

void GetPhysicalDeviceFeatures(VkInstance instance, const uint32_t apiVersion, QueriedDeviceFeatures& features);

static const std::unordered_map<std::string, windowing_mode> windowing_mode_str_to_flag
{
    { "Windowed", windowing_mode::Windowed },
    { "BorderlessWindowed", windowing_mode::BorderlessWindowed },
    { "Fullscreen", windowing_mode::Fullscreen }
};

static std::atomic<bool>& GetShouldResizeFlag()
{
    static std::atomic<bool> should_resize{ false };
    return should_resize;
}

DescriptorLimits::DescriptorLimits(const vpr::PhysicalDevice* hostDevice)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(hostDevice->vkHandle(), &properties);
    const VkPhysicalDeviceLimits& limits = properties.limits;
    MaxSamplers = limits.maxDescriptorSetSamplers;
    MaxUniformBuffers = limits.maxDescriptorSetUniformBuffers;
    MaxDynamicUniformBuffers = limits.maxDescriptorSetUniformBuffersDynamic;
    MaxStorageBuffers = limits.maxDescriptorSetStorageBuffers;
    MaxDynamicStorageBuffers = limits.maxDescriptorSetStorageBuffersDynamic;
    MaxSampledImages = limits.maxDescriptorSetSampledImages;
    MaxStorageImages = limits.maxDescriptorSetStorageImages;
    MaxInputAttachments = limits.maxDescriptorSetInputAttachments;
}

RenderingContext::~RenderingContext()
{
    Destroy();
}

RenderingContext::RenderingContext() noexcept {}

RenderingContext& RenderingContext::Get() noexcept
{
    static RenderingContext ctxt;
    return ctxt;
}

void RenderingContext::SetShouldResize(bool resize)
{
    auto& flag = GetShouldResizeFlag();
    flag = resize;
}

bool RenderingContext::ShouldResizeExchange(bool value)
{
    return GetShouldResizeFlag().exchange(value);
}

void RenderingContext::Construct(const char* file_path)
{

    std::ifstream input_file(file_path);

    if (!input_file.is_open())
    {
        throw std::runtime_error("Couldn't open input file.");
    }

    nlohmann::json json_file;
    input_file >> json_file;
    
    vpr::VprExtensionPack extensionPack;

    createInstanceAndWindow(json_file, windowMode, extensionPack);
    window->SetWindowUserPointer(this);

    if (postPhysicalPreLogicalSetupFunction != nullptr)
    {
        postPhysicalPreLogicalSetupFunction(physicalDevices.back()->vkHandle(), nullptr, &usedNextPtr);
    }

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

    windowSurface = std::make_unique<vpr::SurfaceKHR>(vulkanInstance.get(), physicalDevices[0]->vkHandle(), (void*)window->glfwWindow());

    createLogicalDevice(json_file, extensionPack);

    if constexpr (RENDERING_CONTEXT_VALIDATION_ENABLED)
    {
        SetObjectNameFn = logicalDevice->DebugUtilsHandler().vkSetDebugUtilsObjectName;

        const VkDebugUtilsMessengerCreateInfoEXT messenger_info{
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

    static const std::unordered_map<std::string, vpr::vertical_sync_mode> present_mode_from_str_map
    {
        { "None", vpr::vertical_sync_mode::None },
        { "VerticalSync", vpr::vertical_sync_mode::VerticalSync },
        { "VerticalSyncRelaxed", vpr::vertical_sync_mode::VerticalSyncRelaxed },
        { "VerticalSyncMailbox", vpr::vertical_sync_mode::VerticalSyncMailbox }
    };

    auto iter = json_file.find("VerticalSyncMode");
    // We want to go for this, as it's the ideal mode usually.
    vpr::vertical_sync_mode desired_mode = vpr::vertical_sync_mode::VerticalSyncMailbox;
    if (iter != json_file.end()) {
        auto present_mode_iter = present_mode_from_str_map.find(json_file.at("VerticalSyncMode"));
        if (present_mode_iter != std::cend(present_mode_from_str_map)) {
            desired_mode = present_mode_iter->second;
        }
    }

    swapchain = std::make_unique<vpr::Swapchain>(logicalDevice.get(), window->glfwWindow(), windowSurface->vkHandle(), desired_mode);

    if constexpr (RENDERING_CONTEXT_VALIDATION_ENABLED && RENDERING_CONTEXT_USE_DEBUG_INFO)
    {
        SetObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, (uint64_t)swapchain->vkHandle(), "RenderingContextSwapchain");

        for (size_t i = 0u; i < swapchain->ImageCount(); ++i)
        {
            const std::string view_name = std::string("RenderingContextSwapchain_ImageView") + std::to_string(i);
            SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)swapchain->ImageView(i), view_name.c_str());
            const std::string img_name = std::string("RenderingContextSwapchain_Image") + std::to_string(i);
            SetObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)swapchain->Image(i), img_name.c_str());
        }
    }

}

void RenderingContext::Update()
{
    window->Update();
    if (ShouldResizeExchange(false))
    {
        RecreateSwapchain();
    }
}

void RenderingContext::Destroy()
{
    swapchain.reset();
    windowSurface.reset();
    if constexpr (RENDERING_CONTEXT_VALIDATION_ENABLED)
    {
        logicalDevice->DebugUtilsHandler().vkDestroyDebugUtilsMessenger(vulkanInstance->vkHandle(), DebugUtilsMessenger, nullptr);
    }
    logicalDevice.reset();
    physicalDevices.clear();
    vulkanInstance.reset();
    window.reset();
    queriedDeviceFeatures.reset();
    enabledDeviceFeatures.reset();
}

vpr::Instance * RenderingContext::Instance() noexcept
{
    return vulkanInstance.get();
}

vpr::PhysicalDevice * RenderingContext::PhysicalDevice(const size_t idx) noexcept
{
    return physicalDevices[idx].get();
}

vpr::Device* RenderingContext::Device() noexcept
{
    return logicalDevice.get();
}

vpr::Swapchain* RenderingContext::Swapchain() noexcept
{
    return swapchain.get();
}

vpr::SurfaceKHR* RenderingContext::Surface() noexcept
{
    return windowSurface.get();
}

PlatformWindow* RenderingContext::Window() noexcept
{
    return window.get();
}

GLFWwindow* RenderingContext::glfwWindow() noexcept
{
    return window->glfwWindow();
}

inline GLFWwindow* getWindow()
{
    auto& ctxt = RenderingContext::Get();
    return ctxt.glfwWindow();
}

#pragma warning(push)
#pragma warning(disable: 4302)
#pragma warning(disable: 4311)
inline void RecreateSwapchain()
{

    auto& Context = RenderingContext::Get();

    int width = 0;
    int height = 0;
    // We wait while this is true so that we don't bother running the app while this is zero
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(Context.glfwWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(Context.Device()->vkHandle());

    Context.Window()->GetWindowSize(width, height);

    for (auto& fn : SwapchainCallbacksStorage.BeginFns)
    {
        fn(Context.Swapchain()->vkHandle(), width, height);
    }

    vpr::RecreateSwapchainAndSurface(Context.Swapchain(), Context.Surface());
    Context.Device()->UpdateSurface(Context.Surface()->vkHandle());

    Context.Window()->GetWindowSize(width, height);
    for (auto& fn : SwapchainCallbacksStorage.CompleteFns)
    {
        fn(Context.Swapchain()->vkHandle(), width, height);
    }

    vkDeviceWaitIdle(Context.Device()->vkHandle());
}
#pragma warning(pop)


void AddSwapchainCallbacks(SwapchainCallbacks callbacks)
{
    if (callbacks.SwapchainCreated)
    {
        SwapchainCallbacksStorage.CreationFns.emplace_front(callbacks.SwapchainCreated);
    }

    if (callbacks.BeginResize)
    {
        SwapchainCallbacksStorage.BeginFns.emplace_front(callbacks.BeginResize);
    }

    if (callbacks.CompleteResize)
    {
        SwapchainCallbacksStorage.CompleteFns.emplace_front(callbacks.CompleteResize);
    }

    if (callbacks.SwapchainDestroyed)
    {
        SwapchainCallbacksStorage.DestroyedFns.emplace_front(callbacks.SwapchainDestroyed);
    }
}

void RenderingContext::AddSetupFunctions(post_physical_device_pre_logical_device_function_t fn0, post_logical_device_function_t fn1)
{
    postPhysicalPreLogicalSetupFunction = fn0;
    postLogicalDeviceFunction = fn1;
}

void RenderingContext::AddSwapchainCallbacks(SwapchainCallbacks callbacks)
{
    SwapchainCallbacksStorage.BeginFns.emplace_front(callbacks.BeginResize);
    SwapchainCallbacksStorage.CompleteFns.emplace_front(callbacks.CompleteResize);
}

void RenderingContext::GetWindowSize(int& w, int& h)
{
    glfwGetWindowSize(getWindow(), &w, &h);
}

void RenderingContext::GetFramebufferSize(int& w, int& h)
{
    glfwGetFramebufferSize(getWindow(), &w, &h);
}

void RenderingContext::RegisterCursorPosCallback(cursor_pos_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddCursorPosCallbackFn(callback_fn);
}

void RenderingContext::RegisterCursorEnterCallback(cursor_enter_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddCursorEnterCallbackFn(callback_fn);
}

void RenderingContext::RegisterScrollCallback(scroll_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddScrollCallbackFn(callback_fn);
}

void RenderingContext::RegisterCharCallback(char_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddCharCallbackFn(callback_fn);
}

void RenderingContext::RegisterPathDropCallback(path_drop_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddPathDropCallbackFn(callback_fn);
}

void RenderingContext::RegisterMouseButtonCallback(mouse_button_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddMouseButtonCallbackFn(callback_fn);
}

void RenderingContext::RegisterKeyboardKeyCallback(keyboard_key_callback_t callback_fn)
{
    auto& ctxt = Get();
    ctxt.Window()->AddKeyboardKeyCallbackFn(callback_fn);
}

int RenderingContext::GetMouseButton(int button)
{
    return glfwGetMouseButton(getWindow(), button);
}

void RenderingContext::GetCursorPosition(double& x, double& y)
{
    glfwGetCursorPos(getWindow(), &x, &y);
}

void RenderingContext::SetCursorPosition(double x, double y)
{
    glfwSetCursorPos(getWindow(), x, y);
}

void RenderingContext::SetCursor(GLFWcursor* cursor)
{
    glfwSetCursor(getWindow(), cursor);
}

GLFWcursor* RenderingContext::CreateCursor(GLFWimage* image, int w, int h)
{
    return glfwCreateCursor(image, w, h);
}

GLFWcursor* RenderingContext::CreateStandardCursor(int type)
{
    return glfwCreateStandardCursor(type);
}

void RenderingContext::DestroyCursor(GLFWcursor* cursor)
{
    glfwDestroyCursor(cursor);
}

bool RenderingContext::ShouldWindowClose()
{
    return glfwWindowShouldClose(getWindow());
}

int RenderingContext::GetWindowAttribute(int attrib)
{
    return glfwGetWindowAttrib(getWindow(), attrib);
}

void RenderingContext::SetInputMode(int mode, int val)
{
    glfwSetInputMode(getWindow(), mode, val);
}

int RenderingContext::GetInputMode(int mode)
{
    return glfwGetInputMode(getWindow(), mode);
}

const char* RenderingContext::GetShaderCacheDir()
{
    auto& ctxt = Get();
    return ctxt.shaderCacheDir.c_str();
}

void RenderingContext::SetShaderCacheDir(const char* dir)
{
    auto& ctxt = Get();
    ctxt.shaderCacheDir = dir;
}

VkResult RenderingContext::SetObjectName(VkObjectType object_type, uint64_t handle, const char* name)
{
    if constexpr (RENDERING_CONTEXT_VALIDATION_ENABLED && RENDERING_CONTEXT_USE_DEBUG_INFO)
    {
        auto& ctxt = Get();

        if constexpr (RENDERING_CONTEXT_DEBUG_INFO_THREAD_ID || RENDERING_CONTEXT_DEBUG_INFO_TIMESTAMPS)
        {
            std::string object_name_str{ name };
            std::stringstream extra_info_stream;
            if constexpr (RENDERING_CONTEXT_DEBUG_INFO_THREAD_ID)
            {
                extra_info_stream << std::string("_ThreadID:") << std::this_thread::get_id();
            }
            if constexpr (RENDERING_CONTEXT_DEBUG_INFO_TIMESTAMPS)
            {

            }

            object_name_str += extra_info_stream.str();

            const VkDebugUtilsObjectNameInfoEXT name_info
            {
                VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                nullptr,
                object_type,
                handle,
                object_name_str.c_str()
            };

            return ctxt.SetObjectNameFn(ctxt.logicalDevice->vkHandle(), &name_info);
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
            return ctxt.SetObjectNameFn(ctxt.logicalDevice->vkHandle(), &name_info);
        }
    }
    else
    {
        return VK_SUCCESS;
    }
}

void RenderingContext::createInstanceAndWindow(const nlohmann::json& json_file, std::string& _window_mode, vpr::VprExtensionPack& extensionPack)
{
    int window_width = json_file.at("InitialWindowWidth");
    int window_height = json_file.at("InitialWindowHeight");
    const std::string app_name = json_file.at("ApplicationName");
    const std::string windowing_mode_str = json_file.at("InitialWindowMode");
    windowingModeBuffer = windowing_mode_str;
    _window_mode = windowingModeBuffer;
    auto iter = windowing_mode_str_to_flag.find(windowing_mode_str);
    windowing_mode window_mode = windowing_mode::Windowed;
    if (iter != std::cend(windowing_mode_str_to_flag))
    {
        window_mode = iter->second;
    }

    window = std::make_unique<PlatformWindow>(window_width, window_height, app_name.c_str(), window_mode);

    const std::string engine_name = json_file.at("EngineName");
    const bool using_validation = json_file.at("EnableValidation");
    validationEnabled = using_validation;

    uint32_t app_version = 0;
    uint32_t engine_version = 0;
    uint32_t api_version = 0;
    GetVersions(json_file, app_version, engine_version, api_version);

    std::vector<std::string> required_extensions_strs;
    {
        nlohmann::json req_ext_json = json_file.at("RequiredInstanceExtensions");
        for (auto& entry : req_ext_json)
        {
            required_extensions_strs.emplace_back(entry);
        }
    }

    // Populate dependencies required for required instance extensions
    AddDependenciesForSetOfExtensions(required_extensions_strs);

    std::vector<std::string> requested_extensions_strs;
    {
        nlohmann::json ext_json = json_file.at("RequestedInstanceExtensions");
        for (auto& entry : ext_json)
        {
            requested_extensions_strs.emplace_back(entry);
        }
    }

    AddDependenciesForSetOfExtensions(requested_extensions_strs);

    std::vector<const char*> required_extensions;
    for (auto& str : required_extensions_strs)
    {
        required_extensions.emplace_back(str.c_str());
    }

    std::vector<const char*> requested_extensions;
    for (auto& str : requested_extensions_strs)
    {
        requested_extensions.emplace_back(str.c_str());
    }

    VkPhysicalDeviceFeatures features;
    

    extensionPack.PreferredApiVersion = vpr::VprExtensionPack::ApiVersion::Vulkan13;
    extensionPack.RequiredExtensionCount = static_cast<uint32_t>(required_extensions.size());
    extensionPack.RequiredExtensionNames = required_extensions.data();
    extensionPack.OptionalExtensionCount = static_cast<uint32_t>(requested_extensions.size());
    extensionPack.OptionalExtensionNames = requested_extensions.data();
    extensionPack.featuresToEnable2 = nullptr;
    extensionPack.featuresToEnable = &features;

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

    queriedDeviceFeatures = std::make_unique<QueriedDeviceFeatures>();
    GetPhysicalDeviceFeatures(vulkanInstance->vkHandle(), api_version, *queriedDeviceFeatures);

    extensionPack.featuresToEnable = nullptr;
    extensionPack.featuresToEnable2 = &queriedDeviceFeatures->deviceFeaturesBase;

    physicalDevices.emplace_back(std::make_unique<vpr::PhysicalDevice>(vulkanInstance->vkHandle(), &extensionPack));
}

void RenderingContext::createLogicalDevice(const nlohmann::json& json_file, vpr::VprExtensionPack& extensionPack)
{
    std::vector<std::string> required_extensions_strs;
    {
        nlohmann::json req_ext_json = json_file.at("RequiredDeviceExtensions");
        for (auto& entry : req_ext_json)
        {
            required_extensions_strs.emplace_back(entry);
        }
    }

    AddDependenciesForSetOfExtensions(required_extensions_strs);

    std::vector<std::string> requested_extensions_strs;
    {
        nlohmann::json ext_json = json_file.at("RequestedDeviceExtensions");
        for (auto& entry : ext_json)
        {
            requested_extensions_strs.emplace_back(entry);
        }
    }

    AddDependenciesForSetOfExtensions(requested_extensions_strs);

    std::vector<const char*> required_extensions;
    for (auto& str : required_extensions_strs)
    {
        required_extensions.emplace_back(str.c_str());
    }

    std::vector<const char*> requested_extensions;
    for (auto& str : requested_extensions_strs)
    {
        requested_extensions.emplace_back(str.c_str());
    }

    extensionPack.RequiredExtensionCount = static_cast<uint32_t>(required_extensions.size());
    extensionPack.RequiredExtensionNames = required_extensions.data();
    extensionPack.OptionalExtensionCount = static_cast<uint32_t>(requested_extensions.size());
    extensionPack.OptionalExtensionNames = requested_extensions.data();

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
    default:
        return std::string("TYPE_UNKNOWN:" + std::to_string(size_t(type)));
    };
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagBitsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
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

    if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        std::cerr << output_string_stream.str() << "\n";
    }
    else if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << output_string_stream.str() << "\n";
    }
    else if (message_severity <= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
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

void GetPhysicalDeviceFeatures(VkInstance instance, const uint32_t apiVersion, QueriedDeviceFeatures& features)
{
    uint32_t deviceCount = 0u;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount, VK_NULL_HANDLE);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice bestScoringDevice = vpr::ChooseBestScoringPhysicalDevice(devices.size(), devices.data());

    vkGetPhysicalDeviceFeatures2(bestScoringDevice, &features.deviceFeaturesBase);
}

void AddDependenciesForSetOfExtensions(std::vector<std::string>& extensions)
{
	std::vector<std::string> extensions_dependencies_strs;
	for (const auto& str : extensions)
	{
		const auto iter = extensionIndexLookupMap.find(str);
		if (iter != extensionIndexLookupMap.cend())
		{
			auto& dependencies = dependencyTable.at(iter->second);
			for (auto iter = dependencies.cbegin(); iter != dependencies.cend() && *iter != std::numeric_limits<size_t>::max(); ++iter)
			{
				extensions_dependencies_strs.emplace_back(masterExtensionNameTable[*iter]);
			}
		}
	}


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
