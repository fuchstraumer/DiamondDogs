#include "ResourceBarriers.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiTypes.hpp"
#include "ResourceTypes.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

/**
 * Quick note about the barriers built here: they are global barriers whenever the layout stays the same, because with
 * synchronization2 a global barrier is all that a buffer or an image in the same layout needs. IHVs have confirmed
 * that global barriers cost no more than resource barriers in most cases. An image gets its own barrier only when its
 * layout changes. Each usage bit has a row in a table below: the stages that use it, and the accesses that it makes.
 */

namespace
{
    using rhi::Access;
    using rhi::ImageLayout;
    using rhi::PipelineStage;

    struct BufferUsageRow
    {
        BufferUsageBits Bit;
        PipelineStage Stages;
        Access Accesses;
    };

    struct ImageUsageRow
    {
        ImageUsageBits Bit;
        PipelineStage Stages;
        Access Accesses;
    };

    // a read makes nothing available, so a source access mask keeps only these
    constexpr Access k_WriteAccesses =
        Access::ShaderWrite |
        Access::ColorAttachmentWrite |
        Access::DepthStencilAttachmentWrite |
        Access::TransferWrite |
        Access::HostWrite |
        Access::MemoryWrite |
        Access::CommandPreprocessWrite |
        Access::AccelerationStructureWrite |
        Access::TransformFeedbackWrite |
        Access::TransformFeedbackCounterWrite |
        Access::ShaderStorageWrite |
        Access::MicromapWrite |
        Access::MemoryDecompressionWrite;

    constexpr Access k_ShaderSampledAndStorageRead = Access::ShaderSampledRead | Access::ShaderStorageRead;
    constexpr Access k_ShaderStorageReadWrite = Access::ShaderStorageRead | Access::ShaderStorageWrite;

    // the stages that a sampled image can be read in without a feature: every pre-rasterization stage is in
    // PreRasterizationShaders, so tessellation and mesh stages are never named one by one. Ray tracing shaders are not
    // in it: a ray tracing caller uses RayTracingShaderSRV
    constexpr PipelineStage k_SampledImageStages =
        PipelineStage::PreRasterizationShaders |
        PipelineStage::FragmentShader |
        PipelineStage::ComputeShader;

    constexpr PipelineStage k_DepthStencilStages = PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests;

    constexpr PipelineStage k_AccelerationStructureReaders =
        PipelineStage::AccelerationStructureBuild |
        PipelineStage::AccelerationStructureCopy |
        PipelineStage::RayTracingShader |
        PipelineStage::PreRasterizationShaders |
        PipelineStage::FragmentShader |
        PipelineStage::ComputeShader;

    constexpr BufferUsageRow k_BufferUsageRows[] =
    {
        { BufferUsageBits::VertexShaderUBO, PipelineStage::VertexShader, Access::UniformRead },
        { BufferUsageBits::TessellationControlShaderUBO, PipelineStage::TessellationControlShader, Access::UniformRead },
        { BufferUsageBits::TessellationEvaluationShaderUBO, PipelineStage::TessellationEvaluationShader, Access::UniformRead },
        { BufferUsageBits::GeometryShaderUBO, PipelineStage::GeometryShader, Access::UniformRead },
        { BufferUsageBits::TaskShaderUBO, PipelineStage::TaskShader, Access::UniformRead },
        { BufferUsageBits::MeshShaderUBO, PipelineStage::MeshShader, Access::UniformRead },
        { BufferUsageBits::FragmentShaderUBO, PipelineStage::FragmentShader, Access::UniformRead },
        { BufferUsageBits::ComputeShaderUBO, PipelineStage::ComputeShader, Access::UniformRead },
        { BufferUsageBits::RayTracingShaderUBO, PipelineStage::RayTracingShader, Access::UniformRead },

        { BufferUsageBits::VertexShaderSRV, PipelineStage::VertexShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::TessellationControlShaderSRV, PipelineStage::TessellationControlShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::TessellationEvaluationShaderSRV, PipelineStage::TessellationEvaluationShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::GeometryShaderSRV, PipelineStage::GeometryShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::TaskShaderSRV, PipelineStage::TaskShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::MeshShaderSRV, PipelineStage::MeshShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::FragmentShaderSRV, PipelineStage::FragmentShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::ComputeShaderSRV, PipelineStage::ComputeShader, k_ShaderSampledAndStorageRead },
        { BufferUsageBits::RayTracingShaderSRV, PipelineStage::RayTracingShader, k_ShaderSampledAndStorageRead },

        { BufferUsageBits::VertexShaderUAV, PipelineStage::VertexShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::TessellationControlShaderUAV, PipelineStage::TessellationControlShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::TessellationEvaluationShaderUAV, PipelineStage::TessellationEvaluationShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::GeometryShaderUAV, PipelineStage::GeometryShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::TaskShaderUAV, PipelineStage::TaskShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::MeshShaderUAV, PipelineStage::MeshShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::FragmentShaderUAV, PipelineStage::FragmentShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::ComputeShaderUAV, PipelineStage::ComputeShader, k_ShaderStorageReadWrite },
        { BufferUsageBits::RayTracingShaderUAV, PipelineStage::RayTracingShader, k_ShaderStorageReadWrite },

        { BufferUsageBits::IndexInput, PipelineStage::IndexInput, Access::IndexRead },
        { BufferUsageBits::VertexInput, PipelineStage::VertexAttributeInput, Access::VertexAttributeRead },
        { BufferUsageBits::DrawIndirect, PipelineStage::DrawIndirect, Access::IndirectCommandRead },
        { BufferUsageBits::DispatchIndirect, PipelineStage::DrawIndirect, Access::IndirectCommandRead },
        { BufferUsageBits::TraceRaysIndirect, PipelineStage::DrawIndirect | PipelineStage::RayTracingShader, Access::IndirectCommandRead },
        { BufferUsageBits::TransferSource, PipelineStage::AllTransfer, Access::TransferRead },
        { BufferUsageBits::TransferDestination, PipelineStage::AllTransfer, Access::TransferWrite },
        { BufferUsageBits::ConditionalRendering, PipelineStage::ConditionalRendering, Access::ConditionalRenderingRead },
        { BufferUsageBits::TransformFeedback, PipelineStage::TransformFeedback, Access::TransformFeedbackWrite },
        {
            BufferUsageBits::TransformFeedbackCounter,
            PipelineStage::TransformFeedback | PipelineStage::DrawIndirect,
            Access::TransformFeedbackCounterRead | Access::TransformFeedbackCounterWrite | Access::IndirectCommandRead
        },
        {
            BufferUsageBits::GeneratedCommandsInput,
            PipelineStage::CommandPreprocess | PipelineStage::DrawIndirect,
            Access::CommandPreprocessRead | Access::IndirectCommandRead
        },
        {
            BufferUsageBits::GeneratedCommandsPreprocess,
            PipelineStage::CommandPreprocess | PipelineStage::DrawIndirect,
            Access::CommandPreprocessRead | Access::CommandPreprocessWrite
        },
        {
            BufferUsageBits::AccelerationStructureStorage,
            k_AccelerationStructureReaders,
            Access::AccelerationStructureRead | Access::AccelerationStructureWrite
        },
        { BufferUsageBits::AccelerationStructureBuildInput, PipelineStage::AccelerationStructureBuild, Access::ShaderRead },
        {
            BufferUsageBits::AccelerationStructureBuildScratch,
            PipelineStage::AccelerationStructureBuild,
            Access::AccelerationStructureRead | Access::AccelerationStructureWrite
        },
        { BufferUsageBits::ShaderBindingTable, PipelineStage::RayTracingShader, Access::ShaderBindingTableRead },
        {
            BufferUsageBits::MicromapStorage,
            PipelineStage::MicromapBuild | PipelineStage::AccelerationStructureBuild,
            Access::MicromapRead | Access::MicromapWrite
        },
        { BufferUsageBits::MicromapBuildInput, PipelineStage::MicromapBuild, Access::ShaderRead },
    };

    constexpr ImageUsageRow k_ImageUsageRows[] =
    {
        { ImageUsageBits::VertexShaderSRV, PipelineStage::VertexShader, Access::ShaderSampledRead },
        { ImageUsageBits::TessellationControlShaderSRV, PipelineStage::TessellationControlShader, Access::ShaderSampledRead },
        { ImageUsageBits::TessellationEvaluationShaderSRV, PipelineStage::TessellationEvaluationShader, Access::ShaderSampledRead },
        { ImageUsageBits::GeometryShaderSRV, PipelineStage::GeometryShader, Access::ShaderSampledRead },
        { ImageUsageBits::TaskShaderSRV, PipelineStage::TaskShader, Access::ShaderSampledRead },
        { ImageUsageBits::MeshShaderSRV, PipelineStage::MeshShader, Access::ShaderSampledRead },
        { ImageUsageBits::FragmentShaderSRV, PipelineStage::FragmentShader, Access::ShaderSampledRead },
        { ImageUsageBits::ComputeShaderSRV, PipelineStage::ComputeShader, Access::ShaderSampledRead },
        { ImageUsageBits::RayTracingShaderSRV, PipelineStage::RayTracingShader, Access::ShaderSampledRead },

        { ImageUsageBits::VertexShaderUAV, PipelineStage::VertexShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::TessellationControlShaderUAV, PipelineStage::TessellationControlShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::TessellationEvaluationShaderUAV, PipelineStage::TessellationEvaluationShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::GeometryShaderUAV, PipelineStage::GeometryShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::TaskShaderUAV, PipelineStage::TaskShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::MeshShaderUAV, PipelineStage::MeshShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::FragmentShaderUAV, PipelineStage::FragmentShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::ComputeShaderUAV, PipelineStage::ComputeShader, k_ShaderStorageReadWrite },
        { ImageUsageBits::RayTracingShaderUAV, PipelineStage::RayTracingShader, k_ShaderStorageReadWrite },

        { ImageUsageBits::SampledImage, k_SampledImageStages, Access::ShaderSampledRead },
        { ImageUsageBits::RenderTargetRead, PipelineStage::ColorAttachmentOutput, Access::ColorAttachmentRead },
        { ImageUsageBits::RenderTargetWrite, PipelineStage::ColorAttachmentOutput, Access::ColorAttachmentWrite },
        { ImageUsageBits::DepthRead, k_DepthStencilStages, Access::DepthStencilAttachmentRead },
        { ImageUsageBits::DepthWrite, k_DepthStencilStages, Access::DepthStencilAttachmentWrite },
        { ImageUsageBits::StencilRead, k_DepthStencilStages, Access::DepthStencilAttachmentRead },
        { ImageUsageBits::StencilWrite, k_DepthStencilStages, Access::DepthStencilAttachmentWrite },
        { ImageUsageBits::DepthStencilRead, k_DepthStencilStages, Access::DepthStencilAttachmentRead },
        { ImageUsageBits::DepthStencilWrite, k_DepthStencilStages, Access::DepthStencilAttachmentWrite },
        { ImageUsageBits::InputAttachmentRead, PipelineStage::FragmentShader, Access::InputAttachmentRead },
        { ImageUsageBits::ShadingRateRead, PipelineStage::FragmentShadingRateAttachment, Access::FragmentShadingRateAttachmentRead },
        { ImageUsageBits::FragmentDensityMapRead, PipelineStage::FragmentDensityProcess, Access::FragmentDensityMapRead },
        { ImageUsageBits::TransferSource, PipelineStage::AllTransfer, Access::TransferRead },
        { ImageUsageBits::TransferDestination, PipelineStage::AllTransfer, Access::TransferWrite },
        // presentation waits for the work before it, and makes no access of its own
        { ImageUsageBits::PresentSource, PipelineStage::BottomOfPipe, Access::None },
    };

    /** @brief What one change contributes to a barrier: the work that must finish, and the work that must wait. */
    struct UsageScope
    {
        PipelineStage SrcStages{ PipelineStage::None };
        Access SrcAccess{ Access::None };
        PipelineStage DstStages{ PipelineStage::None };
        Access DstAccess{ Access::None };
        /** @brief True when neither side writes. Two reads need no memory dependency. */
        bool BothRead{ true };
    };

    [[nodiscard]] bool HasAny(BufferUsageBits bits, BufferUsageBits mask) noexcept;
    [[nodiscard]] bool HasAny(ImageUsageBits bits, ImageUsageBits mask) noexcept;
    [[nodiscard]] bool HasAnyAccess(Access accesses, Access mask) noexcept;
    [[nodiscard]] rhi::ImageAspect ToRhiAspect(ImageAspectFlags aspect) noexcept;
    [[nodiscard]] rhi::ImageSubresourceRange ToRhiRange(const ImageRange& range) noexcept;
    [[nodiscard]] UsageScope ScopeOf(PipelineStage before_stages, Access before_accesses, PipelineStage after_stages, Access after_accesses) noexcept;
    void MergeIntoGlobal(rhi::GlobalBarrier& global, const UsageScope& scope) noexcept;
}

PipelineStage StagesOfUsage(BufferUsageBits usage) noexcept
{
    PipelineStage stages = PipelineStage::None;

    for (const BufferUsageRow& row : k_BufferUsageRows)
    {
        if (HasAny(usage, row.Bit))
        {
            stages |= row.Stages;
        }
    }

    return stages;
}

Access AccessesOfUsage(BufferUsageBits usage) noexcept
{
    Access accesses = Access::None;

    for (const BufferUsageRow& row : k_BufferUsageRows)
    {
        if (HasAny(usage, row.Bit))
        {
            accesses |= row.Accesses;
        }
    }

    return accesses;
}

PipelineStage StagesOfUsage(ImageUsageBits usage) noexcept
{
    PipelineStage stages = PipelineStage::None;

    for (const ImageUsageRow& row : k_ImageUsageRows)
    {
        if (HasAny(usage, row.Bit))
        {
            stages |= row.Stages;
        }
    }

    return stages;
}

Access AccessesOfUsage(ImageUsageBits usage) noexcept
{
    Access accesses = Access::None;

    for (const ImageUsageRow& row : k_ImageUsageRows)
    {
        if (HasAny(usage, row.Bit))
        {
            accesses |= row.Accesses;
        }
    }

    return accesses;
}

ImageLayout ImageLayoutOfUsage(ImageUsageBits usage) noexcept
{
    // first match wins
    if (HasAny(usage, ImageUsageBits::Discard))
    {
        return ImageLayout::Undefined;
    }

    if (HasAny(usage, ImageUsageBits::PresentSource))
    {
        return ImageLayout::PresentSource;
    }

    // with synchronization2 one read-only layout serves every read-only attachment use, and the driver tells depth from color
    if (HasAny(usage, ImageUsageBits::AllAttachmentRead) && !HasAny(usage, ImageUsageBits::AllAttachmentWrite))
    {
        return ImageLayout::ReadOnly;
    }

    if (HasAny(usage, ImageUsageBits::AllAttachmentWrite))
    {
        return ImageLayout::Attachment;
    }

    // storage images must be in GENERAL. this is checked before the read-only case, so that an image that is both
    // sampled and written by shaders stays in a layout that permits both
    if (HasAny(usage, ImageUsageBits::AllShaderUAV))
    {
        return ImageLayout::General;
    }

    if (HasAny(usage, ImageUsageBits::AllShaderSRV | ImageUsageBits::SampledImage | ImageUsageBits::InputAttachmentRead))
    {
        return ImageLayout::ReadOnly;
    }

    if (HasAny(usage, ImageUsageBits::ShadingRateRead))
    {
        return ImageLayout::FragmentShadingRateAttachment;
    }

    if (HasAny(usage, ImageUsageBits::FragmentDensityMapRead))
    {
        return ImageLayout::FragmentDensityMap;
    }

    // the transfer bits come last: they name a layout only when they are the whole usage. an uploaded texture also
    // carries TransferDestination, and its resting layout is the shader one
    if (HasAny(usage, ImageUsageBits::TransferDestination))
    {
        return ImageLayout::TransferDestination;
    }

    if (HasAny(usage, ImageUsageBits::TransferSource))
    {
        return ImageLayout::TransferSource;
    }

    return ImageLayout::Undefined;
}

UsageBarriers TranslateUsageChanges(
    std::span<const BufferBarrierInfo> buffer_changes,
    std::span<const ImageBarrierInfo> image_changes,
    std::span<rhi::ImageBarrier> images_out) noexcept
{
    UsageBarriers result;

    for (const BufferBarrierInfo& change : buffer_changes)
    {
        const UsageScope scope = ScopeOf(
            StagesOfUsage(change.BeforeUsage),
            AccessesOfUsage(change.BeforeUsage),
            StagesOfUsage(change.AfterUsage),
            AccessesOfUsage(change.AfterUsage));

        if (!scope.BothRead)
        {
            MergeIntoGlobal(result.Global, scope);
        }
    }

    for (const ImageBarrierInfo& change : image_changes)
    {
        // a discard has no prior work to wait for here: the caller's semaphores or the ordering barrier do that
        const bool beforeIsDiscard = HasAny(change.BeforeUsage, ImageUsageBits::Discard);
        const ImageLayout oldLayout = ImageLayoutOfUsage(change.BeforeUsage);
        const ImageLayout newLayout = ImageLayoutOfUsage(change.AfterUsage);

        const PipelineStage beforeStages = beforeIsDiscard ? PipelineStage::None : StagesOfUsage(change.BeforeUsage);
        const Access beforeAccesses = beforeIsDiscard ? Access::None : AccessesOfUsage(change.BeforeUsage);
        const UsageScope scope = ScopeOf(beforeStages, beforeAccesses, StagesOfUsage(change.AfterUsage), AccessesOfUsage(change.AfterUsage));

        // a before usage with no layout and no discard says nothing about where the image is, so it starts no transition
        const bool oldLayoutIsKnown = beforeIsDiscard || oldLayout != ImageLayout::Undefined;
        const bool layoutChanges = newLayout != ImageLayout::Undefined && oldLayoutIsKnown && newLayout != oldLayout;

        if (!layoutChanges)
        {
            if (!scope.BothRead)
            {
                MergeIntoGlobal(result.Global, scope);
            }

            continue;
        }

        const std::size_t imageIndex = static_cast<std::size_t>(result.ImageCount);

        if (imageIndex >= images_out.size())
        {
            assert(false && "TranslateUsageChanges: images_out is too small for the image changes");
            continue;
        }

        rhi::ImageBarrier& barrier = images_out[imageIndex];
        barrier = rhi::ImageBarrier{};
        barrier.Image = change.Handle;
        barrier.SrcStages = scope.SrcStages;
        barrier.SrcAccess = scope.SrcAccess;
        barrier.DstStages = scope.DstStages;
        barrier.DstAccess = scope.DstAccess;
        barrier.OldLayout = oldLayout;
        barrier.NewLayout = newLayout;
        barrier.Range = ToRhiRange(change.Range);
        ++result.ImageCount;
    }

    return result;
}

namespace
{
    bool HasAny(BufferUsageBits bits, BufferUsageBits mask) noexcept
    {
        return (bits & mask) != BufferUsageBits::Invalid;
    }

    bool HasAny(ImageUsageBits bits, ImageUsageBits mask) noexcept
    {
        return (bits & mask) != ImageUsageBits::Invalid;
    }

    bool HasAnyAccess(Access accesses, Access mask) noexcept
    {
        return (accesses & mask) != Access::None;
    }

    rhi::ImageAspect ToRhiAspect(ImageAspectFlags aspect) noexcept
    {
        rhi::ImageAspect result = rhi::ImageAspect::None;

        if ((aspect & ImageAspectFlags::Color) != ImageAspectFlags::None)
        {
            result |= rhi::ImageAspect::Color;
        }

        if ((aspect & ImageAspectFlags::Depth) != ImageAspectFlags::None)
        {
            result |= rhi::ImageAspect::Depth;
        }

        if ((aspect & ImageAspectFlags::Stencil) != ImageAspectFlags::None)
        {
            result |= rhi::ImageAspect::Stencil;
        }

        return result;
    }

    rhi::ImageSubresourceRange ToRhiRange(const ImageRange& range) noexcept
    {
        rhi::ImageSubresourceRange result{};
        result.Aspects = ToRhiAspect(range.AspectMask);
        result.BaseMipLevel = range.BaseMipLevel;
        result.LevelCount = range.LevelCount;
        result.BaseArrayLayer = range.BaseArrayLayer;
        result.LayerCount = range.LayerCount;
        return result;
    }

    UsageScope ScopeOf(PipelineStage before_stages, Access before_accesses, PipelineStage after_stages, Access after_accesses) noexcept
    {
        UsageScope scope;
        scope.SrcStages = before_stages;
        // only a write makes memory available. a write after a read needs the source stages and no source access
        scope.SrcAccess = before_accesses & k_WriteAccesses;
        scope.DstStages = after_stages;
        scope.DstAccess = after_accesses;
        scope.BothRead = !HasAnyAccess(before_accesses, k_WriteAccesses) && !HasAnyAccess(after_accesses, k_WriteAccesses);
        return scope;
    }

    void MergeIntoGlobal(rhi::GlobalBarrier& global, const UsageScope& scope) noexcept
    {
        global.SrcStages |= scope.SrcStages;
        global.SrcAccess |= scope.SrcAccess;
        global.DstStages |= scope.DstStages;
        global.DstAccess |= scope.DstAccess;
    }
}
