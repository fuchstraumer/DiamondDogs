#include "RenderTarget.hpp"
#include "ResourceTypes.hpp"
#include "ResourceContext.hpp"
#include "RenderingContext.hpp"
#include "LogicalDevice.hpp"
#include "CreateInfoBase.hpp"

RenderTarget::RenderTarget() : numViews{ 0 }, depthTarget{ nullptr }, hasDepthTarget{ false } {}

RenderTarget::~RenderTarget() {
    Clear();
}

void RenderTarget::Create(uint32_t width, uint32_t height, const VkFormat image_format, const AttachDepthTarget is_depth_target, const uint32_t mip_maps, const VkSampleCountFlags sample_count, 
    const AllowReadback allow_readback) {
    hasDepthTarget = is_depth_target;

    Clear();
    auto& rsrc_context = ResourceContext::Get();
    auto& renderer = RenderingContext::Get();

    VkImageCreateInfo image_info = vpr::vk_image_create_info_base;

    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.mipLevels = mip_maps;
    image_info.samples = VkSampleCountFlagBits(sample_count);

    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (allow_readback) {
        image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    image_info.format = image_format;
    requiredFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

    if (allow_readback) {
        requiredFeatures |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }

    image_info.tiling = renderer.Device()->GetFormatTiling(image_format, requiredFeatures);

    VkImageViewCreateInfo view_info = vpr::vk_image_view_create_info_base;
    view_info.format = image_info.format;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.subresourceRange = VkImageSubresourceRange{
        VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT),
        0, mip_maps, 0, 1 
    };

    //renderTargets.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));

    if (sample_count > 1) {
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        //renderTargetsMSAA.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
        msaaUpToDate.emplace_back(VK_FALSE);
    }
    else {
        msaaUpToDate.emplace_back(VK_TRUE);
    }

    
    Viewport.width = static_cast<float>(width);
    Viewport.height = static_cast<float>(height);
    Viewport.minDepth = 0.0f;
    Viewport.maxDepth = 1.0f;
    Viewport.x = 0;
    Viewport.y = 0;
    numViews += 1;

    if (hasDepthTarget) {
        AddDepthTarget(renderer.Device()->FindDepthFormat(), allow_readback);
    }

}

void RenderTarget::CreateAsCube(uint32_t size, const VkFormat image_format, const AttachDepthTarget is_depth_target, const uint32_t mip_maps, const AllowReadback allow_readback) {
    Clear();
    hasDepthTarget = is_depth_target;

    auto& renderer = RenderingContext::Get();

    auto& rsrc_context = ResourceContext::Get();
    VkImageCreateInfo image_info = vpr::vk_image_create_info_base;
    image_info.extent.width = size;
    image_info.extent.height = size;
    image_info.mipLevels = mip_maps;
    image_info.arrayLayers = 6;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;

    image_info.usage = is_depth_target ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (allow_readback) {
        image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    image_info.format = image_format;
    requiredFeatures = is_depth_target ?
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT :
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

    if (allow_readback) {
        requiredFeatures |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }

    image_info.tiling = renderer.Device()->GetFormatTiling(image_format, requiredFeatures);

    VkImageViewCreateInfo view_info = vpr::vk_image_view_create_info_base;
    view_info.format = image_info.format;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.subresourceRange = VkImageSubresourceRange{
        is_depth_target ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT),
        0, mip_maps, 0, 6
    };
    //renderTargets.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));

    // no msaa for RTs
    msaaUpToDate.emplace_back(VK_TRUE);
    
    Viewport.width = static_cast<float>(size);
    Viewport.height = static_cast<float>(size);
    Viewport.minDepth = 0.0f;
    Viewport.maxDepth = 1.0f;
    Viewport.x = 0;
    Viewport.y = 0;
    numViews += 1;

    if (hasDepthTarget) {
        AddDepthTarget(renderer.Device()->FindDepthFormat(), allow_readback);
    }

}

void RenderTarget::AddDepthTarget(const VkFormat image_format, const AllowReadback allow_readback) {

    auto& rsrc = ResourceContext::Get();
    auto& context = RenderingContext::Get();

    if (depthTarget != nullptr) {
        rsrc.DestroyResource(depthTarget);
    }

    const VkImageCreateInfo* image_info_original = reinterpret_cast<VkImageCreateInfo*>(renderTargets.front()->Info);
    const VkImageViewCreateInfo* view_info_original = reinterpret_cast<VkImageViewCreateInfo*>(renderTargets.front()->ViewInfo);

    VkImageCreateInfo image_info = *image_info_original;
    image_info.format = image_format;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (allow_readback) {
        image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkFormatFeatureFlags req_flags = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (allow_readback) {
        req_flags |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
    image_info.tiling = context.Device()->GetFormatTiling(image_format, req_flags);

    VkImageViewCreateInfo view_info = *view_info_original;
    view_info.format = image_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    //depthTarget = rsrc.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    // DOn't need msaa for depth, we can't conventionally resolve to depth anyways
}

void RenderTarget::AddView(const VkFormat new_format) {

    if (renderTargets.empty()) {
        throw std::runtime_error("Tried to add format to empty rendertarget object!");
    }

    ++numViews;
    auto& renderer = RenderingContext::Get();
    VkImageCreateInfo image_info = *reinterpret_cast<VkImageCreateInfo*>(renderTargets.back()->Info);
    image_info.format = new_format;
    image_info.tiling = renderer.Device()->GetFormatTiling(new_format, requiredFeatures);
    VkImageViewCreateInfo view_info = *reinterpret_cast<VkImageViewCreateInfo*>(renderTargets.back()->ViewInfo);
    view_info.format = new_format;

    auto& rsrc_context = ResourceContext::Get();
    //renderTargets.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
    if (image_info.samples > 1) {
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        //renderTargetsMSAA.emplace_back(rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr));
        msaaUpToDate.emplace_back(VK_FALSE);
    }
    else {
        msaaUpToDate.emplace_back(VK_TRUE);
    }
}

void RenderTarget::Clear() {

    auto& rsrc_context = ResourceContext::Get();

    if (depthTarget != nullptr) {
        rsrc_context.DestroyResource(depthTarget);
    }

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

image_info_t RenderTarget::GetDepthInfo() const {
    if (!hasDepthTarget) {
        throw std::logic_error("Tried to retrieve depth info, when this RT has no depth attachment!");
    }
    image_info_t result;
    VkImageCreateInfo* info = reinterpret_cast<VkImageCreateInfo*>(depthTarget->Info);
    result.Format = info->format;
    result.ArrayLayers = info->arrayLayers;
    result.MipLevels = info->mipLevels;
    result.Samples = info->samples;
    result.SizeClass = image_info_t::size_class::SwapchainRelative;
    result.Usage = info->usage;
    return result;
}

bool RenderTarget::IsDepthRT() const noexcept {
    return bool(depthTarget);
}
