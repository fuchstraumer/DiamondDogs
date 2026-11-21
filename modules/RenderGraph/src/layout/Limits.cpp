#include "LoomDeviceLimits.hpp"
#include "layout/LayoutContext.hpp"
#include "LoomKeys.hpp"
#include "PassStatements.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "layout/Limits.hpp"
#include "layout/Diagnostics.hpp"
#include "layout/LaidOutGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "Records.hpp"
#include "ResourceFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include "size/SizedGraph.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <utility>

// LM601 for the limits that a pass's pipeline meets: descriptor sets, descriptors per stage, color attachments and
// buffer ranges. An image's extent meets maxImageDimension. Only Bound slots are descriptors: a Handle or a Pointer is
// an 8-byte value in a buffer or in the push constants. An unbounded array (ArrayCount 0) is bindless, which has its
// own update-after-bind limits; it is not counted here.
namespace loom::layout
{

namespace
{
    // Descriptors of one stage, by VkPhysicalDeviceLimits' kinds
    struct StageCounts
    {
        int64_t Samplers{ 0 };
        int64_t UniformBuffers{ 0 };
        int64_t StorageBuffers{ 0 };
        int64_t SampledImages{ 0 };
        int64_t StorageImages{ 0 };
        int64_t InputAttachments{ 0 };
        int64_t ColorAttachments{ 0 };

        // maxPerStageResources counts every kind except samplers
        [[nodiscard]] int64_t Resources() const noexcept
        {
            return UniformBuffers + StorageBuffers + SampledImages + StorageImages + InputAttachments +
                   ColorAttachments;
        }
    };

    // A combined image sampler counts as one sampler and one sampled image. A texel buffer that the shader only reads
    // is a uniform texel buffer, which counts as a sampled image; one that it writes counts as a storage image.
    void Count(StageCounts& counts, const BindingSlot& slot)
    {
        const int64_t count = static_cast<int64_t>(slot.ArrayCount);

        switch (slot.Kind)
        {
        case lodestone::BindingKind::Sampler:
            counts.Samplers += count;
            break;
        case lodestone::BindingKind::CombinedTextureSampler:
            counts.Samplers += count;
            counts.SampledImages += count;
            break;
        case lodestone::BindingKind::Texture:
            counts.SampledImages += count;
            break;
        case lodestone::BindingKind::TexelBuffer:
            if (slot.Access == ResourceAccess::Read)
            {
                counts.SampledImages += count;
                break;
            }

            counts.StorageImages += count;
            break;
        case lodestone::BindingKind::UniformBuffer:
            [[fallthrough]];
        case lodestone::BindingKind::ParameterBlock:
            counts.UniformBuffers += count;
            break;
        case lodestone::BindingKind::StorageBuffer:
            counts.StorageBuffers += count;
            break;
        case lodestone::BindingKind::StorageTexture:
            counts.StorageImages += count;
            break;
        case lodestone::BindingKind::InputRenderTarget:
            counts.InputAttachments += count;
            break;
        default:
            break;
        }
    }

    bool IsDescriptor(const BindingSlot& slot)
    {
        return slot.Form == lodestone::PlacementKind::Bound && slot.ArrayCount > 0u;
    }

    bool HasStage(lodestone::ShaderStageKind stages, lodestone::ShaderStageKind stage)
    {
        return (static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0u;
    }

    // The stages that a dispatch or a draw can have
    constexpr std::array<lodestone::ShaderStageKind, 3u> k_Stages
    {
        lodestone::ShaderStageKind::Vertex,
        lodestone::ShaderStageKind::Fragment,
        lodestone::ShaderStageKind::Compute,
    };

    constexpr size_t k_FragmentStage = 1u;

    std::array<StageCounts, 3u> CountStages(const LayoutContext& context, const FrozenPass& pass,
                                            const BoundPass& bound)
    {
        std::array<StageCounts, 3u> counts{};

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            const BindingSlot& slot = context.Bound.Slots[slotIndex];

            if (!IsDescriptor(slot))
            {
                continue;
            }

            for (size_t stage = 0u; stage < k_Stages.size(); ++stage)
            {
                if (HasStage(slot.Stages, k_Stages[stage]))
                {
                    Count(counts[stage], slot);
                }
            }
        }

        // the fragment stage writes the color attachments
        counts[k_FragmentStage].ColorAttachments = static_cast<int64_t>(pass.Colors.size());
        return counts;
    }

    // The largest count over the stages, against one limit
    void CheckStageLimit(LayoutContext& context, const FrozenPass& pass, const std::array<StageCounts, 3u>& counts,
                         std::string_view limit_name, uint32_t limit, int64_t (*value_of)(const StageCounts&))
    {
        int64_t largest = 0;

        for (const StageCounts& stage : counts)
        {
            largest = std::max(largest, value_of(stage));
        }

        if (std::cmp_greater(largest ,limit))
        {
            ReportLimit(context, pass.Key, KeyRef{}, pass.Shader.Where, limit_name, limit, largest);
        }
    }

    int64_t SamplersOf(const StageCounts& counts)
    {
        return counts.Samplers;
    }

    int64_t UniformBuffersOf(const StageCounts& counts)
    {
        return counts.UniformBuffers;
    }

    int64_t StorageBuffersOf(const StageCounts& counts)
    {
        return counts.StorageBuffers;
    }

    int64_t SampledImagesOf(const StageCounts& counts)
    {
        return counts.SampledImages;
    }

    int64_t StorageImagesOf(const StageCounts& counts)
    {
        return counts.StorageImages;
    }

    int64_t ResourcesOf(const StageCounts& counts)
    {
        return counts.Resources();
    }

    void CheckDescriptorLimits(LayoutContext& context, const FrozenPass& pass, const BoundPass& bound)
    {
        const LayoutLimits& limits = context.Limits;
        const std::array<StageCounts, 3u> counts = CountStages(context, pass, bound);

        CheckStageLimit(context, pass, counts, "MaxPerStageDescriptorSamplers",
                        limits.MaxPerStageDescriptorSamplers, &SamplersOf);
        CheckStageLimit(context, pass, counts, "MaxPerStageDescriptorUniformBuffers",
                        limits.MaxPerStageDescriptorUniformBuffers, &UniformBuffersOf);
        CheckStageLimit(context, pass, counts, "MaxPerStageDescriptorStorageBuffers",
                        limits.MaxPerStageDescriptorStorageBuffers, &StorageBuffersOf);
        CheckStageLimit(context, pass, counts, "MaxPerStageDescriptorSampledImages",
                        limits.MaxPerStageDescriptorSampledImages, &SampledImagesOf);
        CheckStageLimit(context, pass, counts, "MaxPerStageDescriptorStorageImages",
                        limits.MaxPerStageDescriptorStorageImages, &StorageImagesOf);
        CheckStageLimit(context, pass, counts, "MaxPerStageResources", limits.MaxPerStageResources, &ResourcesOf);
    }

    // The highest group, plus one: a pipeline layout holds every set up to it
    void CheckDescriptorSets(LayoutContext& context, const FrozenPass& pass, const BoundPass& bound)
    {
        int64_t sets = 0;

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            const BindingSlot& slot = context.Bound.Slots[slotIndex];

            if (slot.Form == lodestone::PlacementKind::Bound)
            {
                sets = std::max(sets, static_cast<int64_t>(slot.Group) + 1);
            }
        }

        if (std::cmp_greater(sets ,context.Limits.MaxBoundDescriptorSets))
        {
            ReportLimit(context, pass.Key, KeyRef{}, pass.Shader.Where, "MaxBoundDescriptorSets",
                        context.Limits.MaxBoundDescriptorSets, sets);
        }
    }

    // The highest slot, plus one. Where is that slot's Color statement.
    void CheckColorAttachments(LayoutContext& context, const FrozenPass& pass)
    {
        if (pass.Colors.empty())
        {
            return;
        }

        const auto bySlot = [](const FrozenColorTarget& lhs, const FrozenColorTarget& rhs)
        {
            return lhs.Slot < rhs.Slot;
        };

        const FrozenColorTarget& highest = *std::ranges::max_element(pass.Colors, bySlot);
        const int64_t attachments = static_cast<int64_t>(highest.Slot) + 1;

        if (std::cmp_greater(attachments ,context.Limits.MaxColorAttachments))
        {
            ReportLimit(context, pass.Key, KeyRef{}, highest.Where, "MaxColorAttachments",
                        context.Limits.MaxColorAttachments, attachments);
        }
    }

    // A descriptor covers the whole buffer: Bind has no offset or range
    void CheckBufferRanges(LayoutContext& context, const FrozenPass& pass, const BoundPass& bound)
    {
        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            const BindingSlot& slot = context.Bound.Slots[slotIndex];
            const SlotSource& source = context.Bound.Sources[slotIndex];
            const bool uniform = slot.Kind == lodestone::BindingKind::UniformBuffer;
            const bool storage = slot.Kind == lodestone::BindingKind::StorageBuffer;

            // a slot that no bind names is already LM302
            if (!IsDescriptor(slot) || source.Bind < 0 || (!uniform && !storage))
            {
                continue;
            }

            // an imported buffer's size arrives with its future, and Realize checks it
            const int64_t bytes = context.Sized.Resources[slot.Resource.Value].ByteSize;
            const uint32_t limit = uniform ? context.Limits.MaxUniformBufferRange :
                                             context.Limits.MaxStorageBufferRange;
            if (bytes == k_UnknownByteSize || bytes <= static_cast<int64_t>(limit))
            {
                continue;
            }

            const FrozenBind& bind = pass.Binds[source.Bind];
            const std::string_view limitName = uniform ? "MaxUniformBufferRange" : "MaxStorageBufferRange";
            ReportLimit(context, pass.Key, context.Frozen.Resources[slot.Resource.Value].Key, bind.Where, limitName,
                        limit, bytes);
        }
    }

}

    void CheckPassLimits(LayoutContext& context, int32_t pass_index)
    {
        const FrozenPass& pass = context.Frozen.Passes[pass_index];
        const BoundPass& bound = context.Bound.Passes[pass_index];

        // a Record pass has no pipeline: its callback owns what it records
        if (pass.Kind == PassKind::Record)
        {
            return;
        }

        CheckDescriptorSets(context, pass, bound);
        CheckDescriptorLimits(context, pass, bound);
        CheckColorAttachments(context, pass);
        CheckBufferRanges(context, pass, bound);
    }

    // The largest axis that the dimension uses, against that dimension's limit. Where is the declaration.
    void CheckImageLimits(LayoutContext& context)
    {
        for (size_t rowIndex = 0u; rowIndex < context.Frozen.Resources.size(); ++rowIndex)
        {
            const FrozenResource& row = context.Frozen.Resources[rowIndex];
            const SizedResource& sized = context.Sized.Resources[rowIndex];

            // an image sized from a frame target, or an import of any extent: Realize knows its extent
            if (row.Kind != ResourceKind::Image || sized.Extent.Width == k_UnknownExtent)
            {
                continue;
            }

            const ImageExtent& extent = sized.Extent;
            int64_t largest = extent.Width;
            uint32_t limit = context.Limits.MaxImageDimension1D;
            std::string_view limitName = "MaxImageDimension1D";

            if (sized.Dimension == lodestone::TextureDimension::Texture2D)
            {
                largest = std::max(extent.Width, extent.Height);
                limit = context.Limits.MaxImageDimension2D;
                limitName = "MaxImageDimension2D";
            }
            else if (sized.Dimension == lodestone::TextureDimension::Texture3D)
            {
                largest = std::max({ extent.Width, extent.Height, extent.Depth });
                limit = context.Limits.MaxImageDimension3D;
                limitName = "MaxImageDimension3D";
            }

            if (std::cmp_greater(largest ,limit))
            {
                ReportLimit(context, KeyRef{}, row.Key, row.Where, limitName, limit, largest);
            }
        }
    }

}
