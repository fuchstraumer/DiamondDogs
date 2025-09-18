#include "Instance.hpp"
#include "ExtensionPack.hpp"
#include <iostream>
#include <array>
#include <cassert>

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
    }

    Instance::~Instance()
    {
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
        const std::vector<const char*>& enabledInstanceExtensions = extensions.GetInstanceExtensions();
        
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
            static_cast<uint32_t>(enabledInstanceExtensions.size()),
            enabledInstanceExtensions.data()
        };
        
        VkResult result = vkCreateInstance(&create_info, nullptr, &handle);
        assert(result == VK_SUCCESS);
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
        assert(checkLayerSupport(required_layers));
        
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

    VkInstance Instance::vkHandle() const noexcept
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
