#pragma once
#ifndef DIAMOND_DOGS_RHI_VULKAN_CONVERSIONS_HPP
#define DIAMOND_DOGS_RHI_VULKAN_CONVERSIONS_HPP
// Internal to RhiSystem: conversions from the RHI types to Vulkan, shared by the objects that record or create things
#include "RhiFlags.hpp"
#include "RhiTypes.hpp"
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace rhi::vk
{
    static_assert(ToUnderlying(PipelineStage::TopOfPipe) == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    static_assert(ToUnderlying(PipelineStage::DrawIndirect) == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    static_assert(ToUnderlying(PipelineStage::VertexInput) == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
    static_assert(ToUnderlying(PipelineStage::VertexShader) == VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
    static_assert(ToUnderlying(PipelineStage::FragmentShader) == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    static_assert(ToUnderlying(PipelineStage::EarlyFragmentTests) == VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
    static_assert(ToUnderlying(PipelineStage::LateFragmentTests) == VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    static_assert(ToUnderlying(PipelineStage::ColorAttachmentOutput) == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    static_assert(ToUnderlying(PipelineStage::ComputeShader) == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    static_assert(ToUnderlying(PipelineStage::AllTransfer) == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
    static_assert(ToUnderlying(PipelineStage::BottomOfPipe) == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    static_assert(ToUnderlying(PipelineStage::Host) == VK_PIPELINE_STAGE_2_HOST_BIT);
    static_assert(ToUnderlying(PipelineStage::AllGraphics) == VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    static_assert(ToUnderlying(PipelineStage::AllCommands) == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    static_assert(ToUnderlying(PipelineStage::TaskShader) == VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT);
    static_assert(ToUnderlying(PipelineStage::MeshShader) == VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    static_assert(ToUnderlying(PipelineStage::Copy) == VK_PIPELINE_STAGE_2_COPY_BIT);
    static_assert(ToUnderlying(PipelineStage::Resolve) == VK_PIPELINE_STAGE_2_RESOLVE_BIT);
    static_assert(ToUnderlying(PipelineStage::Blit) == VK_PIPELINE_STAGE_2_BLIT_BIT);
    static_assert(ToUnderlying(PipelineStage::Clear) == VK_PIPELINE_STAGE_2_CLEAR_BIT);

    static_assert(ToUnderlying(Access::IndirectCommandRead) == VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    static_assert(ToUnderlying(Access::IndexRead) == VK_ACCESS_2_INDEX_READ_BIT);
    static_assert(ToUnderlying(Access::VertexAttributeRead) == VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    static_assert(ToUnderlying(Access::UniformRead) == VK_ACCESS_2_UNIFORM_READ_BIT);
    static_assert(ToUnderlying(Access::InputAttachmentRead) == VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT);
    static_assert(ToUnderlying(Access::ShaderRead) == VK_ACCESS_2_SHADER_READ_BIT);
    static_assert(ToUnderlying(Access::ShaderWrite) == VK_ACCESS_2_SHADER_WRITE_BIT);
    static_assert(ToUnderlying(Access::ColorAttachmentRead) == VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    static_assert(ToUnderlying(Access::ColorAttachmentWrite) == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    static_assert(ToUnderlying(Access::DepthStencilAttachmentRead) == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    static_assert(ToUnderlying(Access::DepthStencilAttachmentWrite) == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    static_assert(ToUnderlying(Access::TransferRead) == VK_ACCESS_2_TRANSFER_READ_BIT);
    static_assert(ToUnderlying(Access::TransferWrite) == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    static_assert(ToUnderlying(Access::HostRead) == VK_ACCESS_2_HOST_READ_BIT);
    static_assert(ToUnderlying(Access::HostWrite) == VK_ACCESS_2_HOST_WRITE_BIT);
    static_assert(ToUnderlying(Access::MemoryRead) == VK_ACCESS_2_MEMORY_READ_BIT);
    static_assert(ToUnderlying(Access::MemoryWrite) == VK_ACCESS_2_MEMORY_WRITE_BIT);
    static_assert(ToUnderlying(Access::ShaderSampledRead) == VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    static_assert(ToUnderlying(Access::ShaderStorageRead) == VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    static_assert(ToUnderlying(Access::ShaderStorageWrite) == VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    static_assert(ToUnderlying(ImageLayout::Undefined) == VK_IMAGE_LAYOUT_UNDEFINED);
    static_assert(ToUnderlying(ImageLayout::General) == VK_IMAGE_LAYOUT_GENERAL);
    static_assert(ToUnderlying(ImageLayout::ColorAttachment) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::DepthStencilAttachment) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::DepthStencilReadOnly) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::ShaderReadOnly) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::TransferSource) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::TransferDestination) == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::Preinitialized) == VK_IMAGE_LAYOUT_PREINITIALIZED);
    static_assert(ToUnderlying(ImageLayout::ReadOnly) == VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::Attachment) == VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
    static_assert(ToUnderlying(ImageLayout::PresentSource) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    static_assert(ToUnderlying(ImageAspect::Color) == VK_IMAGE_ASPECT_COLOR_BIT);
    static_assert(ToUnderlying(ImageAspect::Depth) == VK_IMAGE_ASPECT_DEPTH_BIT);
    static_assert(ToUnderlying(ImageAspect::Stencil) == VK_IMAGE_ASPECT_STENCIL_BIT);

    static_assert(static_cast<int>(LoadOp::Load) == VK_ATTACHMENT_LOAD_OP_LOAD);
    static_assert(static_cast<int>(LoadOp::Clear) == VK_ATTACHMENT_LOAD_OP_CLEAR);
    static_assert(static_cast<int>(LoadOp::DontCare) == VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    static_assert(static_cast<int>(StoreOp::Store) == VK_ATTACHMENT_STORE_OP_STORE);
    static_assert(static_cast<int>(StoreOp::DontCare) == VK_ATTACHMENT_STORE_OP_DONT_CARE);

    [[nodiscard]] constexpr VkPipelineStageFlags2 ToVulkan(PipelineStage stages) noexcept
    {
        return static_cast<VkPipelineStageFlags2>(stages);
    }

    [[nodiscard]] constexpr VkAccessFlags2 ToVulkan(Access access) noexcept
    {
        return static_cast<VkAccessFlags2>(access);
    }

    [[nodiscard]] constexpr VkImageLayout ToVulkan(ImageLayout layout) noexcept
    {
        return static_cast<VkImageLayout>(layout);
    }

    [[nodiscard]] constexpr VkImageAspectFlags ToVulkan(ImageAspect aspects) noexcept
    {
        return static_cast<VkImageAspectFlags>(aspects);
    }

    [[nodiscard]] constexpr VkAttachmentLoadOp ToVulkan(LoadOp op) noexcept
    {
        return static_cast<VkAttachmentLoadOp>(op);
    }

    [[nodiscard]] constexpr VkAttachmentStoreOp ToVulkan(StoreOp op) noexcept
    {
        return static_cast<VkAttachmentStoreOp>(op);
    }

    [[nodiscard]] constexpr VkPipelineBindPoint ToVulkan(PipelineBindPoint bind_point) noexcept
    {
        return bind_point == PipelineBindPoint::Compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    }

    [[nodiscard]] constexpr VkIndexType ToVulkan(IndexType index_type) noexcept
    {
        return index_type == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    }

    [[nodiscard]] constexpr VkImageSubresourceRange ToVulkan(const ImageSubresourceRange& range) noexcept
    {
        return VkImageSubresourceRange{ ToVulkan(range.Aspects), range.BaseMipLevel, range.LevelCount, range.BaseArrayLayer, range.LayerCount };
    }

    [[nodiscard]] inline VkShaderStageFlags ToVulkanStage(ShaderStageFlags stage)
    {
        switch (stage)
        {
            case ShaderStageFlags::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderStageFlags::TesselationControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            case ShaderStageFlags::TesselationEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            case ShaderStageFlags::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ShaderStageFlags::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderStageFlags::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderStageFlags::RayGeneration: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            case ShaderStageFlags::AnyHit: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            case ShaderStageFlags::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            case ShaderStageFlags::Miss: return VK_SHADER_STAGE_MISS_BIT_KHR;
            case ShaderStageFlags::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            case ShaderStageFlags::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            case ShaderStageFlags::Task: return VK_SHADER_STAGE_TASK_BIT_EXT;
            case ShaderStageFlags::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT;
            default:
                throw std::invalid_argument("Unsupported shader stage for Vulkan conversion");
        }
    }

    /** @brief Converts every stage in the mask (a push constant range shared by vertex and fragment, say). Throws on an empty or unknown mask */
    [[nodiscard]] inline VkShaderStageFlags ToVulkanStages(ShaderStageFlags stages)
    {
        uint32_t remaining = static_cast<uint32_t>(stages);
        if (remaining == 0)
        {
            throw std::invalid_argument("Unsupported shader stage for Vulkan conversion");
        }
        VkShaderStageFlags result = 0;
        while (remaining != 0)
        {
            const uint32_t lowestBit = remaining & (~remaining + 1u);
            result |= ToVulkanStage(static_cast<ShaderStageFlags>(lowestBit));
            remaining &= ~lowestBit;
        }
        return result;
    }
}

#endif // !DIAMOND_DOGS_RHI_VULKAN_CONVERSIONS_HPP
