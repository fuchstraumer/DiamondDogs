#include "ExtensionPack.hpp"
#include "ExtensionWrangler.hpp"
#include <stdexcept>
#include <unordered_set>
#include <algorithm>

namespace rhi 
{

    ExtensionPack::ExtensionPack(ApiVersion preferred_version) noexcept :
        apiVersion{ preferred_version },
        deviceFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr },
        extensionWrangler{ nullptr },
        deviceExtensionsResolved{ false }
    {
    }

    ExtensionPack::~ExtensionPack() = default;

    ExtensionPack::ExtensionPack(ExtensionPack&& other) noexcept :
        apiVersion{ other.apiVersion },
        requiredInstanceExts{ std::move(other.requiredInstanceExts) },
        optionalInstanceExts{ std::move(other.optionalInstanceExts) },
        requiredDeviceExts{ std::move(other.requiredDeviceExts) },
        optionalDeviceExts{ std::move(other.optionalDeviceExts) },
        instanceExtensions{ std::move(other.instanceExtensions) },
        deviceExtensions{ std::move(other.deviceExtensions) },
        deviceFeatures{ other.deviceFeatures },
        extensionWrangler{ std::move(other.extensionWrangler) },
        deviceExtensionsResolved{ other.deviceExtensionsResolved }
    {
        other.deviceFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr };
        other.deviceExtensionsResolved = false;
    }

    ExtensionPack& ExtensionPack::operator=(ExtensionPack&& other) noexcept
    {
        if (this != &other)
        {
            apiVersion = other.apiVersion;
            requiredInstanceExts = std::move(other.requiredInstanceExts);
            optionalInstanceExts = std::move(other.optionalInstanceExts);
            requiredDeviceExts = std::move(other.requiredDeviceExts);
            optionalDeviceExts = std::move(other.optionalDeviceExts);
            instanceExtensions = std::move(other.instanceExtensions);
            deviceExtensions = std::move(other.deviceExtensions);
            deviceFeatures = other.deviceFeatures;
            extensionWrangler = std::move(other.extensionWrangler);
            deviceExtensionsResolved = other.deviceExtensionsResolved;
            
            other.deviceFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr };
            other.deviceExtensionsResolved = false;
        }
        return *this;
    }

    uint32_t ExtensionPack::GetVulkanApiVersion() const noexcept
    {
        switch (apiVersion)
        {
            case ApiVersion::Vulkan10:
                return VK_API_VERSION_1_0;
            case ApiVersion::Vulkan11:
                return VK_API_VERSION_1_1;
            case ApiVersion::Vulkan12:
                return VK_API_VERSION_1_2;
            case ApiVersion::Vulkan13:
                return VK_API_VERSION_1_3;
            case ApiVersion::Vulkan14:
                [[fallthrough]];
            case ApiVersion::Latest:
                [[fallthrough]];
            default:
                return VK_API_VERSION_1_4;
        }
    }

    void ExtensionPack::AddRequiredInstanceExtensions(std::vector<std::string> extensions)
    {
        requiredInstanceExts = std::move(extensions);
    }

    void ExtensionPack::AddOptionalInstanceExtensions(std::vector<std::string> extensions)
    {
        optionalInstanceExts = std::move(extensions);
    }

    void ExtensionPack::ResolveInstanceDependencies()
    {
        if (!extensionWrangler)
        {
            // Create extension wrangler without physical device for instance extensions
            extensionWrangler = std::make_unique<ExtensionWrangler>(GetVulkanApiVersion(), VK_NULL_HANDLE);
        }
        
        // Combine required and optional extensions
        std::vector<std::string_view> allInstanceExts;
        allInstanceExts.reserve(requiredInstanceExts.size() + optionalInstanceExts.size());
        
        for (const std::string& ext : requiredInstanceExts)
        {
            allInstanceExts.emplace_back(ext);
        }
        
        for (const std::string& ext : optionalInstanceExts)
        {
            allInstanceExts.emplace_back(ext);
        }
        
        if (allInstanceExts.empty())
        {
            return;
        }
        
        // Get dependencies
        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> deps = 
            extensionWrangler->GetExtensionDependencies(allInstanceExts.size(), allInstanceExts.data());
        
        // Build final extension list
        std::unordered_set<std::string> finalExtensions;
        
        // Add original extensions
        for (const std::string& ext : requiredInstanceExts)
        {
            finalExtensions.insert(ext);
        }
        
        for (const std::string& ext : optionalInstanceExts)
        {
            // Only add optional extensions if they're supported
            if (extensionWrangler->IsExtensionSupported(ext))
            {
                finalExtensions.insert(ext);
            }
        }
        
        // Add dependencies
        if (deps.has_value())
        {
            for (uint32_t i = 0; i < deps->numInstanceExtensionDeps; ++i)
            {
                finalExtensions.insert(deps->instanceExtensionDeps[i]);
            }
        }
        
        // Convert to c_str array
        instanceExtensions.clear();
        instanceExtensions.reserve(finalExtensions.size());
        
        for (const std::string& ext : finalExtensions)
        {
            // Find the extension in our stored strings to get stable pointer
            auto findRequired = std::find(requiredInstanceExts.begin(), requiredInstanceExts.end(), ext);
            if (findRequired != requiredInstanceExts.end())
            {
                instanceExtensions.push_back(findRequired->c_str());
                continue;
            }
            
            auto findOptional = std::find(optionalInstanceExts.begin(), optionalInstanceExts.end(), ext);
            if (findOptional != optionalInstanceExts.end())
            {
                instanceExtensions.push_back(findOptional->c_str());
                continue;
            }
            
            // This is a dependency - add it to required list for stable storage
            requiredInstanceExts.push_back(ext);
            instanceExtensions.push_back(requiredInstanceExts.back().c_str());
        }

        instanceExtensionsResolved = true;
    }

    void ExtensionPack::SetPhysicalDevice(VkPhysicalDevice physicalDevice) noexcept
    {
        // if already resolved or physical device already set, don't need to do this step
        if (deviceExtensionsResolved || (extensionWrangler && extensionWrangler->HasValidPhysicalDevice()))
        {
            return;
        }
        
        extensionWrangler->SetPhysicalDevice(physicalDevice);
    }

    void ExtensionPack::AddRequiredDeviceExtensions(std::vector<std::string> extensions)
    {
        requiredDeviceExts = std::move(extensions);
    }

    void ExtensionPack::AddOptionalDeviceExtensions(std::vector<std::string> extensions)
    {
        optionalDeviceExts = std::move(extensions);
    }

    void ExtensionPack::ResolveDeviceDependencies()
    {
        if (!extensionWrangler)
        {
            return;
        }
        
        // Combine required and optional device extensions
        std::vector<std::string_view> allDeviceExts;
        allDeviceExts.reserve(requiredDeviceExts.size() + optionalDeviceExts.size());
        
        for (const std::string& ext : requiredDeviceExts)
        {
            allDeviceExts.emplace_back(ext);
        }
        
        for (const std::string& ext : optionalDeviceExts)
        {
            allDeviceExts.emplace_back(ext);
        }
        
        if (allDeviceExts.empty())
        {
            return;
        }
        
        // Get dependencies
        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> deps = 
            extensionWrangler->GetExtensionDependencies(allDeviceExts.size(), allDeviceExts.data());
        
        // Build final extension list
        std::unordered_set<std::string> finalExtensions;
        
        // Add original extensions (filtering optional by support)
        for (const std::string& ext : requiredDeviceExts)
        {
            finalExtensions.insert(ext);
        }
        
        for (const std::string& ext : optionalDeviceExts)
        {
            if (extensionWrangler->IsExtensionSupported(ext))
            {
                finalExtensions.insert(ext);
            }
        }
        
        // Add dependencies
        if (deps.has_value())
        {
            for (uint32_t i = 0; i < deps->numDeviceExtensionDeps; ++i)
            {
                finalExtensions.insert(deps->deviceExtensionDeps[i]);
            }
        }
        
        // Convert to c_str array
        deviceExtensions.clear();
        deviceExtensions.reserve(finalExtensions.size());
        
        for (const std::string& ext : finalExtensions)
        {
            // Find the extension in our stored strings to get stable pointer
            auto findRequired = std::find(requiredDeviceExts.begin(), requiredDeviceExts.end(), ext);
            if (findRequired != requiredDeviceExts.end())
            {
                deviceExtensions.push_back(findRequired->c_str());
                continue;
            }
            
            auto findOptional = std::find(optionalDeviceExts.begin(), optionalDeviceExts.end(), ext);
            if (findOptional != optionalDeviceExts.end())
            {
                deviceExtensions.push_back(findOptional->c_str());
                continue;
            }
            
            // This is a dependency - add it to required list for stable storage
            requiredDeviceExts.push_back(ext);
            deviceExtensions.push_back(requiredDeviceExts.back().c_str());
        }
        
        // Get device features for enabled extensions
        std::expected<VkPhysicalDeviceFeatures2, ExtensionWrangler::DependencyError> features = 
            extensionWrangler->GetExtensionFeatures(allDeviceExts.size(), allDeviceExts.data(), 
                                                ExtensionWrangler::GetVersionFeatures::True,
                                                ExtensionWrangler::CollectDependencies::False);
        
        if (features.has_value())
        {
            deviceFeatures = features.value();
        }

        deviceExtensionsResolved = true;
    }

    const std::vector<const char*>& ExtensionPack::GetInstanceExtensions() const noexcept
    {
        return instanceExtensions;
    }

    const std::vector<const char*>& ExtensionPack::GetDeviceExtensions() const noexcept
    {
        return deviceExtensions;
    }

    const VkPhysicalDeviceFeatures2* ExtensionPack::GetDeviceFeatures() const noexcept
    {
        return &deviceFeatures;
    }

    ApiVersion ExtensionPack::GetApiVersion() const noexcept
    {
        return apiVersion;
    }

} // namespace rhi