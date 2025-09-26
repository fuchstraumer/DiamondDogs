#pragma once
#ifndef DIAMOND_DOGS_RHI_FUNCTIONS_HPP
#define DIAMOND_DOGS_RHI_FUNCTIONS_HPP

#include <cstdint>

namespace rhi
{
    // Forward declarations
    class Instance;
    class PhysicalDevice;
    class Device;
    class Fence;
    class Swapchain;
    class Semaphore;
    class CommandPool;
    struct CommandBuffer;
    class GraphicsPipeline;
    class ComputePipeline;
    class DescriptorSet;
    class DescriptorPool;
    class ShaderModule;
    
    enum class Result : uint32_t;
    enum class Format : uint32_t;
    enum class ImageLayout : uint32_t;
    enum class PipelineStage : uint32_t;
    enum class Access : uint32_t;
    
    /** @brief Initialize RHI function pointers for the given device */
    void InitializeRhi(const Device* device);
    
    /** @brief Cleanup RHI resources */
    void CleanupRhi(const Device* device);
    
    [[nodiscard]] Result WaitForFences(const Device* device, uint32_t fenceCount, 
                                       const Fence* const* fences, bool waitAll, uint64_t timeout) noexcept;
    
    [[nodiscard]] Result ResetFences(const Device* device, uint32_t fenceCount, 
                                     const Fence* const* fences) noexcept;
    
    [[nodiscard]] Result GetFenceStatus(const Device* device, const Fence* fence) noexcept;
    
    [[nodiscard]] Result BeginCommandBuffer(CommandBuffer* commandBuffer) noexcept;
    
    [[nodiscard]] Result EndCommandBuffer(CommandBuffer* commandBuffer) noexcept;
    
    [[nodiscard]] Result ResetCommandBuffer(CommandBuffer* commandBuffer) noexcept;
    
    void CmdBeginRenderPass(CommandBuffer* commandBuffer, const RenderPass* renderPass) noexcept;
    
    void CmdEndRenderPass(CommandBuffer* commandBuffer) noexcept;
    
    void CmdBindPipeline(CommandBuffer* commandBuffer, const GraphicsPipeline* pipeline) noexcept;
    
    void CmdBindDescriptorSets(CommandBuffer* commandBuffer, uint32_t firstSet, 
                              uint32_t descriptorSetCount, const DescriptorSet* const* descriptorSets) noexcept;
    
    void CmdDraw(CommandBuffer* commandBuffer, uint32_t vertexCount, 
                uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept;
    
    void CmdDrawIndexed(CommandBuffer* commandBuffer, uint32_t indexCount, 
                       uint32_t instanceCount, uint32_t firstIndex, 
                       int32_t vertexOffset, uint32_t firstInstance) noexcept;
    
    void CmdDispatch(CommandBuffer* commandBuffer, uint32_t groupCountX, 
                    uint32_t groupCountY, uint32_t groupCountZ) noexcept;

    void CmdPipelineBarrier(CommandBuffer* commandBuffer, PipelineStage srcStage, 
                           PipelineStage dstStage, uint32_t memoryBarrierCount, 
                           uint32_t bufferMemoryBarrierCount, uint32_t imageMemoryBarrierCount) noexcept;
    
    [[nodiscard]] Result QueueSubmit(const Device* device, uint32_t submitCount, 
                                    CommandBuffer* const* commandBuffers, const Fence* fence) noexcept;
    
    [[nodiscard]] Result QueueWaitIdle(const Device* device) noexcept;
    
    [[nodiscard]] Result DeviceWaitIdle(const Device* device) noexcept;
    
    [[nodiscard]] Result AcquireNextImage(const Device* device, const Swapchain* swapchain, 
                                         uint64_t timeout, const Semaphore* semaphore, 
                                         const Fence* fence, uint32_t* imageIndex) noexcept;
    
    [[nodiscard]] Result QueuePresent(const Device* device, const Swapchain* swapchain, 
                                     uint32_t imageIndex, const Semaphore* waitSemaphore) noexcept;
}

#endif // !DIAMOND_DOGS_RHI_FUNCTIONS_HPP
