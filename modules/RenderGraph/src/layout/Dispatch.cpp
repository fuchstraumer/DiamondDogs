#include "layout/LayoutContext.hpp"
#include "bind/BindContext.hpp"
#include "size/SizedGraph.hpp"
#include "Records.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestLayout.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// A dispatch states threads; the device runs groups. The group count on each axis is the thread count divided by the
// reflected workgroup size, rounded up, so the last group can hold threads past the end: the shader tests its bounds.
namespace loom::layout
{

namespace
{
    struct ThreadCount
    {
        int64_t X{ 0 };
        int64_t Y{ 0 };
        int64_t Z{ 0 };
    };

    int64_t GroupsFor(int64_t threads, uint32_t workgroup_size)
    {
        const int64_t size = static_cast<int64_t>(workgroup_size);
        return (threads + size - 1) / size;
    }

    // The threads of a dispatch, when Compile knows them. DispatchThreadsOver an image with no extent yet (sized from
    // a frame target, or an import of any extent) has none.
    std::optional<ThreadCount> ThreadsOf(const LayoutContext& context, const FrozenDispatch& dispatch)
    {
        if (dispatch.Form == DispatchForm::Threads)
        {
            return ThreadCount{ dispatch.X, dispatch.Y, dispatch.Z };
        }

        const ImageExtent& extent = context.Sized.Resources[dispatch.Resource.Value].Extent;
        const bool unknown = extent.Width == k_UnknownExtent || extent.Height == k_UnknownExtent ||
                             extent.Depth == k_UnknownExtent;
        if (unknown)
        {
            return std::nullopt;
        }

        return ThreadCount{ extent.Width, extent.Height, extent.Depth };
    }

    // Reports each axis whose count exceeds its limit
    void CheckGroupLimits(LayoutContext& context, const FrozenPass& pass, const ThreadCount& groups)
    {
        struct AxisLimit
        {
            std::string_view Name;
            uint32_t Limit{ 0u };
            int64_t Groups{ 0 };
        };

        const std::array<AxisLimit, 3u> axes
        {
            AxisLimit{ "MaxComputeWorkGroupCountX", context.Limits.MaxComputeWorkGroupCountX, groups.X },
            AxisLimit{ "MaxComputeWorkGroupCountY", context.Limits.MaxComputeWorkGroupCountY, groups.Y },
            AxisLimit{ "MaxComputeWorkGroupCountZ", context.Limits.MaxComputeWorkGroupCountZ, groups.Z },
        };

        for (const AxisLimit& axis : axes)
        {
            if (axis.Groups > static_cast<int64_t>(axis.Limit))
            {
                ReportLimit(context, pass.Key, KeyRef{}, pass.Dispatch.Where, axis.Name, axis.Limit, axis.Groups);
            }
        }
    }

}

    LaidOutDispatch LayOutDispatch(LayoutContext& context, const FrozenPass& pass, const ResolvedPass& resolved)
    {
        LaidOutDispatch result;
        const std::vector<lodestone::manifest::EntryPointInstanceView> instances = bind::PassInstances(resolved);
        result.Workgroup = instances.front().Workgroup();

        if (pass.Dispatch.Form == DispatchForm::Indirect)
        {
            result.Source = GroupCountSource::Indirect;
            return result;
        }

        const std::optional<ThreadCount> threads = ThreadsOf(context, pass.Dispatch);

        if (!threads.has_value())
        {
            result.Source = GroupCountSource::AtRealize;
            return result;
        }

        const ThreadCount groups
        {
            .X = GroupsFor(threads->X, result.Workgroup.X),
            .Y = GroupsFor(threads->Y, result.Workgroup.Y),
            .Z = GroupsFor(threads->Z, result.Workgroup.Z),
        };

        CheckGroupLimits(context, pass, groups);

        // a group count is never larger than its thread count, which is an int32_t
        result.Source = GroupCountSource::Known;
        result.Groups = GroupCount
        {
            .X = static_cast<int32_t>(groups.X),
            .Y = static_cast<int32_t>(groups.Y),
            .Z = static_cast<int32_t>(groups.Z),
        };

        return result;
    }

}
