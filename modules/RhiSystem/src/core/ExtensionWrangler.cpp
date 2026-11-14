#include "ExtensionWrangler.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
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
static_assert(std::ranges::is_sorted(extensionIndexLookupTable, {}, &ExtensionIndexEntry::Name),
              "extensionIndexLookupTable must be sorted by Name for binary search");
static_assert(std::ranges::is_sorted(extensionDependencyTable, {}, &VersionDependencySlice::ApiVersion),
              "extensionDependencyTable must be sorted by ApiVersion for binary search");
static_assert(std::ranges::is_sorted(promotedExtensionsTable, {}, &PromotedVersionSlice::ApiVersion),
              "promotedExtensionsTable must be sorted by ApiVersion");

namespace
{
    std::string_view ExtensionNameOf(const VkExtensionProperties& properties) noexcept;
    const char* StringViewData(std::string_view view) noexcept;
    [[nodiscard]] const size_t* FindExtensionIndex(std::string_view extension_name) noexcept;
    [[nodiscard]] const VersionDependencySlice* FindVersionDependencySlice(uint32_t api_version) noexcept;
    [[nodiscard]] const ExtensionDependencyRange* FindExtensionDependencyRange(
        const VersionDependencySlice& slice,
        size_t extension_index) noexcept;
}

//NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
constexpr std::array<uint32_t, 5> k_BuiltInVkVersions
{
    VK_API_VERSION_1_0,
    VK_API_VERSION_1_1,
    VK_API_VERSION_1_2,
    VK_API_VERSION_1_3,
    VK_API_VERSION_1_4
};
static_assert(std::ranges::is_sorted(k_BuiltInVkVersions));

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

bool ExtensionDependencies::IsOnlyVersionDependent() const noexcept
{
    return InstanceExtensionDeps.empty() && DeviceExtensionDeps.empty() && VersionDep != 0;
}

ExtensionWrangler::ExtensionWrangler(
    const uint32_t api_version,
    VkPhysicalDevice physical_device) :
    apiVersion(api_version),
    physicalDevice(physical_device),
    dependencyCache(std::make_unique<DependencyCache>(physical_device))
{
    // Find the highest API version that is <= the input API version
    // upper_bound returns iterator to first element > apiVersion, so we want the element before that
    auto upperBoundIt = std::ranges::upper_bound(k_BuiltInVkVersions, apiVersion);

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
    const size_t* const extensionIndex = FindExtensionIndex(extension_name);

    if (extensionIndex == nullptr)
    {
        return false;
    }

    return std::ranges::binary_search(deviceExtensionTable, *extensionIndex);
}

bool ExtensionWrangler::ExtensionIsInstanceExtension(const std::string_view extension_name) const
{
    const size_t* const extensionIndex = FindExtensionIndex(extension_name);

    if (extensionIndex == nullptr)
    {
        return false;
    }

    return std::ranges::binary_search(instanceExtensionTable, *extensionIndex);
}

bool ExtensionWrangler::ExtensionCoreInActiveVersion(const std::string_view extension_name) const
{
    const size_t* const extensionIndex = FindExtensionIndex(extension_name);

    if (extensionIndex == nullptr)
    {
        return false;
    }

    for (const auto& slice : promotedExtensionsTable)
    {
        if (slice.ApiVersion > apiVersion)
        {
            break;
        }

        const std::span<const size_t> subspan{
            promotedExtensionIndices.data() + slice.Offset,
            slice.Count
        };

        if (std::ranges::binary_search(subspan, *extensionIndex))
        {
            return true;
        }
    }

    return false;
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::GetExtensionDependencies(const std::string_view extension_name) const
{
    if (ExtensionCoreInActiveVersion(extension_name))
    {
        return ExtensionDependencies{ .VersionDep = apiVersion };
    }
    else if (ExtensionIsInstanceExtension(extension_name))
    {
        auto& supportedInstanceExtensions = dependencyCache->InstanceExtensionDependencies;

        if (!std::ranges::contains(supportedInstanceExtensions, extension_name, ExtensionNameOf))
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotSupported);
        }

        // found the extension, now get its dependencies
        const size_t* const extensionIndex = FindExtensionIndex(extension_name);

        if (extensionIndex == nullptr)
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotInstanceOrDeviceExtension);
        }

        return getExtensionDependenciesInternal(extension_name, *extensionIndex);
    }
    else if (physicalDevice != VK_NULL_HANDLE && ExtensionIsDeviceExtension(extension_name))
    {
        // if extension is device extension, we need to check if the physical device supports it
        auto& supportedDeviceExtensions = dependencyCache->DeviceExtensionDependencies;

        if (!std::ranges::contains(supportedDeviceExtensions, extension_name, ExtensionNameOf))
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotSupported);
        }

        const size_t* const extensionIndex = FindExtensionIndex(extension_name);

        if (extensionIndex == nullptr)
        {
            return std::unexpected<DependencyError>(DependencyError::ExtensionNotInstanceOrDeviceExtension);
        }

        return getExtensionDependenciesInternal(extension_name, *extensionIndex);
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
        uniqueInstanceDependencies.insert_range(dependencies->InstanceExtensionDeps);
        uniqueDeviceDependencies.insert_range(dependencies->DeviceExtensionDeps);
    }

    return ExtensionDependencies
    {
        .VersionDep = apiVersion,
        .InstanceExtensionDeps = uniqueInstanceDependencies | std::views::transform(StringViewData) | std::ranges::to<std::vector>(),
        .DeviceExtensionDeps = uniqueDeviceDependencies | std::views::transform(StringViewData) | std::ranges::to<std::vector>(),
    };
}

std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> ExtensionWrangler::getExtensionDependenciesInternal(
    const std::string_view extension_name,
    const size_t extension_index) const
{
    const VersionDependencySlice* versionSlice = FindVersionDependencySlice(apiVersion);

    if (versionSlice == nullptr)
    {
        return std::unexpected<DependencyError>(DependencyError::ApiVersionNotValid);
    }

    const ExtensionDependencyRange* range = FindExtensionDependencyRange(*versionSlice, extension_index);

    if (range == nullptr && apiVersion != 0u)
    {
        const VersionDependencySlice* anyVersionSlice = FindVersionDependencySlice(0u);

        if (anyVersionSlice != nullptr)
        {
            range = FindExtensionDependencyRange(*anyVersionSlice, extension_index);
        }
    }

    if (range == nullptr)
    {
        return std::unexpected<DependencyError>(
            DependencyError::ExtensionNotInDependencyTableForVersion);
    }

    if (range->Count == 0u)
    {
        return std::unexpected<DependencyError>(DependencyError::NoDependenciesForExtension);
    }

    std::vector<std::string_view> instanceDeps;
    std::vector<std::string_view> deviceDeps;

    const std::span<const size_t> targets{
        extensionDependencyTargets.data() + range->Offset,
        range->Count
    };

    for (const size_t dependencyIndex : targets)
    {
        if (std::ranges::binary_search(k_BuiltInVkVersions, static_cast<uint32_t>(dependencyIndex)))
        {
            if (apiVersion >= dependencyIndex)
            {
                continue;
            }
            else
            {
                return std::unexpected<DependencyError>(
                    DependencyError::CurrentApiVersionDoesNotSupportExtension);
            }
        }
        else
        {
            const std::string_view dependencyName = masterExtensionNameTable[dependencyIndex];

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
                return std::unexpected<DependencyError>(
                    DependencyError::ExtensionNotInstanceOrDeviceExtension);
            }
        }
    }

    return ExtensionDependencies
    {
        .VersionDep = apiVersion,
        .InstanceExtensionDeps = instanceDeps | std::views::transform(StringViewData) | std::ranges::to<std::vector>(),
        .DeviceExtensionDeps = deviceDeps | std::views::transform(StringViewData) | std::ranges::to<std::vector>(),
    };
}

namespace
{

    std::string_view ExtensionNameOf(const VkExtensionProperties& properties) noexcept
    {
        return std::string_view{ properties.extensionName };
    }

    const char* StringViewData(const std::string_view view) noexcept
    {
        return view.data();
    }

    const size_t* FindExtensionIndex(std::string_view extension_name) noexcept
    {
        const auto found = std::ranges::lower_bound(
            extensionIndexLookupTable,
            extension_name,
            {},
            &ExtensionIndexEntry::Name);

        if (found != extensionIndexLookupTable.end() && found->Name == extension_name)
        {
            return &found->Index;
        }

        return nullptr;
    }

    const VersionDependencySlice* FindVersionDependencySlice(uint32_t api_version) noexcept
    {
        const auto found = std::ranges::lower_bound(
            extensionDependencyTable,
            api_version,
            {},
            &VersionDependencySlice::ApiVersion);

        if (found != extensionDependencyTable.end() && found->ApiVersion == api_version)
        {
            return &*found;
        }

        return nullptr;
    }

    const ExtensionDependencyRange* FindExtensionDependencyRange(
        const VersionDependencySlice& slice,
        size_t extension_index) noexcept
    {
        const std::span<const ExtensionDependencyRange> subspan{
            extensionDependencyEntries.data() + slice.EntryOffset,
            slice.EntryCount
        };
        const auto found = std::ranges::lower_bound(
            subspan,
            extension_index,
            {},
            &ExtensionDependencyRange::ExtensionIndex);

        if (found != subspan.end() && found->ExtensionIndex == extension_index)
        {
            return &*found;
        }

        return nullptr;
    }

}
//NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
