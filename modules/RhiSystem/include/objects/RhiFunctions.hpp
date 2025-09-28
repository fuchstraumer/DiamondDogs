#pragma once
#ifndef DIAMOND_DOGS_RHI_FUNCTIONS_HPP
#define DIAMOND_DOGS_RHI_FUNCTIONS_HPP
#include "RhiDefines.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RenderingInfo.hpp"
#include <span>

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
    
    enum class Format : uint32_t;
    enum class ImageLayout : uint32_t;
    enum class PipelineStage : uint32_t;
    enum class Access : uint32_t;
    
    /** @brief Initialize RHI function pointers for the given device */
    void InitializeRhi(const DeviceHandle device);
    
    /** @brief Cleanup RHI resources */
    void CleanupRhi(const DeviceHandle device);
    
    [[nodiscard]] Result WaitForFences(const DeviceHandle device, std::span<FenceHandle> fences, bool waitAll, uint64_t timeout) noexcept;
    [[nodiscard]] Result ResetFences(const DeviceHandle device, std::span<FenceHandle> fences) noexcept;

    void CmdBeginRendering(CommandBufferHandle cmd, const RenderingInfo& renderingInfo) noexcept;
    void CmdEndRendering(CommandBufferHandle cmd) noexcept;
    void CmdSetViewport(CommandBufferHandle cmd, std::span<const Viewport> viewports) noexcept;
    void CmdSetScissor(CommandBufferHandle cmd, std::span<const Rect2D> scissors) noexcept;

    void CmdBindPipeline(CommandBufferHandle commandBuffer, GraphicsPipelineHandle pipeline) noexcept;
    void CmdBindDescriptorSets(CommandBufferHandle commandBuffer, uint32_t firstSet, 
                              uint32_t descriptorSetCount, const DescriptorSet* const* descriptorSets) noexcept;
    void CmdDraw(CommandBufferHandle commandBuffer, uint32_t vertexCount, 
                uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept;
    void CmdDrawIndexed(CommandBufferHandle commandBuffer, uint32_t indexCount, 
                       uint32_t instanceCount, uint32_t firstIndex, 
                       int32_t vertexOffset, uint32_t firstInstance) noexcept;
    void CmdDispatch(CommandBufferHandle commandBuffer, uint32_t groupCountX, 
                    uint32_t groupCountY, uint32_t groupCountZ) noexcept;
    void CmdPipelineBarrier(CommandBufferHandle commandBuffer, PipelineStage srcStage, 
                           PipelineStage dstStage, uint32_t memoryBarrierCount, 
                           uint32_t bufferMemoryBarrierCount, uint32_t imageMemoryBarrierCount) noexcept;
    [[nodiscard]] Result QueueSubmit(const DeviceHandle device, uint32_t submitCount, 
                                    CommandBufferHandle* const* commandBuffers, const FenceHandle fence) noexcept;

    [[nodiscard]] Result QueueWaitIdle(const DeviceHandle device) noexcept;

    [[nodiscard]] Result DeviceWaitIdle(const DeviceHandle device) noexcept;

}

#endif // !DIAMOND_DOGS_RHI_FUNCTIONS_HPP
