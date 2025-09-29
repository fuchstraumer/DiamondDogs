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
        ShaderStageFlags stageFlags = ShaderStageFlags::All;
        uint32_t offset = 0;           // Byte offset within push constant block
        uint32_t size = 0;             // Size in bytes (must be multiple of 4 for DX12)
        
        constexpr PushConstantRange() noexcept = default;
        constexpr PushConstantRange(ShaderStageFlags stages, uint32_t offset, uint32_t size) noexcept
            : stageFlags{ stages }, offset{ offset }, size{ size }
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
        float depth = 1.0f;
        uint32_t stencil = 0;
    };

    struct ColorAttachment
    {
        ImageViewHandle imageView{};
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        ClearColorValue clearValue{};
    };

    struct DepthStencilAttachment
    {
        ImageViewHandle imageView{};
        LoadOp depthLoadOp = LoadOp::Clear;
        StoreOp depthStoreOp = StoreOp::DontCare;
        LoadOp stencilLoadOp = LoadOp::DontCare;
        StoreOp stencilStoreOp = StoreOp::DontCare;
        ClearDepthStencilValue clearValue{};
    };

    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct Rect2D
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct RenderingInfo
    {
        std::span<const ColorAttachment> colorAttachments;
        std::optional<DepthStencilAttachment> depthStencilAttachment = std::nullopt;
        Rect2D renderArea{};
        uint32_t layerCount = 1;
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
