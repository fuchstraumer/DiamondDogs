#include "ResourceBarriers.hpp"
#include "ResourceContextFixture.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "Queue.hpp"
#include "RhiFlags.hpp"
#include "RhiTypes.hpp"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <thread>
#include <vector>
#include <vulkan/vulkan_core.h>

// Part one needs no GPU. It checks the translation against tables that this file writes out by itself, so that a
// wrong row in ResourceBarriers.cpp fails here. Part two records the barriers with CommandBuffer::Barrier, and
// synchronization validation (on for every test in this suite) is the judge: a missing dependency or a wrong layout
// is a validation message, and the runner counts those.

namespace
{
    using rhi::Access;
    using rhi::ImageLayout;
    using rhi::PipelineStage;

    // the last bit of each enum, so that the sweeps cover every bit without a count that someone must keep in step
    constexpr int32_t k_BufferBitCount = std::countr_zero(ToUnderlying(BufferUsageBits::DeviceAddress)) + 1;
    constexpr int32_t k_ImageBitCount = std::countr_zero(ToUnderlying(ImageUsageBits::Discard)) + 1;

    // every access that writes, written out here. A source access may hold nothing else
    constexpr Access k_Writes =
        Access::ShaderWrite | Access::ColorAttachmentWrite | Access::DepthStencilAttachmentWrite | Access::TransferWrite |
        Access::HostWrite | Access::MemoryWrite | Access::CommandPreprocessWrite | Access::AccelerationStructureWrite |
        Access::TransformFeedbackWrite | Access::TransformFeedbackCounterWrite | Access::ShaderStorageWrite |
        Access::MicromapWrite | Access::MemoryDecompressionWrite;

    struct ShaderStageRow
    {
        BufferUsageBits Ubo;
        BufferUsageBits BufferSrv;
        BufferUsageBits BufferUav;
        ImageUsageBits ImageSrv;
        ImageUsageBits ImageUav;
        PipelineStage Stage;
    };

    constexpr ShaderStageRow k_ShaderStages[] =
    {
        { BufferUsageBits::VertexShaderUBO, BufferUsageBits::VertexShaderSRV, BufferUsageBits::VertexShaderUAV, ImageUsageBits::VertexShaderSRV, ImageUsageBits::VertexShaderUAV, PipelineStage::VertexShader },
        { BufferUsageBits::TessellationControlShaderUBO, BufferUsageBits::TessellationControlShaderSRV, BufferUsageBits::TessellationControlShaderUAV, ImageUsageBits::TessellationControlShaderSRV, ImageUsageBits::TessellationControlShaderUAV, PipelineStage::TessellationControlShader },
        { BufferUsageBits::TessellationEvaluationShaderUBO, BufferUsageBits::TessellationEvaluationShaderSRV, BufferUsageBits::TessellationEvaluationShaderUAV, ImageUsageBits::TessellationEvaluationShaderSRV, ImageUsageBits::TessellationEvaluationShaderUAV, PipelineStage::TessellationEvaluationShader },
        { BufferUsageBits::GeometryShaderUBO, BufferUsageBits::GeometryShaderSRV, BufferUsageBits::GeometryShaderUAV, ImageUsageBits::GeometryShaderSRV, ImageUsageBits::GeometryShaderUAV, PipelineStage::GeometryShader },
        { BufferUsageBits::TaskShaderUBO, BufferUsageBits::TaskShaderSRV, BufferUsageBits::TaskShaderUAV, ImageUsageBits::TaskShaderSRV, ImageUsageBits::TaskShaderUAV, PipelineStage::TaskShader },
        { BufferUsageBits::MeshShaderUBO, BufferUsageBits::MeshShaderSRV, BufferUsageBits::MeshShaderUAV, ImageUsageBits::MeshShaderSRV, ImageUsageBits::MeshShaderUAV, PipelineStage::MeshShader },
        { BufferUsageBits::FragmentShaderUBO, BufferUsageBits::FragmentShaderSRV, BufferUsageBits::FragmentShaderUAV, ImageUsageBits::FragmentShaderSRV, ImageUsageBits::FragmentShaderUAV, PipelineStage::FragmentShader },
        { BufferUsageBits::ComputeShaderUBO, BufferUsageBits::ComputeShaderSRV, BufferUsageBits::ComputeShaderUAV, ImageUsageBits::ComputeShaderSRV, ImageUsageBits::ComputeShaderUAV, PipelineStage::ComputeShader },
        { BufferUsageBits::RayTracingShaderUBO, BufferUsageBits::RayTracingShaderSRV, BufferUsageBits::RayTracingShaderUAV, ImageUsageBits::RayTracingShaderSRV, ImageUsageBits::RayTracingShaderUAV, PipelineStage::RayTracingShader },
    };

    struct BufferRow
    {
        BufferUsageBits Bit;
        PipelineStage Stages;
        Access Accesses;
    };

    constexpr BufferRow k_BufferRows[] =
    {
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
            PipelineStage::AccelerationStructureBuild | PipelineStage::AccelerationStructureCopy | PipelineStage::RayTracingShader |
                PipelineStage::PreRasterizationShaders | PipelineStage::FragmentShader | PipelineStage::ComputeShader,
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
        { BufferUsageBits::DeviceAddress, PipelineStage::None, Access::None },
    };

    struct ImageRow
    {
        ImageUsageBits Bit;
        PipelineStage Stages;
        Access Accesses;
    };

    constexpr ImageRow k_ImageRows[] =
    {
        { ImageUsageBits::SampledImage, PipelineStage::PreRasterizationShaders | PipelineStage::FragmentShader | PipelineStage::ComputeShader, Access::ShaderSampledRead },
        { ImageUsageBits::RenderTargetRead, PipelineStage::ColorAttachmentOutput, Access::ColorAttachmentRead },
        { ImageUsageBits::RenderTargetWrite, PipelineStage::ColorAttachmentOutput, Access::ColorAttachmentWrite },
        { ImageUsageBits::DepthRead, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests, Access::DepthStencilAttachmentRead },
        { ImageUsageBits::DepthWrite, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests, Access::DepthStencilAttachmentWrite },
        { ImageUsageBits::StencilRead, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests, Access::DepthStencilAttachmentRead },
        { ImageUsageBits::StencilWrite, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests, Access::DepthStencilAttachmentWrite },
        { ImageUsageBits::DepthStencilRead, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests, Access::DepthStencilAttachmentRead },
        { ImageUsageBits::DepthStencilWrite, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests, Access::DepthStencilAttachmentWrite },
        { ImageUsageBits::InputAttachmentRead, PipelineStage::FragmentShader, Access::InputAttachmentRead },
        { ImageUsageBits::ShadingRateRead, PipelineStage::FragmentShadingRateAttachment, Access::FragmentShadingRateAttachmentRead },
        { ImageUsageBits::FragmentDensityMapRead, PipelineStage::FragmentDensityProcess, Access::FragmentDensityMapRead },
        { ImageUsageBits::TransferSource, PipelineStage::AllTransfer, Access::TransferRead },
        { ImageUsageBits::TransferDestination, PipelineStage::AllTransfer, Access::TransferWrite },
        { ImageUsageBits::PresentSource, PipelineStage::BottomOfPipe, Access::None },
        { ImageUsageBits::Discard, PipelineStage::None, Access::None },
    };

    struct LayoutRow
    {
        ImageUsageBits Usage;
        ImageLayout Layout;
    };

    constexpr LayoutRow k_LayoutRows[] =
    {
        { ImageUsageBits::Invalid, ImageLayout::Undefined },
        { ImageUsageBits::Discard, ImageLayout::Undefined },
        { ImageUsageBits::Discard | ImageUsageBits::ComputeShaderUAV, ImageLayout::Undefined },
        { ImageUsageBits::PresentSource, ImageLayout::PresentSource },
        { ImageUsageBits::PresentSource | ImageUsageBits::RenderTargetWrite, ImageLayout::PresentSource },
        { ImageUsageBits::RenderTargetRead, ImageLayout::ReadOnly },
        { ImageUsageBits::DepthRead, ImageLayout::ReadOnly },
        { ImageUsageBits::StencilRead | ImageUsageBits::DepthRead, ImageLayout::ReadOnly },
        { ImageUsageBits::DepthStencilRead, ImageLayout::ReadOnly },
        { ImageUsageBits::RenderTargetWrite, ImageLayout::Attachment },
        { ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite, ImageLayout::Attachment },
        { ImageUsageBits::DepthWrite, ImageLayout::Attachment },
        { ImageUsageBits::StencilWrite, ImageLayout::Attachment },
        { ImageUsageBits::DepthStencilWrite | ImageUsageBits::DepthRead, ImageLayout::Attachment },
        { ImageUsageBits::ComputeShaderUAV, ImageLayout::General },
        { ImageUsageBits::FragmentShaderUAV, ImageLayout::General },
        { ImageUsageBits::ComputeShaderUAV | ImageUsageBits::ComputeShaderSRV, ImageLayout::General },
        { ImageUsageBits::ComputeShaderUAV | ImageUsageBits::TransferDestination, ImageLayout::General },
        { ImageUsageBits::ComputeShaderSRV, ImageLayout::ReadOnly },
        { ImageUsageBits::RayTracingShaderSRV, ImageLayout::ReadOnly },
        { ImageUsageBits::SampledImage, ImageLayout::ReadOnly },
        { ImageUsageBits::InputAttachmentRead, ImageLayout::ReadOnly },
        { ImageUsageBits::SampledImage | ImageUsageBits::TransferDestination, ImageLayout::ReadOnly },
        { ImageUsageBits::ShadingRateRead, ImageLayout::FragmentShadingRateAttachment },
        { ImageUsageBits::FragmentDensityMapRead, ImageLayout::FragmentDensityMap },
        { ImageUsageBits::TransferDestination, ImageLayout::TransferDestination },
        { ImageUsageBits::TransferSource, ImageLayout::TransferSource },
        { ImageUsageBits::TransferSource | ImageUsageBits::TransferDestination, ImageLayout::TransferDestination },
    };

    [[nodiscard]] BufferUsageBits BufferBit(int32_t index) noexcept
    {
        return static_cast<BufferUsageBits>(1ull << index);
    }

    [[nodiscard]] ImageUsageBits ImageBit(int32_t index) noexcept
    {
        return static_cast<ImageUsageBits>(1ull << index);
    }

    [[nodiscard]] rhi::ImageHandle FakeImage(uint64_t value) noexcept
    {
        return rhi::ImageHandle{ value };
    }

    [[nodiscard]] rhi::BufferHandle FakeBuffer(uint64_t value) noexcept
    {
        return rhi::BufferHandle{ value };
    }

    [[nodiscard]] ImageRange ColorRange() noexcept
    {
        return ImageRange{ ImageAspectFlags::Color, 0u, 1u, 0u, 1u };
    }

    [[nodiscard]] bool IsEmpty(const rhi::GlobalBarrier& barrier) noexcept
    {
        return barrier.SrcStages == PipelineStage::None && barrier.SrcAccess == Access::None &&
               barrier.DstStages == PipelineStage::None && barrier.DstAccess == Access::None;
    }
}

TEST(BarrierTranslation, BufferShaderBitsUseTheirStageAndTheirKindOfAccess)
{
    for (const ShaderStageRow& row : k_ShaderStages)
    {
        EXPECT_EQ(StagesOfUsage(row.Ubo), row.Stage);
        EXPECT_EQ(AccessesOfUsage(row.Ubo), Access::UniformRead);

        EXPECT_EQ(StagesOfUsage(row.BufferSrv), row.Stage);
        EXPECT_EQ(AccessesOfUsage(row.BufferSrv), Access::ShaderSampledRead | Access::ShaderStorageRead);

        EXPECT_EQ(StagesOfUsage(row.BufferUav), row.Stage);
        EXPECT_EQ(AccessesOfUsage(row.BufferUav), Access::ShaderStorageRead | Access::ShaderStorageWrite);
    }
}

TEST(BarrierTranslation, BufferFixedFunctionBitsMatchTheirRows)
{
    for (const BufferRow& row : k_BufferRows)
    {
        EXPECT_EQ(StagesOfUsage(row.Bit), row.Stages) << "bit " << ToUnderlying(row.Bit);
        EXPECT_EQ(AccessesOfUsage(row.Bit), row.Accesses) << "bit " << ToUnderlying(row.Bit);
    }
}

TEST(BarrierTranslation, ImageShaderBitsUseTheirStageAndTheirKindOfAccess)
{
    for (const ShaderStageRow& row : k_ShaderStages)
    {
        EXPECT_EQ(StagesOfUsage(row.ImageSrv), row.Stage);
        EXPECT_EQ(AccessesOfUsage(row.ImageSrv), Access::ShaderSampledRead);

        EXPECT_EQ(StagesOfUsage(row.ImageUav), row.Stage);
        EXPECT_EQ(AccessesOfUsage(row.ImageUav), Access::ShaderStorageRead | Access::ShaderStorageWrite);
    }
}

TEST(BarrierTranslation, ImageFixedFunctionBitsMatchTheirRows)
{
    for (const ImageRow& row : k_ImageRows)
    {
        EXPECT_EQ(StagesOfUsage(row.Bit), row.Stages) << "bit " << ToUnderlying(row.Bit);
        EXPECT_EQ(AccessesOfUsage(row.Bit), row.Accesses) << "bit " << ToUnderlying(row.Bit);
    }
}

TEST(BarrierTranslation, EveryBitHasARowInTheTest)
{
    // the tables above must name every bit, so that a new bit cannot slip in without an expected value
    BufferUsageBits namedBufferBits = BufferUsageBits::Invalid;

    for (const ShaderStageRow& row : k_ShaderStages)
    {
        namedBufferBits |= row.Ubo | row.BufferSrv | row.BufferUav;
    }

    for (const BufferRow& row : k_BufferRows)
    {
        namedBufferBits |= row.Bit;
    }

    ImageUsageBits namedImageBits = ImageUsageBits::Invalid;

    for (const ShaderStageRow& row : k_ShaderStages)
    {
        namedImageBits |= row.ImageSrv | row.ImageUav;
    }

    for (const ImageRow& row : k_ImageRows)
    {
        namedImageBits |= row.Bit;
    }

    const uint64_t allBufferBits = (1ull << k_BufferBitCount) - 1ull;
    const uint64_t allImageBits = (1ull << k_ImageBitCount) - 1ull;
    EXPECT_EQ(ToUnderlying(namedBufferBits), allBufferBits);
    EXPECT_EQ(ToUnderlying(namedImageBits), allImageBits);
}

TEST(BarrierTranslation, DepthAndStencilReadsIncludeTheLateTests)
{
    const ImageUsageBits reads[] = { ImageUsageBits::DepthRead, ImageUsageBits::StencilRead, ImageUsageBits::DepthStencilRead };

    for (const ImageUsageBits usage : reads)
    {
        EXPECT_NE(StagesOfUsage(usage) & PipelineStage::EarlyFragmentTests, PipelineStage::None);
        EXPECT_NE(StagesOfUsage(usage) & PipelineStage::LateFragmentTests, PipelineStage::None);
    }
}

TEST(BarrierTranslation, ASampledImageNamesComputeAndNoSingleTessellationOrMeshStage)
{
    const PipelineStage stages = StagesOfUsage(ImageUsageBits::SampledImage);

    EXPECT_NE(stages & PipelineStage::ComputeShader, PipelineStage::None);
    EXPECT_NE(stages & PipelineStage::FragmentShader, PipelineStage::None);
    EXPECT_NE(stages & PipelineStage::PreRasterizationShaders, PipelineStage::None);
    EXPECT_EQ(stages & PipelineStage::TessellationControlShader, PipelineStage::None);
    EXPECT_EQ(stages & PipelineStage::MeshShader, PipelineStage::None);
    EXPECT_EQ(stages & PipelineStage::RayTracingShader, PipelineStage::None);
}

TEST(BarrierTranslation, LayoutRules)
{
    for (const LayoutRow& row : k_LayoutRows)
    {
        EXPECT_EQ(ImageLayoutOfUsage(row.Usage), row.Layout) << "usage " << ToUnderlying(row.Usage);
    }
}

TEST(BarrierTranslation, NoSourceAccessHoldsARead)
{
    std::array<rhi::ImageBarrier, 1> imageBarriers{};

    for (int32_t beforeIndex = 0; beforeIndex < k_BufferBitCount; ++beforeIndex)
    {
        for (int32_t afterIndex = 0; afterIndex < k_BufferBitCount; ++afterIndex)
        {
            const BufferBarrierInfo change{ FakeBuffer(1u), BufferBit(beforeIndex), BufferBit(afterIndex) };
            const UsageBarriers result = TranslateUsageChanges(std::span{ &change, 1u }, {}, imageBarriers);

            EXPECT_EQ(result.Global.SrcAccess & ~k_Writes, Access::None) << "buffer bits " << beforeIndex << " to " << afterIndex;
        }
    }

    for (int32_t beforeIndex = 0; beforeIndex < k_ImageBitCount; ++beforeIndex)
    {
        for (int32_t afterIndex = 0; afterIndex < k_ImageBitCount; ++afterIndex)
        {
            const ImageBarrierInfo change{ FakeImage(1u), ImageBit(beforeIndex), ImageBit(afterIndex), ColorRange() };
            const UsageBarriers result = TranslateUsageChanges({}, std::span{ &change, 1u }, imageBarriers);

            EXPECT_EQ(result.Global.SrcAccess & ~k_Writes, Access::None) << "image bits " << beforeIndex << " to " << afterIndex;

            if (result.ImageCount == 1)
            {
                EXPECT_EQ(imageBarriers[0].SrcAccess & ~k_Writes, Access::None) << "image bits " << beforeIndex << " to " << afterIndex;
            }
        }
    }
}

TEST(BarrierTranslation, AWriteAfterAReadKeepsTheSourceStagesAndNoSourceAccess)
{
    const BufferBarrierInfo change{ FakeBuffer(1u), BufferUsageBits::ComputeShaderSRV, BufferUsageBits::ComputeShaderUAV };
    const UsageBarriers result = TranslateUsageChanges(std::span{ &change, 1u }, {}, {});

    EXPECT_EQ(result.Global.SrcStages, PipelineStage::ComputeShader);
    EXPECT_EQ(result.Global.SrcAccess, Access::None);
    EXPECT_EQ(result.Global.DstStages, PipelineStage::ComputeShader);
    EXPECT_EQ(result.Global.DstAccess, Access::ShaderStorageRead | Access::ShaderStorageWrite);
    EXPECT_EQ(result.ImageCount, 0);
}

TEST(BarrierTranslation, ReadToReadWithTheSameLayoutContributesNothing)
{
    std::array<rhi::ImageBarrier, 4> imageBarriers{};

    const BufferBarrierInfo bufferChanges[] =
    {
        { FakeBuffer(1u), BufferUsageBits::ComputeShaderSRV, BufferUsageBits::FragmentShaderUBO },
        { FakeBuffer(2u), BufferUsageBits::DispatchIndirect, BufferUsageBits::ComputeShaderSRV },
        { FakeBuffer(3u), BufferUsageBits::TransferSource, BufferUsageBits::VertexInput | BufferUsageBits::IndexInput },
    };

    // each pair has the same layout: the read-only layout, or the transfer source layout
    const ImageBarrierInfo imageChanges[] =
    {
        { FakeImage(1u), ImageUsageBits::ComputeShaderSRV, ImageUsageBits::FragmentShaderSRV, ColorRange() },
        { FakeImage(2u), ImageUsageBits::SampledImage, ImageUsageBits::ComputeShaderSRV, ColorRange() },
        { FakeImage(3u), ImageUsageBits::DepthRead, ImageUsageBits::SampledImage, ColorRange() },
        { FakeImage(4u), ImageUsageBits::TransferSource, ImageUsageBits::TransferSource, ColorRange() },
    };

    const UsageBarriers result = TranslateUsageChanges(bufferChanges, imageChanges, imageBarriers);

    EXPECT_TRUE(IsEmpty(result.Global));
    EXPECT_EQ(result.ImageCount, 0);
}

TEST(BarrierTranslation, BufferChangesAndSameLayoutImageChangesMergeIntoOneGlobalBarrier)
{
    std::array<rhi::ImageBarrier, 4> imageBarriers{};

    const BufferBarrierInfo bufferChanges[] =
    {
        { FakeBuffer(1u), BufferUsageBits::TransferDestination, BufferUsageBits::ComputeShaderSRV },
        { FakeBuffer(2u), BufferUsageBits::ComputeShaderUAV, BufferUsageBits::DispatchIndirect },
    };

    // general to general: the layout stays, so the image joins the global barrier
    const ImageBarrierInfo imageChange{ FakeImage(1u), ImageUsageBits::ComputeShaderUAV, ImageUsageBits::ComputeShaderUAV, ColorRange() };

    const UsageBarriers result = TranslateUsageChanges(bufferChanges, std::span{ &imageChange, 1u }, imageBarriers);

    EXPECT_EQ(result.ImageCount, 0);
    EXPECT_EQ(result.Global.SrcStages, PipelineStage::AllTransfer | PipelineStage::ComputeShader);
    EXPECT_EQ(result.Global.SrcAccess, Access::TransferWrite | Access::ShaderStorageWrite);
    EXPECT_EQ(result.Global.DstStages, PipelineStage::ComputeShader | PipelineStage::DrawIndirect);
    EXPECT_EQ(result.Global.DstAccess,
              Access::ShaderSampledRead | Access::ShaderStorageRead | Access::ShaderStorageWrite | Access::IndirectCommandRead);
}

TEST(BarrierTranslation, EachLayoutChangeGivesOneImageBarrierWithItsOwnRange)
{
    std::array<rhi::ImageBarrier, 4> imageBarriers{};

    const ImageRange firstRange{ ImageAspectFlags::Color, 0u, 3u, 0u, 1u };
    const ImageRange secondRange{ ImageAspectFlags::Depth | ImageAspectFlags::Stencil, 1u, 1u, 2u, 4u };
    const ImageBarrierInfo imageChanges[] =
    {
        { FakeImage(11u), ImageUsageBits::TransferDestination, ImageUsageBits::ComputeShaderSRV, firstRange },
        { FakeImage(12u), ImageUsageBits::ComputeShaderUAV, ImageUsageBits::DepthWrite, secondRange },
    };

    const UsageBarriers result = TranslateUsageChanges({}, imageChanges, imageBarriers);

    ASSERT_EQ(result.ImageCount, 2);
    EXPECT_TRUE(IsEmpty(result.Global));

    EXPECT_EQ(imageBarriers[0].Image, FakeImage(11u));
    EXPECT_EQ(imageBarriers[0].OldLayout, ImageLayout::TransferDestination);
    EXPECT_EQ(imageBarriers[0].NewLayout, ImageLayout::ReadOnly);
    EXPECT_EQ(imageBarriers[0].SrcStages, PipelineStage::AllTransfer);
    EXPECT_EQ(imageBarriers[0].SrcAccess, Access::TransferWrite);
    EXPECT_EQ(imageBarriers[0].DstStages, PipelineStage::ComputeShader);
    EXPECT_EQ(imageBarriers[0].DstAccess, Access::ShaderSampledRead);
    EXPECT_EQ(imageBarriers[0].Range.Aspects, rhi::ImageAspect::Color);
    EXPECT_EQ(imageBarriers[0].Range.BaseMipLevel, 0u);
    EXPECT_EQ(imageBarriers[0].Range.LevelCount, 3u);
    EXPECT_EQ(imageBarriers[0].Range.BaseArrayLayer, 0u);
    EXPECT_EQ(imageBarriers[0].Range.LayerCount, 1u);

    EXPECT_EQ(imageBarriers[1].Image, FakeImage(12u));
    EXPECT_EQ(imageBarriers[1].OldLayout, ImageLayout::General);
    EXPECT_EQ(imageBarriers[1].NewLayout, ImageLayout::Attachment);
    EXPECT_EQ(imageBarriers[1].SrcStages, PipelineStage::ComputeShader);
    EXPECT_EQ(imageBarriers[1].SrcAccess, Access::ShaderStorageWrite);
    EXPECT_EQ(imageBarriers[1].DstStages, PipelineStage::EarlyFragmentTests | PipelineStage::LateFragmentTests);
    EXPECT_EQ(imageBarriers[1].DstAccess, Access::DepthStencilAttachmentWrite);
    EXPECT_EQ(imageBarriers[1].Range.Aspects, rhi::ImageAspect::Depth | rhi::ImageAspect::Stencil);
    EXPECT_EQ(imageBarriers[1].Range.BaseMipLevel, 1u);
    EXPECT_EQ(imageBarriers[1].Range.LevelCount, 1u);
    EXPECT_EQ(imageBarriers[1].Range.BaseArrayLayer, 2u);
    EXPECT_EQ(imageBarriers[1].Range.LayerCount, 4u);
}

TEST(BarrierTranslation, ALayoutChangeBetweenTwoReadsStillGivesAnImageBarrier)
{
    std::array<rhi::ImageBarrier, 1> imageBarriers{};

    const ImageBarrierInfo change{ FakeImage(1u), ImageUsageBits::TransferSource, ImageUsageBits::ComputeShaderSRV, ColorRange() };
    const UsageBarriers result = TranslateUsageChanges({}, std::span{ &change, 1u }, imageBarriers);

    ASSERT_EQ(result.ImageCount, 1);
    EXPECT_EQ(imageBarriers[0].OldLayout, ImageLayout::TransferSource);
    EXPECT_EQ(imageBarriers[0].NewLayout, ImageLayout::ReadOnly);
    EXPECT_EQ(imageBarriers[0].SrcStages, PipelineStage::AllTransfer);
    EXPECT_EQ(imageBarriers[0].SrcAccess, Access::None);
}

TEST(BarrierTranslation, ADiscardBeforeStartsFromUndefinedWithNoSource)
{
    std::array<rhi::ImageBarrier, 1> imageBarriers{};

    const ImageBarrierInfo change{ FakeImage(5u), ImageUsageBits::Discard, ImageUsageBits::ComputeShaderUAV, ColorRange() };
    const UsageBarriers result = TranslateUsageChanges({}, std::span{ &change, 1u }, imageBarriers);

    ASSERT_EQ(result.ImageCount, 1);
    EXPECT_TRUE(IsEmpty(result.Global));
    EXPECT_EQ(imageBarriers[0].Image, FakeImage(5u));
    EXPECT_EQ(imageBarriers[0].OldLayout, ImageLayout::Undefined);
    EXPECT_EQ(imageBarriers[0].NewLayout, ImageLayout::General);
    EXPECT_EQ(imageBarriers[0].SrcStages, PipelineStage::None);
    EXPECT_EQ(imageBarriers[0].SrcAccess, Access::None);
    EXPECT_EQ(imageBarriers[0].DstStages, PipelineStage::ComputeShader);
    EXPECT_EQ(imageBarriers[0].DstAccess, Access::ShaderStorageRead | Access::ShaderStorageWrite);
}

namespace
{
    // GLSL for each module below (glslc -O, Vulkan 1.3 target). A test shader is not a production shader, so these
    // four are kept as words: the cooked path would need a device with shader objects and a manifest for each one.
    //
    // write_buffer.comp: writes three words of binding 0
    //   #version 450
    //   layout(local_size_x = 1) in;
    //   layout(set = 0, binding = 0, std430) buffer Target { uint words[]; };
    //   void main() { words[0] = 1u; words[1] = 1u; words[2] = 1u; }
    constexpr uint32_t k_WriteBufferSpirv[] =
    {
        0x07230203, 0x00010600, 0x000d000b, 0x00000014, 0x00000000, 0x00020011,
        0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000005,
        0x00000004, 0x6e69616d, 0x00000000, 0x0000000a, 0x00060010, 0x00000004,
        0x00000011, 0x00000001, 0x00000001, 0x00000001, 0x00040047, 0x00000007,
        0x00000006, 0x00000004, 0x00030047, 0x00000008, 0x00000002, 0x00050048,
        0x00000008, 0x00000000, 0x00000023, 0x00000000, 0x00040047, 0x0000000a,
        0x00000021, 0x00000000, 0x00040047, 0x0000000a, 0x00000022, 0x00000000,
        0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
        0x00000006, 0x00000020, 0x00000000, 0x0003001d, 0x00000007, 0x00000006,
        0x0003001e, 0x00000008, 0x00000007, 0x00040020, 0x00000009, 0x0000000c,
        0x00000008, 0x0004003b, 0x00000009, 0x0000000a, 0x0000000c, 0x00040015,
        0x0000000b, 0x00000020, 0x00000001, 0x0004002b, 0x0000000b, 0x0000000c,
        0x00000000, 0x0004002b, 0x00000006, 0x0000000d, 0x00000001, 0x00040020,
        0x0000000e, 0x0000000c, 0x00000006, 0x0004002b, 0x0000000b, 0x00000010,
        0x00000001, 0x0004002b, 0x0000000b, 0x00000012, 0x00000002, 0x00050036,
        0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
        0x00060041, 0x0000000e, 0x0000000f, 0x0000000a, 0x0000000c, 0x0000000c,
        0x0003003e, 0x0000000f, 0x0000000d, 0x00060041, 0x0000000e, 0x00000011,
        0x0000000a, 0x0000000c, 0x00000010, 0x0003003e, 0x00000011, 0x0000000d,
        0x00060041, 0x0000000e, 0x00000013, 0x0000000a, 0x0000000c, 0x00000012,
        0x0003003e, 0x00000013, 0x0000000d, 0x000100fd, 0x00010038,
    };
    // read_buffer.comp: copies word 0 of binding 0 into binding 1
    //   #version 450
    //   layout(local_size_x = 1) in;
    //   layout(set = 0, binding = 0, std430) readonly buffer Source { uint inWords[]; };
    //   layout(set = 0, binding = 1, std430) writeonly buffer Sink { uint outWords[]; };
    //   void main() { outWords[0] = inWords[0]; }
    constexpr uint32_t k_ReadBufferSpirv[] =
    {
        0x07230203, 0x00010600, 0x000d000b, 0x00000015, 0x00000000, 0x00020011,
        0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000005,
        0x00000004, 0x6e69616d, 0x00000000, 0x0000000a, 0x00000010, 0x00060010,
        0x00000004, 0x00000011, 0x00000001, 0x00000001, 0x00000001, 0x00040047,
        0x00000007, 0x00000006, 0x00000004, 0x00030047, 0x00000008, 0x00000002,
        0x00040048, 0x00000008, 0x00000000, 0x00000019, 0x00050048, 0x00000008,
        0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x0000000a, 0x00000019,
        0x00040047, 0x0000000a, 0x00000021, 0x00000001, 0x00040047, 0x0000000a,
        0x00000022, 0x00000000, 0x00040047, 0x0000000d, 0x00000006, 0x00000004,
        0x00030047, 0x0000000e, 0x00000002, 0x00040048, 0x0000000e, 0x00000000,
        0x00000018, 0x00050048, 0x0000000e, 0x00000000, 0x00000023, 0x00000000,
        0x00030047, 0x00000010, 0x00000018, 0x00040047, 0x00000010, 0x00000021,
        0x00000000, 0x00040047, 0x00000010, 0x00000022, 0x00000000, 0x00020013,
        0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006,
        0x00000020, 0x00000000, 0x0003001d, 0x00000007, 0x00000006, 0x0003001e,
        0x00000008, 0x00000007, 0x00040020, 0x00000009, 0x0000000c, 0x00000008,
        0x0004003b, 0x00000009, 0x0000000a, 0x0000000c, 0x00040015, 0x0000000b,
        0x00000020, 0x00000001, 0x0004002b, 0x0000000b, 0x0000000c, 0x00000000,
        0x0003001d, 0x0000000d, 0x00000006, 0x0003001e, 0x0000000e, 0x0000000d,
        0x00040020, 0x0000000f, 0x0000000c, 0x0000000e, 0x0004003b, 0x0000000f,
        0x00000010, 0x0000000c, 0x00040020, 0x00000011, 0x0000000c, 0x00000006,
        0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
        0x00000005, 0x00060041, 0x00000011, 0x00000012, 0x00000010, 0x0000000c,
        0x0000000c, 0x0004003d, 0x00000006, 0x00000013, 0x00000012, 0x00060041,
        0x00000011, 0x00000014, 0x0000000a, 0x0000000c, 0x0000000c, 0x0003003e,
        0x00000014, 0x00000013, 0x000100fd, 0x00010038,
    };
    // sample_image.comp: copies texel (0, 0) of the sampled image at binding 0 into word 0 of binding 1
    //   #version 450
    //   #extension GL_EXT_samplerless_texture_functions : require
    //   layout(local_size_x = 1) in;
    //   layout(set = 0, binding = 0) uniform utexture2D sampledImage;
    //   layout(set = 0, binding = 1, std430) writeonly buffer Sink { uint outWords[]; };
    //   void main() { outWords[0] = texelFetch(sampledImage, ivec2(0, 0), 0).x; }
    constexpr uint32_t k_SampleImageSpirv[] =
    {
        0x07230203, 0x00010600, 0x000d000b, 0x00000019, 0x00000000, 0x00020011,
        0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000005,
        0x00000004, 0x6e69616d, 0x00000000, 0x0000000a, 0x0000000f, 0x00060010,
        0x00000004, 0x00000011, 0x00000001, 0x00000001, 0x00000001, 0x00040047,
        0x00000007, 0x00000006, 0x00000004, 0x00030047, 0x00000008, 0x00000002,
        0x00040048, 0x00000008, 0x00000000, 0x00000019, 0x00050048, 0x00000008,
        0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x0000000a, 0x00000019,
        0x00040047, 0x0000000a, 0x00000021, 0x00000001, 0x00040047, 0x0000000a,
        0x00000022, 0x00000000, 0x00040047, 0x0000000f, 0x00000021, 0x00000000,
        0x00040047, 0x0000000f, 0x00000022, 0x00000000, 0x00020013, 0x00000002,
        0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020,
        0x00000000, 0x0003001d, 0x00000007, 0x00000006, 0x0003001e, 0x00000008,
        0x00000007, 0x00040020, 0x00000009, 0x0000000c, 0x00000008, 0x0004003b,
        0x00000009, 0x0000000a, 0x0000000c, 0x00040015, 0x0000000b, 0x00000020,
        0x00000001, 0x0004002b, 0x0000000b, 0x0000000c, 0x00000000, 0x00090019,
        0x0000000d, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
        0x00000001, 0x00000000, 0x00040020, 0x0000000e, 0x00000000, 0x0000000d,
        0x0004003b, 0x0000000e, 0x0000000f, 0x00000000, 0x00040017, 0x00000011,
        0x0000000b, 0x00000002, 0x0005002c, 0x00000011, 0x00000012, 0x0000000c,
        0x0000000c, 0x00040017, 0x00000013, 0x00000006, 0x00000004, 0x00040020,
        0x00000017, 0x0000000c, 0x00000006, 0x00050036, 0x00000002, 0x00000004,
        0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000d,
        0x00000010, 0x0000000f, 0x0007005f, 0x00000013, 0x00000014, 0x00000010,
        0x00000012, 0x00002002, 0x0000000c, 0x00050051, 0x00000006, 0x00000016,
        0x00000014, 0x00000000, 0x00060041, 0x00000017, 0x00000018, 0x0000000a,
        0x0000000c, 0x0000000c, 0x0003003e, 0x00000018, 0x00000016, 0x000100fd,
        0x00010038,
    };
    // write_image.comp: stores 7 in texel (0, 0) of the storage image at binding 0
    //   #version 450
    //   layout(local_size_x = 1) in;
    //   layout(set = 0, binding = 0, r32ui) uniform writeonly uimage2D storageImage;
    //   void main() { imageStore(storageImage, ivec2(0, 0), uvec4(7u)); }
    constexpr uint32_t k_WriteImageSpirv[] =
    {
        0x07230203, 0x00010600, 0x000d000b, 0x00000012, 0x00000000, 0x00020011,
        0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
        0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000005,
        0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00060010, 0x00000004,
        0x00000011, 0x00000001, 0x00000001, 0x00000001, 0x00030047, 0x00000009,
        0x00000019, 0x00040047, 0x00000009, 0x00000021, 0x00000000, 0x00040047,
        0x00000009, 0x00000022, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
        0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020, 0x00000000,
        0x00090019, 0x00000007, 0x00000006, 0x00000001, 0x00000000, 0x00000000,
        0x00000000, 0x00000002, 0x00000021, 0x00040020, 0x00000008, 0x00000000,
        0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000000, 0x00040015,
        0x0000000b, 0x00000020, 0x00000001, 0x00040017, 0x0000000c, 0x0000000b,
        0x00000002, 0x0004002b, 0x0000000b, 0x0000000d, 0x00000000, 0x0005002c,
        0x0000000c, 0x0000000e, 0x0000000d, 0x0000000d, 0x00040017, 0x0000000f,
        0x00000006, 0x00000004, 0x0004002b, 0x00000006, 0x00000010, 0x00000007,
        0x0007002c, 0x0000000f, 0x00000011, 0x00000010, 0x00000010, 0x00000010,
        0x00000010, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
        0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000a, 0x00000009,
        0x00050063, 0x0000000a, 0x0000000e, 0x00000011, 0x00002000, 0x000100fd,
        0x00010038,
    };
    constexpr uint32_t k_WordCount = 4u;
    constexpr size_t k_BufferBytes = k_WordCount * sizeof(uint32_t);
    constexpr uint32_t k_ImageSize = 4u;
    constexpr uint32_t k_FirstTexel = 0xC0FFEE42u;

    /** One compute pipeline built from a test module, with the descriptor set layout of its bindings. */
    struct ComputeProbe
    {
        VkShaderModule Module{ VK_NULL_HANDLE };
        VkDescriptorSetLayout SetLayout{ VK_NULL_HANDLE };
        VkPipelineLayout PipelineLayout{ VK_NULL_HANDLE };
        VkPipeline Pipeline{ VK_NULL_HANDLE };
    };
}

// Records with CommandBuffer::Barrier, on barriers from TranslateUsageChanges. The device runs with full validation,
// synchronization validation included, so a barrier that is too small is a validation message.
//
// The validation layer ignores what a shader reads and writes through a descriptor unless its setting
// syncval_shader_accesses_heuristic is on, and it is off by default. Every dispatch in these tests reads or writes
// through a descriptor, so this suite turns the setting on for its own device, through the layer's environment variable.
// Instance does not set it, and no other suite in this executable sees it.
class BarrierGpuTest : public ResourceContextTest
{
public:
    static void SetUpTestSuite()
    {
        ASSERT_EQ(_putenv_s("VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC", "true"), 0);
        ResourceContextTest::SetUpTestSuite();
    }

    static void TearDownTestSuite()
    {
        ResourceContextTest::TearDownTestSuite();
        ASSERT_EQ(_putenv_s("VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC", ""), 0);
    }

protected:
    void SetUp() override
    {
        ResourceContextTest::SetUp();

        device = rhiSystem->GetDevice();
        ASSERT_NE(device, nullptr);
        vkDevice = device->Handle().As<VkDevice>();
        queue = device->GetGeneralQueue();
        commandPool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Graphics, device->GetQueueFamilyIndices().Graphics);
        ASSERT_TRUE(commandPool->AllocateCommandBuffers(1).IsSuccess());

        constexpr VkDescriptorPoolSize k_PoolSizes[]
        {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 },
        };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 8, 3, k_PoolSizes };
        ASSERT_EQ(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool), VK_SUCCESS);

        constexpr VkDescriptorType k_OneBuffer[] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
        constexpr VkDescriptorType k_TwoBuffers[] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
        constexpr VkDescriptorType k_ImageAndBuffer[] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
        constexpr VkDescriptorType k_OneStorageImage[] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
        writeBuffer = createProbe(k_WriteBufferSpirv, k_OneBuffer);
        readBuffer = createProbe(k_ReadBufferSpirv, k_TwoBuffers);
        sampleImage = createProbe(k_SampleImageSpirv, k_ImageAndBuffer);
        writeImage = createProbe(k_WriteImageSpirv, k_OneStorageImage);
    }

    void TearDown() override
    {
        if (device != nullptr)
        {
            device->WaitDeviceIdle();
        }

        for (const ComputeProbe& probe : { writeBuffer, readBuffer, sampleImage, writeImage })
        {
            vkDestroyPipeline(vkDevice, probe.Pipeline, nullptr);
            vkDestroyPipelineLayout(vkDevice, probe.PipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(vkDevice, probe.SetLayout, nullptr);
            vkDestroyShaderModule(vkDevice, probe.Module, nullptr);
        }

        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        commandPool.reset();
        ResourceContextTest::TearDown();
    }

    ComputeProbe createProbe(std::span<const uint32_t> words, std::span<const VkDescriptorType> binding_types)
    {
        ComputeProbe probe{};

        const VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, words.size_bytes(), words.data() };
        EXPECT_EQ(vkCreateShaderModule(vkDevice, &moduleInfo, nullptr, &probe.Module), VK_SUCCESS);

        std::vector<VkDescriptorSetLayoutBinding> bindings;

        for (uint32_t index = 0; index < binding_types.size(); ++index)
        {
            bindings.push_back(VkDescriptorSetLayoutBinding{ index, binding_types[index], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
        }

        const VkDescriptorSetLayoutCreateInfo setLayoutInfo
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, static_cast<uint32_t>(bindings.size()), bindings.data()
        };
        EXPECT_EQ(vkCreateDescriptorSetLayout(vkDevice, &setLayoutInfo, nullptr, &probe.SetLayout), VK_SUCCESS);

        const VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &probe.SetLayout, 0, nullptr };
        EXPECT_EQ(vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr, &probe.PipelineLayout), VK_SUCCESS);

        const VkComputePipelineCreateInfo pipelineInfo
        {
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, probe.Module, "main", nullptr },
            probe.PipelineLayout, VK_NULL_HANDLE, 0
        };
        EXPECT_EQ(vkCreateComputePipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &probe.Pipeline), VK_SUCCESS);

        return probe;
    }

    VkDescriptorSet allocateSet(const ComputeProbe& probe)
    {
        const VkDescriptorSetAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &probe.SetLayout };
        VkDescriptorSet set = VK_NULL_HANDLE;
        EXPECT_EQ(vkAllocateDescriptorSets(vkDevice, &allocateInfo, &set), VK_SUCCESS);
        return set;
    }

    void writeBufferDescriptor(VkDescriptorSet set, uint32_t binding, const GraphicsResource& buffer)
    {
        const VkDescriptorBufferInfo bufferInfo{ reinterpret_cast<VkBuffer>(buffer.VkHandle), 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr };
        vkUpdateDescriptorSets(vkDevice, 1, &write, 0, nullptr);
    }

    void writeImageDescriptor(VkDescriptorSet set, uint32_t binding, const GraphicsResource& image, VkDescriptorType type, VkImageLayout layout)
    {
        const VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, reinterpret_cast<VkImageView>(image.VkViewHandle), layout };
        const VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0, 1, type, &imageInfo, nullptr, nullptr };
        vkUpdateDescriptorSets(vkDevice, 1, &write, 0, nullptr);
    }

    /** Binds the probe and its set, and dispatches one workgroup. */
    void dispatch(rhi::CommandBuffer& cmd, const ComputeProbe& probe, VkDescriptorSet set)
    {
        const VkCommandBuffer vkCmd = cmd.Handle().As<VkCommandBuffer>();
        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, probe.Pipeline);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, probe.PipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(vkCmd, 1, 1, 1);
    }

    /** Records the barriers that the usage changes need, through CommandBuffer::Barrier. */
    void recordBarriers(rhi::CommandBuffer& cmd, std::span<const BufferBarrierInfo> buffer_changes, std::span<const ImageBarrierInfo> image_changes)
    {
        std::array<rhi::ImageBarrier, 4> imageBarriers{};
        const UsageBarriers translated = TranslateUsageChanges(buffer_changes, image_changes, imageBarriers);

        const bool hasGlobal = !IsEmpty(translated.Global);
        const std::span<const rhi::GlobalBarrier> globals = hasGlobal ? std::span<const rhi::GlobalBarrier>{ &translated.Global, 1u } : std::span<const rhi::GlobalBarrier>{};
        const std::span<const rhi::ImageBarrier> images{ imageBarriers.data(), static_cast<size_t>(translated.ImageCount) };

        cmd.Barrier(rhi::DependencyInfo{ globals, {}, images });
    }

    /** Copies the buffer into a readback buffer, after a barrier from `before` to a transfer read. */
    GraphicsResource recordBufferReadback(rhi::CommandBuffer& cmd, const GraphicsResource& source, BufferUsageBits before)
    {
        GraphicsResource readback = CreateReadbackBuffer(k_BufferBytes);

        const BufferBarrierInfo toCopy{ rhi::BufferHandle{ source.VkHandle }, before, BufferUsageBits::TransferSource };
        recordBarriers(cmd, std::span{ &toCopy, 1u }, {});

        const rhi::BufferCopy region{ 0u, 0u, k_BufferBytes };
        cmd.CopyBuffer(rhi::BufferHandle{ source.VkHandle }, rhi::BufferHandle{ readback.VkHandle }, std::span{ &region, 1u });

        // the transfer writes must reach the host that reads them after the fence wait
        const rhi::GlobalBarrier toHost{ PipelineStage::AllTransfer, Access::TransferWrite, PipelineStage::Host, Access::HostRead };
        cmd.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1u }, {}, {} });

        return readback;
    }

    /** Ends the command buffer, submits it behind the waits, and waits for the fence. */
    void submitAndWait(rhi::CommandBuffer& cmd, std::span<const rhi::SemaphoreSubmitInfo> waits)
    {
        ASSERT_TRUE(cmd.End().IsSuccess());

        rhi::Fence fence(device->Handle());
        const rhi::CommandBufferHandle cmdHandle = cmd.Handle();
        const rhi::SubmitInfo submit{ waits, std::span{ &cmdHandle, 1u }, {} };

        ASSERT_TRUE(rhi::QueueSubmit(queue, submit, fence.Handle()).IsSuccess());
        ASSERT_TRUE(fence.Wait(k_TimeoutNs));
    }

    /** Waits until the handles of the resource are known. The GPU waits for the upload through its ticket. */
    [[nodiscard]] bool waitUntilKnown(const rhi::ResourceFuture<GraphicsResource>& future)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);

        while (std::chrono::steady_clock::now() < deadline)
        {
            const rhi::FutureState state = future.State();

            if (state == rhi::FutureState::Created || state == rhi::FutureState::Complete)
            {
                return true;
            }

            if (state == rhi::FutureState::Failed)
            {
                return false;
            }

            std::this_thread::yield();
        }

        return false;
    }

    /** Waits until the upload of the resource is submitted, so that its ticket on the transfer timeline is real. */
    [[nodiscard]] bool waitUntilUploadIsSubmitted(const rhi::ResourceFuture<GraphicsResource>& future)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (future.State() == rhi::FutureState::Failed)
            {
                return false;
            }

            if (future.Completion().Submitted)
            {
                return true;
            }

            std::this_thread::yield();
        }

        return false;
    }

    [[nodiscard]] rhi::SemaphoreSubmitInfo waitFor(const rhi::ResourceFuture<GraphicsResource>& future) const
    {
        const rhi::GpuWait completion = future.Completion();
        return rhi::SemaphoreSubmitInfo{ completion.Semaphore, completion.Value, PipelineStage::AllCommands };
    }

    [[nodiscard]] BufferCreateInfo storageBuffer(BufferUsageBits usage) const
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = usage;
        info.Size = k_BufferBytes;
        return info;
    }

    [[nodiscard]] ImageCreateInfo wordImage(ImageUsageBits usage) const
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = usage;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::R32, ImageDataType::UInt };
        info.Width = k_ImageSize;
        info.Height = k_ImageSize;
        info.Depth = 1u;
        info.ViewInfo = ImageViewCreateInfo{};
        return info;
    }

    rhi::Device* device{ nullptr };
    VkDevice vkDevice{ VK_NULL_HANDLE };
    rhi::QueueHandle queue;
    std::unique_ptr<rhi::CommandPool> commandPool;
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    ComputeProbe writeBuffer;
    ComputeProbe readBuffer;
    ComputeProbe sampleImage;
    ComputeProbe writeImage;
};

TEST_F(BarrierGpuTest, ComputeWriteThenIndirectDispatchThatReadsIt)
{
    // the dispatch arguments are written by a shader, and read by the indirect dispatch that follows
    const BufferUsageBits argumentsUsage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::DispatchIndirect | BufferUsageBits::TransferSource;
    rhi::ResourceFuture<GraphicsResource> argumentsReply = context->CreateBuffer(storageBuffer(argumentsUsage), {});
    rhi::ResourceFuture<GraphicsResource> outputReply = context->CreateBuffer(storageBuffer(BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource), {});
    ASSERT_TRUE(waitUntilKnown(argumentsReply));
    ASSERT_TRUE(waitUntilKnown(outputReply));
    const GraphicsResource arguments = argumentsReply.Get();
    const GraphicsResource output = outputReply.Get();

    const VkDescriptorSet argumentsSet = allocateSet(writeBuffer);
    const VkDescriptorSet outputSet = allocateSet(writeBuffer);
    writeBufferDescriptor(argumentsSet, 0u, arguments);
    writeBufferDescriptor(outputSet, 0u, output);

    rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    dispatch(cmd, writeBuffer, argumentsSet);

    const BufferBarrierInfo toIndirect{ rhi::BufferHandle{ arguments.VkHandle }, BufferUsageBits::ComputeShaderUAV, BufferUsageBits::DispatchIndirect };
    recordBarriers(cmd, std::span{ &toIndirect, 1u }, {});

    const VkCommandBuffer vkCmd = cmd.Handle().As<VkCommandBuffer>();
    vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, writeBuffer.Pipeline);
    vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, writeBuffer.PipelineLayout, 0, 1, &outputSet, 0, nullptr);
    vkCmdDispatchIndirect(vkCmd, reinterpret_cast<VkBuffer>(arguments.VkHandle), 0);

    const GraphicsResource argumentsReadback = recordBufferReadback(cmd, arguments, BufferUsageBits::ComputeShaderUAV);
    const GraphicsResource outputReadback = recordBufferReadback(cmd, output, BufferUsageBits::ComputeShaderUAV);
    submitAndWait(cmd, {});

    // the arguments are 1, 1, 1, so the indirect dispatch ran one workgroup that wrote the output
    const std::vector<uint32_t> argumentWords = ReadWordsAndDestroy(argumentsReadback, k_BufferBytes);
    const std::vector<uint32_t> outputWords = ReadWordsAndDestroy(outputReadback, k_BufferBytes);

    for (uint32_t index = 0; index < 3u; ++index)
    {
        EXPECT_EQ(argumentWords[index], 1u);
        EXPECT_EQ(outputWords[index], 1u);
    }

    DestroyAndWait(arguments);
    DestroyAndWait(output);
}

TEST_F(BarrierGpuTest, ImageUploadThenComputeSample)
{
    const std::vector<uint32_t> texels(k_ImageSize * k_ImageSize, k_FirstTexel);
    RhiImageResourceData data{};
    data.Data = std::as_bytes(std::span{ texels });
    data.Width = k_ImageSize;
    data.Height = k_ImageSize;

    // the image stays in the transfer destination layout after the upload, so the barrier below has a layout to leave
    rhi::ResourceFuture<GraphicsResource> imageReply = context->CreateImage(wordImage(ImageUsageBits::ComputeShaderSRV), std::span{ &data, 1u }, ImageUsageBits::TransferDestination);
    rhi::ResourceFuture<GraphicsResource> sinkReply = context->CreateBuffer(storageBuffer(BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource), {});
    ASSERT_TRUE(waitUntilKnown(imageReply));
    ASSERT_TRUE(waitUntilKnown(sinkReply));
    ASSERT_TRUE(waitUntilUploadIsSubmitted(imageReply));
    const GraphicsResource image = imageReply.Get();
    const GraphicsResource sink = sinkReply.Get();

    const VkDescriptorSet set = allocateSet(sampleImage);
    writeImageDescriptor(set, 0u, image, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    writeBufferDescriptor(set, 1u, sink);

    rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    const ImageBarrierInfo toSampled{ rhi::ImageHandle{ image.VkHandle }, ImageUsageBits::TransferDestination, ImageUsageBits::ComputeShaderSRV, ImageRange{ ImageAspectFlags::Color, 0u, 1u, 0u, 1u } };
    recordBarriers(cmd, {}, std::span{ &toSampled, 1u });
    dispatch(cmd, sampleImage, set);
    const GraphicsResource readback = recordBufferReadback(cmd, sink, BufferUsageBits::ComputeShaderUAV);

    const rhi::SemaphoreSubmitInfo uploadWait = waitFor(imageReply);
    submitAndWait(cmd, std::span{ &uploadWait, 1u });

    const std::vector<uint32_t> words = ReadWordsAndDestroy(readback, k_BufferBytes);
    EXPECT_EQ(words[0], k_FirstTexel);

    DestroyAndWait(image);
    DestroyAndWait(sink);
}

TEST_F(BarrierGpuTest, ComputeReadThenComputeWriteOfOneBuffer)
{
    const std::vector<uint32_t> fives(k_WordCount, 5u);
    const RhiBufferResourceData initial{ std::as_bytes(std::span{ fives }), 0u };
    const BufferUsageBits sharedUsage = BufferUsageBits::ComputeShaderSRV | BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource;
    rhi::ResourceFuture<GraphicsResource> sharedReply = context->CreateBuffer(storageBuffer(sharedUsage), std::span{ &initial, 1u });
    rhi::ResourceFuture<GraphicsResource> sinkReply = context->CreateBuffer(storageBuffer(BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource), {});
    ASSERT_TRUE(waitUntilKnown(sharedReply));
    ASSERT_TRUE(waitUntilKnown(sinkReply));
    ASSERT_TRUE(waitUntilUploadIsSubmitted(sharedReply));
    const GraphicsResource shared = sharedReply.Get();
    const GraphicsResource sink = sinkReply.Get();

    const VkDescriptorSet readSet = allocateSet(readBuffer);
    const VkDescriptorSet writeSet = allocateSet(writeBuffer);
    writeBufferDescriptor(readSet, 0u, shared);
    writeBufferDescriptor(readSet, 1u, sink);
    writeBufferDescriptor(writeSet, 0u, shared);

    rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    // the shader reads the shared buffer
    dispatch(cmd, readBuffer, readSet);

    // and the next dispatch writes it: a write after a read, which needs the source stages and no source access
    const BufferBarrierInfo toWrite{ rhi::BufferHandle{ shared.VkHandle }, BufferUsageBits::ComputeShaderSRV, BufferUsageBits::ComputeShaderUAV };
    recordBarriers(cmd, std::span{ &toWrite, 1u }, {});
    dispatch(cmd, writeBuffer, writeSet);

    const GraphicsResource sharedReadback = recordBufferReadback(cmd, shared, BufferUsageBits::ComputeShaderUAV);
    const GraphicsResource sinkReadback = recordBufferReadback(cmd, sink, BufferUsageBits::ComputeShaderUAV);

    const rhi::SemaphoreSubmitInfo uploadWait = waitFor(sharedReply);
    submitAndWait(cmd, std::span{ &uploadWait, 1u });

    const std::vector<uint32_t> sharedWords = ReadWordsAndDestroy(sharedReadback, k_BufferBytes);
    const std::vector<uint32_t> sinkWords = ReadWordsAndDestroy(sinkReadback, k_BufferBytes);

    // the read saw the upload, and the write came after the read
    EXPECT_EQ(sinkWords[0], 5u);
    EXPECT_EQ(sharedWords[0], 1u);
    EXPECT_EQ(sharedWords[3], 5u);

    DestroyAndWait(shared);
    DestroyAndWait(sink);
}

TEST_F(BarrierGpuTest, DiscardThenComputeStorageWrite)
{
    const ImageUsageBits usage = ImageUsageBits::ComputeShaderUAV | ImageUsageBits::TransferSource;
    rhi::ResourceFuture<GraphicsResource> imageReply = context->CreateImage(wordImage(usage));
    ASSERT_TRUE(waitUntilKnown(imageReply));
    const GraphicsResource image = imageReply.Get();

    GraphicsResource readback = CreateReadbackBuffer(k_ImageSize * k_ImageSize * sizeof(uint32_t));

    const VkDescriptorSet set = allocateSet(writeImage);
    writeImageDescriptor(set, 0u, image, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);

    rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());

    // the image has no contents yet, so the barrier starts from the undefined layout
    const rhi::ImageHandle imageHandle{ image.VkHandle };
    const ImageRange range{ ImageAspectFlags::Color, 0u, 1u, 0u, 1u };
    const ImageBarrierInfo toStorage{ imageHandle, ImageUsageBits::Discard, ImageUsageBits::ComputeShaderUAV, range };
    recordBarriers(cmd, {}, std::span{ &toStorage, 1u });
    dispatch(cmd, writeImage, set);

    const ImageBarrierInfo toCopy{ imageHandle, ImageUsageBits::ComputeShaderUAV, ImageUsageBits::TransferSource, range };
    recordBarriers(cmd, {}, std::span{ &toCopy, 1u });

    rhi::BufferImageCopy region{};
    region.Width = k_ImageSize;
    region.Height = k_ImageSize;
    cmd.CopyImageToBuffer(imageHandle, rhi::ImageLayout::TransferSource, rhi::BufferHandle{ readback.VkHandle }, std::span{ &region, 1u });

    const rhi::GlobalBarrier toHost{ PipelineStage::AllTransfer, Access::TransferWrite, PipelineStage::Host, Access::HostRead };
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1u }, {}, {} });
    submitAndWait(cmd, {});

    const std::vector<uint32_t> words = ReadWordsAndDestroy(readback, k_ImageSize * k_ImageSize * sizeof(uint32_t));
    EXPECT_EQ(words[0], 7u);

    DestroyAndWait(image);
}
