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
    BestSupported = 255
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
    void AddRequiredInstanceExtensions(const std::vector<std::string_view>& extensions);
    void AddOptionalInstanceExtensions(const std::vector<std::string_view>& extensions);
    void AddRequiredDeviceExtensions(const std::vector<std::string_view>& extensions);
    void AddOptionalDeviceExtensions(const std::vector<std::string_view>& extensions);

    // Get finalized extension lists (with dependencies resolved)
    const std::vector<const char*>& GetInstanceExtensions() const noexcept;
    
    const std::vector<const char*>& GetDeviceExtensions() const noexcept;
    
    // Get device features for enabled extensions
    const VkPhysicalDeviceFeatures2* GetDeviceFeatures() const noexcept;
    
    ApiVersion GetApiVersion() const noexcept;
    
    uint32_t GetVulkanApiVersion() const noexcept;

    // Called internally when physical device is available
    void ResolveDeviceExtensions(VkPhysicalDevice physical_device);

private:
    void resolveDependencies();
    void resolveInstanceDependencies();
    void resolveDeviceDependencies();
    
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
    bool deviceExtensionsResolved;
};

} // namespace rhi

#endif // RHI_SYSTEM_EXTENSION_PACK_HPP