#pragma once
#ifndef DIAMOND_DOGS_RENDER_TARGET_HPP
#define DIAMOND_DOGS_RENDER_TARGET_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include "PipelineResource.hpp"
#include "tagged_bool.hpp"

struct VulkanResource;

using AttachDepthTarget = tagged_bool<struct IsDepthTarget_tag>;
using AllowReadback = tagged_bool<struct AllowReadback_tag>;

class RenderTarget {
    RenderTarget(const RenderTarget& other) = delete;
    RenderTarget& operator=(const RenderTarget& other) = delete;
public:

    RenderTarget();
    ~RenderTarget();

    void Create(uint32_t width, uint32_t height, const VkFormat image_format, const AttachDepthTarget is_depth_target, const uint32_t mip_maps = 1,
        const VkSampleCountFlags sample_count = VK_SAMPLE_COUNT_1_BIT, const AllowReadback allow_readback = AllowReadback{ false });
    void CreateAsCube(uint32_t size, const VkFormat image_format, const AttachDepthTarget is_depth_target, const uint32_t mip_maps = 1,
        const AllowReadback allow_readback = AllowReadback{ false });
    void AddDepthTarget(const VkFormat image_format, const AllowReadback allow_readback = AllowReadback{ false });

    void AddView(const VkFormat new_format);

    void Clear();

    const VulkanResource* GetImage(const size_t view_idx = 0) const;
    VulkanResource* GetImage(const size_t view_idx = 0);
    const VulkanResource* GetImageMSAA(const size_t view_idx = 0) const;
    VulkanResource* GetImageMSAA(const size_t view_idx = 0);
    VulkanResource* GetDepth();
    const VulkanResource* GetDepth() const noexcept;
    image_info_t GetImageInfo() const;

    bool IsDepthRT() const noexcept;

    VkViewport Viewport;

private:
    size_t numViews;
    std::vector<VulkanResource*> renderTargets;
    std::vector<VulkanResource*> renderTargetsMSAA;
    VulkanResource* depthTarget;
    AttachDepthTarget hasDepthTarget;
    std::vector<uint32_t> msaaUpToDate;
    VkFormatFeatureFlags requiredFeatures;
};

#endif // !DIAMOND_DOGS_RENDER_TARGET_HPP
