#include "ResourceBarriers.hpp"
#include <cassert>
#include <optional>
#include <vulkan/vulkan_core.h>

/**
 * Quick note about the barriers applied here: we pretty much always use global barriers, because with the synchronization2 featureset and the updates
 * to Vulkan that have occurred over time, these are all we need. IHVs have also confirmed that global barriers have no extra performance overhead in
 * most use cases, so we can probably just count on doing individual optimizations as needed to barriers if it comes up.
*/

namespace
{
    VkPipelineStageFlags2 StageFlagsFromBufferUsageBits(const BufferUsageBits bits) noexcept;
    VkAccessFlags2 AccessFlagsfromBufferUsageBits(const BufferUsageBits bits) noexcept;
    VkAccessFlags2 GetAccessMaskFromImageUsageBits(const ImageUsageBits bits) noexcept;
    VkPipelineStageFlags2 GetPipelineStageFromImageUsageBits(const ImageUsageBits bits) noexcept;
    
    struct ImageLayouts
    {
        VkImageLayout BeforeLayout;
        VkImageLayout AfterLayout;
    };

    VkImageLayout GetLayoutFromUsageBits(const ImageUsageBits bits) noexcept;
    std::optional<ImageLayouts> GetImageLayouts(const ImageUsageBits before, const ImageUsageBits after) noexcept;
}

void ExecuteBufferBarrier(VkCommandBuffer cmd, const BufferBarrierInfo& Barrier)
{
    VkMemoryBarrier2 memory_barrier = {};
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    memory_barrier.pNext = nullptr;

    VkPipelineStageFlags2 before_stages = StageFlagsFromBufferUsageBits(Barrier.BeforeUsage);
    VkPipelineStageFlags2 after_stages = StageFlagsFromBufferUsageBits(Barrier.AfterUsage);

    VkAccessFlags2 before_access_flags = AccessFlagsfromBufferUsageBits(Barrier.BeforeUsage);
    VkAccessFlags2 after_access_flags = AccessFlagsfromBufferUsageBits(Barrier.AfterUsage);

    memory_barrier.srcStageMask = before_stages;
    memory_barrier.dstStageMask = after_stages;
    memory_barrier.srcAccessMask = before_access_flags;
    memory_barrier.dstAccessMask = after_access_flags;

    VkDependencyInfo dependency_info = {};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.pNext = nullptr;
    dependency_info.memoryBarrierCount = 1;
    dependency_info.pMemoryBarriers = &memory_barrier;
    dependency_info.bufferMemoryBarrierCount = 0;
    dependency_info.pBufferMemoryBarriers = nullptr;
    dependency_info.imageMemoryBarrierCount = 0;
    dependency_info.pImageMemoryBarriers = nullptr;

    vkCmdPipelineBarrier2(cmd, &dependency_info);
}

void ExecuteBufferBarriers(VkCommandBuffer cmd, const size_t BarrierCount, const BufferBarrierInfo* Barriers)
{
    if (BarrierCount == 0 || Barriers == nullptr)
    {
        return;
    }

    for (size_t i = 0; i < BarrierCount; ++i)
    {
        ExecuteBufferBarrier(cmd, Barriers[i]);
    }
}

void ExecuteImageBarrier(VkCommandBuffer cmd, const ImageBarrierInfo& Barrier)
{
    // Little more assessment needed here: if there is a layout transition from the two simplified states Vulkan uses now, we'll need to
    // do an image barrier instead of a global barrier. We can figure out if that will be needed by looking at the before and after usage bits.
    // For example, if the image is transitioning from a transfer destination to a shader resource view, we need to insert an image memory barrier.
    // Another example: GBuffer resources. From writeable rendertargets to present sources.
    // If no layout transition is needed, a global barrier might suffice.

    auto layouts = GetImageLayouts(Barrier.BeforeUsage, Barrier.AfterUsage);
    if (layouts)
    {
        // If we have a valid layout transition, we need to insert an image memory barrier.
        VkImageMemoryBarrier2 imageBarrier{};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.pNext = nullptr;
        imageBarrier.srcStageMask = GetPipelineStageFromImageUsageBits(Barrier.BeforeUsage);
        imageBarrier.srcAccessMask = GetAccessMaskFromImageUsageBits(Barrier.BeforeUsage);
        imageBarrier.dstStageMask = GetPipelineStageFromImageUsageBits(Barrier.AfterUsage);
        imageBarrier.dstAccessMask = GetAccessMaskFromImageUsageBits(Barrier.AfterUsage);
        imageBarrier.oldLayout = layouts->BeforeLayout;
        imageBarrier.newLayout = layouts->AfterLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = reinterpret_cast<VkImage>(Barrier.Handle);
        
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = nullptr;
        dependencyInfo.memoryBarrierCount = 0;
        dependencyInfo.pMemoryBarriers = nullptr;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers = nullptr;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }
    else
    {
        // No layout transition needed; a global barrier might suffice.
        VkMemoryBarrier2 memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.pNext = nullptr;
        memoryBarrier.srcStageMask = GetPipelineStageFromImageUsageBits(Barrier.BeforeUsage);
        memoryBarrier.srcAccessMask = GetAccessMaskFromImageUsageBits(Barrier.BeforeUsage);
        memoryBarrier.dstStageMask = GetPipelineStageFromImageUsageBits(Barrier.AfterUsage);
        memoryBarrier.dstAccessMask = GetAccessMaskFromImageUsageBits(Barrier.AfterUsage);

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = nullptr;
        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &memoryBarrier;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers = nullptr;
        dependencyInfo.imageMemoryBarrierCount = 0;
        dependencyInfo.pImageMemoryBarriers = nullptr;

        vkCmdPipelineBarrier2(cmd, &dependencyInfo);
    }
}

namespace
{

    VkPipelineStageFlags2 StageFlagsFromBufferUsageBits(const BufferUsageBits bits) noexcept
    {
        VkPipelineStageFlags2 stage_flags = 0;

        if (bits & BufferUsageBits::IndirectCompute || bits & BufferUsageBits::IndirectDraw || bits & BufferUsageBits::IndirectRaytracing)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        }

        if (bits & BufferUsageBits::VertexOrIndex)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            stage_flags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        }

        if (bits & BufferUsageBits::VertexShaderUBO || bits & BufferUsageBits::VertexShaderSRV || bits & BufferUsageBits::VertexShaderUAV)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        }

        if (bits & BufferUsageBits::FragmentShaderUBO || bits & BufferUsageBits::FragmentShaderSRV || bits & BufferUsageBits::FragmentShaderUAV)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }

        if (bits & BufferUsageBits::ComputeShaderUBO || bits & BufferUsageBits::ComputeShaderSRV || bits & BufferUsageBits::ComputeShaderUAV)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        if (bits & BufferUsageBits::RaytracingShaderUBO || bits & BufferUsageBits::RaytracingShaderSRV || bits & BufferUsageBits::RaytracingShaderUAV)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        }

        if (bits & BufferUsageBits::CopySource || bits & BufferUsageBits::CopyDestination)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        }

        if (bits & BufferUsageBits::AccelerationStructureBuild || bits & BufferUsageBits::ShaderBindingTable || bits & BufferUsageBits::AccelerationStructureBuildScratch)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }

        if (bits & BufferUsageBits::ConditionalRendering)
        {
            stage_flags |= VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT;
        }

        return stage_flags;
    }

    VkAccessFlags2 AccessFlagsfromBufferUsageBits(const BufferUsageBits bits) noexcept
    {
        VkAccessFlags2 access_flags = 0;

        if ((bits & BufferUsageBits::AllIndirect) != BufferUsageBits::Invalid)
        {
            access_flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }

        if ((bits & BufferUsageBits::VertexOrIndex) != BufferUsageBits::Invalid)
        {
            access_flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            access_flags |= VK_ACCESS_2_INDEX_READ_BIT;
        }

        if ((bits & BufferUsageBits::AllShaderUBO) != BufferUsageBits::Invalid)
        {
            access_flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
        }

        if ((bits & BufferUsageBits::AllShaderSRV) != BufferUsageBits::Invalid)
        {
            access_flags |= VK_ACCESS_2_SHADER_READ_BIT;
        }

        if ((bits & BufferUsageBits::AllShaderUAV) != BufferUsageBits::Invalid)
        {
            access_flags |= VK_ACCESS_2_SHADER_READ_BIT;
            access_flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        if (bits & BufferUsageBits::RaytracingShaderUBO || bits & BufferUsageBits::RaytracingShaderSRV || bits & BufferUsageBits::RaytracingShaderUAV)
        {
            access_flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
            access_flags |= VK_ACCESS_2_SHADER_READ_BIT;
            access_flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
        }

        if (bits & BufferUsageBits::CopySource)
        {
            access_flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }

        if (bits & BufferUsageBits::CopyDestination)
        {
            access_flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        if (bits & BufferUsageBits::AccelerationStructureBuild)
        {
            access_flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }

        if (bits & BufferUsageBits::ShaderBindingTable)
        {
            access_flags |= VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR;
        }

        if (bits & BufferUsageBits::AccelerationStructureBuildScratch)
        {
            access_flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            access_flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }

        if (bits & BufferUsageBits::ConditionalRendering)
        {
            access_flags |= VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT;
        }

        return access_flags;
    }

    VkAccessFlags2 GetAccessMaskFromImageUsageBits(const ImageUsageBits bits) noexcept
    {
        VkAccessFlags2 accessMask = 0;

        if ((bits & ImageUsageBits::AllShaderSRV) != ImageUsageBits::Invalid)
        {
            accessMask |= VK_ACCESS_2_SHADER_READ_BIT;
        }

        // image UAVs are shader storage images, formatted buffers. I don't think we present them through the API though, especially through slang?
        // They should just become buffers so we can use device addresses and buffer references I think
        if ((bits & ImageUsageBits::AllShaderUAV) != ImageUsageBits::Invalid)
        {
            accessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }

        if (bits & ImageUsageBits::RenderTargetRead)
        {
            accessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        }

        if (bits & ImageUsageBits::RenderTargetWrite)
        {
            accessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if ((bits & ImageUsageBits::AllDsRead) != ImageUsageBits::Invalid)
        {
            accessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }

        if ((bits & ImageUsageBits::AllDsWrite) != ImageUsageBits::Invalid)
        {
            accessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        if (bits & ImageUsageBits::SampledImage)
        {
            accessMask |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }

        if (bits & ImageUsageBits::ShadingRateImage)
        {
            accessMask |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
        }

        return accessMask;
    }

    VkPipelineStageFlags2 GetPipelineStageFromImageUsageBits(const ImageUsageBits bits) noexcept
    {
        VkPipelineStageFlags2 stageFlags = 0;

        if ((bits & ImageUsageBits::AllVertexStage) != ImageUsageBits::Invalid)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        }

        if ((bits & ImageUsageBits::AllFragmentStage) != ImageUsageBits::Invalid)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }

        if ((bits & ImageUsageBits::AllRenderTarget) != ImageUsageBits::Invalid)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        if ((bits & ImageUsageBits::AllComputeStage) != ImageUsageBits::Invalid)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        if ((bits & ImageUsageBits::AllRaytracingStage) != ImageUsageBits::Invalid)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        }

        if (bits & ImageUsageBits::PresentSource)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        }

        if ((bits & ImageUsageBits::AllDsWrite) != ImageUsageBits::Invalid)
        {
            // depth or stencil *writes* cannot occur until frag shader
            stageFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }

        // but conversely, depth or stencil *reads* can occur as soon as early fragment testing!
        if ((bits & ImageUsageBits::AllDsRead) != ImageUsageBits::Invalid)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        }

        if (bits & ImageUsageBits::ShadingRateImage)
        {
            stageFlags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
        }

        return stageFlags;
    }

    VkImageLayout GetLayoutFromUsageBits(const ImageUsageBits bits) noexcept
    {
        if (bits & ImageUsageBits::PresentSource)
        {
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        // as of VK_KHR_synchronization2, we can use VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL for all read-only usages of our attachments and driver will distinguish between depth/RTV
        else if ((bits & ImageUsageBits::RenderTargetRead) ||
                (bits & ImageUsageBits::DepthRead) ||
                (bits & ImageUsageBits::StencilRead) ||
                (bits & ImageUsageBits::DepthStencilRead))
        {
            return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        }
        else if ((bits & ImageUsageBits::AllAttachmentWrite) != ImageUsageBits::Invalid)
        {
            // If any attachment write flag is set, we use the VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL layout introduced in Vulkan 1.2 and synchronization2.
            // The driver or API machinery should disambiguate between specifics here (as far as I know, it often doesn't, and attachments writ large are a special case that share similar machineries regardless of format).
            return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        }
        else if (bits & ImageUsageBits::TransferDestination)
        {
            // we should rarely hit this. the transfer system should be doing all of this work for us...
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        else if ((bits & ImageUsageBits::AllShaderSRV) != ImageUsageBits::Invalid)
        {
            return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        }
        else if ((bits & ImageUsageBits::AllShaderUAV) != ImageUsageBits::Invalid)
        {
            return VK_IMAGE_LAYOUT_GENERAL;
        }
        else if (bits & ImageUsageBits::ShadingRateImage)
        {
            // might naviely map to the NV attachment type, but this should be the actual one
            return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
        }
        else
        {
            // if we get here, we couldn't find a suitable layout for the "before" usage. this is probably a bug in the caller
            assert(false && "GetDestinationImageLayout: couldn't determine suitable 'before' image layout from usage bits");
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    std::optional<ImageLayouts> GetImageLayouts(const ImageUsageBits before, const ImageUsageBits after) noexcept
    {
        // If either usage is invalid, we can't do anything meaningful. if they're the same, no transition needed
        if ((before == ImageUsageBits::Invalid || after == ImageUsageBits::Invalid) || (before == after))
        {
            return std::nullopt;
        }

        // note that after this point, we know the two usages are at least asymmetric. we will then find the layouts, and depending on how that returns
        // (which can change based on version) we know if we need to execute the layout transition
        VkImageLayout before_layout = GetLayoutFromUsageBits(before);
        VkImageLayout after_layout = GetLayoutFromUsageBits(after);

        if (before_layout == VK_IMAGE_LAYOUT_UNDEFINED || after_layout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            // If we couldn't determine a valid layout for either usage, we can't proceed
            return std::nullopt;
        }

        if (before_layout == after_layout)
        {
            // No transition needed if layouts are the same
            return std::nullopt;
        }

        return ImageLayouts{before_layout, after_layout};
    }

}
