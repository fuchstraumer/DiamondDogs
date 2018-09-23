#include "objects/RenderTarget.hpp"
#include "objects/DepthTarget.hpp"
#include "ResourceTypes.hpp"
#include "ResourceContext.hpp"
#include "RenderingContext.hpp"
#include "LogicalDevice.hpp"
#include "CreateInfoBase.hpp"

RenderTarget::RenderTarget() : depth(nullptr), numViews(0) {}

RenderTarget::~RenderTarget() {
    Clear();
}

void RenderTarget::Create(uint32_t width, uint32_t height, const VkFormat image_format, const bool has_depth, const uint32_t mip_maps, const VkSampleCountFlags sample_count, bool depth_only) {
    Clear();
    auto& rsrc_context = ResourceContext::Get();
    auto& renderer = RenderingContext::Get();
    if (!depth_only) {
        VkImageCreateInfo image_info = vpr::vk_image_create_info_base;
        image_info.extent.width = width;
        image_info.extent.height = height;
        image_info.mipLevels = mip_maps;
        image_info.samples = VkSampleCountFlagBits(sample_count);
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image_info.format = image_format;
        image_info.tiling = renderer.Device()->GetFormatTiling(image_format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        VkImageViewCreateInfo view_info = vpr::vk_image_view_create_info_base;
        view_info.format = image_info.format;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_maps, 0, 1 };
        renderTargets.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
        if (sample_count > 1) {
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            renderTargetsMSAA.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
            msaaUpToDate.emplace_back(VK_FALSE);
        }
        else {
            msaaUpToDate.emplace_back(VK_TRUE);
        }
    }

    Viewport.width = static_cast<float>(width);
    Viewport.height = static_cast<float>(height);
    Viewport.minDepth = 0.0f;
    Viewport.maxDepth = 1.0f;
    Viewport.x = 0;
    Viewport.y = 0;
    numViews = 1;

    if (has_depth) {
        depth = std::make_unique<DepthTarget>();
        depth->Create(width, height, sample_count);
    }
    else {
        depth = nullptr;
    }

}

void RenderTarget::CreateAsCube(uint32_t size, const VkFormat image_format, const bool has_depth, const uint32_t mip_maps, bool depth_only) {
    Clear();
    auto& renderer = RenderingContext::Get();
    if (!depth_only) {
        auto& rsrc_context = ResourceContext::Get();
        VkImageCreateInfo image_info = vpr::vk_image_create_info_base;
        image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        image_info.arrayLayers = 6;
        image_info.mipLevels = mip_maps;
        image_info.extent.width = size;
        image_info.extent.height = size;
        image_info.format = image_format;
        image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        image_info.tiling = renderer.Device()->GetFormatTiling(image_format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);        
        VkImageViewCreateInfo view_info = vpr::vk_image_view_create_info_base;
        view_info.format = image_info.format;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_maps, 0, 6 };
        renderTargets.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
        msaaUpToDate.emplace_back(VK_TRUE);
    }

    Viewport.width = static_cast<float>(size);
    Viewport.height = static_cast<float>(size);
    Viewport.minDepth = 0.0f;
    Viewport.maxDepth = 1.0f;
    Viewport.x = 0;
    Viewport.y = 0;
    numViews = 1;

    if (has_depth) {
        depth = std::make_unique<DepthTarget>();
        depth->CreateAsCube(size);
    }
}

void RenderTarget::Add(const VkFormat new_format) {
        
    if (renderTargets.empty()) {
        throw std::runtime_error("Tried to add format to empty rendertarget object!");
    }

    ++numViews;
    auto& renderer = RenderingContext::Get();
    VkImageCreateInfo image_info = *reinterpret_cast<VkImageCreateInfo*>(renderTargets.back()->Info);
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.format = new_format;
    image_info.tiling = renderer.Device()->GetFormatTiling(new_format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    VkImageViewCreateInfo view_info = *reinterpret_cast<VkImageViewCreateInfo*>(renderTargets.back()->ViewInfo);
    view_info.format = new_format;

    auto& rsrc_context = ResourceContext::Get();
    renderTargets.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
    if (image_info.samples > 1) {
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        renderTargetsMSAA.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
        msaaUpToDate.emplace_back(VK_FALSE);
    }
    else {
        msaaUpToDate.emplace_back(VK_TRUE);
    }
}

void RenderTarget::Clear() {
    if (depth.get() != nullptr) {
        depth.reset();
    }

    auto& rsrc_context = ResourceContext::Get();
    for (auto& msaa_target : renderTargetsMSAA) {
        rsrc_context.DestroyResource(msaa_target);
    }

    for (auto& target : renderTargets) {
        rsrc_context.DestroyResource(target);
    }

    renderTargets.clear(); renderTargets.shrink_to_fit();
    renderTargetsMSAA.clear(); renderTargetsMSAA.shrink_to_fit();
    msaaUpToDate.clear(); msaaUpToDate.shrink_to_fit();
}

const VulkanResource* RenderTarget::GetImage(const size_t view_idx) const {
    return renderTargets[view_idx];
}

VulkanResource* RenderTarget::GetImage(const size_t view_idx) {
    return renderTargets[view_idx];
}

const VulkanResource* RenderTarget::GetImageMSAA(const size_t view_idx) const {
    return renderTargetsMSAA[view_idx];
}

VulkanResource* RenderTarget::GetImageMSAA(const size_t view_idx) {
    return renderTargetsMSAA[view_idx];
}

image_info_t RenderTarget::GetImageInfo() const {
    image_info_t result;
    VkImageCreateInfo* info = reinterpret_cast<VkImageCreateInfo*>(renderTargets.front()->Info);
    result.Format = info->format;
    result.ArrayLayers = info->arrayLayers;
    result.MipLevels = info->mipLevels;
    result.Samples = info->samples;
    result.SizeClass = image_info_t::size_class::SwapchainRelative;
    result.Usage = info->usage;
    return result;
}

DepthTarget* RenderTarget::Depth() noexcept {
    return depth.get();
}

const DepthTarget* RenderTarget::Depth() const noexcept {
    return depth.get();
}
