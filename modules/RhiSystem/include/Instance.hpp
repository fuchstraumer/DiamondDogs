#pragma once
#ifndef RHI_SYSTEM_INSTANCE_HPP
#define RHI_SYSTEM_INSTANCE_HPP
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>

namespace rhi 
{

    class ExtensionPack;

    enum class ValidationLayers : uint8_t 
    {
        None = 0,
        BaseOnly = 1,               // Basic validation
        WithSynchronization = 2,    // Base + synchronization validation
        Full = 3                    // All available layers
    };

    class Instance 
    {
    public:
        Instance(const std::string& app_name,
                const std::string& engine_name,
                uint32_t app_version,
                uint32_t engine_version,
                ValidationLayers validation_level,
                const ExtensionPack& extensions);
        
        ~Instance();
        
        // No copy/move
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        
        // Core access
        VkInstance vkHandle() const noexcept;
        
        bool HasValidation() const noexcept;
        
        // Extension queries
        bool HasExtension(std::string_view extension_name) const noexcept;
        const std::vector<std::string>& GetEnabledExtensions() const noexcept;

    private:
        void createInstance(const VkApplicationInfo& app_info, const ExtensionPack& extensions);
        void setupValidation(ValidationLayers level);
        bool checkLayerSupport(const std::vector<const char*>& required_layers) const;
        
        VkInstance handle;
        bool validationEnabled;
        std::vector<std::string> enabledExtensions;
        std::vector<std::string> enabledLayers;
    };

} // namespace rhi

#endif // RHI_SYSTEM_INSTANCE_HPP
