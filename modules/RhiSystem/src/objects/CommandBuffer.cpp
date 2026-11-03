#include "CommandBuffer.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "ShaderProgram.hpp"
#include "core/VulkanConversions.hpp"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan_core.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <wrl/client.h>
#endif

namespace rhi
{
    CommandBuffer::CommandBuffer(const CommandBufferHandle& handle) noexcept : handle{ handle }
    {
    }

    CommandBuffer::~CommandBuffer()
    {
#ifdef DIAMOND_DOGS_GRAPHICS_DEBUG
        // In graphics debug builds, assert if still recording
        assert(!isRecording && "CommandBuffer destroyed while still in recording mode. Ensure End() is called before destruction.");
#else
        // In all other builds, just end recording if still in recording mode. Better to clean up gracefully than crash.
        if (isRecording)
        {
            std::cerr << "Warning: CommandBuffer destroyed while still in recording mode. Automatically ending recording.\n";
            End();
        }
#endif
    }

    CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
        : isRecording{ other.isRecording }, handle{ std::move(other.handle) }
    {
        other.isRecording = false;
        other.handle = CommandBufferHandle{ 0 };
    }

    CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept
    {
        if (this != &other)
        {
            isRecording = other.isRecording;
            handle = std::move(other.handle);
            other.isRecording = false;
            other.handle = CommandBufferHandle{ 0 };
        }
        return *this;
    }

    CommandBufferHandle CommandBuffer::Handle() const noexcept
    {
        return handle;
    }

    Result CommandBuffer::Begin() noexcept
    {
        isRecording = true;
#ifdef RHI_SYSTEM_USE_VULKAN
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = nullptr;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr; // Only relevant for secondary command buffers
        VkResult result = vkBeginCommandBuffer(handle.As<VkCommandBuffer>(), &beginInfo);
        return {result};
#elifdef RHI_SYSTEM_USE_DX12
        // In DX12, command lists are implicitly open for recording after being created and passed to the ctor of CommandBuffer
        return Result::Success();
#endif
    }

    Result CommandBuffer::End() noexcept
    {
        assert(isRecording && "CommandBuffer::End() called while not in recording mode.");
        isRecording = false;
#ifdef RHI_SYSTEM_USE_VULKAN
        VkResult result = vkEndCommandBuffer(handle.As<VkCommandBuffer>());
        return {result};
#elifdef RHI_SYSTEM_USE_DX12
        ID3D12GraphicsCommandList* cmdList = handle.As<ID3D12GraphicsCommandList*>();
        HRESULT hr = cmdList->Close();
        if (SUCCEEDED(hr))
        {
            return Result::Success();
        }
        else
        {
            return Result(hr);
        }
#endif
    }

#ifdef RHI_SYSTEM_USE_VULKAN
    namespace
    {
        // the RHI handles are 64-bit values, and so are the Vulkan non-dispatchable handles they carry
        template<typename VkType, typename HandleType>
        std::vector<VkType> ToVulkanHandles(std::span<const HandleType> handles)
        {
            std::vector<VkType> result;
            result.reserve(handles.size());
            for (const HandleType& handle : handles)
            {
                result.push_back(handle.template As<VkType>());
            }
            return result;
        }
    }

    void CommandBuffer::BeginRendering(const RenderingInfo& rendering_info) noexcept
    {
        assert(isRecording && "CommandBuffer::BeginRendering() called while not in recording mode.");
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(rendering_info.ColorAttachments.size());
        for (const ColorAttachment& attachment : rendering_info.ColorAttachments)
        {
            VkRenderingAttachmentInfo& vkAttachment = colorAttachments.emplace_back();
            vkAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            vkAttachment.imageView = attachment.ImageView.As<VkImageView>();
            vkAttachment.imageLayout = vk::ToVulkan(attachment.Layout);
            vkAttachment.loadOp = vk::ToVulkan(attachment.LoadOp);
            vkAttachment.storeOp = vk::ToVulkan(attachment.StoreOp);
            vkAttachment.clearValue.color.float32[0] = attachment.ClearValue[0];
            vkAttachment.clearValue.color.float32[1] = attachment.ClearValue[1];
            vkAttachment.clearValue.color.float32[2] = attachment.ClearValue[2];
            vkAttachment.clearValue.color.float32[3] = attachment.ClearValue[3];
            if (attachment.ResolveImageView)
            {
                vkAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                vkAttachment.resolveImageView = attachment.ResolveImageView.As<VkImageView>();
                vkAttachment.resolveImageLayout = vk::ToVulkan(attachment.ResolveLayout);
            }
        }

        VkRenderingAttachmentInfo depthAttachment{};
        VkRenderingAttachmentInfo stencilAttachment{};
        const std::optional<DepthStencilAttachment>& depthStencil = rendering_info.DepthStencilAttachment;
        if (depthStencil)
        {
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = depthStencil->ImageView.As<VkImageView>();
            depthAttachment.imageLayout = vk::ToVulkan(depthStencil->Layout);
            depthAttachment.loadOp = vk::ToVulkan(depthStencil->DepthLoadOp);
            depthAttachment.storeOp = vk::ToVulkan(depthStencil->DepthStoreOp);
            depthAttachment.clearValue.depthStencil.depth = depthStencil->ClearValue.Depth;
            depthAttachment.clearValue.depthStencil.stencil = depthStencil->ClearValue.Stencil;
            stencilAttachment = depthAttachment;
            stencilAttachment.loadOp = vk::ToVulkan(depthStencil->StencilLoadOp);
            stencilAttachment.storeOp = vk::ToVulkan(depthStencil->StencilStoreOp);
        }

        const VkRenderingInfo vkRenderingInfo
        {
            VK_STRUCTURE_TYPE_RENDERING_INFO,
            nullptr,
            0,
            VkRect2D{ { rendering_info.RenderArea.PosX, rendering_info.RenderArea.PosY }, { rendering_info.RenderArea.Width, rendering_info.RenderArea.Height } },
            rendering_info.LayerCount,
            0, // viewMask
            static_cast<uint32_t>(colorAttachments.size()),
            colorAttachments.data(),
            depthStencil ? &depthAttachment : nullptr,
            (depthStencil && depthStencil->HasStencil) ? &stencilAttachment : nullptr
        };
        vkCmdBeginRendering(handle.As<VkCommandBuffer>(), &vkRenderingInfo);
    }

    void CommandBuffer::EndRendering() noexcept
    {
        assert(isRecording && "CommandBuffer::EndRendering() called while not in recording mode.");
        vkCmdEndRendering(handle.As<VkCommandBuffer>());
    }

    void CommandBuffer::SetViewports(std::span<const Viewport> viewports) noexcept
    {
        assert(isRecording && "CommandBuffer::SetViewports() called while not in recording mode.");
        static_assert(sizeof(Viewport) == sizeof(VkViewport) && offsetof(Viewport, MaxDepth) == offsetof(VkViewport, maxDepth));
        vkCmdSetViewportWithCount(handle.As<VkCommandBuffer>(), static_cast<uint32_t>(viewports.size()), reinterpret_cast<const VkViewport*>(viewports.data()));
    }

    void CommandBuffer::SetScissors(std::span<const Rect2D> scissors) noexcept
    {
        assert(isRecording && "CommandBuffer::SetScissors() called while not in recording mode.");
        static_assert(sizeof(Rect2D) == sizeof(VkRect2D) && offsetof(Rect2D, Height) == offsetof(VkRect2D, extent.height));
        vkCmdSetScissorWithCount(handle.As<VkCommandBuffer>(), static_cast<uint32_t>(scissors.size()), reinterpret_cast<const VkRect2D*>(scissors.data()));
    }

    Result CommandBuffer::BindShaders(const ShaderProgram& program)
    {
        assert(isRecording && "CommandBuffer::BindShaders() called while not in recording mode.");
        return program.BindShaders(handle);
    }

    void CommandBuffer::BindDescriptorSets(PipelineBindPoint bind_point,
                                           const PipelineLayoutHandle& layout,
                                           uint32_t first_set,
                                           std::span<const DescriptorSetHandle> descriptor_sets,
                                           std::span<const uint32_t> dynamic_offsets) noexcept
    {
        assert(isRecording && "CommandBuffer::BindDescriptorSets() called while not in recording mode.");
        const std::vector<VkDescriptorSet> sets = ToVulkanHandles<VkDescriptorSet>(descriptor_sets);
        vkCmdBindDescriptorSets(handle.As<VkCommandBuffer>(),
                                vk::ToVulkan(bind_point),
                                layout.As<VkPipelineLayout>(),
                                first_set,
                                static_cast<uint32_t>(sets.size()),
                                sets.data(),
                                static_cast<uint32_t>(dynamic_offsets.size()),
                                dynamic_offsets.data());
    }

    void CommandBuffer::PushConstants(const PipelineLayoutHandle& layout, ShaderStageFlags stages, uint32_t offset, std::span<const std::byte> data)
    {
        assert(isRecording && "CommandBuffer::PushConstants() called while not in recording mode.");
        vkCmdPushConstants(handle.As<VkCommandBuffer>(),
                           layout.As<VkPipelineLayout>(),
                           vk::ToVulkanStages(stages),
                           offset,
                           static_cast<uint32_t>(data.size()),
                           data.data());
    }

    void CommandBuffer::BindIndexBuffer(const BufferHandle& buffer, uint64_t offset, IndexType index_type) noexcept
    {
        assert(isRecording && "CommandBuffer::BindIndexBuffer() called while not in recording mode.");
        vkCmdBindIndexBuffer(handle.As<VkCommandBuffer>(), buffer.As<VkBuffer>(), offset, vk::ToVulkan(index_type));
    }

    void CommandBuffer::BindVertexBuffers(uint32_t first_binding, std::span<const BufferHandle> buffers, std::span<const uint64_t> offsets) noexcept
    {
        assert(isRecording && "CommandBuffer::BindVertexBuffers() called while not in recording mode.");
        assert(buffers.size() == offsets.size() && "CommandBuffer::BindVertexBuffers() needs one offset per buffer.");
        const std::vector<VkBuffer> vkBuffers = ToVulkanHandles<VkBuffer>(buffers);
        vkCmdBindVertexBuffers(handle.As<VkCommandBuffer>(), first_binding, static_cast<uint32_t>(vkBuffers.size()), vkBuffers.data(), offsets.data());
    }

    void CommandBuffer::Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) noexcept
    {
        assert(isRecording && "CommandBuffer::Draw() called while not in recording mode.");
        vkCmdDraw(handle.As<VkCommandBuffer>(), vertex_count, instance_count, first_vertex, first_instance);
    }

    void CommandBuffer::DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) noexcept
    {
        assert(isRecording && "CommandBuffer::DrawIndexed() called while not in recording mode.");
        vkCmdDrawIndexed(handle.As<VkCommandBuffer>(), index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void CommandBuffer::Dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) noexcept
    {
        assert(isRecording && "CommandBuffer::Dispatch() called while not in recording mode.");
        vkCmdDispatch(handle.As<VkCommandBuffer>(), group_count_x, group_count_y, group_count_z);
    }

    void CommandBuffer::Barrier(const DependencyInfo& dependencies) noexcept
    {
        assert(isRecording && "CommandBuffer::Barrier() called while not in recording mode.");
        std::vector<VkMemoryBarrier2> memoryBarriers;
        memoryBarriers.reserve(dependencies.GlobalBarriers.size());
        for (const GlobalBarrier& barrier : dependencies.GlobalBarriers)
        {
            memoryBarriers.push_back(VkMemoryBarrier2
            {
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                nullptr,
                vk::ToVulkan(barrier.SrcStages),
                vk::ToVulkan(barrier.SrcAccess),
                vk::ToVulkan(barrier.DstStages),
                vk::ToVulkan(barrier.DstAccess)
            });
        }

        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
        bufferBarriers.reserve(dependencies.BufferBarriers.size());
        for (const BufferBarrier& barrier : dependencies.BufferBarriers)
        {
            bufferBarriers.push_back(VkBufferMemoryBarrier2
            {
                VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                nullptr,
                vk::ToVulkan(barrier.SrcStages),
                vk::ToVulkan(barrier.SrcAccess),
                vk::ToVulkan(barrier.DstStages),
                vk::ToVulkan(barrier.DstAccess),
                barrier.SrcQueueFamily,
                barrier.DstQueueFamily,
                barrier.Buffer.As<VkBuffer>(),
                barrier.Offset,
                barrier.Size
            });
        }

        std::vector<VkImageMemoryBarrier2> imageBarriers;
        imageBarriers.reserve(dependencies.ImageBarriers.size());
        for (const ImageBarrier& barrier : dependencies.ImageBarriers)
        {
            imageBarriers.push_back(VkImageMemoryBarrier2
            {
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                nullptr,
                vk::ToVulkan(barrier.SrcStages),
                vk::ToVulkan(barrier.SrcAccess),
                vk::ToVulkan(barrier.DstStages),
                vk::ToVulkan(barrier.DstAccess),
                vk::ToVulkan(barrier.OldLayout),
                vk::ToVulkan(barrier.NewLayout),
                barrier.SrcQueueFamily,
                barrier.DstQueueFamily,
                barrier.Image.As<VkImage>(),
                vk::ToVulkan(barrier.Range)
            });
        }

        const VkDependencyInfo dependencyInfo
        {
            VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(memoryBarriers.size()),
            memoryBarriers.data(),
            static_cast<uint32_t>(bufferBarriers.size()),
            bufferBarriers.data(),
            static_cast<uint32_t>(imageBarriers.size()),
            imageBarriers.data()
        };
        vkCmdPipelineBarrier2(handle.As<VkCommandBuffer>(), &dependencyInfo);
    }

    void CommandBuffer::FillBuffer(const BufferHandle& buffer, uint64_t offset, uint64_t size, uint32_t value) noexcept
    {
        assert(isRecording && "CommandBuffer::FillBuffer() called while not in recording mode.");
        vkCmdFillBuffer(handle.As<VkCommandBuffer>(), buffer.As<VkBuffer>(), offset, size, value);
    }

    void CommandBuffer::CopyBuffer(const BufferHandle& src, const BufferHandle& dst, std::span<const BufferCopy> regions) noexcept
    {
        assert(isRecording && "CommandBuffer::CopyBuffer() called while not in recording mode.");
        static_assert(sizeof(BufferCopy) == sizeof(VkBufferCopy) && offsetof(BufferCopy, Size) == offsetof(VkBufferCopy, size));
        vkCmdCopyBuffer(handle.As<VkCommandBuffer>(), src.As<VkBuffer>(), dst.As<VkBuffer>(),
                        static_cast<uint32_t>(regions.size()), reinterpret_cast<const VkBufferCopy*>(regions.data()));
    }

    void CommandBuffer::CopyImageToBuffer(const ImageHandle& src, ImageLayout src_layout, const BufferHandle& dst, std::span<const BufferImageCopy> regions) noexcept
    {
        assert(isRecording && "CommandBuffer::CopyImageToBuffer() called while not in recording mode.");
        std::vector<VkBufferImageCopy> vkRegions;
        vkRegions.reserve(regions.size());
        for (const BufferImageCopy& region : regions)
        {
            vkRegions.push_back(VkBufferImageCopy
            {
                region.BufferOffset,
                region.BufferRowLength,
                region.BufferImageHeight,
                VkImageSubresourceLayers{ vk::ToVulkan(region.Aspect), region.MipLevel, region.BaseArrayLayer, region.LayerCount },
                VkOffset3D{ region.OffsetX, region.OffsetY, region.OffsetZ },
                VkExtent3D{ region.Width, region.Height, region.Depth }
            });
        }
        vkCmdCopyImageToBuffer(handle.As<VkCommandBuffer>(), src.As<VkImage>(), vk::ToVulkan(src_layout), dst.As<VkBuffer>(),
                               static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
    }

    void CommandBuffer::CopyBufferToImage(const BufferHandle& src, const ImageHandle& dst, ImageLayout dst_layout, std::span<const BufferImageCopy> regions) noexcept
    {
        assert(isRecording && "CommandBuffer::CopyBufferToImage() called while not in recording mode.");
        std::vector<VkBufferImageCopy> vkRegions;
        vkRegions.reserve(regions.size());

        for (const BufferImageCopy& region : regions)
        {
            vkRegions.push_back(VkBufferImageCopy
            {
                region.BufferOffset,
                region.BufferRowLength,
                region.BufferImageHeight,
                VkImageSubresourceLayers{ vk::ToVulkan(region.Aspect), region.MipLevel, region.BaseArrayLayer, region.LayerCount },
                VkOffset3D{ region.OffsetX, region.OffsetY, region.OffsetZ },
                VkExtent3D{ region.Width, region.Height, region.Depth }
            });
        }

        vkCmdCopyBufferToImage(handle.As<VkCommandBuffer>(), src.As<VkBuffer>(), dst.As<VkImage>(), vk::ToVulkan(dst_layout),
                               static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
    }

    void CommandBuffer::BlitImage(const ImageHandle& src, ImageLayout src_layout, const ImageHandle& dst, ImageLayout dst_layout,
                                  std::span<const ImageBlit> regions, BlitFilter filter) noexcept
    {
        assert(isRecording && "CommandBuffer::BlitImage() called while not in recording mode.");
        std::vector<VkImageBlit> vkRegions;
        vkRegions.reserve(regions.size());

        for (const ImageBlit& region : regions)
        {
            const VkImageAspectFlags aspect = vk::ToVulkan(region.Aspect);
            vkRegions.push_back(VkImageBlit
            {
                VkImageSubresourceLayers{ aspect, region.SrcMipLevel, region.BaseArrayLayer, region.LayerCount },
                {
                    VkOffset3D{ 0, 0, 0 },
                    VkOffset3D{ static_cast<int32_t>(region.SrcWidth), static_cast<int32_t>(region.SrcHeight), static_cast<int32_t>(region.SrcDepth) }
                },
                VkImageSubresourceLayers{ aspect, region.DstMipLevel, region.BaseArrayLayer, region.LayerCount },
                {
                    VkOffset3D{ 0, 0, 0 },
                    VkOffset3D{ static_cast<int32_t>(region.DstWidth), static_cast<int32_t>(region.DstHeight), static_cast<int32_t>(region.DstDepth) }
                }
            });
        }

        const VkFilter vkFilter = filter == BlitFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        vkCmdBlitImage(handle.As<VkCommandBuffer>(), src.As<VkImage>(), vk::ToVulkan(src_layout), dst.As<VkImage>(), vk::ToVulkan(dst_layout),
                       static_cast<uint32_t>(vkRegions.size()), vkRegions.data(), vkFilter);
    }

    void CommandBuffer::ResetQueryPool(const QueryPoolHandle& pool, uint32_t first, uint32_t count) noexcept
    {
        assert(isRecording && "CommandBuffer::ResetQueryPool() called while not in recording mode.");
        vkCmdResetQueryPool(handle.As<VkCommandBuffer>(), pool.As<VkQueryPool>(), first, count);
    }

    void CommandBuffer::WriteTimestamp(PipelineStage stage, const QueryPoolHandle& pool, uint32_t query) noexcept
    {
        assert(isRecording && "CommandBuffer::WriteTimestamp() called while not in recording mode.");
        vkCmdWriteTimestamp2(handle.As<VkCommandBuffer>(), vk::ToVulkan(stage), pool.As<VkQueryPool>(), query);
    }
#endif // RHI_SYSTEM_USE_VULKAN

}
