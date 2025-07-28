#pragma once
#ifndef RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#define RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <memory>
#include <optional>
#include <string_view>

// Will have to allocate room for the extension deps, so need the whole rule of 5
struct ExtensionDependencies
{
private:
    friend class ExtensionWrangler;
    ExtensionDependencies(uint32_t numInstanceExtensionDeps, uint32_t numDeviceExtensionDeps);
public:
    ~ExtensionDependencies();
    ExtensionDependencies(const ExtensionDependencies&) = delete;
    ExtensionDependencies& operator=(const ExtensionDependencies&) = delete;
    ExtensionDependencies(ExtensionDependencies&&) noexcept;
    ExtensionDependencies& operator=(ExtensionDependencies&&) noexcept;

    bool IsOnlyVersionDependent() const
    {
        return numInstanceExtensionDeps == 0 && numDeviceExtensionDeps == 0 && versionDep != 0;
    }

    uint32_t versionDep;

    uint32_t numInstanceExtensionDeps;
    const char** instanceExtensionDeps;
    uint32_t numDeviceExtensionDeps;
    const char** deviceExtensionDeps;
};

class ExtensionWrangler
{
public:

    /**
     * @brief Constructs an ExtensionWrangler instance. Can be used before the Vulkan instance or a physical device is created by passing VK_NULL_HANDLE for the _physicalDevice parameter.
     * @param _apiVersion The Vulkan API version to use.
     * @param _physicalDevice The physical device to use for device extensions. If VK_NULL_HANDLE, only instance extensions will be considered.
     * @note Expect to construct two instances of this, one for pre-instance extension wrangling and one for post-instance pre-logical device extension wrangling.
     */
    ExtensionWrangler(
        const uint32_t _apiVersion,
        VkPhysicalDevice _physicalDevice);
    ~ExtensionWrangler() noexcept;
    
    /** @brief Simple query for is an extension is supported or not */
    bool IsExtensionSupported(const std::string_view extensionName) const;
    /** @brief Simply query for if a set of extensions is supported or not */
    bool AreExtensionsSupported(const size_t numExtensions, const std::string_view* extensionNames) const;
    bool ExtensionIsDeviceExtension(const std::string_view extensionName) const;
    bool ExtensionIsInstanceExtension(const std::string_view extensionName) const;
    /** @brief If an extension is core in the current Vulkan API version in use in this environment, then it does not need to be explicitly enabled at all. */
    bool ExtensionCoreInActiveVersion(const std::string_view extensionName) const;

    /** @brief Returns an instance of the ExtensionDependencies struct containing all required dependencies for the specified extension. */
    std::optional<ExtensionDependencies> GetExtensionDependencies(const std::string_view extensionName) const;
    /** @brief Returns a deduplicated instance of ExtensionDependencies with all required dependencies for the specified set of extensions (i.e, the common set of required dependencies) */
    std::optional<ExtensionDependencies> GetExtensionDependencies(const size_t numExtensions, const std::string_view* extensionNames) const;

    enum class GetVersionFeatures
    {
        True,
        False
    };

    enum class CollectDependencies
    {
        True,
        False
    };

    /** @brief Returns a VkPhysicalDeviceFeatures2 struct with the pNext chain fully populated for the given set of input extensions, which can be used to create a device.
     * @param numExtensions The number of extensions in the extensionNames array.
     * @param extensionNames An array of C-style strings representing the names of the extensions to get features for.
     * @param getVersionFeatures Whether to include the version features in the pNext chain (default: false)
     * @param collectDependencies Whether to collect dependencies for the extensions (default: false)
     * @return A VkPhysicalDeviceFeatures2 struct with the pNext chain populated with the features for the given extensions
     */
    VkPhysicalDeviceFeatures2 GetExtensionFeatures(
        const size_t numExtensions,
        const std::string_view* extensionNames,
        GetVersionFeatures getVersionFeatures = GetVersionFeatures::False,
        CollectDependencies collectDependencies = CollectDependencies::False) const;

private:

    std::optional<ExtensionDependencies> getExtensionDependenciesInternal(const std::string_view extensionName, const size_t extensionIndex) const;

    uint32_t apiVersion;
    VkPhysicalDevice physicalDevice;

    std::unique_ptr<struct DependencyCache> dependencyCache;

};

#endif //!RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
