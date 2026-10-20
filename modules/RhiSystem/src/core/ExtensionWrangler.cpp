#include "ExtensionWrangler.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <memory>
#include <utility>
#include <vector>
#include <cstring>
#include <set>
#include <span>
#include <string_view>
#include <ranges>

#ifndef VK_BASE_API_VERSION_1_0
#define VK_BASE_API_VERSION_1_0 VK_API_VERSION_1_0
#endif

#ifndef VK_COMPUTE_API_VERSION_1_0
#define VK_COMPUTE_API_VERSION_1_0 VK_API_VERSION_1_0
#endif

#ifndef VK_GRAPHICS_API_VERSION_1_0
#define VK_GRAPHICS_API_VERSION_1_0 VK_API_VERSION_1_0
#endif

#ifndef VK_BASE_API_VERSION_1_1
#define VK_BASE_API_VERSION_1_1 VK_API_VERSION_1_1
#endif

#ifndef VK_COMPUTE_API_VERSION_1_1
#define VK_COMPUTE_API_VERSION_1_1 VK_API_VERSION_1_1
#endif

#ifndef VK_GRAPHICS_API_VERSION_1_1
#define VK_GRAPHICS_API_VERSION_1_1 VK_API_VERSION_1_1
#endif

#ifndef VK_BASE_API_VERSION_1_2
#define VK_BASE_API_VERSION_1_2 VK_API_VERSION_1_2
#endif

#ifndef VK_COMPUTE_API_VERSION_1_2
#define VK_COMPUTE_API_VERSION_1_2 VK_API_VERSION_1_2
#endif

#ifndef VK_GRAPHICS_API_VERSION_1_2
#define VK_GRAPHICS_API_VERSION_1_2 VK_API_VERSION_1_2
#endif

#ifndef VK_BASE_API_VERSION_1_3
#define VK_BASE_API_VERSION_1_3 VK_API_VERSION_1_3
#endif

#ifndef VK_COMPUTE_API_VERSION_1_3
#define VK_COMPUTE_API_VERSION_1_3 VK_API_VERSION_1_3
#endif

#ifndef VK_GRAPHICS_API_VERSION_1_3
#define VK_GRAPHICS_API_VERSION_1_3 VK_API_VERSION_1_3
#endif

#ifndef VK_BASE_API_VERSION_1_4
#define VK_BASE_API_VERSION_1_4 VK_API_VERSION_1_4
#endif

#ifndef VK_COMPUTE_API_VERSION_1_4
#define VK_COMPUTE_API_VERSION_1_4 VK_API_VERSION_1_4
#endif

#ifndef VK_GRAPHICS_API_VERSION_1_4
#define VK_GRAPHICS_API_VERSION_1_4 VK_API_VERSION_1_4
#endif

#include "GeneratedExtensionHeader.hpp"

// ExtensionIsDeviceExtension and ExtensionIsInstanceExtension binary-search these, so an unsorted table from the
// generator must fail the build rather than misclassify extensions at runtime
static_assert(std::ranges::is_sorted(deviceExtensionTable), "deviceExtensionTable must be sorted for binary search");
static_assert(std::ranges::is_sorted(instanceExtensionTable), "instanceExtensionTable must be sorted for binary search");

namespace
{
    bool AreStringsEqual(const VkExtensionProperties& ext_props, const char* str);
    std::string_view ExtensionNameOf(const VkExtensionProperties& properties) noexcept;
    const char* StringViewData(std::string_view view) noexcept;
}

//NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static const std::set<uint32_t> k_BuiltInVkVersions
{
    VK_API_VERSION_1_0,
    VK_API_VERSION_1_1,
    VK_API_VERSION_1_2,
    VK_API_VERSION_1_3,
    VK_API_VERSION_1_4
};

static VkPhysicalDeviceVulkan11Features k_LocalVulkan11Features
{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    nullptr
};

static VkPhysicalDeviceVulkan12Features k_LocalVulkan12Features
{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    nullptr
};

static VkPhysicalDeviceVulkan13Features k_LocalVulkan13Features
{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    nullptr
};

static VkPhysicalDeviceVulkan14Features k_LocalVulkan14Features
{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
    nullptr
};

struct DependencyCache
{
    DependencyCache(VkPhysicalDevice _physicalDevice)
    {
        uint32_t numInstanceExtensions = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
        InstanceExtensionDependencies.resize(numInstanceExtensions);
        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, InstanceExtensionDependencies.data());

        if (_physicalDevice != VK_NULL_HANDLE)
        {
            uint32_t numDeviceExtensions = 0;
            vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numDeviceExtensions, nullptr);
            DeviceExtensionDependencies.resize(numDeviceExtensions);
            vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numDeviceExtensions, DeviceExtensionDependencies.data());
        }
    }
    std::vector<VkExtensionProperties> DeviceExtensionDependencies;
    std::vector<VkExtensionProperties> InstanceExtensionDependencies;
};

ExtensionDependencies::ExtensionDependencies(
    uint32_t num_instance_extension_deps,
    uint32_t num_device_extension_deps) :
    VersionDep{ 0 },
    NumInstanceExtensionDeps{ num_instance_extension_deps },
    InstanceExtensionDeps{ nullptr },
    NumDeviceExtensionDeps{ num_device_extension_deps },
    DeviceExtensionDeps{ nullptr }
{

    if (NumInstanceExtensionDeps > 0)
    {
        InstanceExtensionDeps = new const char*[NumInstanceExtensionDeps];
    }

    if (NumDeviceExtensionDeps > 0)
    {
        DeviceExtensionDeps = new const char*[NumDeviceExtensionDeps];
    }

}

ExtensionDependencies::~ExtensionDependencies()
{
    if (NumInstanceExtensionDeps > 0 && InstanceExtensionDeps != nullptr)
    {
        delete[] InstanceExtensionDeps;
    }
    if (NumDeviceExtensionDeps > 0 && DeviceExtensionDeps != nullptr)
    {
        delete[] DeviceExtensionDeps;
    }
}

ExtensionDependencies::ExtensionDependencies(ExtensionDependencies&& other) noexcept :
    VersionDep{ other.VersionDep },
    NumInstanceExtensionDeps{ other.NumInstanceExtensionDeps },
    InstanceExtensionDeps{ other.InstanceExtensionDeps },
    NumDeviceExtensionDeps{ other.NumDeviceExtensionDeps },
    DeviceExtensionDeps{ other.DeviceExtensionDeps }
{
    other.InstanceExtensionDeps = nullptr;
    other.DeviceExtensionDeps = nullptr;
    other.NumInstanceExtensionDeps = 0;
    other.NumDeviceExtensionDeps = 0;
}

ExtensionDependencies& ExtensionDependencies::operator=(ExtensionDependencies&& other) noexcept
{
    if (this != &other)
    {
        delete[] InstanceExtensionDeps;
        delete[] DeviceExtensionDeps;

        VersionDep = other.VersionDep;
        InstanceExtensionDeps = other.InstanceExtensionDeps;
        DeviceExtensionDeps = other.DeviceExtensionDeps;
        NumInstanceExtensionDeps = other.NumInstanceExtensionDeps;
        NumDeviceExtensionDeps = other.NumDeviceExtensionDeps;

        other.InstanceExtensionDeps = nullptr;
        other.DeviceExtensionDeps = nullptr;
        other.NumInstanceExtensionDeps = 0;
        other.NumDeviceExtensionDeps = 0;
    }
    return *this;
}

ExtensionWrangler::ExtensionWrangler(
    const uint32_t _apiVersion,
    VkPhysicalDevice _physicalDevice) :
    apiVersion(_apiVersion),
    physicalDevice(_physicalDevice),
    dependencyCache(std::make_unique<DependencyCache>(_physicalDevice))
{
    // Find the highest API version that is <= the input API version
    // upper_bound returns iterator to first element > apiVersion, so we want the element before that
    auto upperBoundIt = k_BuiltInVkVersions.upper_bound(apiVersion);
    
    // If upper_bound points to begin(), then apiVersion is less than all built-in versions, use the first one
    if (upperBoundIt == k_BuiltInVkVersions.begin())
    {
        apiVersion = *k_BuiltInVkVersions.begin();
    }
    else
    {
        // Move back one position to get the last version <= _apiVersion
        --upperBoundIt;
        apiVersion = *upperBoundIt;
    }

}

ExtensionWrangler::~ExtensionWrangler() noexcept
{
}

bool ExtensionWrangler::HasValidPhysicalDevice() const noexcept
{
    return physicalDevice != VK_NULL_HANDLE;
}

void ExtensionWrangler::SetPhysicalDevice(VkPhysicalDevice physical_device) noexcept
{
    if (physical_device != physicalDevice)
    {
        physicalDevice = physical_device;
        dependencyCache = std::make_unique<DependencyCache>(physicalDevice);
    }
}

bool ExtensionWrangler::IsExtensionSupported(const std::string_view extension_name) const
{
    // if extension is core in active version, we're all good!
    if (ExtensionCoreInActiveVersion(extension_name))
    {
        return true;
    }

    if (ExtensionIsDeviceExtension(extension_name))
    {
        // if extension is device extension, we need to check if the physical device supports it
        auto& supportedDeviceExtensions = dependencyCache->DeviceExtensionDependencies;
        return std::ranges::contains(supportedDeviceExtensions, extension_name, ExtensionNameOf);
    }
    else if (ExtensionIsInstanceExtension(extension_name))
    {
        auto& supportedInstanceExtensions = dependencyCache->InstanceExtensionDependencies;
        return std::ranges::contains(supportedInstanceExtensions, extension_name, ExtensionNameOf);
    }
    else
    {
        // hunh???
        return false;
    }
}

bool ExtensionWrangler::AreExtensionsSupported(const size_t num_extensions, const std::string_view* extension_names) const
{
    const std::span<const std::string_view> extensionNames{ extension_names, num_extensions };
    return std::ranges::all_of(extensionNames, [this](const std::string_view name) { return IsExtensionSupported(name); });
}

bool ExtensionWrangler::ExtensionIsDeviceExtension(const std::string_view extension_name) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extension_name);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    size_t extensionIndex = extensionNameIter->second;

    return std::ranges::binary_search(deviceExtensionTable, extensionIndex);
}

bool ExtensionWrangler::ExtensionIsInstanceExtension(const std::string_view extension_name) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extension_name);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    size_t extensionIndex = extensionNameIter->second;

    return std::ranges::binary_search(instanceExtensionTable, extensionIndex);
}

bool ExtensionWrangler::ExtensionCoreInActiveVersion(const std::string_view extension_name) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extension_name);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    size_t extensionIndex = extensionNameIter->second;

    // an extension promoted in any version up to the active one is core. fixed: this only checked the row of the
    // active version, so an extension promoted in 1.1 read as "not core" on 1.4
    auto promotedFilter = [&](const auto& promoted_entry)
    {
        const auto& [promotedInVersion, promotedExtensions] = promoted_entry;
        return promotedInVersion <= apiVersion && std::ranges::contains(promotedExtensions, extensionIndex);
    };
    return std::ranges::any_of(promotedExtensionsMap, promotedFilter);
}

std::vector<std::string_view> ExtensionWrangler::GetSupportedExtensionsByPrefix(std::string_view prefix) const
{
    const auto isSupportedDeviceExtensionWithPrefix = [&](const std::string_view extension_name)
    {
        return extension_name.starts_with(prefix) &&
            ExtensionIsDeviceExtension(extension_name) &&
            !ExtensionCoreInActiveVersion(extension_name) &&
            IsExtensionSupported(extension_name);
    };

    return masterExtensionNameTable |
        std::views::transform([](const char* extension_name) { return std::string_view{ extension_name }; }) |
        std::views::filter(isSupportedDeviceExtensionWithPrefix) |
        std::ranges::to<std::vector>();
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionDependencies(const std::string_view extension_name) const
{
    if (ExtensionCoreInActiveVersion(extension_name))
    {
        ExtensionDependencies dependencies(0, 0);
        dependencies.VersionDep = apiVersion;
        return std::move(dependencies);
    }
    else if (ExtensionIsInstanceExtension(extension_name))
    {
        auto& supportedInstanceExtensions = dependencyCache->InstanceExtensionDependencies;
        if (!std::ranges::contains(supportedInstanceExtensions, extension_name, ExtensionNameOf))
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotSupported);
        }

        // found the extension, now get its dependencies
        const size_t extensionIndex = extensionIndexLookupMap.find(extension_name)->second;
        return getExtensionDependenciesInternal(extension_name, extensionIndex);
    }
    else if (physicalDevice != VK_NULL_HANDLE && ExtensionIsDeviceExtension(extension_name))
    {
        // if extension is device extension, we need to check if the physical device supports it
        auto& supportedDeviceExtensions = dependencyCache->DeviceExtensionDependencies;
        if (!std::ranges::contains(supportedDeviceExtensions, extension_name, ExtensionNameOf))
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotSupported);
        }

        const size_t extensionIndex = extensionIndexLookupMap.find(extension_name)->second;
        return getExtensionDependenciesInternal(extension_name, extensionIndex);
    }
    else
    {
        // hunh???
        return std::unexpected<DependencyError>(DependencyError::ExtensionNotInstanceOrDeviceExtension);
    }
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionDependencies(std::span<std::string_view> extension_names) const
{
    if (extension_names.empty())
    {
        return std::unexpected<DependencyError>(DependencyError::NoExtensionsProvided);
    }

    std::set<std::string_view> uniqueInstanceDependencies;
    std::set<std::string_view> uniqueDeviceDependencies;

    for (const std::string_view extensionName : extension_names)
    {
        auto dependencies = GetExtensionDependencies(extensionName);
        if (!dependencies.has_value() && (dependencies.error() == DependencyError::NoDependenciesForExtension))
        {
            // totally fine, we just don't have dependencies for this particular extension!
            continue;
        }
        else if (!dependencies.has_value())
        {
            // error was an actual error type, forward it
            return std::unexpected<DependencyError>(dependencies.error());
        }

        // Collect unique dependencies
        const std::span<const char* const> instanceDependencyNames{ dependencies->InstanceExtensionDeps, dependencies->NumInstanceExtensionDeps };
        const std::span<const char* const> deviceDependencyNames{ dependencies->DeviceExtensionDeps, dependencies->NumDeviceExtensionDeps };
        uniqueInstanceDependencies.insert_range(instanceDependencyNames);
        uniqueDeviceDependencies.insert_range(deviceDependencyNames);
    }

    // Create a new ExtensionDependencies instance with the collected unique dependencies
    ExtensionDependencies combinedDependencies(
        static_cast<uint32_t>(uniqueInstanceDependencies.size()),
        static_cast<uint32_t>(uniqueDeviceDependencies.size()));
    
    // set the version dependency to the API version
    combinedDependencies.VersionDep = apiVersion;
    if (uniqueDeviceDependencies.empty() && uniqueInstanceDependencies.empty())
    {
        // if no dependencies, just return the API version
        return std::move(combinedDependencies);
    }

    // Move unique dependencies into the combined instance
    std::ranges::copy(uniqueInstanceDependencies | std::views::transform(StringViewData), combinedDependencies.InstanceExtensionDeps);
    std::ranges::copy(uniqueDeviceDependencies | std::views::transform(StringViewData), combinedDependencies.DeviceExtensionDeps);

    return std::move(combinedDependencies);
}

std::expected<VkPhysicalDeviceFeatures2, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionFeatures(
    std::span<std::string_view> extension_names,
    GetVersionFeatures get_version_features,
    CollectDependencies collect_dependencies) const
{
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = nullptr;

    if (extension_names.empty() && get_version_features == GetVersionFeatures::False)
    {
        // No extensions, return empty features
        return features2;
    }

    // little bit of reinterpret_cast magic, and bam every features struct can be accessed through this
    // this is totally safe, since we're not ever storing actual objects, just pointers to these structs
    struct FeaturesStructAlias
    {
        VkStructureType SType;
        void* PNext;
    };

    auto castToFeaturesStructAlias = [](void* ptr) -> FeaturesStructAlias*
    {
        return reinterpret_cast<FeaturesStructAlias*>(ptr);
    };

    std::vector<FeaturesStructAlias*> featuresVec;
    featuresVec.emplace_back(castToFeaturesStructAlias(&features2));

    // We're going to want to use a stack of sorts to build the pNext chain, so we start with the base features
    if (get_version_features == GetVersionFeatures::True)
    {

        // Cumulatively add feature structs for all versions up to and including apiVersion
        if (apiVersion >= VK_API_VERSION_1_1)
        {
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features11Alias = castToFeaturesStructAlias(&k_LocalVulkan11Features);
            currentBack->PNext = features11Alias;
            featuresVec.push_back(features11Alias);
        }

        if (apiVersion >= VK_API_VERSION_1_2)
        {
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features12Alias = castToFeaturesStructAlias(&k_LocalVulkan12Features);
            currentBack->PNext = features12Alias;
            featuresVec.push_back(features12Alias);
        }

        if (apiVersion >= VK_API_VERSION_1_3)
        {
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features13Alias = castToFeaturesStructAlias(&k_LocalVulkan13Features);
            currentBack->PNext = features13Alias;
            featuresVec.push_back(features13Alias);
        }

        if (apiVersion >= VK_API_VERSION_1_4)
        {
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features14Alias = castToFeaturesStructAlias(&k_LocalVulkan14Features);
            currentBack->PNext = features14Alias;
            featuresVec.push_back(features14Alias);
        }
    }

    // unfortunately need a duplicate list of the names, since we may need to add to it for dependencies (I expect this to be a common case)
    std::vector<std::string_view> extensionNamesVec{ extension_names.begin(), extension_names.end() };
    if (collect_dependencies == CollectDependencies::True)
    {
        // Collect dependencies for the extensions
        auto extensionDeps = GetExtensionDependencies(extension_names);
        if (!extensionDeps.has_value())
        {
            // If we couldn't collect dependencies, forward the error
            return std::unexpected<DependencyError>(extensionDeps.error());
        }
        else
        {
            // Only care about the device extensions here, since instance extensions don't have features
            const std::span<const char* const> deviceDependencyNames{ extensionDeps->DeviceExtensionDeps, extensionDeps->NumDeviceExtensionDeps };
            extensionNamesVec.append_range(deviceDependencyNames);
        }
    }


    // Iterate through the extensions and populate the features
    for (const std::string_view extensionName : extensionNamesVec)
    {
        if (ExtensionCoreInActiveVersion(extensionName) || ExtensionIsInstanceExtension(extensionName))
        {
            // If the extension is core in the active version or an instance extension, we don't need to query features for it
            continue;
        }

        auto extensionNameIdxIter = extensionIndexLookupMap.find(extensionName);
        if (extensionNameIdxIter == extensionIndexLookupMap.end())
        {
            // Extension not found in the lookup map, skip it
            continue;
        }

        const size_t extensionIndex = extensionNameIdxIter->second;

        // Now query for the feature struct, to see if this extension requires it
        // (main reason we even need this map: static storage for the feature structs, so they persist through call to create device)
        auto extensionFeatureIter = extensionFeatureStructMap.find(extensionIndex);
        if (extensionFeatureIter == extensionFeatureStructMap.end())
        {
            // Extension does not have a feature struct, skip it
            continue;
        }

        const void* featureStructPtr = extensionFeatureIter->second;
        // oops, bad const_cast but I swear it's fine this time....
        FeaturesStructAlias* currentFeatureAlias = castToFeaturesStructAlias(const_cast<void*>(featureStructPtr));
        // Now we need to add this to the pNext chain
        FeaturesStructAlias* lastFeatureAlias = featuresVec.back();
        lastFeatureAlias->PNext = currentFeatureAlias;
        featuresVec.emplace_back(currentFeatureAlias);
    }

    // vkGetPhysicalDeviceFeatures2 is core from 1.1. A 1.0 device can't take extension feature structs without
    // VK_KHR_get_physical_device_properties2, which we don't require, so it gets the 1.0 features and no chain
    if (apiVersion < VK_API_VERSION_1_1)
    {
        features2.pNext = nullptr;
        vkGetPhysicalDeviceFeatures(physicalDevice, &features2.features);
        return features2;
    }

    // last step: call vkGetPhysicalDeviceFeatures2 to populate the features
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    return features2;
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::getExtensionDependenciesInternal(
    const std::string_view extension_name,
    const size_t extension_index) const
{
    // Now use API version + extension index to find the dependencies in the dependency table
    auto extensionDepMapIter = extensionDependencyTable.find(apiVersion);
    if (extensionDepMapIter == extensionDependencyTable.end())
    {
        // failing here suggests apiVersion is not in the table, which is odd
        return std::unexpected<DependencyError>(DependencyError::ApiVersionNotValid);
    }

    const ExtensionDependencyMap& extensionDepMap = extensionDepMapIter->second;
    auto depIter = extensionDepMap.find(extension_index);
    if (depIter == extensionDepMap.end() && apiVersion != 0)
    {
        // We first try to find the extension in the dependency map for this version, but now we'll try to find it in the any version map (which is totally fine!)
        extensionDepMapIter = extensionDependencyTable.find(0);
        const ExtensionDependencyMap& extensionDepMapTakeTwo = extensionDepMapIter->second;
        depIter = extensionDepMapTakeTwo.find(extension_index);
        // now if we're still at the end of the map, then we have a problem
        if (depIter == extensionDepMapTakeTwo.end())
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotInDependencyTableForVersion);
        }
    }
    else if (depIter == extensionDepMap.end())
    {
        return std::unexpected<DependencyError>(DependencyError::ExtensionNotInDependencyTableForVersion);
    }

    // Now we have the list of indices this extension depends on, build the ExtensionDependencies object
    const std::vector<size_t>& deps = depIter->second;

    if (deps.empty())
    {
        // No dependencies for this extension, woo!
        return std::unexpected<DependencyError>(DependencyError::NoDependenciesForExtension);
    }
    
    // Now need to iterate each extension and count how many instance and device extensions there are, and collect the name strings
    // Realized a bit late that I'm not certain if instance deps can or cannot have device deps, and vice versa.....
    std::vector<std::string_view> instanceDeps;
    std::vector<std::string_view> deviceDeps;

    for (const size_t dependencyIndex : deps)
    {
        // First check - is this dependency index an extension or version dep?
        if (k_BuiltInVkVersions.contains(static_cast<uint32_t>(dependencyIndex)))
        {
            // It is a version dependency, so check that against API version
            if (apiVersion >= dependencyIndex)
            {
                continue; // we are already using a compatible API version, nothing to do here
            }
            else
            {
                return std::unexpected<DependencyError>(DependencyError::CurrentApiVersionDoesNotSupportExtension);
            }
        }
        else
        {
            // extension index is just a regular dependency index
            std::string_view dependencyName = masterExtensionNameTable[dependencyIndex];
            if (ExtensionIsInstanceExtension(dependencyName.data()))
            {
                instanceDeps.push_back(dependencyName);
            }
            else if (ExtensionIsDeviceExtension(dependencyName.data()))
            {
                deviceDeps.push_back(dependencyName);
            }
            else
            {
                // This is a problem, we have an extension dependency that is neither instance nor device extension
                return std::unexpected<DependencyError>(DependencyError::ExtensionNotInstanceOrDeviceExtension);
            }
        }
    }

    ExtensionDependencies dependencies(static_cast<uint32_t>(instanceDeps.size()), static_cast<uint32_t>(deviceDeps.size()));
    dependencies.VersionDep = apiVersion;
    
    std::ranges::copy(instanceDeps | std::views::transform(StringViewData), dependencies.InstanceExtensionDeps);
    std::ranges::copy(deviceDeps | std::views::transform(StringViewData), dependencies.DeviceExtensionDeps);

    return std::move(dependencies);
}

namespace
{

    bool AreStringsEqual(const VkExtensionProperties& ext_props, const char* str)
    {
        return strcmp(ext_props.extensionName, str) == 0;
    }

    std::string_view ExtensionNameOf(const VkExtensionProperties& properties) noexcept
    {
        return std::string_view{ properties.extensionName };
    }

    const char* StringViewData(const std::string_view view) noexcept
    {
        return view.data();
    }

}
//NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
