#include "RhiFunctions.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
#include <cassert>

namespace rhi
{
    namespace detail
    {
        using WaitForFencesFunc = Result(*)(const Device* device, uint32_t fenceCount, 
                                        const Fence* const* fences, bool waitAll, uint64_t timeout);
        using ResetFencesFunc = Result(*)(const Device* device, uint32_t fenceCount, 
                                        const Fence* const* fences);
        using GetFenceStatusFunc = Result(*)(const Device* device, const Fence* fence);
        using BeginCommandBufferFunc = Result(*)(CommandBuffer* commandBuffer);
        using EndCommandBufferFunc = Result(*)(CommandBuffer* commandBuffer);
        using ResetCommandBufferFunc = Result(*)(CommandBuffer* commandBuffer);
        
        using CmdBeginRenderPassFunc = void(*)(CommandBuffer* commandBuffer, const RenderPass* renderPass);
        using CmdEndRenderPassFunc = void(*)(CommandBuffer* commandBuffer);
        using CmdBindPipelineFunc = void(*)(CommandBuffer* commandBuffer, const GraphicsPipeline* pipeline);
        using CmdBindDescriptorSetsFunc = void(*)(CommandBuffer* commandBuffer, uint32_t firstSet, 
                                                uint32_t descriptorSetCount, const DescriptorSet* const* descriptorSets);
        using CmdDrawFunc = void(*)(CommandBuffer* commandBuffer, uint32_t vertexCount, 
                                uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
        using CmdDrawIndexedFunc = void(*)(CommandBuffer* commandBuffer, uint32_t indexCount, 
                                        uint32_t instanceCount, uint32_t firstIndex, 
                                        int32_t vertexOffset, uint32_t firstInstance);
        using CmdDispatchFunc = void(*)(CommandBuffer* commandBuffer, uint32_t groupCountX, 
                                    uint32_t groupCountY, uint32_t groupCountZ);
        using CmdCopyBufferFunc = void(*)(CommandBuffer* commandBuffer, const Buffer* srcBuffer, 
                                        const Buffer* dstBuffer, uint64_t size, uint64_t srcOffset, uint64_t dstOffset);
        using CmdCopyImageFunc = void(*)(CommandBuffer* commandBuffer, const Image* srcImage, 
                                        const Image* dstImage, ImageLayout srcLayout, ImageLayout dstLayout); 
        using CmdPipelineBarrierFunc = void(*)(CommandBuffer* commandBuffer, PipelineStage srcStage, 
                                            PipelineStage dstStage, uint32_t memoryBarrierCount, 
                                            uint32_t bufferMemoryBarrierCount, uint32_t imageMemoryBarrierCount);
        using QueueSubmitFunc = Result(*)(const Device* device, uint32_t submitCount, 
                                        CommandBuffer* const* commandBuffers, const Fence* fence);
        using QueueWaitIdleFunc = Result(*)(const Device* device);
        using DeviceWaitIdleFunc = Result(*)(const Device* device);
        using AcquireNextImageFunc = Result(*)(const Device* device, const Swapchain* swapchain, 
                                            uint64_t timeout, const Semaphore* semaphore, 
                                            const Fence* fence, uint32_t* imageIndex);
        using QueuePresentFunc = Result(*)(const Device* device, const Swapchain* swapchain, 
                                        uint32_t imageIndex, const Semaphore* waitSemaphore);

        static RhiFunctionTable g_FunctionTable{};
        
        static PFN_vkWaitForFences vkWaitForFences = nullptr;
        static PFN_vkResetFences vkResetFences = nullptr;
        static PFN_vkGetFenceStatus vkGetFenceStatus = nullptr;
        static PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
        static PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
        static PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
        static PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
        static PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
        static PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
        static PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
        static PFN_vkCmdDraw vkCmdDraw = nullptr;
        static PFN_vkCmdDrawIndexed vkCmdDrawIndexed = nullptr;
        static PFN_vkCmdDispatch vkCmdDispatch = nullptr;
        static PFN_vkCmdCopyBuffer vkCmdCopyBuffer = nullptr;
        static PFN_vkCmdCopyImage vkCmdCopyImage = nullptr;
        static PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
        static PFN_vkQueueSubmit vkQueueSubmit = nullptr;
        static PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
        static PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
        static PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
        static PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;

        Result VulkanWaitForFences(const Device* device, uint32_t fenceCount, 
                                  const Fence* const* fences, bool waitAll, uint64_t timeout)
        {
            VkFence vkFences[64];
            for (uint32_t i = 0; i < fenceCount; ++i)
            {
                vkFences[i] = fences[i]->vkHandle();
            }
            VkResult result = vkWaitForFences(device->vkHandle(), fenceCount, vkFences, waitAll ? VK_TRUE : VK_FALSE, timeout);
            return static_cast<Result>(result);
        }

        Result VulkanResetFences(const Device* device, uint32_t fenceCount, 
                                const Fence* const* fences)
        {
            VkFence vkFences[64];
            for (uint32_t i = 0; i < fenceCount; ++i)
            {
                vkFences[i] = fences[i]->vkHandle();
            }
            VkResult result = vkResetFences(device->vkHandle(), fenceCount, vkFences);
            return static_cast<Result>(result);
        }

        Result VulkanGetFenceStatus(const Device* device, const Fence* fence)
        {
            VkResult result = vkGetFenceStatus(device->vkHandle(), reinterpret_cast<VkFence>(fence));
            return static_cast<Result>(result);
        }

        Result VulkanBeginCommandBuffer(CommandBuffer* commandBuffer)
        {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VkResult result = vkBeginCommandBuffer(reinterpret_cast<VkCommandBuffer>(commandBuffer), &beginInfo);
            return static_cast<Result>(result);
        }

        Result VulkanEndCommandBuffer(CommandBuffer* commandBuffer)
        {
            VkResult result = vkEndCommandBuffer(reinterpret_cast<VkCommandBuffer>(commandBuffer));
            return static_cast<Result>(result);
        }

        Result VulkanResetCommandBuffer(CommandBuffer* commandBuffer)
        {
            VkResult result = vkResetCommandBuffer(reinterpret_cast<VkCommandBuffer>(commandBuffer), 0);
            return static_cast<Result>(result);
        }

        void VulkanCmdBeginRenderPass(CommandBuffer* commandBuffer, const RenderPass* renderPass)
        {
        }

        void VulkanCmdEndRenderPass(CommandBuffer* commandBuffer)
        {
            vkCmdEndRenderPass(reinterpret_cast<VkCommandBuffer>(commandBuffer));
        }

        void VulkanCmdBindPipeline(CommandBuffer* commandBuffer, const GraphicsPipeline* pipeline)
        {
            vkCmdBindPipeline(reinterpret_cast<VkCommandBuffer>(commandBuffer), VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipeline>(pipeline));
        }

        void VulkanCmdBindDescriptorSets(CommandBuffer* commandBuffer, uint32_t firstSet, 
                                        uint32_t descriptorSetCount, const DescriptorSet* const* descriptorSets)
        {
        }

        void VulkanCmdDraw(CommandBuffer* commandBuffer, uint32_t vertexCount, 
                          uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
        {
            vkCmdDraw(reinterpret_cast<VkCommandBuffer>(commandBuffer), vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void VulkanCmdDrawIndexed(CommandBuffer* commandBuffer, uint32_t indexCount, 
                                 uint32_t instanceCount, uint32_t firstIndex, 
                                 int32_t vertexOffset, uint32_t firstInstance)
        {
            vkCmdDrawIndexed(reinterpret_cast<VkCommandBuffer>(commandBuffer), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

        void VulkanCmdDispatch(CommandBuffer* commandBuffer, uint32_t groupCountX, 
                              uint32_t groupCountY, uint32_t groupCountZ)
        {
            vkCmdDispatch(reinterpret_cast<VkCommandBuffer>(commandBuffer), groupCountX, groupCountY, groupCountZ);
        }

        void VulkanCmdCopyImage(CommandBuffer* commandBuffer, const Image* srcImage, 
                               const Image* dstImage, ImageLayout srcLayout, ImageLayout dstLayout)
        {
        }

        void VulkanCmdPipelineBarrier(CommandBuffer* commandBuffer, PipelineStage srcStage, 
                                     PipelineStage dstStage, uint32_t memoryBarrierCount, 
                                     uint32_t bufferMemoryBarrierCount, uint32_t imageMemoryBarrierCount)
        {
        }

        Result VulkanQueueSubmit(const Device* device, uint32_t submitCount, 
                                CommandBuffer* const* commandBuffers, const Fence* fence)
        {
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = submitCount;
            
            VkCommandBuffer vkCommandBuffers[64];
            for (uint32_t i = 0; i < submitCount; ++i)
            {
                vkCommandBuffers[i] = reinterpret_cast<VkCommandBuffer>(commandBuffers[i]);
            }
            submitInfo.pCommandBuffers = vkCommandBuffers;
            
            VkFence vkFence = fence ? reinterpret_cast<VkFence>(fence) : VK_NULL_HANDLE;
            VkResult result = vkQueueSubmit(device->GetGeneralQueue(), 1, &submitInfo, vkFence);
            return static_cast<Result>(result);
        }

        Result VulkanQueueWaitIdle(const Device* device)
        {
            VkResult result = vkQueueWaitIdle(device->GetGeneralQueue());
            return static_cast<Result>(result);
        }

        Result VulkanDeviceWaitIdle(const Device* device)
        {
            VkResult result = vkDeviceWaitIdle(device->vkHandle());
            return static_cast<Result>(result);
        }

        Result VulkanAcquireNextImage(const Device* device, const Swapchain* swapchain, 
                                     uint64_t timeout, const Semaphore* semaphore, 
                                     const Fence* fence, uint32_t* imageIndex)
        {
            VkSemaphore vkSemaphore = semaphore ? reinterpret_cast<VkSemaphore>(semaphore) : VK_NULL_HANDLE;
            VkFence vkFence = fence ? reinterpret_cast<VkFence>(fence) : VK_NULL_HANDLE;
            VkResult result = vkAcquireNextImageKHR(device->vkHandle(), reinterpret_cast<VkSwapchainKHR>(swapchain), timeout, vkSemaphore, vkFence, imageIndex);
            return static_cast<Result>(result);
        }

        Result VulkanQueuePresent(const Device* device, const Swapchain* swapchain, 
                                 uint32_t imageIndex, const Semaphore* waitSemaphore)
        {
            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
            VkSemaphore vkWaitSemaphore = waitSemaphore ? reinterpret_cast<VkSemaphore>(waitSemaphore) : VK_NULL_HANDLE;
            presentInfo.pWaitSemaphores = waitSemaphore ? &vkWaitSemaphore : nullptr;
            presentInfo.swapchainCount = 1;
            VkSwapchainKHR vkSwapchain = reinterpret_cast<VkSwapchainKHR>(swapchain);
            presentInfo.pSwapchains = &vkSwapchain;
            presentInfo.pImageIndices = &imageIndex;
            VkResult result = vkQueuePresentKHR(device->GetGeneralQueue(), &presentInfo);
            return static_cast<Result>(result);
        }

        const RhiFunctionTable& GetFunctions() noexcept
        {
            return g_FunctionTable;
        }
    }

    void InitializeRhi(const Device* device)
    {
        VkDevice vkDevice = device->vkHandle();
        
        detail::vkWaitForFences = reinterpret_cast<PFN_vkWaitForFences>(vkGetDeviceProcAddr(vkDevice, "vkWaitForFences"));
        detail::vkResetFences = reinterpret_cast<PFN_vkResetFences>(vkGetDeviceProcAddr(vkDevice, "vkResetFences"));
        detail::vkGetFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(vkGetDeviceProcAddr(vkDevice, "vkGetFenceStatus"));
        detail::vkBeginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(vkGetDeviceProcAddr(vkDevice, "vkBeginCommandBuffer"));
        detail::vkEndCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(vkGetDeviceProcAddr(vkDevice, "vkEndCommandBuffer"));
        detail::vkResetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(vkGetDeviceProcAddr(vkDevice, "vkResetCommandBuffer"));
        detail::vkCmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(vkGetDeviceProcAddr(vkDevice, "vkCmdBeginRenderPass"));
        detail::vkCmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(vkGetDeviceProcAddr(vkDevice, "vkCmdEndRenderPass"));
        detail::vkCmdBindPipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(vkGetDeviceProcAddr(vkDevice, "vkCmdBindPipeline"));
        detail::vkCmdBindDescriptorSets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(vkGetDeviceProcAddr(vkDevice, "vkCmdBindDescriptorSets"));
        detail::vkCmdDraw = reinterpret_cast<PFN_vkCmdDraw>(vkGetDeviceProcAddr(vkDevice, "vkCmdDraw"));
        detail::vkCmdDrawIndexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(vkGetDeviceProcAddr(vkDevice, "vkCmdDrawIndexed"));
        detail::vkCmdDispatch = reinterpret_cast<PFN_vkCmdDispatch>(vkGetDeviceProcAddr(vkDevice, "vkCmdDispatch"));
        detail::vkCmdCopyBuffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(vkGetDeviceProcAddr(vkDevice, "vkCmdCopyBuffer"));
        detail::vkCmdCopyImage = reinterpret_cast<PFN_vkCmdCopyImage>(vkGetDeviceProcAddr(vkDevice, "vkCmdCopyImage"));
        detail::vkCmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr(vkDevice, "vkCmdPipelineBarrier"));
        detail::vkQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(vkGetDeviceProcAddr(vkDevice, "vkQueueSubmit"));
        detail::vkQueueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(vkGetDeviceProcAddr(vkDevice, "vkQueueWaitIdle"));
        detail::vkDeviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(vkGetDeviceProcAddr(vkDevice, "vkDeviceWaitIdle"));
        detail::vkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkGetDeviceProcAddr(vkDevice, "vkAcquireNextImageKHR"));
        detail::vkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(vkGetDeviceProcAddr(vkDevice, "vkQueuePresentKHR"));

    }

    void CleanupRhi(const Device* device)
    {
    }

    Result WaitForFences(const Device* device, uint32_t fenceCount, 
                        const Fence* const* fences, bool waitAll, uint64_t timeout) noexcept
    {
        assert(fenceCount <= 8 && "Exceeded maximum supported fences in WaitForFences");
        constexpr size_t MaxFences = 8;
        std::array<VkFence, MaxFences> vkFences;
        for (uint32_t i = 0; i < fenceCount; ++i)
        {
            vkFences[i] = fences[i]->vkHandle();
        }

        return detail::vkWaitForFences(device->vkHandle(), fenceCount, vkFences.data(), waitAll, timeout);
    }

    Result ResetFences(const Device* device, uint32_t fenceCount, 
                      const Fence* const* fences) noexcept
    {
        return detail::vkResetFences(device->vkHandle(), fenceCount, fences);
    }

    Result GetFenceStatus(const Device* device, const Fence* fence) noexcept
    {
        return detail::GetFunctions().GetFenceStatus(device, fence);
    }

    Result BeginCommandBuffer(CommandBuffer* commandBuffer) noexcept
    {
        return detail::GetFunctions().BeginCommandBuffer(commandBuffer);
    }

    Result EndCommandBuffer(CommandBuffer* commandBuffer) noexcept
    {
        return detail::GetFunctions().EndCommandBuffer(commandBuffer);
    }

    Result ResetCommandBuffer(CommandBuffer* commandBuffer) noexcept
    {
        return detail::GetFunctions().ResetCommandBuffer(commandBuffer);
    }

    void CmdBeginRenderPass(CommandBuffer* commandBuffer, const RenderPass* renderPass) noexcept
    {
        detail::GetFunctions().CmdBeginRenderPass(commandBuffer, renderPass);
    }

    void CmdEndRenderPass(CommandBuffer* commandBuffer) noexcept
    {
        detail::GetFunctions().CmdEndRenderPass(commandBuffer);
    }

    void CmdBindPipeline(CommandBuffer* commandBuffer, const GraphicsPipeline* pipeline) noexcept
    {
        detail::GetFunctions().CmdBindPipeline(commandBuffer, pipeline);
    }

    void CmdBindDescriptorSets(CommandBuffer* commandBuffer, uint32_t firstSet, 
                              uint32_t descriptorSetCount, const DescriptorSet* const* descriptorSets) noexcept
    {
        detail::GetFunctions().CmdBindDescriptorSets(commandBuffer, firstSet, descriptorSetCount, descriptorSets);
    }

    void CmdDraw(CommandBuffer* commandBuffer, uint32_t vertexCount, 
                uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept
    {
        detail::GetFunctions().CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CmdDrawIndexed(CommandBuffer* commandBuffer, uint32_t indexCount, 
                       uint32_t instanceCount, uint32_t firstIndex, 
                       int32_t vertexOffset, uint32_t firstInstance) noexcept
    {
        detail::GetFunctions().CmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CmdDispatch(CommandBuffer* commandBuffer, uint32_t groupCountX, 
                    uint32_t groupCountY, uint32_t groupCountZ) noexcept
    {
        detail::GetFunctions().CmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
    }

    void CmdCopyBuffer(CommandBuffer* commandBuffer, const Buffer* srcBuffer, 
                      const Buffer* dstBuffer, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) noexcept
    {
        detail::GetFunctions().CmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, size, srcOffset, dstOffset);
    }

    void CmdCopyImage(CommandBuffer* commandBuffer, const Image* srcImage, 
                     const Image* dstImage, ImageLayout srcLayout, ImageLayout dstLayout) noexcept
    {
        detail::GetFunctions().CmdCopyImage(commandBuffer, srcImage, dstImage, srcLayout, dstLayout);
    }

    void CmdPipelineBarrier(CommandBuffer* commandBuffer, PipelineStage srcStage, 
                           PipelineStage dstStage, uint32_t memoryBarrierCount, 
                           uint32_t bufferMemoryBarrierCount, uint32_t imageMemoryBarrierCount) noexcept
    {
        detail::GetFunctions().CmdPipelineBarrier(commandBuffer, srcStage, dstStage, memoryBarrierCount, bufferMemoryBarrierCount, imageMemoryBarrierCount);
    }

    Result QueueSubmit(const Device* device, uint32_t submitCount, 
                      CommandBuffer* const* commandBuffers, const Fence* fence) noexcept
    {
        return detail::GetFunctions().QueueSubmit(device, submitCount, commandBuffers, fence);
    }

    Result QueueWaitIdle(const Device* device) noexcept
    {
        return detail::GetFunctions().QueueWaitIdle(device);
    }

    Result DeviceWaitIdle(const Device* device) noexcept
    {
        return detail::GetFunctions().DeviceWaitIdle(device);
    }

    Result AcquireNextImage(const Device* device, const Swapchain* swapchain, 
                           uint64_t timeout, const Semaphore* semaphore, 
                           const Fence* fence, uint32_t* imageIndex) noexcept
    {
        return detail::GetFunctions().AcquireNextImage(device, swapchain, timeout, semaphore, fence, imageIndex);
    }

    Result QueuePresent(const Device* device, const Swapchain* swapchain, 
                       uint32_t imageIndex, const Semaphore* waitSemaphore) noexcept
    {
        return detail::GetFunctions().QueuePresent(device, swapchain, imageIndex, waitSemaphore);
    }
}