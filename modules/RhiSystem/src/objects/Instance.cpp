#include "Instance.hpp"
#include "ExtensionPack.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include "vulkan/vulkan_core.h"

namespace rhi 
{

#ifdef RHI_SYSTEM_USE_VULKAN
    // Validation layer names
    static constexpr std::array<const char*, 1> k_BaseValidationLayers =
    {
        "VK_LAYER_KHRONOS_validation"
    };

    // synchronization validation is a setting of the validation layer. this used to load
    // VK_LAYER_KHRONOS_synchronization2, which only emulates sync2 on drivers without it and validates nothing
    static constexpr const char* k_SynchronizationValidationSetting = "validate_sync";

    Instance::Instance(const std::string& app_name,
                    const std::string& engine_name,
                    uint32_t app_version,
                    uint32_t engine_version,
                    ValidationLayers validation_level,
                    const ExtensionPack& extensions) :
        handle{ 0u },
        validationLevel{ validation_level }
    {
        const VkApplicationInfo appInfo = 
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
        createInstance(appInfo, extensions);
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
        return std::ranges::contains(enabledExtensions, extension_name);
    }

    void Instance::createInstance(const VkApplicationInfo& app_info, const ExtensionPack& extensions)
    {
        std::vector<const char*> enabledInstanceExtensions = extensions.GetInstanceExtensions();

        const VkBool32 settingEnabled = VK_TRUE;
        const VkLayerSettingEXT syncValidationSetting
        {
            k_BaseValidationLayers[0],
            k_SynchronizationValidationSetting,
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
        std::vector<const char*> layerNames;
        layerNames.reserve(enabledLayers.size());
        for (const std::string& layer : enabledLayers)
        {
            layerNames.push_back(layer.c_str());
        }

        // Create instance
        VkInstanceCreateInfo createInfo =
        {
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            enableSyncValidation ? &layerSettings : nullptr,
            0,
            &app_info,
            static_cast<uint32_t>(layerNames.size()),
            layerNames.data(),
            static_cast<uint32_t>(enabledInstanceExtensions.size()),
            enabledInstanceExtensions.data()
        };
        
        VkInstance resultInstance = VK_NULL_HANDLE;
        rhi::Result result = vkCreateInstance(&createInfo, nullptr, &resultInstance);
        RhiAssert(result);
        handle.Set<VkInstance>(resultInstance);
    }

    void Instance::setupValidation()
    {
        if (validationLevel == ValidationLayers::None)
        {
            return;
        }
        
        std::vector<const char*> requiredLayers;
        
        // Add base validation layers
        if (validationLevel >= ValidationLayers::BaseOnly)
        {
            for (const char* layer : k_BaseValidationLayers)
            {
                requiredLayers.push_back(layer);
            }
        }
        
        if (validationLevel >= ValidationLayers::CrashDiagnostic)
        {
            requiredLayers.push_back("VK_LAYER_LUNARG_crash_diagnostic");
            requiredLayers.push_back("VK_LAYER_KHRONOS_shader_object");
        }
        
        // Check layer support
        assert(checkLayerSupport(requiredLayers));
        
        // Store enabled layers
        enabledLayers.reserve(requiredLayers.size());
        for (const char* layer : requiredLayers)
        {
            enabledLayers.emplace_back(layer);
        }

    }

    bool Instance::checkLayerSupport(const std::vector<const char*>& required_layers) const
    {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        
        for (const char* layerName : required_layers)
        {
            bool found = false;
            for (const VkLayerProperties& layerProps : availableLayers)
            {
                if (std::strcmp(layerName, layerProps.layerName) == 0)
                {
                    found = true;
                    break;
                }
            }
            
            if (!found)
            {
                std::cerr << "Validation layer not available: " << layerName << std::endl;
                return false;
            }
        }
        
        return true;
    }

    bool Instance::validationLayerHasExtension(const char* extension_name) const
    {
        const char* layerName = k_BaseValidationLayers[0];
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
