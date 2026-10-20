#include "ResourceBarriers.hpp"
#include "ResourceFlags.hpp"
#include "RhiHandle.hpp"
#include "VulkanConversions.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

/**
 * Quick note about the barriers applied here: we pretty much always use global barriers, because with the synchronization2 featureset and the updates
 * to Vulkan that have occurred over time, these are all we need. IHVs have also confirmed that global barriers have no extra performance overhead in
 * most use cases, so we can probably just count on doing individual optimizations as needed to barriers if it comes up.
*/

namespace
{
    [[nodiscard]] bool HasAny(BufferUsageBits bits, BufferUsageBits mask) noexcept;
    [[nodiscard]] bool HasAny(ImageUsageBits bits, ImageUsageBits mask) noexcept;
    void RecordDependency(VkCommandBuffer cmd, std::span<const VkMemoryBarrier2> memory_barriers, std::span<const VkImageMemoryBarrier2> image_barriers) noexcept;
    [[nodiscard]] VkMemoryBarrier2 MakeBufferMemoryBarrier(const BufferBarrierInfo& barrier) noexcept;
    [[nodiscard]] VkMemoryBarrier2 MakeImageMemoryBarrier(const ImageBarrierInfo& barrier) noexcept;
    VkPipelineStageFlags2 StageFlagsFromBufferUsageBits(const BufferUsageBits bits) noexcept;
    VkAccessFlags2 AccessFlagsfromBufferUsageBits(const BufferUsageBits bits) noexcept;
    VkAccessFlags2 GetAccessMaskFromImageUsageBits(const ImageUsageBits bits) noexcept;
    VkPipelineStageFlags2 GetPipelineStageFromImageUsageBits(const ImageUsageBits bits) noexcept;

    struct ImageLayouts
    {
        VkImageLayout BeforeLayout;
        VkImageLayout AfterLayout;
    };

    [[nodiscard]] std::optional<ImageLayouts> GetImageLayouts(const ImageUsageBits before, const ImageUsageBits after) noexcept;
    [[nodiscard]] std::optional<VkImageMemoryBarrier2> MakeLayoutTransitionBarrier(const ImageBarrierInfo& barrier) noexcept;
}

void ExecuteBufferBarrier(rhi::CommandBufferHandle cmd, const BufferBarrierInfo& barrier)
{
    ExecuteBufferBarriers(cmd, std::span<const BufferBarrierInfo>{ &barrier, 1u });
}

void ExecuteBufferBarriers(rhi::CommandBufferHandle cmd, std::span<const BufferBarrierInfo> barriers)
{
    ExecuteBarriers(cmd, barriers, std::span<const ImageBarrierInfo>{});
}

void ExecuteImageBarrier(rhi::CommandBufferHandle cmd, const ImageBarrierInfo& barrier)
{
    ExecuteImageBarriers(cmd, std::span<const ImageBarrierInfo>{ &barrier, 1u });
}

void ExecuteImageBarriers(rhi::CommandBufferHandle cmd, std::span<const ImageBarrierInfo> barriers)
{
    ExecuteBarriers(cmd, std::span<const BufferBarrierInfo>{}, barriers);
}

void ExecuteBarriers(rhi::CommandBufferHandle cmd,
                     std::span<const BufferBarrierInfo> buffer_barriers,
                     std::span<const ImageBarrierInfo> image_barriers)
{
    // Images need an image barrier only when the usages ask for a layout transition: the transfer destination to
    // shader resource view case, or GBuffer render targets to present sources. Otherwise a global barrier does it.
    // All of them go in one dependency, so a batch costs one vkCmdPipelineBarrier2 call
    std::vector<VkMemoryBarrier2> memoryBarriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    memoryBarriers.reserve(buffer_barriers.size() + image_barriers.size());
    imageBarriers.reserve(image_barriers.size());

    for (const BufferBarrierInfo& barrier : buffer_barriers)
    {
        memoryBarriers.push_back(MakeBufferMemoryBarrier(barrier));
    }

    for (const ImageBarrierInfo& barrier : image_barriers)
    {
        std::optional<VkImageMemoryBarrier2> layoutTransition = MakeLayoutTransitionBarrier(barrier);
        if (layoutTransition.has_value())
        {
            imageBarriers.push_back(*layoutTransition);
        }
        else
        {
            memoryBarriers.push_back(MakeImageMemoryBarrier(barrier));
        }
    }

    RecordDependency(cmd.As<VkCommandBuffer>(), memoryBarriers, imageBarriers);
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

    void RecordDependency(VkCommandBuffer cmd, std::span<const VkMemoryBarrier2> memory_barriers, std::span<const VkImageMemoryBarrier2> image_barriers) noexcept
    {
        if (memory_barriers.empty() && image_barriers.empty())
        {
            return;
        }

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(memory_barriers.size());
        dependencyInfo.pMemoryBarriers = memory_barriers.data();
        dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size());
        dependencyInfo.pImageMemoryBarriers = image_barriers.data();
        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }

    VkMemoryBarrier2 MakeBufferMemoryBarrier(const BufferBarrierInfo& barrier) noexcept
    {
        VkMemoryBarrier2 memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.srcStageMask = StageFlagsFromBufferUsageBits(barrier.BeforeUsage);
        memoryBarrier.srcAccessMask = AccessFlagsfromBufferUsageBits(barrier.BeforeUsage);
        memoryBarrier.dstStageMask = StageFlagsFromBufferUsageBits(barrier.AfterUsage);
        memoryBarrier.dstAccessMask = AccessFlagsfromBufferUsageBits(barrier.AfterUsage);
        return memoryBarrier;
    }

    VkMemoryBarrier2 MakeImageMemoryBarrier(const ImageBarrierInfo& barrier) noexcept
    {
        VkMemoryBarrier2 memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.srcStageMask = GetPipelineStageFromImageUsageBits(barrier.BeforeUsage);
        memoryBarrier.srcAccessMask = GetAccessMaskFromImageUsageBits(barrier.BeforeUsage);
        memoryBarrier.dstStageMask = GetPipelineStageFromImageUsageBits(barrier.AfterUsage);
        memoryBarrier.dstAccessMask = GetAccessMaskFromImageUsageBits(barrier.AfterUsage);
        return memoryBarrier;
    }

    VkPipelineStageFlags2 StageFlagsFromBufferUsageBits(const BufferUsageBits bits) noexcept
    {
        VkPipelineStageFlags2 stageFlags = 0;

        if (HasAny(bits, BufferUsageBits::AllIndirect))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        }

        if (HasAny(bits, BufferUsageBits::VertexOrIndex))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            stageFlags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllVertexStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllFragmentStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllComputeStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllRaytracingStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        }

        if (HasAny(bits, BufferUsageBits::AllTransfer))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AccelerationStructureBuild | BufferUsageBits::ShaderBindingTable | BufferUsageBits::AccelerationStructureBuildScratch))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }

        if (HasAny(bits, BufferUsageBits::ConditionalRendering))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT;
        }

        return stageFlags;
    }

    VkAccessFlags2 AccessFlagsfromBufferUsageBits(const BufferUsageBits bits) noexcept
    {
        VkAccessFlags2 accessFlags = 0;

        if (HasAny(bits, BufferUsageBits::AllIndirect))
        {
            accessFlags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }

        if (HasAny(bits, BufferUsageBits::VertexOrIndex))
        {
            accessFlags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            accessFlags |= VK_ACCESS_2_INDEX_READ_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllShaderUBO))
        {
            accessFlags |= VK_ACCESS_2_UNIFORM_READ_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllShaderSRV))
        {
            accessFlags |= VK_ACCESS_2_SHADER_READ_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AllShaderUAV))
        {
            accessFlags |= VK_ACCESS_2_SHADER_READ_BIT;
            accessFlags |= VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        if (HasAny(bits, BufferUsageBits::CopySource))
        {
            accessFlags |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        if (HasAny(bits, BufferUsageBits::CopyDestination))
        {
            accessFlags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        if (HasAny(bits, BufferUsageBits::AccelerationStructureBuild))
        {
            accessFlags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }

        if (HasAny(bits, BufferUsageBits::ShaderBindingTable))
        {
            accessFlags |= VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR;
        }

        if (HasAny(bits, BufferUsageBits::AccelerationStructureBuildScratch))
        {
            accessFlags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            accessFlags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }

        if (HasAny(bits, BufferUsageBits::ConditionalRendering))
        {
            accessFlags |= VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT;
        }

        return accessFlags;
    }

    VkAccessFlags2 GetAccessMaskFromImageUsageBits(const ImageUsageBits bits) noexcept
    {
        VkAccessFlags2 accessMask = 0;

        if (HasAny(bits, ImageUsageBits::AllShaderSRV))
        {
            accessMask |= VK_ACCESS_2_SHADER_READ_BIT;
        }

        // image UAVs are shader storage images, formatted buffers. I don't think we present them through the API though, especially through slang?
        // They should just become buffers so we can use device addresses and buffer references I think
        if (HasAny(bits, ImageUsageBits::AllShaderUAV))
        {
            accessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }

        if (HasAny(bits, ImageUsageBits::RenderTargetRead))
        {
            accessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        }

        if (HasAny(bits, ImageUsageBits::RenderTargetWrite))
        {
            accessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllDsRead))
        {
            accessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllDsWrite))
        {
            accessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        if (HasAny(bits, ImageUsageBits::SampledImage))
        {
            accessMask |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }

        if (HasAny(bits, ImageUsageBits::ShadingRateImage))
        {
            accessMask |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
        }

        if (HasAny(bits, ImageUsageBits::TransferDestination))
        {
            accessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        if (HasAny(bits, ImageUsageBits::TransferSource))
        {
            accessMask |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        return accessMask;
    }

    VkPipelineStageFlags2 GetPipelineStageFromImageUsageBits(const ImageUsageBits bits) noexcept
    {
        VkPipelineStageFlags2 stageFlags = 0;

        if (HasAny(bits, ImageUsageBits::AllVertexStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllFragmentStage | ImageUsageBits::SampledImage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllRenderTarget))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllComputeStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllRaytracingStage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        }

        if (HasAny(bits, ImageUsageBits::PresentSource))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        }

        if (HasAny(bits, ImageUsageBits::AllDsWrite))
        {
            // fixed: this was FRAGMENT_SHADER. depth and stencil attachment writes happen in the fragment test stages,
            // and validation rejects the attachment write access on the fragment shader stage
            stageFlags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        }

        // but conversely, depth or stencil *reads* can occur as soon as early fragment testing!
        if (HasAny(bits, ImageUsageBits::AllDsRead))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        }

        if (HasAny(bits, ImageUsageBits::ShadingRateImage))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
        }

        if (HasAny(bits, ImageUsageBits::AllTransfer))
        {
            stageFlags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        }

        return stageFlags;
    }

    std::optional<ImageLayouts> GetImageLayouts(const ImageUsageBits before, const ImageUsageBits after) noexcept
    {
        // If either usage is invalid, we can't do anything meaningful. if they're the same, no transition needed
        if ((before == ImageUsageBits::Invalid || after == ImageUsageBits::Invalid) || (before == after))
        {
            return std::nullopt;
        }

        // note that after this point, we know the two usages are at least asymmetric. we will then find the
        // layouts, and depending on how that returns (which can change based on version) we know if we need
        // to execute the layout transition
        VkImageLayout beforeLayout = ImageLayoutFromUsageBits(before);
        VkImageLayout afterLayout = ImageLayoutFromUsageBits(after);

        if (beforeLayout == VK_IMAGE_LAYOUT_UNDEFINED || afterLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            // If we couldn't determine a valid layout for either usage, we can't proceed
            return std::nullopt;
        }

        if (beforeLayout == afterLayout)
        {
            // No transition needed if layouts are the same
            return std::nullopt;
        }

        return ImageLayouts{beforeLayout, afterLayout};
    }

    std::optional<VkImageMemoryBarrier2> MakeLayoutTransitionBarrier(const ImageBarrierInfo& barrier) noexcept
    {
        std::optional<ImageLayouts> layouts = GetImageLayouts(barrier.BeforeUsage, barrier.AfterUsage);
        
        if (!layouts.has_value())
        {
            return std::nullopt;
        }

        VkImageMemoryBarrier2 imageBarrier{};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.srcStageMask = GetPipelineStageFromImageUsageBits(barrier.BeforeUsage);
        imageBarrier.srcAccessMask = GetAccessMaskFromImageUsageBits(barrier.BeforeUsage);
        imageBarrier.dstStageMask = GetPipelineStageFromImageUsageBits(barrier.AfterUsage);
        imageBarrier.dstAccessMask = GetAccessMaskFromImageUsageBits(barrier.AfterUsage);
        imageBarrier.oldLayout = layouts->BeforeLayout;
        imageBarrier.newLayout = layouts->AfterLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = barrier.Handle.As<VkImage>();
        // fixed: the range was left zeroed, and a layout transition with an empty aspect mask is invalid
        imageBarrier.subresourceRange = VkImageSubresourceRange
        {
            ToVkImageAspect(barrier.Range.AspectMask),
            barrier.Range.BaseMipLevel,
            barrier.Range.LevelCount,
            barrier.Range.BaseArrayLayer,
            barrier.Range.LayerCount,
        };

        return imageBarrier;
    }

}
