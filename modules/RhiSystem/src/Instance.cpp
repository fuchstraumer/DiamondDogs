#include "Instance.hpp"
#include "ExtensionPack.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <array>

namespace rhi 
{

// Validation layer names
static constexpr std::array<const char*, 3> BASE_VALIDATION_LAYERS = 
{
    "VK_LAYER_KHRONOS_validation"
};

static constexpr std::array<const char*, 1> SYNCHRONIZATION_LAYERS = 
{
    "VK_LAYER_KHRONOS_synchronization2"
};

Instance::Instance(const std::string& app_name,
                   const std::string& engine_name,
                   uint32_t app_version,
                   uint32_t engine_version,
                   ValidationLayers validation_level,
                   const ExtensionPack& extensions) :
    handle{ VK_NULL_HANDLE },
    debugMessenger{ VK_NULL_HANDLE },
    validationEnabled{ validation_level != ValidationLayers::None }
{
    const VkApplicationInfo app_info = 
    {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        app_name.c_str(),
        app_version,
        engine_name.c_str(),
        engine_version,
        extensions.GetVulkanApiVersion()
    };
    
    setupValidation(validation_level);
    createInstance(app_info, extensions);
    
    if (validationEnabled)
    {
        setupDebugMessenger();
    }
}

Instance::~Instance()
{
    if (debugMessenger != VK_NULL_HANDLE)
    {
        auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(handle, "vkDestroyDebugUtilsMessengerEXT"));
        if (vkDestroyDebugUtilsMessengerEXT != nullptr)
        {
            vkDestroyDebugUtilsMessengerEXT(handle, debugMessenger, nullptr);
        }
    }
    
    if (handle != VK_NULL_HANDLE)
    {
        vkDestroyInstance(handle, nullptr);
    }
}

bool Instance::HasExtension(std::string_view extension_name) const noexcept
{
    return std::find(enabledExtensions.begin(), enabledExtensions.end(), extension_name) != enabledExtensions.end();
}

void Instance::createInstance(const VkApplicationInfo& app_info, const ExtensionPack& extensions)
{
    // Store enabled extensions
    const std::vector<const char*>& ext_names = extensions.GetInstanceExtensions();
    enabledExtensions.reserve(ext_names.size());
    for (const char* name : ext_names)
    {
        enabledExtensions.emplace_back(name);
    }
    
    // Convert layer names to const char*
    std::vector<const char*> layer_names;
    layer_names.reserve(enabledLayers.size());
    for (const std::string& layer : enabledLayers)
    {
        layer_names.push_back(layer.c_str());
    }
    
    // Create instance
    VkInstanceCreateInfo create_info = 
    {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &app_info,
        static_cast<uint32_t>(layer_names.size()),
        layer_names.data(),
        static_cast<uint32_t>(ext_names.size()),
        ext_names.data()
    };
    
    VkResult result = vkCreateInstance(&create_info, nullptr, &handle);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void Instance::setupValidation(ValidationLayers level)
{
    if (level == ValidationLayers::None)
    {
        return;
    }
    
    std::vector<const char*> required_layers;
    
    // Add base validation layers
    if (level >= ValidationLayers::BaseOnly)
    {
        for (const char* layer : BASE_VALIDATION_LAYERS)
        {
            required_layers.push_back(layer);
        }
    }
    
    // Add synchronization layers
    if (level >= ValidationLayers::WithSynchronization)
    {
        for (const char* layer : SYNCHRONIZATION_LAYERS)
        {
            required_layers.push_back(layer);
        }
    }
    
    // Check layer support
    if (!checkLayerSupport(required_layers))
    {
        throw std::runtime_error("Required validation layers not available");
    }
    
    // Store enabled layers
    enabledLayers.reserve(required_layers.size());
    for (const char* layer : required_layers)
    {
        enabledLayers.emplace_back(layer);
    }
}

bool Instance::checkLayerSupport(const std::vector<const char*>& required_layers) const
{
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
    
    for (const char* layer_name : required_layers)
    {
        bool found = false;
        for (const VkLayerProperties& layer_props : available_layers)
        {
            if (std::strcmp(layer_name, layer_props.layerName) == 0)
            {
                found = true;
                break;
            }
        }
        
        if (!found)
        {
            std::cerr << "Validation layer not available: " << layer_name << std::endl;
            return false;
        }
    }
    
    return true;
}

void Instance::setupDebugMessenger()
{
    auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(handle, "vkCreateDebugUtilsMessengerEXT"));
    
    if (vkCreateDebugUtilsMessengerEXT == nullptr)
    {
        std::cerr << "Debug utils messenger extension not available" << std::endl;
        return;
    }
    
    VkDebugUtilsMessengerCreateInfoEXT create_info = 
    {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        nullptr,
        0,
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        debugCallback,
        nullptr
    };
    
    VkResult result = vkCreateDebugUtilsMessengerEXT(handle, &create_info, nullptr, &debugMessenger);
    if (result != VK_SUCCESS)
    {
        std::cerr << "Failed to create debug messenger" << std::endl;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL Instance::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagBitsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    std::stringstream message;
    
    // Add severity
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        message << "[ERROR] ";
    }
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        message << "[WARNING] ";
    }
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        message << "[INFO] ";
    }
    else
    {
        message << "[VERBOSE] ";
    }
    
    // Add type
    if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
    {
        message << "[GENERAL] ";
    }
    else if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
    {
        message << "[VALIDATION] ";
    }
    else if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
    {
        message << "[PERFORMANCE] ";
    }
    
    // Add message ID
    if (callback_data->messageIdNumber != 0)
    {
        message << "[ID: " << callback_data->messageIdNumber << "] ";
    }
    
    message << callback_data->pMessage;
    
    // Output based on severity
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        std::cerr << message.str() << std::endl;
    }
    else
    {
        std::cout << message.str() << std::endl;
    }
    
    return VK_FALSE; // Don't abort execution
}

VkInstance Instance::GetHandle() const noexcept
{
    return handle;
}

bool Instance::HasValidation() const noexcept
{
    return validationEnabled;
}

const std::vector<std::string>& Instance::GetEnabledExtensions() const noexcept
{
    return enabledExtensions;
}

} // namespace rhi
