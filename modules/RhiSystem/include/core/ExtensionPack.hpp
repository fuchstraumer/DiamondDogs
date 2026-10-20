#pragma once
#ifndef RHI_SYSTEM_EXTENSION_PACK_HPP
#define RHI_SYSTEM_EXTENSION_PACK_HPP
#include "RhiFlags.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

class ExtensionWrangler;

namespace rhi 
{

    // Simplified extension management - combines ExtensionWrangler functionality
    class ExtensionPack 
    {
    public:
        ExtensionPack(ApiVersion preferred_version = ApiVersion::Vulkan13) noexcept;
        ~ExtensionPack();
        
        // No copy, move only
        ExtensionPack(const ExtensionPack&) = delete;
        ExtensionPack& operator=(const ExtensionPack&) = delete;
        ExtensionPack(ExtensionPack&&) noexcept;
        ExtensionPack& operator=(ExtensionPack&&) noexcept;

        // Add extensions (automatically resolves dependencies using internal ExtensionWrangler)
        // Using pass-by-value because we'll be moving these into internal storage, since we'll be done with them at the callsite by this point
        void AddRequiredInstanceExtensions(std::vector<std::string> extensions);
        void AddOptionalInstanceExtensions(std::vector<std::string> extensions);
        void ResolveInstanceDependencies();

        void SetPhysicalDevice(VkPhysicalDevice physical_device) noexcept;

        void AddRequiredDeviceExtensions(std::vector<std::string> extensions);
        void AddOptionalDeviceExtensions(std::vector<std::string> extensions);
        /** @brief Queries the extension wrangler for all VK_KHR_maintenance[N] extensions supported by
         * the current physical device that are not already promoted to core, then adds them as optional
         * device extensions. Must be called after SetPhysicalDevice and before ResolveDeviceDependencies.
         */
        void AddAllSupportedMaintenanceExtensions();
        void ResolveDeviceDependencies();

        // Get finalized extension lists (with dependencies resolved)
        [[nodiscard]] const std::vector<const char*>& GetInstanceExtensions() const noexcept;
        [[nodiscard]] const std::vector<const char*>& GetDeviceExtensions() const noexcept;
        /** @brief Returns the requested device extensions that are core in the active API version, which are
         *  therefore not enabled by name. */
        [[nodiscard]] const std::vector<std::string>& GetCoreDeviceExtensions() const noexcept;
        // Get device features for enabled extensions
        [[nodiscard]] const VkPhysicalDeviceFeatures2* GetDeviceFeatures() const noexcept;
        [[nodiscard]] ApiVersion GetApiVersion() const noexcept;
        [[nodiscard]] uint32_t GetVulkanApiVersion() const noexcept;

    private:

        [[nodiscard]] bool maintenanceDependenciesSatisfied(std::string_view extension_name) const;

        ApiVersion apiVersion;
        
        // Input extension lists
        std::vector<std::string> requiredInstanceExts;
        std::vector<std::string> optionalInstanceExts;
        std::vector<std::string> requiredDeviceExts;
        std::vector<std::string> optionalDeviceExts;
        
        // Finalized extension names: requested extensions that survived support filtering, plus dependencies
        std::vector<std::string> enabledInstanceExtNames;
        std::vector<std::string> enabledDeviceExtNames;
        std::vector<std::string> coreDeviceExtNames;

        // c_str pointers into the enabled name vectors above, in the form Vulkan create infos take
        std::vector<const char*> instanceExtensions;
        std::vector<const char*> deviceExtensions;
        
        // Device features for enabled extensions
        VkPhysicalDeviceFeatures2 deviceFeatures;
        
        // Extension wrangler for dependency resolution
        std::unique_ptr<ExtensionWrangler> extensionWrangler;
        bool instanceExtensionsResolved{ false };
        bool deviceExtensionsResolved{ false };
    };

} // namespace rhi

#endif // RHI_SYSTEM_EXTENSION_PACK_HPP
