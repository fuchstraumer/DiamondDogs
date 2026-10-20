#include "RhiFunctions.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <cassert>

namespace rhi
{
    namespace functions
    {

        static PFN_vkCmdBeginRendering pfn_vkCmdBeginRendering = nullptr;
        static PFN_vkCmdEndRendering pfn_vkCmdEndRendering = nullptr;
        static PFN_vkCmdBindPipeline pfn_vkCmdBindPipeline = nullptr;
        static PFN_vkCmdBindDescriptorSets pfn_vkCmdBindDescriptorSets = nullptr;
        static PFN_vkCmdPushConstants2 pfn_vkCmdPushConstants = nullptr;
        static PFN_vkCmdDraw pfn_vkCmdDraw = nullptr;
        static PFN_vkCmdDrawIndexed pfn_vkCmdDrawIndexed = nullptr;
        static PFN_vkCmdDispatch pfn_vkCmdDispatch = nullptr;
        static PFN_vkCmdCopyBuffer pfn_vkCmdCopyBuffer = nullptr;
        static PFN_vkCmdCopyImage pfn_vkCmdCopyImage = nullptr;
        static PFN_vkCmdPipelineBarrier2 pfn_vkCmdPipelineBarrier2 = nullptr;
        static PFN_vkQueueSubmit pfn_vkQueueSubmit = nullptr;

        void VulkanBeginRendering(const CommandBufferHandle& cmd, const RenderingInfo& rendering_info)
        {
            assert(pfn_vkCmdBeginRendering && "Failed to load vkCmdBeginRendering");

            std::vector<VkRenderingAttachmentInfo> colorAttachments;
            colorAttachments.reserve(rendering_info.ColorAttachments.size());
            
            for (const auto& attachment : rendering_info.ColorAttachments)
            {
                VkRenderingAttachmentInfo vkAttachment{};
                vkAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                vkAttachment.imageView = attachment.ImageView.As<VkImageView>();
                vkAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                vkAttachment.loadOp = static_cast<VkAttachmentLoadOp>(attachment.LoadOp);
                vkAttachment.storeOp = static_cast<VkAttachmentStoreOp>(attachment.StoreOp);
                vkAttachment.clearValue.color.float32[0] = attachment.ClearValue.r;
                vkAttachment.clearValue.color.float32[1] = attachment.ClearValue.g;
                vkAttachment.clearValue.color.float32[2] = attachment.ClearValue.b;
                vkAttachment.clearValue.color.float32[3] = attachment.ClearValue.a;

                colorAttachments.push_back(vkAttachment);
            }
            
            VkRenderingAttachmentInfo depthAttachment{};
            if (rendering_info.DepthStencilAttachment)
            {
                depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttachment.imageView = rendering_info.DepthStencilAttachment->ImageView.As<VkImageView>();
                depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAttachment.loadOp = static_cast<VkAttachmentLoadOp>(rendering_info.DepthStencilAttachment->DepthLoadOp);
                depthAttachment.storeOp = static_cast<VkAttachmentStoreOp>(rendering_info.DepthStencilAttachment->DepthStoreOp);
                depthAttachment.clearValue.depthStencil.depth = rendering_info.DepthStencilAttachment->ClearValue.Depth;
                depthAttachment.clearValue.depthStencil.stencil = rendering_info.DepthStencilAttachment->ClearValue.Stencil;
            }
            
            VkRenderingInfo vkRenderingInfo{};
            vkRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            vkRenderingInfo.renderArea.offset = { rendering_info.RenderArea.PosX, rendering_info.RenderArea.PosY };
            vkRenderingInfo.renderArea.extent = { rendering_info.RenderArea.Width, rendering_info.RenderArea.Height };
            vkRenderingInfo.layerCount = rendering_info.LayerCount;
            vkRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
            vkRenderingInfo.pColorAttachments = colorAttachments.data();
            vkRenderingInfo.pDepthAttachment = rendering_info.DepthStencilAttachment ? &depthAttachment : nullptr;

            pfn_vkCmdBeginRendering(cmd.As<VkCommandBuffer>(), &vkRenderingInfo);
        }

        void VulkanEndRendering(const CommandBufferHandle& cmd)
        {
            assert(pfn_vkCmdEndRendering && "Failed to load vkCmdEndRendering");
            pfn_vkCmdEndRendering(cmd.As<VkCommandBuffer>());
        }

    }

    void InitializeRhi(const DeviceHandle& device)
    {
        const VkDevice deviceVk = device.As<VkDevice>();
        functions::pfn_vkCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(deviceVk, "vkCmdBeginRendering"));
        functions::pfn_vkCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(deviceVk, "vkCmdEndRendering"));
        functions::pfn_vkCmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetDeviceProcAddr(deviceVk, "vkCmdBindPipeline"));
        functions::pfn_vkCmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(vkGetDeviceProcAddr(deviceVk, "vkCmdBindDescriptorSets"));
        functions::pfn_vkCmdDraw = reinterpret_cast<PFN_vkCmdDraw>(vkGetDeviceProcAddr(deviceVk, "vkCmdDraw"));

    }

    void CleanupRhi(const Device* device)
    {
    }

    Result WaitForFences(const DeviceHandle& device, std::span<FenceHandle> fences, bool wait_all, uint64_t timeout)
    {
        assert(fences.size() <= 64 && "Too many fences to wait on");
        VkFence vkFences[64];
        for (uint32_t i = 0; i < fences.size(); ++i)
        {
            vkFences[i] = fences[i].As<VkFence>();
        }
        VkResult result = vkWaitForFences(device.As<VkDevice>(), static_cast<uint32_t>(fences.size()), vkFences, wait_all ? VK_TRUE : VK_FALSE, timeout);
        return static_cast<Result>(result);
    }

    Result ResetFences(const DeviceHandle& device, std::span<FenceHandle> fences)
    {
        VkFence vkFences[64];
        for (uint32_t i = 0; i < fences.size(); ++i)
        {
            vkFences[i] = fences[i].As<VkFence>();
        }
        VkResult result = vkResetFences(device.As<VkDevice>(), static_cast<uint32_t>(fences.size()), vkFences);
        return static_cast<Result>(result);
    }

    Result GetFenceStatus(const Device* device, const Fence* fence) noexcept
    {
        return Result::Success();
    }

    Result BeginCommandBuffer(CommandBuffer* command_buffer) noexcept
    {
        return Result::Success();
    }

    Result EndCommandBuffer(CommandBuffer* command_buffer) noexcept
    {
        return Result::Success();
    }

    Result ResetCommandBuffer(CommandBuffer* command_buffer) noexcept
    {
        return Result::Success();
    }

    void CmdBindPipeline(CommandBuffer* command_buffer, const GraphicsPipeline* pipeline) noexcept
    {

    }

    void CmdBindDescriptorSets(CommandBuffer* command_buffer, uint32_t first_set, 
                              uint32_t descriptor_set_count, const DescriptorSet* const* descriptor_sets) noexcept
    {

    }

    void CmdDraw(CommandBuffer* command_buffer, uint32_t vertex_count, 
                uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) noexcept
    {

    }

    void CmdDrawIndexed(CommandBuffer* command_buffer, uint32_t index_count, 
                       uint32_t instance_count, uint32_t first_index, 
                       int32_t vertex_offset, uint32_t first_instance) noexcept
    {

    }

    void CmdDispatch(CommandBuffer* command_buffer, uint32_t group_count_x, 
                    uint32_t group_count_y, uint32_t group_count_z) noexcept
    {

    }

    void CmdPipelineBarrier(CommandBuffer* command_buffer, PipelineStage src_stage, 
                           PipelineStage dst_stage, uint32_t memory_barrier_count, 
                           uint32_t buffer_memory_barrier_count, uint32_t image_memory_barrier_count) noexcept
    {

    }

}