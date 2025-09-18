#pragma once
#ifndef RHI_SYSTEM_EXTENSION_PACK_HPP
#define RHI_SYSTEM_EXTENSION_PACK_HPP
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <string_view>
#include <memory>

class ExtensionWrangler;

namespace rhi 
{

    enum class ApiVersion : uint8_t 
    {
        None = 0,
        Vulkan10 = 1,
        Vulkan11 = 2,
        Vulkan12 = 3,
        Vulkan13 = 4,
        Vulkan14 = 5,
        Latest = 254
    };

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

        void SetPhysicalDevice(VkPhysicalDevice physicalDevice) noexcept;

        void AddRequiredDeviceExtensions(std::vector<std::string> extensions);
        void AddOptionalDeviceExtensions(std::vector<std::string> extensions);
        void ResolveDeviceDependencies();

        // Get finalized extension lists (with dependencies resolved)
        const std::vector<const char*>& GetInstanceExtensions() const noexcept;
        const std::vector<const char*>& GetDeviceExtensions() const noexcept;
        // Get device features for enabled extensions
        const VkPhysicalDeviceFeatures2* GetDeviceFeatures() const noexcept;
        ApiVersion GetApiVersion() const noexcept;
        uint32_t GetVulkanApiVersion() const noexcept;

    private:
        
        ApiVersion apiVersion;
        
        // Input extension lists
        std::vector<std::string> requiredInstanceExts;
        std::vector<std::string> optionalInstanceExts;
        std::vector<std::string> requiredDeviceExts;
        std::vector<std::string> optionalDeviceExts;
        
        // Finalized extension lists (c_str pointers into above vectors)
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
