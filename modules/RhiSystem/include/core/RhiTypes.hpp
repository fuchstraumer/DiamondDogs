#pragma once
#ifndef DIAMOND_DOGS_RHI_TYPES_HPP
#define DIAMOND_DOGS_RHI_TYPES_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "Math.hpp"
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
        LoadOp LoadOp = LoadOp::Clear;
        StoreOp StoreOp = StoreOp::Store;
        ClearColorValue ClearValue{};
    };

    struct DepthStencilAttachment
    {
        ImageViewHandle ImageView{};
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
