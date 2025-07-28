#include "ExtensionWrangler.hpp"
#include <algorithm>
#include <vector>
#include <cstring>
#include <set>
#include "GeneratedExtensionHeader.hpp"

struct DependencyCache
{
    DependencyCache(VkPhysicalDevice _physicalDevice)
    {
        uint32_t numInstanceExtensions = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
        instanceExtensionDependencies.resize(numInstanceExtensions);
        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, instanceExtensionDependencies.data());

        if (_physicalDevice != VK_NULL_HANDLE)
        {
            uint32_t numDeviceExtensions = 0;
            vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numDeviceExtensions, nullptr);
            deviceExtensionDependencies.resize(numDeviceExtensions);
            vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numDeviceExtensions, deviceExtensionDependencies.data());
        }
    }
    std::vector<VkExtensionProperties> deviceExtensionDependencies;
    std::vector<VkExtensionProperties> instanceExtensionDependencies;
};

ExtensionDependencies::ExtensionDependencies(uint32_t numInstanceExtensionDeps, uint32_t numDeviceExtensionDeps)
{

    numInstanceExtensionDeps = numInstanceExtensionDeps;
    if (numInstanceExtensionDeps > 0)
    {
        instanceExtensionDeps = new const char*[numInstanceExtensionDeps];
    }

    numDeviceExtensionDeps = numDeviceExtensionDeps;
    if (numDeviceExtensionDeps > 0)
    {
        deviceExtensionDeps = new const char*[numDeviceExtensionDeps];
    }

}

ExtensionDependencies::~ExtensionDependencies()
{
    if (numInstanceExtensionDeps > 0 && instanceExtensionDeps != nullptr)
    {
        delete[] instanceExtensionDeps;
    }
    if (numDeviceExtensionDeps > 0 && deviceExtensionDeps != nullptr)
    {
        delete[] deviceExtensionDeps;
    }
}

ExtensionDependencies::ExtensionDependencies(ExtensionDependencies&& other) noexcept
{
    instanceExtensionDeps = other.instanceExtensionDeps;
    deviceExtensionDeps = other.deviceExtensionDeps;
    numInstanceExtensionDeps = other.numInstanceExtensionDeps;
    numDeviceExtensionDeps = other.numDeviceExtensionDeps;

    other.instanceExtensionDeps = nullptr;
    other.deviceExtensionDeps = nullptr;
    other.numInstanceExtensionDeps = 0;
    other.numDeviceExtensionDeps = 0;
}

ExtensionDependencies& ExtensionDependencies::operator=(ExtensionDependencies&& other) noexcept
{
    if (this != &other)
    {
        delete[] instanceExtensionDeps;
        delete[] deviceExtensionDeps;

        instanceExtensionDeps = other.instanceExtensionDeps;
        deviceExtensionDeps = other.deviceExtensionDeps;
        numInstanceExtensionDeps = other.numInstanceExtensionDeps;
        numDeviceExtensionDeps = other.numDeviceExtensionDeps;

        other.instanceExtensionDeps = nullptr;
        other.deviceExtensionDeps = nullptr;
        other.numInstanceExtensionDeps = 0;
        other.numDeviceExtensionDeps = 0;
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
    // API version we get from ctor is often not an exact match to the actual API versions we have in the tables, so we need to now map
    // the input API version to the closest one we have in the table
    constexpr static uint32_t BuiltInApiVersions[] =
    {
        VK_API_VERSION_1_0,
        VK_API_VERSION_1_1,
        VK_API_VERSION_1_2,
        VK_API_VERSION_1_3,
        VK_API_VERSION_1_4
    };

    // Find the highest API version that is <= the input API version
    // upper_bound returns iterator to first element > apiVersion, so we want the element before that
    auto upperBoundIt = std::upper_bound(std::begin(BuiltInApiVersions), std::end(BuiltInApiVersions), apiVersion);
    
    // If upper_bound points to begin(), then apiVersion is less than all built-in versions, use the first one
    if (upperBoundIt == std::begin(BuiltInApiVersions))
    {
        apiVersion = BuiltInApiVersions[0];
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

bool AreStringsEqual(const VkExtensionProperties& a, const char* b)
{
    return strcmp(a.extensionName, b) == 0;
}

bool ExtensionWrangler::IsExtensionSupported(const std::string_view extensionName) const
{
    // if extension is core in active version, we're all good!
    if (ExtensionCoreInActiveVersion(extensionName))
    {
        return true;
    }

    if (ExtensionIsDeviceExtension(extensionName))
    {
        // if extension is device extension, we need to check if the physical device supports it
        auto& supportedDeviceExtensions = dependencyCache->deviceExtensionDependencies;
        auto extensionIter = std::find_if(
            supportedDeviceExtensions.begin(),
            supportedDeviceExtensions.end(),
            [&extensionName](const VkExtensionProperties& props) { return strcmp(props.extensionName, extensionName.data()) == 0; });
        return extensionIter != supportedDeviceExtensions.end();
    }
    else if (ExtensionIsInstanceExtension(extensionName))
    {
        auto& supportedInstanceExtensions = dependencyCache->instanceExtensionDependencies;
        auto extensionIter = std::find_if(
            supportedInstanceExtensions.begin(),
            supportedInstanceExtensions.end(),
            [&extensionName](const VkExtensionProperties& props) { return strcmp(props.extensionName, extensionName.data()) == 0; });
        return extensionIter != supportedInstanceExtensions.end();
    }
    else
    {
        // hunh???
        return false;
    }
}

bool ExtensionWrangler::AreExtensionsSupported(const size_t numExtensions, const std::string_view* extensionNames) const
{
    for (size_t i = 0; i < numExtensions; i++)
    {
        if (!IsExtensionSupported(extensionNames[i]))
        {
            return false;
        }
    }

    return true;
}

bool ExtensionWrangler::ExtensionIsDeviceExtension(const std::string_view extensionName) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extensionName);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    size_t extensionIndex = extensionNameIter->second;

    return std::binary_search(deviceExtensionTable.begin(), deviceExtensionTable.end(), extensionIndex);
}

bool ExtensionWrangler::ExtensionIsInstanceExtension(const std::string_view extensionName) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extensionName);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    size_t extensionIndex = extensionNameIter->second;

    return std::binary_search(instanceExtensionTable.begin(), instanceExtensionTable.end(), extensionIndex);
}

bool ExtensionWrangler::ExtensionCoreInActiveVersion(const std::string_view extensionName) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extensionName);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    size_t extensionIndex = extensionNameIter->second;
    
    auto promotedExtensionsForVersionIter = promotedExtensionsMap.find(apiVersion);
    if (promotedExtensionsForVersionIter == promotedExtensionsMap.end())
    {
        return false; // no promoted extensions for this version
    }

    const std::vector<size_t>& promotedExtensionsForVersion = promotedExtensionsForVersionIter->second;
    return std::find(promotedExtensionsForVersion.begin(), promotedExtensionsForVersion.end(), extensionIndex) != promotedExtensionsForVersion.end();
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionDependencies(const std::string_view extensionName) const
{
    if (ExtensionCoreInActiveVersion(extensionName))
    {
        ExtensionDependencies dependencies(0, 0);
        dependencies.versionDep = apiVersion;
        return std::move(dependencies);
    }
    else if (ExtensionIsInstanceExtension(extensionName))
    {
        auto& supportedInstanceExtensions = dependencyCache->instanceExtensionDependencies;
        auto supportedInstanceExtensionIter = std::find_if(
            supportedInstanceExtensions.begin(),
            supportedInstanceExtensions.end(),
            [&extensionName](const VkExtensionProperties& props) { return strcmp(props.extensionName, extensionName.data()) == 0; });
        if (supportedInstanceExtensionIter == supportedInstanceExtensions.end())
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotSupported);
        }

        // found the extension, now get its dependencies
        const size_t extensionIndex = extensionIndexLookupMap.find(extensionName)->second;
        return getExtensionDependenciesInternal(extensionName, extensionIndex);
    }
    else if (physicalDevice != VK_NULL_HANDLE && ExtensionIsDeviceExtension(extensionName))
    {
        // if extension is device extension, we need to check if the physical device supports it
        auto& supportedDeviceExtensions = dependencyCache->deviceExtensionDependencies;
        auto supportedDeviceExtensionIter = std::find_if(
            supportedDeviceExtensions.begin(),
            supportedDeviceExtensions.end(),
            [&extensionName](const VkExtensionProperties& props) { return strcmp(props.extensionName, extensionName.data()) == 0; });
        if (supportedDeviceExtensionIter == supportedDeviceExtensions.end())
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotSupported);
        }

        const size_t extensionIndex = extensionIndexLookupMap.find(extensionName)->second;
        return getExtensionDependenciesInternal(extensionName, extensionIndex);
    }
    else
    {
        // hunh???
        return std::unexpected<DependencyError>(DependencyError::ExtensionNotInstanceOrDeviceExtension);
    }
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionDependencies(const size_t numExtensions, const std::string_view* extensionNames) const
{
    if (numExtensions == 0)
    {
        return std::nullopt; // no extensions, no dependencies
    }

    std::set<std::string_view> uniqueInstanceDependencies;
    std::set<std::string_view> uniqueDeviceDependencies;

    for (size_t i = 0; i < numExtensions; ++i)
    {
        auto dependencies = GetExtensionDependencies(extensionNames[i]);
        if (!dependencies.has_value())
        {
            return std::unexpected<DependencyError>(DependencyError::NoDependenciesForExtension);
        }

        // Collect unique dependencies
        for (size_t i = 0; i < dependencies->numInstanceExtensionDeps; ++i)
        {
            uniqueInstanceDependencies.insert(dependencies->instanceExtensionDeps[i]);
        }

        for (size_t i = 0; i < dependencies->numDeviceExtensionDeps; ++i)
        {
            uniqueDeviceDependencies.insert(dependencies->deviceExtensionDeps[i]);
        }
    }

    // Create a new ExtensionDependencies instance with the collected unique dependencies
    ExtensionDependencies combinedDependencies(
        static_cast<uint32_t>(uniqueInstanceDependencies.size()),
        static_cast<uint32_t>(uniqueDeviceDependencies.size()));
    
    // set the version dependency to the API version
    combinedDependencies.versionDep = apiVersion;
    if (uniqueDeviceDependencies.empty() && uniqueInstanceDependencies.empty())
    {
        // if no dependencies, just return the API version
        return std::move(combinedDependencies);
    }

    // Move unique dependencies into the combined instance
    size_t instanceDepIdx = 0;
    for (const auto& dep : uniqueInstanceDependencies)
    {
        combinedDependencies.instanceExtensionDeps[instanceDepIdx++] = dep.data();
    }

    size_t deviceDepIdx = 0;
    for (const auto& dep : uniqueDeviceDependencies)
    {
        combinedDependencies.deviceExtensionDeps[deviceDepIdx++] = dep.data();
    }

    return std::move(combinedDependencies);
}

std::expected<VkPhysicalDeviceFeatures2, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionFeatures(
    const size_t numExtensions,
    const std::string_view* extensionNames,
    GetVersionFeatures getVersionFeatures,
    CollectDependencies collectDependencies) const
{
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = nullptr;

    if (numExtensions == 0 && getVersionFeatures == GetVersionFeatures::False)
    {
        // No extensions, return empty features
        return features2;
    }

    // little bit of reinterpret_cast magic, and bam every features struct can be accessed through this
    // this is totally safe, since we're not ever storing actual objects, just pointers to these structs
    struct FeaturesStructAlias
    {
        VkStructureType sType;
        void* pNext;
    };

    auto castToFeaturesStructAlias = [](void* ptr) -> FeaturesStructAlias*
    {
        return reinterpret_cast<FeaturesStructAlias*>(ptr);
    };

    std::vector<FeaturesStructAlias*> featuresVec;
    featuresVec.emplace_back(castToFeaturesStructAlias(&features2));

    // We're going to want to use a stack of sorts to build the pNext chain, so we start with the base features
    if (getVersionFeatures == GetVersionFeatures::True)
    {

        // Cumulatively add feature structs for all versions up to and including apiVersion
        if (apiVersion >= VK_API_VERSION_1_1)
        {
            VkPhysicalDeviceVulkan11Features features11 = {};
            features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            features11.pNext = nullptr;
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features11Alias = castToFeaturesStructAlias(&features11);
            currentBack->pNext = features11Alias;
            featuresVec.push_back(features11Alias);
        }

        if (apiVersion >= VK_API_VERSION_1_2)
        {
            VkPhysicalDeviceVulkan12Features features12 = {};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features12.pNext = nullptr;
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features12Alias = castToFeaturesStructAlias(&features12);
            currentBack->pNext = features12Alias;
            featuresVec.push_back(features12Alias);
        }

        if (apiVersion >= VK_API_VERSION_1_3)
        {
            VkPhysicalDeviceVulkan13Features features13 = {};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            features13.pNext = nullptr;
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features13Alias = castToFeaturesStructAlias(&features13);
            currentBack->pNext = features13Alias;
            featuresVec.push_back(features13Alias);
        }

        if (apiVersion >= VK_API_VERSION_1_4)
        {
            VkPhysicalDeviceVulkan14Features features14 = {};
            features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
            features14.pNext = nullptr;
            FeaturesStructAlias* currentBack = featuresVec.back();
            FeaturesStructAlias* features14Alias = castToFeaturesStructAlias(&features14);
            currentBack->pNext = features14Alias;
            featuresVec.push_back(features14Alias);
        }
    }

    // unfortunately need a duplicate list of the names, since we may need to add to it for dependencies (I expect this to be a common case)
    std::vector<std::string_view> extensionNamesVec{ extensionNames, extensionNames + numExtensions };
    if (collectDependencies == CollectDependencies::True)
    {
        // Collect dependencies for the extensions
        auto extensionDeps = GetExtensionDependencies(numExtensions, extensionNames);
        if (!extensionDeps.has_value())
        {
            // If we couldn't collect dependencies, forward the error
            return std::unexpected<DependencyError>(extensionDeps.error());
        }
        else
        {
            // Only care about the device extensions here, since instance extensions don't have features
            for (size_t i = 0; i < extensionDeps->numDeviceExtensionDeps; ++i)
            {
                const char* dep = extensionDeps->deviceExtensionDeps[i];
                extensionNamesVec.emplace_back(dep);
            }
        }
    }


    // Iterate through the extensions and populate the features
    for (size_t i = 0; i < extensionNamesVec.size(); i++)
    {
        const char* extensionName = extensionNamesVec[i].data();
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
        lastFeatureAlias->pNext = currentFeatureAlias;
        featuresVec.emplace_back(currentFeatureAlias);
    }

    return features2;
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::getExtensionDependenciesInternal(
    const std::string_view extensionName,
    const size_t extensionIndex) const
{
    // Now use API version + extension index to find the dependencies in the dependency table
    auto extensionDepMapIter = extensionDependencyTable.find(apiVersion);
    if (extensionDepMapIter == extensionDependencyTable.end())
    {
        // failing here suggests apiVersion is not in the table, which is odd
        return std::unexpected<DependencyError>(DependencyError::ApiVersionNotValid);
    }

    const ExtensionDependencyMap& extensionDepMap = extensionDepMapIter->second;
    auto depIter = extensionDepMap.find(extensionIndex);
    if (depIter == extensionDepMap.end() && apiVersion != 0)
    {
        // We first try to find the extension in the dependency map for this version, but now we'll try to find it in the any version map (which is totally fine!)
        extensionDepMapIter = extensionDependencyTable.find(0);
        const ExtensionDependencyMap& extensionDepMapTakeTwo = extensionDepMapIter->second;
        depIter = extensionDepMapTakeTwo.find(extensionIndex);
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
    
    // Now need to iterate each extension and count how many instance and device extensions there are, and collect the name strings
    // Realized a bit late that I'm not certain if instance deps can or cannot have device deps, and vice versa.....
    std::vector<std::string_view> instanceDeps;
    std::vector<std::string_view> deviceDeps;

    for (const size_t dependencyIndex : deps)
    {
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
            return std::unexpected<DependencyError>(DependencyError::InvalidExtensionDependency);
        }
    }

    ExtensionDependencies dependencies(static_cast<uint32_t>(instanceDeps.size()), static_cast<uint32_t>(deviceDeps.size()));
    dependencies.versionDep = apiVersion;
    
    for (size_t i = 0; i < instanceDeps.size(); i++)
    {
        dependencies.instanceExtensionDeps[i] = instanceDeps[i].data();
    }

    for (size_t i = 0; i < deviceDeps.size(); i++)
    {
        dependencies.deviceExtensionDeps[i] = deviceDeps[i].data();
    }

    return std::move(dependencies);
}
