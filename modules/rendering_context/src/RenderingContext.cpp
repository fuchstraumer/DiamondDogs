#include "RenderingContext.hpp"
#include "PlatformWindow.hpp"
#include "nlohmann/json.hpp"
#include "Instance.hpp"
#include "PhysicalDevice.hpp"
#include "LogicalDevice.hpp"
#include "Swapchain.hpp"
#include "SurfaceKHR.hpp"
#include <vulkan/vulkan.h>
#include <fstream>
#include <atomic>

static std::vector<std::string> extensionsBuffer;
static std::string windowingModeBuffer;

static void SplitVersionString(std::string version_string, uint32_t& major_version, uint32_t& minor_version, uint32_t& patch_version) {
    const size_t minor_dot_pos = version_string.find('.');
    const size_t patch_dot_pos = version_string.rfind('.');
    if (patch_dot_pos == std::string::npos) {
        patch_version = 0;
        if (minor_dot_pos == std::string::npos) {
            minor_version = 0;
            major_version = static_cast<uint32_t>(strtod(version_string.c_str(), nullptr));
        }
        else {
            minor_version = static_cast<uint32_t>(strtod(version_string.substr(minor_dot_pos).c_str(), nullptr));
            major_version = static_cast<uint32_t>(strtod(version_string.substr(0, minor_dot_pos).c_str(), nullptr));
        }
    }
    else {
        if (minor_dot_pos == std::string::npos) {
            major_version = static_cast<uint32_t>(strtod(version_string.c_str(), nullptr));
            minor_version = 0;
            patch_version = 0;
            return;
        }
        else {
            major_version = static_cast<uint32_t>(strtod(version_string.substr(0, minor_dot_pos + 1).c_str(), nullptr));
            minor_version = static_cast<uint32_t>(strtod(version_string.substr(minor_dot_pos + 1, patch_dot_pos - minor_dot_pos - 1).c_str(), nullptr));
            patch_version = static_cast<uint32_t>(strtod(version_string.substr(patch_dot_pos).c_str(), nullptr));
        }
    }
}

static void GetVersions(const nlohmann::json& json_file, uint32_t& app_version, uint32_t& engine_version, uint32_t& api_version) {
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
        SplitVersionString(api_version_str, api_version_major, api_version_minor, api_version_patch);
        api_version = VK_MAKE_VERSION(api_version_major, api_version_minor, api_version_patch);
    }
}

static const std::unordered_map<std::string, windowing_mode> windowing_mode_str_to_flag{
    { "Windowed", windowing_mode::Windowed },
    { "BorderlessWindowed", windowing_mode::BorderlessWindowed },
    { "Fullscreen", windowing_mode::Fullscreen }
};

void createInstanceAndWindow(const nlohmann::json& json_file, std::unique_ptr<vpr::Instance>* instance, std::unique_ptr<PlatformWindow>* window, std::string& _window_mode) {

    int window_width = json_file.at("InitialWindowWidth");
    int window_height = json_file.at("InitialWindowHeight");
    const std::string app_name = json_file.at("ApplicationName");
    const std::string windowing_mode_str = json_file.at("InitialWindowMode");
    windowingModeBuffer = windowing_mode_str;
    _window_mode = windowingModeBuffer;
    auto iter = windowing_mode_str_to_flag.find(windowing_mode_str);
    windowing_mode window_mode = windowing_mode::Windowed;
    if (iter != std::cend(windowing_mode_str_to_flag)) {
        window_mode = iter->second;
    }

    *window = std::make_unique<PlatformWindow>(window_width, window_height, app_name.c_str(), window_mode);

    const std::string engine_name = json_file.at("EngineName");
    const bool using_validation = json_file.at("EnableValidation");

    uint32_t app_version = 0;
    uint32_t engine_version = 0;
    uint32_t api_version = 0;
    GetVersions(json_file, app_version, engine_version, api_version);

    std::vector<std::string> required_extensions_strs;
    {
        nlohmann::json req_ext_json = json_file.at("RequiredInstanceExtensions");
        for (auto& entry : req_ext_json) {
            required_extensions_strs.emplace_back(entry);
        }
    }

    std::vector<std::string> requested_extensions_strs;
    {
        nlohmann::json ext_json = json_file.at("RequestedInstanceExtensions");
        for (auto& entry : ext_json) {
            requested_extensions_strs.emplace_back(entry);
        }
    }

    std::vector<const char*> required_extensions;
    for (auto& str : required_extensions_strs) {
        required_extensions.emplace_back(str.c_str());
    }

    std::vector<const char*> requested_extensions;
    for (auto& str : requested_extensions_strs) {
        requested_extensions.emplace_back(str.c_str());
    }

    vpr::VprExtensionPack pack;
    pack.RequiredExtensionCount = static_cast<uint32_t>(required_extensions.size());
    pack.RequiredExtensionNames = required_extensions.data();
    pack.OptionalExtensionCount = static_cast<uint32_t>(requested_extensions.size());
    pack.OptionalExtensionNames = requested_extensions.data();

    const VkApplicationInfo application_info{
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        app_name.c_str(),
        app_version,
        engine_name.c_str(),
        engine_version,
        api_version
    };

    auto layers = using_validation ? vpr::Instance::instance_layers::Full : vpr::Instance::instance_layers::Disabled;
    *instance = std::make_unique<vpr::Instance>(layers, &application_info, &pack);

}

void createLogicalDevice(const nlohmann::json& json_file, VkSurfaceKHR surface, std::unique_ptr<vpr::Device>* device, vpr::Instance* instance, vpr::PhysicalDevice* physical_device) {
    std::vector<std::string> required_extensions_strs;
    {
        nlohmann::json req_ext_json = json_file.at("RequiredDeviceExtensions");
        for (auto& entry : req_ext_json) {
            required_extensions_strs.emplace_back(entry);
        }
    }

    std::vector<std::string> requested_extensions_strs;
    {
        nlohmann::json ext_json = json_file.at("RequestedDeviceExtensions");
        for (auto& entry : ext_json) {
            requested_extensions_strs.emplace_back(entry);
        }
    }

    std::vector<const char*> required_extensions;
    for (auto& str : required_extensions_strs) {
        required_extensions.emplace_back(str.c_str());
    }

    std::vector<const char*> requested_extensions;
    for (auto& str : requested_extensions_strs) {
        requested_extensions.emplace_back(str.c_str());
    }

    vpr::VprExtensionPack pack;
    pack.RequiredExtensionCount = static_cast<uint32_t>(required_extensions.size());
    pack.RequiredExtensionNames = required_extensions.data();
    pack.OptionalExtensionCount = static_cast<uint32_t>(requested_extensions.size());
    pack.OptionalExtensionNames = requested_extensions.data();

    *device = std::make_unique<vpr::Device>(instance, physical_device, surface, &pack, nullptr, 0);

}

static std::atomic<bool>& GetShouldResizeFlag() {
    static std::atomic<bool> should_resize{ false };
    return should_resize;
}

void RenderingContext::SetShouldResize(bool resize) {
    auto& flag = GetShouldResizeFlag();
    flag = resize;
}

bool RenderingContext::ShouldResizeExchange(bool value) {
    return GetShouldResizeFlag().exchange(value);
}

void RenderingContext::Construct(const char* file_path) {
    
    std::ifstream input_file(file_path);

    if (!input_file.is_open()) {
        /*
        // Try to use app_context_api to find the file.
        char* found_path = nullptr;
        if (!app_context_api->FindFile(file_path, nullptr, 5, &found_path)) {
            if (found_path) {
                free(found_path);
            }
            throw std::runtime_error("Couldn't open given path initially for Renderer context config, and search did not find it either!");
        }
        input_file = std::ifstream(found_path);
        free(found_path);
        if (!input_file.is_open()) {
            throw std::runtime_error("File search found Renderer context config JSON file, but opening of file still failed!");
        }
        */
        throw std::runtime_error("Couldn't open input file.");
    }

    nlohmann::json json_file;
    input_file >> json_file;

    createInstanceAndWindow(json_file, &vulkanInstance, &window, windowMode);
    // Physical devices to be redone for multi-GPU support if device group extension is supported.
    physicalDevices.emplace_back(std::make_unique<vpr::PhysicalDevice>(vulkanInstance->vkHandle()));

    {
        size_t num_instance_extensions = 0;
        vulkanInstance->GetEnabledExtensions(&num_instance_extensions, nullptr);
        if (num_instance_extensions != 0) {
            std::vector<char*> extensions_buffer(num_instance_extensions);
            vulkanInstance->GetEnabledExtensions(&num_instance_extensions, extensions_buffer.data());
            for (auto& str : extensions_buffer) {
                instanceExtensions.emplace_back(str);
                free(str);
            }
        }
    }

    windowSurface = std::make_unique<vpr::SurfaceKHR>(vulkanInstance.get(), physicalDevices[0]->vkHandle(), window->glfwWindow());

    createLogicalDevice(json_file, windowSurface->vkHandle(), &logicalDevice, vulkanInstance.get(), physicalDevices[0].get());

    {
        size_t num_device_extensions = 0;
        logicalDevice->GetEnabledExtensions(&num_device_extensions, nullptr);
        if (num_device_extensions != 0) {
            std::vector<char*> extensions_buffer(num_device_extensions);
            logicalDevice->GetEnabledExtensions(&num_device_extensions, extensions_buffer.data());
            for (auto& str : extensions_buffer) {
                deviceExtensions.emplace_back(str);
                free(str);
            }
        }
    }

    static const std::unordered_map<std::string, uint32_t> present_mode_from_str_map{
        { "None", vpr::vertical_sync_mode::None },
        { "VerticalSync", vpr::vertical_sync_mode::VerticalSync },
        { "VerticalSyncRelaxed", vpr::vertical_sync_mode::VerticalSyncRelaxed },
        { "VerticalSyncMailbox", vpr::vertical_sync_mode::VerticalSyncMailbox }
    };

    auto iter = json_file.find("VerticalSyncMode");
    // We want to go for this, as it's the ideal mode usually.
    uint32_t desired_mode = vpr::vertical_sync_mode::VerticalSyncMailbox;
    if (iter != json_file.end()) {
        auto present_mode_iter = present_mode_from_str_map.find(json_file.at("VerticalSyncMode"));
        if (present_mode_iter != std::cend(present_mode_from_str_map)) {
            desired_mode = present_mode_iter->second;
        }
    }

    swapchain = std::make_unique<vpr::Swapchain>(logicalDevice.get(), window->glfwWindow(), windowSurface->vkHandle(), desired_mode);
}
