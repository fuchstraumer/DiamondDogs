#pragma once
#ifndef DIAMOND_DOGS_RHI_FUNCTIONS_HPP
#define DIAMOND_DOGS_RHI_FUNCTIONS_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include <cstdint>
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
    void InitializeRhi(const DeviceHandle& device);
    
    /** @brief Cleanup RHI resources */
    void CleanupRhi(const DeviceHandle& device);
    
    [[nodiscard]] Result WaitForFences(const DeviceHandle& device,
        std::span<FenceHandle> fences, bool wait_all, uint64_t timeout);
    [[nodiscard]] Result ResetFences(const DeviceHandle& device, std::span<FenceHandle> fences);

    void CmdBeginRendering(const CommandBufferHandle& cmd, const RenderingInfo& rendering_info) noexcept;
    void CmdEndRendering(const CommandBufferHandle& cmd) noexcept;
    void CmdSetViewport(const CommandBufferHandle& cmd, std::span<const Viewport> viewports) noexcept;
    void CmdSetScissor(const CommandBufferHandle& cmd, std::span<const Rect2D> scissors) noexcept;

    void CmdBindPipeline(const CommandBufferHandle& command_buffer, const GraphicsPipelineHandle& pipeline) noexcept;
    void CmdBindDescriptorSets(const CommandBufferHandle& command_buffer,
                               uint32_t first_set,
                               std::span<const DescriptorSet> descriptor_sets) noexcept;
    void CmdDraw(const CommandBufferHandle& command_buffer,
                 uint32_t vertex_count, 
                 uint32_t instance_count,
                 uint32_t first_vertex,
                 uint32_t first_instance) noexcept;
    void CmdDrawIndexed(const CommandBufferHandle& command_buffer,
                        uint32_t index_count, 
                        uint32_t instance_count,
                        uint32_t first_index, 
                        int32_t vertex_offset,
                        uint32_t first_instance) noexcept;
    void CmdDispatch(const CommandBufferHandle& command_buffer,
                     uint32_t group_count_x, 
                     uint32_t group_count_y,
                     uint32_t group_count_z) noexcept;
    void CmdPipelineBarrier(const CommandBufferHandle& command_buffer,
                            PipelineStage src_stage, 
                            PipelineStage dst_stage,
                            uint32_t memory_barrier_count, 
                            uint32_t buffer_memory_barrier_count,
                            uint32_t image_memory_barrier_count) noexcept;
    [[nodiscard]] Result QueueSubmit(const DeviceHandle& device,
                                     uint32_t submit_count, 
                                     std::span<const CommandBufferHandle> command_buffers,
                                     const FenceHandle& fence) noexcept;
    [[nodiscard]] Result QueueWaitIdle(const DeviceHandle& device) noexcept;
    [[nodiscard]] Result DeviceWaitIdle(const DeviceHandle& device) noexcept;
}

#endif // !DIAMOND_DOGS_RHI_FUNCTIONS_HPP
