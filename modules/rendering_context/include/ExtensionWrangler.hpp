#pragma once
#ifndef RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#define RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#include <cstdint>
#include <vulkan/vulkan_core.h>

// Will have to allocate room for the extension deps, so need the whole rule of 5
struct ExtensionDependencies
{
    ExtensionDependencies();
    ~ExtensionDependencies();
    ExtensionDependencies(const ExtensionDependencies&) = delete;
    ExtensionDependencies& operator=(const ExtensionDependencies&) = delete;
    ExtensionDependencies(ExtensionDependencies&&) noexcept;
    ExtensionDependencies& operator=(ExtensionDependencies&&) noexcept;

    uint32_t versionDep;
    uint32_t numExtensionDeps;
    const char** extensionDeps;
};

class ExtensionWrangler
{
public:
    ExtensionWrangler(
        const uint32_t _apiVersion,
        VkInstance _instance,
        VkPhysicalDevice _physicalDevice,
        VkDevice _logicalDevice);
    ~ExtensionWrangler() noexcept = default;

    bool IsExtensionSupported(const char* extensionName) const;
    bool ExtensionIsDeviceExtension(const char* extensionName) const;
    bool ExtensionIsInstanceExtension(const char* extensionName) const;
    bool ExtensionCoreInActiveVersion(const char* extensionName) const;

    ExtensionDependencies GetExtensionDependencies(const char* extensionName) const;

private:
    uint32_t apiVersion;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice logicalDevice;
};

#endif //!RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
