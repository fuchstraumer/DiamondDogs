#pragma once
#ifndef DIAMOND_DOGS_RHI_RENDERING_INFO_HPP
#define DIAMOND_DOGS_RHI_RENDERING_INFO_HPP
#include "RhiHandle.hpp"
#include "Math.hpp"
#include <span>
#include <optional>

namespace rhi
{
    enum class LoadOp : uint8_t
    {
        Load,       // VK_ATTACHMENT_LOAD_OP_LOAD / don't clear
        Clear,      // VK_ATTACHMENT_LOAD_OP_CLEAR / clear before rendering
        DontCare    // VK_ATTACHMENT_LOAD_OP_DONT_CARE / undefined
    };

    enum class StoreOp : uint8_t
    {
        Store,      // VK_ATTACHMENT_STORE_OP_STORE / keep contents
        DontCare    // VK_ATTACHMENT_STORE_OP_DONT_CARE / discard contents
    };

    struct ClearColorValue
    {
        math::Float4 color{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

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
}

#endif // !DIAMOND_DOGS_RHI_RENDERING_INFO_HPP