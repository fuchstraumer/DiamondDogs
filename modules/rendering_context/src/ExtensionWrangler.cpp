#include "ExtensionWrangler.hpp"
#include "GeneratedExtensionHeader.hpp"
#include <algorithm>
#include <vector>
#include <cstring>

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
    auto versionedExtensionsIter = versionedExtensionsMap.find(apiVersion);
    if (versionedExtensionsIter == versionedExtensionsMap.end())
    {
        return false;
    }
    
    const std::vector<size_t>& versionedExtensions = versionedExtensionsIter->second;
    return std::binary_search(versionedExtensions.begin(), versionedExtensions.end(), extensionIndex);
}

ExtensionDependencies ExtensionWrangler::GetExtensionDependencies(const char* extensionName) const
{
    if (ExtensionCoreInActiveVersion(extensionName))
    {
        ExtensionDependencies dependencies(0, 0);
        dependencies.versionDep = apiVersion;
        return std::move(dependencies);
    }
    else if (ExtensionIsInstanceExtension(extensionName))
    {
        
    }
}

VkPhysicalDeviceFeatures2 ExtensionWrangler::GetExtensionFeatures(
    const size_t numExtensions,
    const char** extensionNames,
    GetVersionFeatures getVersionFeatures) const
{
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = nullptr;
    void* previousStruct = &features2;

    if (getVersionFeatures == GetVersionFeatures::True)
    {
        // For each version equal to or lesser than the active version, we 
        // need to add that features struct to the pNext chain.
        for (uint32_t version : supportedVulkanVersions)
        {
            if (version > apiVersion)
            {
                break;
            }

            void* currentStruct = versionFeatureStructMap.find(version)->second;
            reinterpret_cast<VkPhysicalDeviceFeatures2*>(previousStruct)->pNext = currentStruct;
            previousStruct = currentStruct;
        }
    }

    for (size_t i = 0; i < numExtensions; i++)
    {
        if (ExtensionIsDeviceExtension(extensionNames[i]))
        {
            size_t extensionIndex = extensionIndexLookupMap.find(extensionNames[i])->second;
            void* currentStruct = extensionFeatureStructMap.find(extensionIndex)->second;
            // should be a safe cast: every struct will have at least a pNext pointer at the same offset from the start
            // as VkPhysicalDeviceFeatures2. Ugly, but lets us avoid worse (for a library) template or variant or so on type of solutions
            reinterpret_cast<VkPhysicalDeviceFeatures2*>(previousStruct)->pNext = currentStruct;
            previousStruct = currentStruct;
        }
    }

    return features2;
}
