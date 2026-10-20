#include "Instance.hpp"
#include "ExtensionPack.hpp"
#include "RhiAssert.hpp"
#include "RhiResult.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <array>
#include "vulkan/vulkan_core.h"

namespace rhi 
{

#ifdef RHI_SYSTEM_USE_VULKAN
    // Validation layer names
    static constexpr std::array<const char*, 1> BASE_VALIDATION_LAYERS =
    {
        "VK_LAYER_KHRONOS_validation"
    };

    // synchronization validation is a setting of the validation layer. this used to load
    // VK_LAYER_KHRONOS_synchronization2, which only emulates sync2 on drivers without it and validates nothing
    static constexpr const char* SYNCHRONIZATION_VALIDATION_SETTING = "validate_sync";

    Instance::Instance(const std::string& app_name,
                    const std::string& engine_name,
                    uint32_t app_version,
                    uint32_t engine_version,
                    ValidationLayers validation_level,
                    const ExtensionPack& extensions) :
        handle{ 0u },
        validationLevel{ validation_level }
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
        
        setupValidation();
        createInstance(app_info, extensions);
    }

    Instance::~Instance()
    {
        if (handle.IsValid())
        {
            vkDestroyInstance(handle.As<VkInstance>(), nullptr);
        }
    }

    bool Instance::HasExtension(std::string_view extension_name) const noexcept
    {
        return std::find(enabledExtensions.begin(), enabledExtensions.end(), extension_name) != enabledExtensions.end();
    }

    void Instance::createInstance(const VkApplicationInfo& app_info, const ExtensionPack& extensions)
    {
        std::vector<const char*> enabledInstanceExtensions = extensions.GetInstanceExtensions();

        const VkBool32 settingEnabled = VK_TRUE;
        const VkLayerSettingEXT syncValidationSetting
        {
            BASE_VALIDATION_LAYERS[0],
            SYNCHRONIZATION_VALIDATION_SETTING,
            VK_LAYER_SETTING_TYPE_BOOL32_EXT,
            1u,
            &settingEnabled
        };
        const VkLayerSettingsCreateInfoEXT layerSettings
        {
            VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            nullptr,
            1u,
            &syncValidationSetting
        };
        const bool enableSyncValidation = validationLevel >= ValidationLayers::WithSynchronization &&
            validationLayerHasExtension(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        if (enableSyncValidation)
        {
            enabledInstanceExtensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        }

        // Store enabled extensions
        for (const char* extension : enabledInstanceExtensions)
        {
            enabledExtensions.emplace_back(extension);
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
            enableSyncValidation ? &layerSettings : nullptr,
            0,
            &app_info,
            static_cast<uint32_t>(layer_names.size()),
            layer_names.data(),
            static_cast<uint32_t>(enabledInstanceExtensions.size()),
            enabledInstanceExtensions.data()
        };
        
        VkInstance resultInstance = VK_NULL_HANDLE;
        rhi::Result result = vkCreateInstance(&create_info, nullptr, &resultInstance);
        RhiAssert(result);
        handle.Set<VkInstance>(resultInstance);
    }

    void Instance::setupValidation()
    {
        if (validationLevel == ValidationLayers::None)
        {
            return;
        }
        
        std::vector<const char*> required_layers;
        
        // Add base validation layers
        if (validationLevel >= ValidationLayers::BaseOnly)
        {
            for (const char* layer : BASE_VALIDATION_LAYERS)
            {
                required_layers.push_back(layer);
            }
        }
        
        if (validationLevel >= ValidationLayers::CrashDiagnostic)
        {
            required_layers.push_back("VK_LAYER_LUNARG_crash_diagnostic");
            required_layers.push_back("VK_LAYER_KHRONOS_shader_object");
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

    bool Instance::validationLayerHasExtension(const char* extension_name) const
    {
        const char* layerName = BASE_VALIDATION_LAYERS[0];
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> layerExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, layerExtensions.data());

        const bool found = std::ranges::any_of(layerExtensions, [extension_name](const VkExtensionProperties& properties)
        {
            return std::strcmp(properties.extensionName, extension_name) == 0;
        });
        if (!found)
        {
            std::cerr << "The validation layer does not provide " << extension_name
                      << ", so synchronization validation stays off\n";
        }
        return found;
    }

    InstanceHandle Instance::Handle() const noexcept
    {
        return handle;
    }

    bool Instance::HasValidation() const noexcept
    {
        return validationLevel != ValidationLayers::None;
    }

    ValidationLayers Instance::GetValidationLevel() const noexcept
    {
        return validationLevel;
    }

    const std::vector<std::string>& Instance::GetEnabledExtensions() const noexcept
    {
        return enabledExtensions;
    }

#endif // RHI_SYSTEM_USE_VULKAN

} // namespace rhi
