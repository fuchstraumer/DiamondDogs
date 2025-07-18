#pragma once
#ifndef RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#define RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <memory>

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

    uint32_t versionDep;

    uint32_t numInstanceExtensionDeps;
    const char** instanceExtensionDeps;
    uint32_t numDeviceExtensionDeps;
    const char** deviceExtensionDeps;
};

class ExtensionWrangler
{
public:
    ExtensionWrangler(
        const uint32_t _apiVersion,
        VkPhysicalDevice _physicalDevice);
    ~ExtensionWrangler() noexcept = default;

    bool IsExtensionSupported(const char* extensionName) const;
    bool AreExtensionsSupported(const size_t numExtensions, const char** extensionNames) const;
    bool ExtensionIsDeviceExtension(const char* extensionName) const;
    bool ExtensionIsInstanceExtension(const char* extensionName) const;
    bool ExtensionCoreInActiveVersion(const char* extensionName) const;

    ExtensionDependencies GetExtensionDependencies(const char* extensionName) const;
    ExtensionDependencies GetExtensionDependencies(const size_t numExtensions, const char** extensionNames) const;

    enum class GetVersionFeatures
    {
        True,
        False
    };

    VkPhysicalDeviceFeatures2 GetExtensionFeatures(
        const size_t numExtensions,
        const char** extensionNames,
        GetVersionFeatures getVersionFeatures = GetVersionFeatures::False) const;

    enum class GetVersionProperties
    {
        True,
        False
    };

    VkPhysicalDeviceProperties2 GetExtensionProperties(
        const size_t numExtensions,
        const char** extensionNames,
        GetVersionProperties getVersionProperties = GetVersionProperties::False) const;

private:
    std::unique_ptr<struct DependencyCache> dependencyCache;
    uint32_t apiVersion;
    VkPhysicalDevice physicalDevice;
};

#endif //!RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
