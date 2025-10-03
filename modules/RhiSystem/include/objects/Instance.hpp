#pragma once
#ifndef RHI_SYSTEM_INSTANCE_HPP
#define RHI_SYSTEM_INSTANCE_HPP
#include "RhiTypes.hpp"
#include "RhiHandle.hpp"
#include <memory>
#include <string>
#include <vector>

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
                const ExtensionPack& extensions);
        
        ~Instance();
        
        // No copy/move
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        
        // Core access
        InstanceHandle Handle() const noexcept;
        
        bool HasValidation() const noexcept;
        ValidationLayers GetValidationLevel() const noexcept;
        
        // Extension queries
        bool HasExtension(std::string_view extension_name) const noexcept;
        const std::vector<std::string>& GetEnabledExtensions() const noexcept;

    private:
        void createInstance(const VkApplicationInfo& app_info, const ExtensionPack& extensions);
        void setupValidation();
        bool checkLayerSupport(const std::vector<const char*>& required_layers) const;

        InstanceHandle handle;
        ValidationLayers validationLevel;
        std::vector<std::string> enabledExtensions;
        std::vector<std::string> enabledLayers;
    };

} // namespace rhi

#endif // RHI_SYSTEM_INSTANCE_HPP
