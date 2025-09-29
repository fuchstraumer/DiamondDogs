#include "RhiFunctions.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
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

        void VulkanBeginRendering(CommandBufferHandle cmd, const RenderingInfo& renderingInfo)
        {
            assert(pfn_vkCmdBeginRendering && "Failed to load vkCmdBeginRendering");

            std::vector<VkRenderingAttachmentInfo> colorAttachments;
            colorAttachments.reserve(renderingInfo.ColorAttachments.size());
            
            for (const auto& attachment : renderingInfo.ColorAttachments)
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
            if (renderingInfo.DepthStencilAttachment)
            {
                depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttachment.imageView = renderingInfo.DepthStencilAttachment->ImageView.As<VkImageView>();
                depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAttachment.loadOp = static_cast<VkAttachmentLoadOp>(renderingInfo.DepthStencilAttachment->DepthLoadOp);
                depthAttachment.storeOp = static_cast<VkAttachmentStoreOp>(renderingInfo.DepthStencilAttachment->DepthStoreOp);
                depthAttachment.clearValue.depthStencil.depth = renderingInfo.DepthStencilAttachment->ClearValue.Depth;
                depthAttachment.clearValue.depthStencil.stencil = renderingInfo.DepthStencilAttachment->ClearValue.Stencil;
            }
            
            VkRenderingInfo vkRenderingInfo{};
            vkRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            vkRenderingInfo.renderArea.offset = { renderingInfo.RenderArea.PosX, renderingInfo.RenderArea.PosY };
            vkRenderingInfo.renderArea.extent = { renderingInfo.RenderArea.Width, renderingInfo.RenderArea.Height };
            vkRenderingInfo.layerCount = renderingInfo.LayerCount;
            vkRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
            vkRenderingInfo.pColorAttachments = colorAttachments.data();
            vkRenderingInfo.pDepthAttachment = renderingInfo.DepthStencilAttachment ? &depthAttachment : nullptr;

            pfn_vkCmdBeginRendering(cmd.As<VkCommandBuffer>(), &vkRenderingInfo);
        }

        void VulkanEndRendering(CommandBufferHandle cmd)
        {
            assert(pfn_vkCmdEndRendering && "Failed to load vkCmdEndRendering");
            pfn_vkCmdEndRendering(cmd.As<VkCommandBuffer>());
        }

    }

    void InitializeRhi(const DeviceHandle device)
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

    Result WaitForFences(DeviceHandle device, std::span<FenceHandle> fences, bool waitAll, uint64_t timeout)
    {
        assert(fences.size() <= 64 && "Too many fences to wait on");
        VkFence vkFences[64];
        for (uint32_t i = 0; i < fences.size(); ++i)
        {
            vkFences[i] = fences[i].As<VkFence>();
        }
        VkResult result = vkWaitForFences(device.As<VkDevice>(), fences.size(), vkFences, waitAll ? VK_TRUE : VK_FALSE, timeout);
        return static_cast<Result>(result);
    }

    Result ResetFences(DeviceHandle device, std::span<FenceHandle> fences)
    {
        VkFence vkFences[64];
        for (uint32_t i = 0; i < fences.size(); ++i)
        {
            vkFences[i] = fences[i].As<VkFence>();
        }
        VkResult result = vkResetFences(device.As<VkDevice>(), fences.size(), vkFences);
        return static_cast<Result>(result);
    }

    Result GetFenceStatus(const Device* device, const Fence* fence) noexcept
    {
        return Result::Success();
    }

    Result BeginCommandBuffer(CommandBuffer* commandBuffer) noexcept
    {
        return Result::Success();
    }

    Result EndCommandBuffer(CommandBuffer* commandBuffer) noexcept
    {
        return Result::Success();
    }

    Result ResetCommandBuffer(CommandBuffer* commandBuffer) noexcept
    {
        return Result::Success();
    }

    void CmdBindPipeline(CommandBuffer* commandBuffer, const GraphicsPipeline* pipeline) noexcept
    {

    }

    void CmdBindDescriptorSets(CommandBuffer* commandBuffer, uint32_t firstSet, 
                              uint32_t descriptorSetCount, const DescriptorSet* const* descriptorSets) noexcept
    {

    }

    void CmdDraw(CommandBuffer* commandBuffer, uint32_t vertexCount, 
                uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept
    {

    }

    void CmdDrawIndexed(CommandBuffer* commandBuffer, uint32_t indexCount, 
                       uint32_t instanceCount, uint32_t firstIndex, 
                       int32_t vertexOffset, uint32_t firstInstance) noexcept
    {

    }

    void CmdDispatch(CommandBuffer* commandBuffer, uint32_t groupCountX, 
                    uint32_t groupCountY, uint32_t groupCountZ) noexcept
    {

    }

    void CmdPipelineBarrier(CommandBuffer* commandBuffer, PipelineStage srcStage, 
                           PipelineStage dstStage, uint32_t memoryBarrierCount, 
                           uint32_t bufferMemoryBarrierCount, uint32_t imageMemoryBarrierCount) noexcept
    {

    }

}