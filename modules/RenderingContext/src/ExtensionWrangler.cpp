#include "ExtensionWrangler.hpp"
#include <algorithm>
#include <vector>
#include <cstring>
#include "GeneratedExtensionHeader.hpp"

struct DependencyCache
{
    DependencyCache(VkPhysicalDevice _physicalDevice)
    {
        uint32_t numInstanceExtensions = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
        instanceExtensionDependencies.resize(numInstanceExtensions);
        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, instanceExtensionDependencies.data());

        uint32_t numDeviceExtensions = 0;
        vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numDeviceExtensions, nullptr);
        deviceExtensionDependencies.resize(numDeviceExtensions);
        vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &numDeviceExtensions, deviceExtensionDependencies.data());
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
{}

bool AreStringsEqual(const char* a, const char* b)
{
    return strcmp(a, b) == 0;
}

bool ExtensionWrangler::IsExtensionSupported(const char* extensionName) const
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
        auto extensionIter = std::find_if(supportedDeviceExtensions.begin(), supportedDeviceExtensions.end(), &AreStringsEqual);
        return extensionIter != supportedDeviceExtensions.end();
    }
    else if (ExtensionIsInstanceExtension(extensionName))
    {
        auto& supportedInstanceExtensions = dependencyCache->instanceExtensionDependencies;
        auto extensionIter = std::find_if(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), &AreStringsEqual);
        return extensionIter != supportedInstanceExtensions.end();

    }
    else
    {
        // hunh???
        return false;
    }
}

bool ExtensionWrangler::AreExtensionsSupported(const size_t numExtensions, const char** extensionNames) const
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

bool ExtensionWrangler::ExtensionIsDeviceExtension(const char* extensionName) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extensionName);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    uint32_t extensionIndex = extensionNameIter->second;

    return std::binary_search(deviceExtensionTable.begin(), deviceExtensionTable.end(), extensionIndex);
}

bool ExtensionWrangler::ExtensionIsInstanceExtension(const char* extensionName) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extensionName);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    uint32_t extensionIndex = extensionNameIter->second;

    return std::binary_search(instanceExtensionTable.begin(), instanceExtensionTable.end(), extensionIndex);
}

bool ExtensionWrangler::ExtensionCoreInActiveVersion(const char* extensionName) const
{
    auto extensionNameIter = extensionIndexLookupMap.find(extensionName);
    if (extensionNameIter == extensionIndexLookupMap.end())
    {
        return false;
    }
    
    uint32_t extensionIndex = extensionNameIter->second;
    
    auto promotedExtensionsForVersionIter = promotedExtensionsMap.find(apiVersion);
    if (promotedExtensionsForVersionIter == promotedExtensionsMap.end())
    {
        return false; // no promoted extensions for this version
    }

    const std::vector<size_t>& promotedExtensionsForVersion = promotedExtensionsForVersionIter->second;
    return std::find(promotedExtensionsForVersion.begin(), promotedExtensionsForVersion.end(), extensionIndex) != promotedExtensionsForVersion.end();
}

std::optional<ExtensionDependencies> ExtensionWrangler::GetExtensionDependencies(const char* extensionName) const
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
        auto extensionIter = std::find_if(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), &AreStringsEqual);
        if (extensionIter == supportedInstanceExtensions.end())
        {
            return std::nullopt;
        }

        // found the extension, now get its dependencies
        const size_t extensionIndex = extensionIndexLookupMap.find(extensionName)->second;
        return getExtensionDependenciesInternal(extensionName, extensionIndex);
    }
    else if (ExtensionIsDeviceExtension(extensionName))
    {
        // if extension is device extension, we need to check if the physical device supports it
        auto& supportedDeviceExtensions = dependencyCache->deviceExtensionDependencies;
        auto supportedDeviceExtensionIter = std::find_if(supportedDeviceExtensions.begin(), supportedDeviceExtensions.end(), &AreStringsEqual);
        if (supportedDeviceExtensionIter == supportedDeviceExtensions.end())
        {
            return std::nullopt; // extension not supported
        }

        const size_t extensionIndex = extensionIndexLookupMap.find(extensionName)->second;
        return getExtensionDependenciesInternal(extensionName, extensionIndex);
    }
    else
    {
        // hunh???
        return std::nullopt;
    }
}

std::optional<ExtensionDependencies> ExtensionWrangler::getExtensionDependenciesInternal(const char* extensionName, const size_t extensionIndex) const
{
    // Now use API version + extension index to find the dependencies in the dependency table
        auto extensionDepMapIter = extensionDependencyTable.find(apiVersion);
        if (extensionDepMapIter == extensionDependencyTable.end())
        {
            // failing here suggests apiVersion is not in the table, which is odd
            return std::nullopt;
        }

        const ExtensionDependencyMap& extensionDepMap = extensionDepMapIter->second;
        auto depIter = extensionDepMap.find(extensionIndex);
        if (depIter == extensionDepMap.end())
        {
            // Extension is not in the dependency map for this version, which is a problem
            return std::nullopt;
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
                return std::nullopt;
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
