#pragma once
#ifndef RHI_SYSTEM_INSTANCE_HPP
#define RHI_SYSTEM_INSTANCE_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

struct VkApplicationInfo;

namespace rhi 
{

    class ExtensionPack;

    class Instance 
    {
    public:
        Instance(const std::string& app_name,
                const std::string& engine_name,
                uint32_t app_version,
                uint32_t engine_version,
                ValidationLayers validation_level,
                const ExtensionPack& extensions,
                const VkDebugUtilsMessengerCreateInfoEXT* debug_messenger_info = nullptr);
        
        ~Instance();
        
        // No copy/move
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        Instance(Instance&&) = delete;
        Instance& operator=(Instance&&) = delete;
        
        // Core access
        [[nodiscard]] InstanceHandle Handle() const noexcept;
        
        [[nodiscard]] bool HasValidation() const noexcept;
        [[nodiscard]] ValidationLayers GetValidationLevel() const noexcept;
        
        // Extension queries
        [[nodiscard]] bool HasExtension(std::string_view extension_name) const noexcept;
        [[nodiscard]] const std::vector<std::string>& GetEnabledExtensions() const noexcept;

    private:
        void createInstance(const VkApplicationInfo& app_info, const ExtensionPack& extensions, const VkDebugUtilsMessengerCreateInfoEXT* debug_messenger_info);
        void setupValidation();
        [[nodiscard]] bool checkLayerSupport(const std::vector<const char*>& required_layers) const;
        bool validationLayerHasExtension(const char* extension_name) const;

        InstanceHandle handle;
        ValidationLayers validationLevel;
        std::vector<std::string> enabledExtensions;
        std::vector<std::string> enabledLayers;
    };

} // namespace rhi

#endif // RHI_SYSTEM_INSTANCE_HPP
