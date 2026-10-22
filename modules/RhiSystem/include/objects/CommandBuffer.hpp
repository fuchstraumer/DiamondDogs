#pragma once
#ifndef DIAMOND_DOGS_RHI_COMMAND_BUFFER_HPP
#define DIAMOND_DOGS_RHI_COMMAND_BUFFER_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace rhi
{
    class ShaderProgram;

    /** @brief An RAII wrapper for CommandBuffers, mostly ensuring that Begin/End semantics stay the same regardless of backend API.
     *  Note that CommandBuffers themselves are not copyable or movable, as they represent a unique recording context. If a CommandBuffer
     *  exits scope while still in recording mode in Graphics debug builds, it will throw an assertion failure to help catch bugs early.
     *
     *  This class is mostly required because on DX12, command buffers are actually command lists, which have different rules and lifetimes relative
     *  to Vulkan command buffers, which are more independent objects that belong to a parent pool (almost entirely unlike DX12 command lists). If
     *  we didn't use this wrapper, users would have to deal with the differences in semantics themselves, which would be error-prone and tedious.
     *
     *  The recording methods are thin: each records one command, and must be called between Begin() and End(). On Vulkan they use the
     *  1.3 core entry points (dynamic rendering, synchronization2, viewport/scissor with count), so the device must be 1.3.
     */
    struct CommandBuffer
    {
        CommandBuffer(const CommandBufferHandle& handle) noexcept;
        ~CommandBuffer();
        CommandBuffer(const CommandBuffer&) = delete;
        CommandBuffer& operator=(const CommandBuffer&) = delete;
        CommandBuffer(CommandBuffer&& other) noexcept;
        CommandBuffer& operator=(CommandBuffer&& other) noexcept;

        [[nodiscard]] CommandBufferHandle Handle() const noexcept;
        Result Begin() noexcept;
        Result End() noexcept;

        void BeginRendering(const RenderingInfo& rendering_info) noexcept;
        void EndRendering() noexcept;
        /** @brief Sets the viewport and scissor counts too, as shader objects require */
        void SetViewports(std::span<const Viewport> viewports) noexcept;
        void SetScissors(std::span<const Rect2D> scissors) noexcept;

        /** @brief Binds every stage of the program, and null shaders for the stages it leaves out */
        Result BindShaders(const ShaderProgram& program);
        void BindDescriptorSets(PipelineBindPoint bind_point,
                                const PipelineLayoutHandle& layout,
                                uint32_t first_set,
                                std::span<const DescriptorSetHandle> descriptor_sets,
                                std::span<const uint32_t> dynamic_offsets = {}) noexcept;
        void PushConstants(const PipelineLayoutHandle& layout, ShaderStageFlags stages, uint32_t offset, std::span<const std::byte> data);
        void BindIndexBuffer(const BufferHandle& buffer, uint64_t offset, IndexType index_type) noexcept;
        void BindVertexBuffers(uint32_t first_binding, std::span<const BufferHandle> buffers, std::span<const uint64_t> offsets) noexcept;

        void Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) noexcept;
        void DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) noexcept;
        void Dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) noexcept;

        void Barrier(const DependencyInfo& dependencies) noexcept;
        void FillBuffer(const BufferHandle& buffer, uint64_t offset, uint64_t size, uint32_t value) noexcept;
        void CopyBuffer(const BufferHandle& src, const BufferHandle& dst, std::span<const BufferCopy> regions) noexcept;
        void CopyImageToBuffer(const ImageHandle& src, ImageLayout src_layout, const BufferHandle& dst, std::span<const BufferImageCopy> regions) noexcept;
        void CopyBufferToImage(const BufferHandle& src, const ImageHandle& dst, ImageLayout dst_layout, std::span<const BufferImageCopy> regions) noexcept;
        /** @brief The images may be the same image, for example to fill a mip chain one level at a time. Each region's
         *  source and destination must not overlap */
        void BlitImage(const ImageHandle& src, ImageLayout src_layout, const ImageHandle& dst, ImageLayout dst_layout,
                       std::span<const ImageBlit> regions, BlitFilter filter) noexcept;

        /** @brief vkCmdResetQueryPool. Graphics and compute queues only: the transfer queue cannot record it, so
         *  pools that it writes use QueryPool::Reset on the host */
        void ResetQueryPool(const QueryPoolHandle& pool, uint32_t first, uint32_t count) noexcept;
        /** @brief Writes the timestamp once every earlier command has reached `stage` (vkCmdWriteTimestamp2). Any
         *  queue whose family has timestamp valid bits */
        void WriteTimestamp(PipelineStage stage, const QueryPoolHandle& pool, uint32_t query) noexcept;

    private:
        bool isRecording = false;
        CommandBufferHandle handle;
    };

}

#endif // !DIAMOND_DOGS_RHI_COMMAND_BUFFER_HPP
