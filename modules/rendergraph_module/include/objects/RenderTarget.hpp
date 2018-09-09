#pragma once
#ifndef DIAMOND_DOGS_RENDER_TARGET_HPP
#define DIAMOND_DOGS_RENDER_TARGET_HPP
#include "ForwardDecl.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include "PipelineResource.hpp"

struct VulkanResource;
class DepthTarget;

class RenderTarget {
    RenderTarget(const RenderTarget& other) = delete;
    RenderTarget& operator=(const RenderTarget& other) = delete;
public:

    RenderTarget();
    ~RenderTarget();

    void Create(uint32_t width, uint32_t height, const VkFormat image_format, const bool has_depth = false, const uint32_t mip_maps = 1,
        const VkSampleCountFlags sample_count = VK_SAMPLE_COUNT_1_BIT, bool depth_only = false);
    void CreateAsCube(uint32_t size, const VkFormat image_format, const bool has_depth = false, const uint32_t mip_maps = 1, bool depth_only = false);
    void Add(const VkFormat new_format);

    void Clear();

    const VulkanResource* GetImage(const size_t view_idx = 0) const;
    VulkanResource* GetImage(const size_t view_idx = 0);
    const VulkanResource* GetImageMSAA(const size_t view_idx = 0) const;
    VulkanResource* GetImageMSAA(const size_t view_idx = 0);
    image_info_t GetImageInfo() const;

    VkViewport Viewport;
private:
    std::unique_ptr<DepthTarget> depth;
    size_t numViews;
    std::vector<VulkanResource*> renderTargets;
    std::vector<VulkanResource*> renderTargetsMSAA;
    std::vector<uint32_t> msaaUpToDate;
};

#endif // !DIAMOND_DOGS_RENDER_TARGET_HPP
