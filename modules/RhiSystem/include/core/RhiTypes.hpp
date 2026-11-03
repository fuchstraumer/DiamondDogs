#pragma once
#ifndef DIAMOND_DOGS_RHI_TYPES_HPP
#define DIAMOND_DOGS_RHI_TYPES_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "math/Math.hpp"
#include <cstdint>
#include <span>
#include <optional>

namespace rhi
{
    /**
     * @brief Cross-API push constant range specification
     * Maps to VkPushConstantRange on Vulkan and root constants on DX12
     */
    struct PushConstantRange
    {
        ShaderStageFlags StageFlags = ShaderStageFlags::None;
        uint32_t Offset{ 0 };           // Byte offset within push constant block
        uint32_t Size{ 0 };             // Size in bytes (must be multiple of 4 for DX12)

        constexpr PushConstantRange() noexcept = default;
        constexpr PushConstantRange(ShaderStageFlags stages, uint32_t offset, uint32_t size) noexcept
            : StageFlags{ stages }, Offset{ offset }, Size{ size }
        {
        }
    };

    struct PipelineLayoutCreateInfo
    {
        std::span<const DescriptorSetLayoutHandle> setLayouts;
        std::span<const PushConstantRange> pushConstantRanges;
    };

    using ColorRgba = math::Float4;
    using ColorRgb = math::Float3;
    using ClearColorValue = ColorRgba;

    struct ClearDepthStencilValue
    {
        float Depth{ 1.0f };
        uint32_t Stencil{ 0 };
    };

    struct ColorAttachment
    {
        ImageViewHandle ImageView{};
        ImageLayout Layout = ImageLayout::ColorAttachment;
        LoadOp LoadOp = LoadOp::Clear;
        StoreOp StoreOp = StoreOp::Store;
        ClearColorValue ClearValue{};
        /** @brief When set, a multisampled attachment is averaged into this single-sampled view at the end of rendering */
        ImageViewHandle ResolveImageView{};
        ImageLayout ResolveLayout = ImageLayout::ColorAttachment;
    };

    struct DepthStencilAttachment
    {
        ImageViewHandle ImageView{};
        ImageLayout Layout = ImageLayout::DepthStencilAttachment;
        // set for a combined depth-stencil format, so the stencil ops apply too
        bool HasStencil{ false };
        LoadOp DepthLoadOp = LoadOp::Clear;
        StoreOp DepthStoreOp = StoreOp::DontCare;
        LoadOp StencilLoadOp = LoadOp::DontCare;
        StoreOp StencilStoreOp = StoreOp::DontCare;
        ClearDepthStencilValue ClearValue{};
    };

    struct Viewport
    {
        float PosX{ 0.0f };
        float PosY{ 0.0f };
        float Width{ 0.0f };
        float Height{ 0.0f };
        float MinDepth{ 0.0f };
        float MaxDepth{ 1.0f };
    };

    struct Rect2D
    {
        int32_t PosX{ 0 };
        int32_t PosY{ 0 };
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };
    };

    struct RenderingInfo
    {
        std::span<const ColorAttachment> ColorAttachments;
        std::optional<DepthStencilAttachment> DepthStencilAttachment = std::nullopt;
        Rect2D RenderArea{};
        uint32_t LayerCount{ 1 };
    };

    constexpr uint64_t k_WholeSize = ~0ull;
    constexpr uint32_t k_QueueFamilyIgnored = ~0u;
    constexpr uint32_t k_RemainingMipLevels = ~0u;
    constexpr uint32_t k_RemainingArrayLayers = ~0u;

    /** @brief A global memory dependency (VkMemoryBarrier2; not named MemoryBarrier, which winnt.h defines as a macro): everything before SrcStages/SrcAccess, against everything after DstStages/DstAccess */
    struct GlobalBarrier
    {
        PipelineStage SrcStages = PipelineStage::None;
        Access SrcAccess = Access::None;
        PipelineStage DstStages = PipelineStage::None;
        Access DstAccess = Access::None;
    };

    struct BufferBarrier
    {
        BufferHandle Buffer{};
        PipelineStage SrcStages = PipelineStage::None;
        Access SrcAccess = Access::None;
        PipelineStage DstStages = PipelineStage::None;
        Access DstAccess = Access::None;
        uint64_t Offset{ 0u };
        uint64_t Size{ k_WholeSize };
        // set both to transfer queue family ownership; the release and acquire barriers must match
        uint32_t SrcQueueFamily{ k_QueueFamilyIgnored };
        uint32_t DstQueueFamily{ k_QueueFamilyIgnored };
    };

    struct ImageSubresourceRange
    {
        ImageAspect Aspects = ImageAspect::Color;
        uint32_t BaseMipLevel{ 0u };
        uint32_t LevelCount{ k_RemainingMipLevels };
        uint32_t BaseArrayLayer{ 0u };
        uint32_t LayerCount{ k_RemainingArrayLayers };
    };

    struct ImageBarrier
    {
        ImageHandle Image;
        PipelineStage SrcStages = PipelineStage::None;
        Access SrcAccess = Access::None;
        PipelineStage DstStages = PipelineStage::None;
        Access DstAccess = Access::None;
        ImageLayout OldLayout = ImageLayout::Undefined;
        ImageLayout NewLayout = ImageLayout::Undefined;
        ImageSubresourceRange Range{};
        uint32_t SrcQueueFamily{ k_QueueFamilyIgnored };
        uint32_t DstQueueFamily{ k_QueueFamilyIgnored };
    };

    /** @brief One barrier command's worth of dependencies (VkDependencyInfo) */
    struct DependencyInfo
    {
        std::span<const GlobalBarrier> GlobalBarriers;
        std::span<const BufferBarrier> BufferBarriers;
        std::span<const ImageBarrier> ImageBarriers;
    };

    struct BufferCopy
    {
        uint64_t SrcOffset{ 0u };
        uint64_t DstOffset{ 0u };
        uint64_t Size{ 0u };
    };

    /** @brief Copies one subresource layer set of an image into a buffer. A zero row length or image height means tightly packed */
    struct BufferImageCopy
    {
        uint64_t BufferOffset{ 0u };
        uint32_t BufferRowLength{ 0u };
        uint32_t BufferImageHeight{ 0u };
        ImageAspect Aspect = ImageAspect::Color;
        uint32_t MipLevel{ 0u };
        uint32_t BaseArrayLayer{ 0u };
        uint32_t LayerCount{ 1u };
        int32_t OffsetX{ 0 };
        int32_t OffsetY{ 0 };
        int32_t OffsetZ{ 0 };
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        uint32_t Depth{ 1u };
    };

    enum class BlitFilter : uint8_t
    {
        Nearest,
        Linear
    };

    /** @brief Scales one mip level of a set of array layers onto another (VkImageBlit). Each box starts at texel
     *  (0, 0, 0), and the whole source box is scaled onto the whole destination box */
    struct ImageBlit
    {
        ImageAspect Aspect = ImageAspect::Color;
        uint32_t SrcMipLevel{ 0u };
        uint32_t DstMipLevel{ 0u };
        uint32_t BaseArrayLayer{ 0u };
        uint32_t LayerCount{ 1u };
        uint32_t SrcWidth{ 0u };
        uint32_t SrcHeight{ 0u };
        uint32_t SrcDepth{ 1u };
        uint32_t DstWidth{ 0u };
        uint32_t DstHeight{ 0u };
        uint32_t DstDepth{ 1u };
    };

    /** @brief A semaphore wait or signal in a queue submission. Value is ignored for binary semaphores */
    struct SemaphoreSubmitInfo
    {
        constexpr SemaphoreSubmitInfo(const TimelineSemaphoreHandle& semaphore, uint64_t value, PipelineStage stages) noexcept
            : Semaphore{ semaphore.Get() }, Value{ value }, Stages{ stages }
        {
        }
        constexpr SemaphoreSubmitInfo(const BinarySemaphoreHandle& semaphore, PipelineStage stages) noexcept
            : Semaphore{ semaphore.Get() }, Stages{ stages }
        {
        }

        uint64_t Semaphore{ 0u };
        uint64_t Value{ 0u };
        PipelineStage Stages = PipelineStage::AllCommands;
    };

    struct SubmitInfo
    {
        std::span<const SemaphoreSubmitInfo> Waits;
        std::span<const CommandBufferHandle> CommandBuffers;
        std::span<const SemaphoreSubmitInfo> Signals;
    };

    struct SpecializationConstant
    {
        union ValueType
        {
            int32_t i32;
            uint32_t u32; // u32 will also store bool values
            float f32;
        };
        uint32_t ConstantId = 0;       // Constant ID in shader
        ValueType Value;
    };

    

}


#endif // !DIAMOND_DOGS_RHI_TYPES_HPP
