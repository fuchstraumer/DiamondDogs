#pragma once
#ifndef RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#define RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <memory>
#include <string_view>
#include <expected>
#include <vector>
#include <span>

// Will have to allocate room for the extension deps, so need the whole rule of 5
struct ExtensionDependencies
{
private:
    friend class ExtensionWrangler;
    ExtensionDependencies(uint32_t num_instance_extension_deps, uint32_t num_device_extension_deps);
public:
    ~ExtensionDependencies();
    ExtensionDependencies(const ExtensionDependencies&) = delete;
    ExtensionDependencies& operator=(const ExtensionDependencies&) = delete;
    ExtensionDependencies(ExtensionDependencies&&) noexcept;
    ExtensionDependencies& operator=(ExtensionDependencies&&) noexcept;

    bool IsOnlyVersionDependent() const
    {
        return NumInstanceExtensionDeps == 0 && NumDeviceExtensionDeps == 0 && VersionDep != 0;
    }

    uint32_t VersionDep;

    uint32_t NumInstanceExtensionDeps;
    const char** InstanceExtensionDeps;
    uint32_t NumDeviceExtensionDeps;
    const char** DeviceExtensionDeps;
};

class ExtensionWrangler
{
public:

    enum class DependencyError : uint8_t
    {
        Invalid,
        NoExtensionsProvided,
        ExtensionNotSupported,
        NoDependenciesForExtension,
        ApiVersionNotValid,
        ExtensionNotInDependencyTableForVersion,
        ExtensionNotInstanceOrDeviceExtension,
        CurrentApiVersionDoesNotSupportExtension
    };

    template<typename T>
    using Result = std::expected<T, DependencyError>;

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

    /** @brief Useful to know if a physical device has been attached already, or if it's not and we need it that we need to handle that. */
    [[nodiscard]] bool HasValidPhysicalDevice() const noexcept;
    /** @brief Set the physical device */
    void SetPhysicalDevice(VkPhysicalDevice physical_device) noexcept;
    /** @brief Simple query for is an extension is supported or not */
    [[nodiscard]] bool IsExtensionSupported(const std::string_view extension_name) const;
    /** @brief Simply query for if a set of extensions is supported or not */
    [[nodiscard]] bool AreExtensionsSupported(const size_t num_extensions, const std::string_view* extension_names) const;
    [[nodiscard]] bool ExtensionIsDeviceExtension(const std::string_view extension_name) const;
    [[nodiscard]] bool ExtensionIsInstanceExtension(const std::string_view extension_name) const;
    /** @brief If an extension is core in the current Vulkan API version in use in this environment, then it does not need to be explicitly enabled at all. */
    [[nodiscard]] bool ExtensionCoreInActiveVersion(const std::string_view extension_name) const;


    /** @brief Returns an instance of the ExtensionDependencies struct containing all required dependencies for the specified extension. */
    [[nodiscard]] Result<ExtensionDependencies> GetExtensionDependencies(const std::string_view extension_name) const;
    /** @brief Returns a deduplicated instance of ExtensionDependencies with all required dependencies for the specified set of extensions (i.e, the common set of required dependencies) */
    [[nodiscard]] Result<ExtensionDependencies> GetExtensionDependencies(std::span<std::string_view> extension_names) const;

private:

    [[nodiscard]] Result<ExtensionDependencies>
        getExtensionDependenciesInternal(const std::string_view extension_name, const size_t extension_index) const;

    uint32_t apiVersion;
    VkPhysicalDevice physicalDevice;

    std::unique_ptr<struct DependencyCache> dependencyCache;

};

#endif //!RENDERING_CONTEXT_EXTENSION_WRANGLER_HPP
