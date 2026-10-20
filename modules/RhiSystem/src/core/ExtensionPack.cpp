#include "ExtensionPack.hpp"
#include "ExtensionWrangler.hpp"
#include <stdexcept>
#include <algorithm>

namespace
{
    std::vector<std::string_view> CollectSupportedRequestedExtensions(
        const ExtensionWrangler& wrangler,
        const std::vector<std::string>& required,
        const std::vector<std::string>& optional);
    std::vector<std::string> MergeExtensionsWithDependencies(
        std::span<const std::string_view> requested,
        uint32_t dependency_count,
        const char* const* dependencies);
    std::vector<const char*> PointersToNames(const std::vector<std::string>& names);
}

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
        enabledInstanceExtNames{ std::move(other.enabledInstanceExtNames) },
        enabledDeviceExtNames{ std::move(other.enabledDeviceExtNames) },
        coreDeviceExtNames{ std::move(other.coreDeviceExtNames) },
        instanceExtensions{ std::move(other.instanceExtensions) },
        deviceExtensions{ std::move(other.deviceExtensions) },
        deviceFeatures{ other.deviceFeatures },
        extensionWrangler{ std::move(other.extensionWrangler) },
        instanceExtensionsResolved{ other.instanceExtensionsResolved },
        deviceExtensionsResolved{ other.deviceExtensionsResolved }
    {
        other.deviceFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr };
        other.instanceExtensionsResolved = false;
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
            enabledInstanceExtNames = std::move(other.enabledInstanceExtNames);
            enabledDeviceExtNames = std::move(other.enabledDeviceExtNames);
            coreDeviceExtNames = std::move(other.coreDeviceExtNames);
            instanceExtensions = std::move(other.instanceExtensions);
            deviceExtensions = std::move(other.deviceExtensions);
            deviceFeatures = other.deviceFeatures;
            extensionWrangler = std::move(other.extensionWrangler);
            instanceExtensionsResolved = other.instanceExtensionsResolved;
            deviceExtensionsResolved = other.deviceExtensionsResolved;

            other.deviceFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr };
            other.instanceExtensionsResolved = false;
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

        std::vector<std::string_view> requested =
            CollectSupportedRequestedExtensions(*extensionWrangler, requiredInstanceExts, optionalInstanceExts);
        if (requested.empty())
        {
            return;
        }

        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> deps =
            extensionWrangler->GetExtensionDependencies(std::span<std::string_view>(requested));

        // instance dependency failures were ignored before this change too: a required extension the
        // loader doesn't know still fails at vkCreateInstance, with a clearer message than ours
        const uint32_t dependencyCount = deps.has_value() ? deps->numInstanceExtensionDeps : 0u;
        const char* const* dependencies = deps.has_value() ? deps->instanceExtensionDeps : nullptr;
        enabledInstanceExtNames = MergeExtensionsWithDependencies(requested, dependencyCount, dependencies);
        instanceExtensions = PointersToNames(enabledInstanceExtNames);
        instanceExtensionsResolved = true;
    }

    void ExtensionPack::SetPhysicalDevice(VkPhysicalDevice physical_device) noexcept
    {
        // if already resolved or physical device already set, don't need to do this step
        if (deviceExtensionsResolved || (extensionWrangler && extensionWrangler->HasValidPhysicalDevice()))
        {
            return;
        }

        extensionWrangler->SetPhysicalDevice(physical_device);
    }

    void ExtensionPack::AddRequiredDeviceExtensions(std::vector<std::string> extensions)
    {
        requiredDeviceExts = std::move(extensions);
    }

    void ExtensionPack::AddOptionalDeviceExtensions(std::vector<std::string> extensions)
    {
        optionalDeviceExts = std::move(extensions);
    }

    void ExtensionPack::AddAllSupportedMaintenanceExtensions()
    {
        // maintenance1 to 3 are core in 1.1, and every later one needs 1.1. The generated dependency table does not
        // model a version requirement above the table's own version (see docs/audit.md), so check it here
        if (!extensionWrangler || !extensionWrangler->HasValidPhysicalDevice() || GetVulkanApiVersion() < VK_API_VERSION_1_1)
        {
            return;
        }

        const std::vector<std::string_view> maintenanceExts =
            extensionWrangler->GetSupportedExtensionsByPrefix("VK_KHR_maintenance");

        for (const std::string_view ext : maintenanceExts)
        {
            const bool alreadyRequired =
                std::find(requiredDeviceExts.begin(), requiredDeviceExts.end(), ext) != requiredDeviceExts.end();
            const bool alreadyOptional =
                std::find(optionalDeviceExts.begin(), optionalDeviceExts.end(), ext) != optionalDeviceExts.end();

            if (!alreadyRequired && !alreadyOptional && MaintenanceDependenciesSatisfied(ext))
            {
                optionalDeviceExts.emplace_back(ext);
            }
        }
    }

    bool ExtensionPack::MaintenanceDependenciesSatisfied(std::string_view extension_name) const
    {
        // the instance already exists when maintenance extensions are gathered, so an extension that needs an
        // instance extension we didn't enable, or a newer API version, can't be enabled. A 1.0 instance hit both:
        // maintenance9 to 11 need VK_KHR_get_physical_device_properties2, and maintenance5 needs 1.1
        std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> deps =
            extensionWrangler->GetExtensionDependencies(extension_name);
        if (!deps.has_value())
        {
            return deps.error() == ExtensionWrangler::DependencyError::NoDependenciesForExtension;
        }
        for (uint32_t i = 0; i < deps->numInstanceExtensionDeps; ++i)
        {
            const std::string_view instanceDependency{ deps->instanceExtensionDeps[i] };
            if (std::ranges::find(enabledInstanceExtNames, instanceDependency) == enabledInstanceExtNames.end())
            {
                return false;
            }
        }
        return true;
    }

    void ExtensionPack::ResolveDeviceDependencies()
    {
        if (!extensionWrangler)
        {
            return;
        }

        // Unsupported optional extensions are dropped before dependency resolution. The wrangler rejects any
        // name it can't find, so one unknown optional extension used to throw here and fail the whole device.
        std::vector<std::string_view> requested =
            CollectSupportedRequestedExtensions(*extensionWrangler, requiredDeviceExts, optionalDeviceExts);
        if (requested.empty())
        {
            return;
        }

        std::span<std::string_view> requestedSpan(requested);

        // the dependency table lists direct dependencies only, so resolve to a fixed point: maintenance5 pulls in
        // VK_KHR_dynamic_rendering on 1.1, and that needs VK_KHR_depth_stencil_resolve in turn. The pass limit
        // only guards against a cycle in generated data; real chains are two or three deep
        constexpr int32_t k_MaxDependencyPasses = 8;
        std::vector<std::string> mergedNames = MergeExtensionsWithDependencies(requested, 0u, nullptr);
        for (int32_t pass = 0; pass < k_MaxDependencyPasses; ++pass)
        {
            std::vector<std::string_view> currentNames(mergedNames.begin(), mergedNames.end());
            std::expected<ExtensionDependencies, ExtensionWrangler::DependencyError> deps =
                extensionWrangler->GetExtensionDependencies(std::span<std::string_view>(currentNames));
            if (!deps.has_value() && deps.error() != ExtensionWrangler::DependencyError::NoDependenciesForExtension)
            {
                throw std::runtime_error("Failed to resolve device extension dependencies: " + std::to_string(static_cast<int>(deps.error())));
            }

            const uint32_t dependencyCount = deps.has_value() ? deps->numDeviceExtensionDeps : 0u;
            const char* const* dependencies = deps.has_value() ? deps->deviceExtensionDeps : nullptr;
            std::vector<std::string> nextNames = MergeExtensionsWithDependencies(currentNames, dependencyCount, dependencies);
            const bool reachedFixedPoint = nextNames.size() == mergedNames.size();
            mergedNames = std::move(nextNames);
            if (reachedFixedPoint)
            {
                break;
            }
        }

        // an extension that is core in the active version is not enabled by name: its functionality is already
        // there, and enabling a promoted extension makes the validation layer demand its pre-promotion dependencies
        // (VK_KHR_dynamic_rendering then needs VK_KHR_depth_stencil_resolve, even on 1.3). Device::HasExtension
        // still answers true for these, through GetCoreDeviceExtensions
        enabledDeviceExtNames.clear();
        coreDeviceExtNames.clear();
        for (std::string& name : mergedNames)
        {
            if (extensionWrangler->ExtensionCoreInActiveVersion(name))
            {
                coreDeviceExtNames.push_back(std::move(name));
            }
            else
            {
                enabledDeviceExtNames.push_back(std::move(name));
            }
        }
        deviceExtensions = PointersToNames(enabledDeviceExtNames);

        // the feature chain is built from the same filtered list: chaining the feature struct of an extension
        // we don't enable is a validation error
        std::expected<VkPhysicalDeviceFeatures2, ExtensionWrangler::DependencyError> features =
            extensionWrangler->GetExtensionFeatures(requestedSpan,
                                                    ExtensionWrangler::GetVersionFeatures::True,
                                                    ExtensionWrangler::CollectDependencies::False);

        if (features.has_value())
        {
            deviceFeatures = *features;
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

    const std::vector<std::string>& ExtensionPack::GetCoreDeviceExtensions() const noexcept
    {
        return coreDeviceExtNames;
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

namespace
{
    std::vector<std::string_view> CollectSupportedRequestedExtensions(
        const ExtensionWrangler& wrangler,
        const std::vector<std::string>& required,
        const std::vector<std::string>& optional)
    {
        std::vector<std::string_view> requested;
        requested.reserve(required.size() + optional.size());
        // required extensions are kept even when unsupported, so that device or instance creation fails
        // loudly and names the extension, rather than us silently creating something the caller didn't ask for
        requested.insert(requested.end(), required.begin(), required.end());
        for (const std::string& ext : optional)
        {
            if (wrangler.IsExtensionSupported(ext))
            {
                requested.emplace_back(ext);
            }
        }
        return requested;
    }

    std::vector<std::string> MergeExtensionsWithDependencies(
        std::span<const std::string_view> requested,
        uint32_t dependency_count,
        const char* const* dependencies)
    {
        std::vector<std::string> merged;
        merged.reserve(requested.size() + dependency_count);
        merged.insert(merged.end(), requested.begin(), requested.end());
        for (uint32_t i = 0; i < dependency_count; ++i)
        {
            merged.emplace_back(dependencies[i]);
        }
        std::ranges::sort(merged);
        const auto [firstDuplicate, last] = std::ranges::unique(merged);
        merged.erase(firstDuplicate, last);
        return merged;
    }

    std::vector<const char*> PointersToNames(const std::vector<std::string>& names)
    {
        std::vector<const char*> pointers;
        pointers.reserve(names.size());
        for (const std::string& name : names)
        {
            pointers.push_back(name.c_str());
        }
        return pointers;
    }
}
