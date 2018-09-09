#include "objects/DepthTarget.hpp"
#include "RenderingContext.hpp"
#include <vulkan/vulkan.h>
#include "RenderingContext.hpp"
#include "ResourceTypes.hpp"
#include "ResourceContext.hpp"
#include "LogicalDevice.hpp"
#include "CreateInfoBase.hpp"

DepthTarget::DepthTarget() : image(nullptr), imageMSAA(nullptr) {}

DepthTarget::~DepthTarget() {
    auto& rsrc_context = ResourceContext::Get();
    rsrc_context.DestroyResource(image);
    rsrc_context.DestroyResource(imageMSAA);
}

void DepthTarget::Create(uint32_t width, uint32_t height, uint32_t sample_count_flags) {
    const VkSampleCountFlagBits sample_count = VkSampleCountFlagBits(sample_count_flags);
    VkImageCreateInfo image_info = vpr::vk_image_create_info_base;

    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;

    auto& renderer = RenderingContext::Get();

    image_info.format = renderer.Device()->FindDepthFormat();
    image_info.samples = sample_count;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.tiling = renderer.Device()->GetFormatTiling(image_info.format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    VkImageViewCreateInfo view_info = vpr::vk_image_view_create_info_base;
    view_info.format = image_info.format;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    auto& rsrc_context = ResourceContext::Get();
    image = rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);

    if (sample_count_flags > 1) {
        image_info.samples = VkSampleCountFlagBits(1);
        imageMSAA = rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
    }

}

void DepthTarget::CreateAsCube(uint32_t size, bool independent_faces) {

    msaaUpToDate = false;
    auto& renderer = RenderingContext::Get();

    VkImageCreateInfo image_info = vpr::vk_image_create_info_base;
    image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    image_info.extent.width = size;
    image_info.extent.height = size;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 6;
    image_info.format = renderer.Device()->FindDepthFormat();
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.tiling = renderer.Device()->GetFormatTiling(image_info.format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    VkImageViewCreateInfo view_info = vpr::vk_image_view_create_info_base;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view_info.subresourceRange = VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6 };
    view_info.format = image_info.format;

    auto& rsrc_context = ResourceContext::Get();
    rsrc_context.CreateImage(&image_info, &view_info, 0, nullptr, memory_type::DEVICE_LOCAL, nullptr);
}

const VulkanResource* DepthTarget::GetImage() const noexcept {
    return image;
}

const VulkanResource* DepthTarget::GetImageMSAA() const noexcept {
    return imageMSAA;
}
