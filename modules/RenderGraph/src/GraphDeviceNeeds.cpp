#include "GraphDeviceNeeds.hpp"
#include "DeviceCapability.hpp"
#include "DeviceRequirements.hpp"
#include "GraphBuilder.hpp"
#include "Records.hpp"
#include "compile/ChoosePaths.hpp"
#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace loom
{

namespace
{

    using bootstrap::DeviceCapability;
    using bootstrap::EntryPointRef;

    // The capabilities and entry points of one group of passes: everything outside the lists and the fallback paths,
    // or the higher paths
    struct PassNeeds
    {
        std::vector<DeviceCapability> Capabilities;
        std::vector<EntryPointRef> EntryPoints;
    };

    void AppendEntryPoint(std::vector<EntryPointRef>& entry_points, EntryPointRef entry_point)
    {
        if (entry_point.EntryPoint.empty())
        {
            return;
        }

        entry_points.push_back(entry_point);
    }

    void AppendPass(PassNeeds& needs, const PassRecord& pass, std::string_view bundle_file)
    {
        AppendStatementCapabilities(pass, needs.Capabilities);

        for (const ShaderRecord& shader : pass.Shaders)
        {
            AppendEntryPoint(needs.EntryPoints, EntryPointRef
            {
                .BundleFile = bundle_file,
                .ShaderModule = shader.ModuleName,
                .EntryPoint = shader.EntryPoint,
            });

            AppendEntryPoint(needs.EntryPoints, EntryPointRef
            {
                .BundleFile = bundle_file,
                .ShaderModule = shader.ModuleName,
                .EntryPoint = shader.FragmentEntryPoint,
            });
        }

    }

    void AppendPasses(PassNeeds& needs, std::span<const PassRecord> passes, std::string_view bundle_file)
    {
        for (const PassRecord& pass : passes)
        {
            AppendPass(needs, pass, bundle_file);
        }

    }

    bool SameEntryPoint(const EntryPointRef& left, const EntryPointRef& right)
    {
        return left.ShaderModule == right.ShaderModule && left.EntryPoint == right.EntryPoint;
    }

    bool HasEntryPoint(std::span<const EntryPointRef> entry_points, const EntryPointRef& entry_point)
    {
        const auto isSame = [&entry_point](const EntryPointRef& other)
        {
            return SameEntryPoint(other, entry_point);
        };

        return std::ranges::any_of(entry_points, isSame);
    }

    void SortUnique(std::vector<DeviceCapability>& capabilities)
    {
        std::ranges::sort(capabilities);
        const auto duplicates = std::ranges::unique(capabilities);
        capabilities.erase(duplicates.begin(), duplicates.end());
    }

}

    GraphDeviceNeeds GatherDeviceNeeds(const GraphBuilder& builder, std::string_view bundle_file)
    {
        const GraphRecord& record = builder.Record();
        PassNeeds required;
        PassNeeds wanted;

        AppendPasses(required, record.Passes, bundle_file);

        for (const SequenceRecord& sequence : record.Sequences)
        {
            AppendPasses(required, sequence.Passes, bundle_file);
        }

        for (const PathListRecord& list : record.PathLists)
        {
            for (size_t index = 0; index < list.Paths.size(); ++index)
            {
                const bool fallback = index + 1 == list.Paths.size();
                AppendPasses(fallback ? required : wanted, list.Paths[index].Body.Passes, bundle_file);
            }

        }

        SortUnique(required.Capabilities);
        SortUnique(wanted.Capabilities);

        GraphDeviceNeeds needs;

        for (const DeviceCapability capability : required.Capabilities)
        {
            bootstrap::Need need = bootstrap::NeutralNeed(capability, bootstrap::NeedLevel::Required);
            need.Source.Module = record.Name;
            needs.Needs.push_back(need);
        }

        for (const DeviceCapability capability : wanted.Capabilities)
        {
            if (std::ranges::binary_search(required.Capabilities, capability))
            {
                continue;
            }

            bootstrap::Need need = bootstrap::NeutralNeed(capability, bootstrap::NeedLevel::Wanted);
            need.Source.Module = record.Name;
            needs.Needs.push_back(need);
        }

        // An entry point that a fallback or a pass outside the lists also runs stays Required
        for (const EntryPointRef& entry_point : wanted.EntryPoints)
        {
            if (HasEntryPoint(required.EntryPoints, entry_point) || HasEntryPoint(needs.WantedOnly, entry_point))
            {
                continue;
            }

            needs.WantedOnly.push_back(entry_point);
        }

        return needs;
    }

}
