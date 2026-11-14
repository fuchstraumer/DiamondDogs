#include "DeviceRequirements.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestDevice.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestLayout.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace manifest = lodestone::manifest;

    constexpr int32_t k_MaskWordBits = std::numeric_limits<uint64_t>::digits;

    [[nodiscard]] std::vector<bootstrap::Need>& NeedsOfKind(bootstrap::DeviceRequirements& requirements,
                                                            bootstrap::NeedKind kind);
    void AddNeed(bootstrap::DeviceRequirements& requirements, const bootstrap::Need& need);
    void AddNames(bootstrap::DeviceRequirements& requirements,
                  std::span<const std::string_view> names,
                  bootstrap::NeedKind kind,
                  bootstrap::NeedLevel level,
                  std::string_view module);
    void AddModuleNeeds(bootstrap::DeviceRequirements& requirements, const bootstrap::ModuleDescriptor& descriptor);
    void AddBundleNeeds(bootstrap::DeviceRequirements& requirements, const bootstrap::LoadedBundle& loaded);
    void AddEntryPointNeeds(bootstrap::DeviceRequirements& requirements,
                            const bootstrap::LoadedBundle& loaded,
                            const manifest::EnvironmentView& environment,
                            int32_t entry_point);
    [[nodiscard]] manifest::DeviceLimits InstanceLimits(const manifest::EntryPointInstanceView& instance) noexcept;
    // not actually smallest limits: one where each field is set to max, so KeepSmaller() sets values
    [[nodiscard]] manifest::DeviceLimits SmallestLimits() noexcept;
    void KeepSmaller(manifest::DeviceLimits& kept, const manifest::DeviceLimits& limits) noexcept;
    void KeepLarger(manifest::DeviceLimits& kept, const manifest::DeviceLimits& limits) noexcept;
}

namespace bootstrap
{
    DeviceRequirements GatherDeviceRequirements(const ContentStage& content, std::span<const Need> spine_needs)
    {
        DeviceRequirements requirements;

        for (const Need& need : spine_needs)
        {
            AddNeed(requirements, need);
        }

        for (const ModuleDescriptor* descriptor : content.Core().Launch().Selected())
        {
            AddModuleNeeds(requirements, *descriptor);
        }

        for (const LoadedBundle& loaded : content.Bundles())
        {
            AddBundleNeeds(requirements, loaded);
        }

        return requirements;
    }

    std::vector<Need> FeatureNeeds(std::span<const std::string_view> features, NeedLevel level)
    {
        std::vector<Need> needs;

        needs.reserve(features.size());

        for (const std::string_view feature : features)
        {
            needs.push_back(Need{ .Name = feature, .Kind = NeedKind::DeviceFeature, .Level = level });
        }

        return needs;
    }

    std::string_view ToString(NeedKind kind) noexcept
    {
        switch (kind)
        {
        case NeedKind::InstanceExtension:
            return "InstanceExtension";
        case NeedKind::DeviceExtension:
            return "DeviceExtension";
        case NeedKind::DeviceFeature:
            return "DeviceFeature";
        case NeedKind::Capability:
            return "Capability";
        default:
            return "Invalid";
        }
    }

    std::string_view ToString(NeedLevel level) noexcept
    {
        switch (level)
        {
        case NeedLevel::Required:
            return "Required";
        case NeedLevel::Wanted:
            return "Wanted";
        default:
            return "Invalid";
        }
    }
}

namespace
{
    std::vector<bootstrap::Need>& NeedsOfKind(bootstrap::DeviceRequirements& requirements, bootstrap::NeedKind kind)
    {
        switch (kind)
        {
        case bootstrap::NeedKind::InstanceExtension:
            return requirements.InstanceExtensions;
        case bootstrap::NeedKind::DeviceExtension:
            return requirements.DeviceExtensions;
        case bootstrap::NeedKind::DeviceFeature:
            return requirements.DeviceFeatures;
        default:
            return requirements.Capabilities;
        }
    }

    void AddNeed(bootstrap::DeviceRequirements& requirements, const bootstrap::Need& need)
    {
        assert(need.Kind != bootstrap::NeedKind::Invalid && "a need must set its kind");
        assert(need.Level != bootstrap::NeedLevel::Invalid && "a need must set its level");

        std::vector<bootstrap::Need>& needs = NeedsOfKind(requirements, need.Kind);
        // tens of names per kind, so a linear search is cheaper than a map
        const auto found = std::ranges::find(needs, need.Name, &bootstrap::Need::Name);

        if (found == needs.end())
        {
            needs.push_back(need);
            return;
        }

        // Required wins. A Wanted entry that becomes Required takes the source of the first Required need
        if (found->Level == bootstrap::NeedLevel::Wanted && need.Level == bootstrap::NeedLevel::Required)
        {
            found->Level = bootstrap::NeedLevel::Required;
            found->Source = need.Source;
        }
    }

    void AddNames(bootstrap::DeviceRequirements& requirements,
                  std::span<const std::string_view> names,
                  bootstrap::NeedKind kind,
                  bootstrap::NeedLevel level,
                  std::string_view module)
    {
        for (const std::string_view name : names)
        {
            AddNeed(requirements, bootstrap::Need
            {
                .Name = name,
                .Kind = kind,
                .Level = level,
                .Source = bootstrap::NeedSource{ .Module = module },
            });
        }
    }

    void AddModuleNeeds(bootstrap::DeviceRequirements& requirements, const bootstrap::ModuleDescriptor& descriptor)
    {
        using bootstrap::NeedKind;
        using bootstrap::NeedLevel;

        const bootstrap::ModuleNeeds& needs = descriptor.Needs;
        const std::string_view module = descriptor.Name;

        AddNames(requirements, needs.RequiredInstanceExtensions, NeedKind::InstanceExtension, NeedLevel::Required, module);
        AddNames(requirements, needs.WantedInstanceExtensions, NeedKind::InstanceExtension, NeedLevel::Wanted, module);
        AddNames(requirements, needs.RequiredDeviceExtensions, NeedKind::DeviceExtension, NeedLevel::Required, module);
        AddNames(requirements, needs.WantedDeviceExtensions, NeedKind::DeviceExtension, NeedLevel::Wanted, module);
        AddNames(requirements, needs.RequiredCapabilities, NeedKind::Capability, NeedLevel::Required, module);
        AddNames(requirements, needs.WantedCapabilities, NeedKind::Capability, NeedLevel::Wanted, module);
    }

    void AddBundleNeeds(bootstrap::DeviceRequirements& requirements, const bootstrap::LoadedBundle& loaded)
    {
        for (const std::string_view moduleName : loaded.Bundle.ModuleNames())
        {
            const rhi::ShaderBundleResult<const manifest::EnvironmentView*> environment =
                loaded.Bundle.ModuleEnvironment(moduleName);
            // the name came from the bundle itself
            assert(environment.has_value());

            const int32_t entryPointCount = static_cast<int32_t>(std::ssize((*environment)->Module().EntryPoints()));

            for (int32_t entryPoint = 0; entryPoint < entryPointCount; ++entryPoint)
            {
                AddEntryPointNeeds(requirements, loaded, **environment, entryPoint);
            }
        }
    }

    void AddEntryPointNeeds(bootstrap::DeviceRequirements& requirements,
                            const bootstrap::LoadedBundle& loaded,
                            const manifest::EnvironmentView& environment,
                            int32_t entry_point)
    {
        const int32_t variantCount = static_cast<int32_t>(std::ssize(environment.Variants()));

        if (variantCount == 0)
        {
            return;
        }

        // each capability name corresponds to a bit in the capability bitmask, and we set
        // the word count of that based on how many are used to represent the full set
        const size_t wordCount = static_cast<size_t>(environment.CapabilityMaskWordCount());
        constexpr int32_t k_StackMaskWords = 4;
        // I'm asserting here because this issue should come up in debug builds first: it suggests
        // we have 256 unique capabilities in our manifest! That's an unusually high number and 
        // I don't even know if that's strictly *possible*, since so many of the caps are aliases
        // (a full ~50ish are just OpenCL capabilities!)
        assert(std::cmp_less(wordCount, k_StackMaskWords));
        std::array<uint64_t, k_StackMaskWords> stackEvery{ 0u, 0u, 0u, 0u };
        std::array<uint64_t, k_StackMaskWords> stackAny{ 0u, 0u, 0u, 0u };
        std::span<uint64_t> every;
        std::span<uint64_t> any;

        // we have to use two-param construct because wordCount may be less than k_StackMaskWords
        // (and in most cases it is)
        every = std::span<uint64_t>{ stackEvery.data(), wordCount };
        any = std::span<uint64_t>{ stackAny.data(), wordCount };
        
        manifest::DeviceLimits smallest = SmallestLimits();

        for (int32_t variant = 0; variant < variantCount; ++variant)
        {
            const manifest::VariantView variantView = environment.VariantAt(static_cast<uint32_t>(variant));
            const manifest::EntryPointInstanceView instance = variantView.EntryPoint(static_cast<uint32_t>(entry_point));
            // each entry point instance stores it's caps as a list of bitmasks, where each bit represents
            // a specific capability in the capabilities table
            const std::span<const uint64_t> required = instance.RequiredCapabilities();

            // we build the every/any sets separately so that we can track which capabilities are required by all variants
            // versus any variant: if we don't support `every` we can't run. if we don't support `any`, we can find fallbacks
            // at bind/graph build time by using the lodestone query selection filters
            for (int32_t word = 0; std::cmp_less(word, wordCount); ++word)
            {
                every[static_cast<size_t>(word)] &= required[static_cast<size_t>(word)];
                any[static_cast<size_t>(word)] |= required[static_cast<size_t>(word)];
            }

            // get the memory footprint and threadgroup sizing of this particular entry point:
            // then use it to find the smallest and largest req limits across all variants
            // (this is especially useful on webgpu, where we are expected to set these limits during
            //  context init!)
            const manifest::DeviceLimits limits = InstanceLimits(instance);

            KeepSmaller(smallest, limits);
            KeepLarger(requirements.WantedLimits, limits);
        }

        KeepLarger(requirements.RequiredLimits, smallest);

        const manifest::ModuleView& module = environment.Module();
        const manifest::EntryPoint& entryPointRecord = module.EntryPoints()[static_cast<size_t>(entry_point)];
        const bootstrap::NeedSource source
        {
            .Module = loaded.RequestedBy,
            .BundleFile = loaded.FileName,
            .ShaderModule = module.Name(),
            .EntryPoint = module.Bundle().String(entryPointRecord.NameString),
        };

        // Now translate the bitsets into actual capability names that we can query and act on 
        // instead of just the lodestone-local indices into the capability table
        for (int32_t word = 0; std::cmp_less(word, wordCount); ++word)
        {
            uint64_t remaining = any[static_cast<size_t>(word)];

            while (remaining != 0u)
            {
                const int32_t bit = std::countr_zero(remaining);
                const uint64_t bitMask = uint64_t{ 1 } << bit;
                const int32_t capability = (word * k_MaskWordBits) + bit;
                const bool everyVariant = (every[static_cast<size_t>(word)] & bitMask) != 0u;

                AddNeed(requirements, bootstrap::Need
                {
                    .Name = environment.CapabilityName(static_cast<uint32_t>(capability)),
                    .Kind = bootstrap::NeedKind::Capability,
                    .Level = everyVariant ? bootstrap::NeedLevel::Required : bootstrap::NeedLevel::Wanted,
                    .Source = source,
                });

                remaining &= ~bitMask;
            }
        }
    }

    manifest::DeviceLimits InstanceLimits(const manifest::EntryPointInstanceView& instance) noexcept
    {
        manifest::DeviceLimits limits{ .MaxWorkgroupMemoryBytes = instance.WorkgroupMemoryBytes() };

        if (instance.Stage() == lodestone::ShaderStageKind::Compute)
        {
            const lodestone::WorkgroupSize size = instance.Workgroup();
            const uint64_t invocations = uint64_t{ size.X } * size.Y * size.Z;

            limits.MaxInvocationsPerWorkgroup = static_cast<uint32_t>(
                std::min<uint64_t>(invocations, std::numeric_limits<uint32_t>::max()));
            limits.MaxWorkgroupSizeX = size.X;
            limits.MaxWorkgroupSizeY = size.Y;
            limits.MaxWorkgroupSizeZ = size.Z;
        }

        const manifest::EntryPointConstantsView constants = instance.Constants();

        if (constants.Delivery() == lodestone::EntryPointConstantsDelivery::PushConstant)
        {
            limits.MaxPushConstantBytes = constants.ByteSize();
        }

        return limits;
    }

    manifest::DeviceLimits SmallestLimits() noexcept
    {
        constexpr uint32_t k_Largest = std::numeric_limits<uint32_t>::max();

        return manifest::DeviceLimits
        {
            .MaxWorkgroupMemoryBytes = k_Largest,
            .MaxInvocationsPerWorkgroup = k_Largest,
            .MaxWorkgroupSizeX = k_Largest,
            .MaxWorkgroupSizeY = k_Largest,
            .MaxWorkgroupSizeZ = k_Largest,
            .MaxPushConstantBytes = k_Largest,
        };
    }

    void KeepSmaller(manifest::DeviceLimits& kept, const manifest::DeviceLimits& limits) noexcept
    {
        kept.MaxWorkgroupMemoryBytes = std::min(kept.MaxWorkgroupMemoryBytes, limits.MaxWorkgroupMemoryBytes);
        kept.MaxInvocationsPerWorkgroup = std::min(kept.MaxInvocationsPerWorkgroup, limits.MaxInvocationsPerWorkgroup);
        kept.MaxWorkgroupSizeX = std::min(kept.MaxWorkgroupSizeX, limits.MaxWorkgroupSizeX);
        kept.MaxWorkgroupSizeY = std::min(kept.MaxWorkgroupSizeY, limits.MaxWorkgroupSizeY);
        kept.MaxWorkgroupSizeZ = std::min(kept.MaxWorkgroupSizeZ, limits.MaxWorkgroupSizeZ);
        kept.MaxPushConstantBytes = std::min(kept.MaxPushConstantBytes, limits.MaxPushConstantBytes);
    }

    void KeepLarger(manifest::DeviceLimits& kept, const manifest::DeviceLimits& limits) noexcept
    {
        kept.MaxWorkgroupMemoryBytes = std::max(kept.MaxWorkgroupMemoryBytes, limits.MaxWorkgroupMemoryBytes);
        kept.MaxInvocationsPerWorkgroup = std::max(kept.MaxInvocationsPerWorkgroup, limits.MaxInvocationsPerWorkgroup);
        kept.MaxWorkgroupSizeX = std::max(kept.MaxWorkgroupSizeX, limits.MaxWorkgroupSizeX);
        kept.MaxWorkgroupSizeY = std::max(kept.MaxWorkgroupSizeY, limits.MaxWorkgroupSizeY);
        kept.MaxWorkgroupSizeZ = std::max(kept.MaxWorkgroupSizeZ, limits.MaxWorkgroupSizeZ);
        kept.MaxPushConstantBytes = std::max(kept.MaxPushConstantBytes, limits.MaxPushConstantBytes);
    }
}
