#include "UpdateTemplateData.hpp"
#include "ResourceContext.hpp"
#include <stdexcept>

static VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags) {
    if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    else {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

UpdateTemplateData::UpdateTemplateData(const UpdateTemplateData & other) noexcept : rawEntries(other.rawEntries) {}

UpdateTemplateData::UpdateTemplateData(UpdateTemplateData && other) noexcept : rawEntries(std::move(other.rawEntries)) {}

UpdateTemplateData& UpdateTemplateData::operator=(const UpdateTemplateData & other) noexcept {
    rawEntries = other.rawEntries;
    return *this;
}

UpdateTemplateData& UpdateTemplateData::operator=(UpdateTemplateData&& other) noexcept {
    rawEntries = std::move(other.rawEntries);
    return *this;
}

void UpdateTemplateData::BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource * rsrc) {
    switch (rsrc->Type) {
    case resource_type::BUFFER:
        bindBufferDescriptor(idx, type, rsrc);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER: 
        [[fallthrough]];
    case resource_type::IMAGE:
        bindImageDescriptor(idx, type, rsrc);
        break;
    case resource_type::SAMPLER:
        bindSamplerDescriptor(idx, rsrc);
        break;
    default:
        throw std::domain_error("VulkanResource pointer had invalid type value!");
    }
}

const void* UpdateTemplateData::Data() const noexcept {
    return rawEntries.data();
}

size_t UpdateTemplateData::Size() const noexcept {
    return rawEntries.size();
}

void UpdateTemplateData::bindBufferDescriptor(const size_t idx, VkDescriptorType type, VulkanResource* rsrc) {

    /// chose to get this ahead of time, as we need it twice: then, the branch is set to check for not-true as this will be more likely
    /// than it being true (most resources aren't texel buffers). layout is weird but trying to help the compiler optimize here i hope
    const bool is_texel_buffer = (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) || (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);

    if (idx < rawEntries.size()) {
        if (!is_texel_buffer) {
            rawEntries[idx].BufferInfo.buffer = (VkBuffer)rsrc->Handle;
            rawEntries[idx].BufferInfo.range = reinterpret_cast<const VkBufferCreateInfo*>(rsrc->Info)->size;
        }
        else {
            rawEntries[idx] = std::move(UpdateTemplateDataEntry((VkBufferView)rsrc->ViewHandle));
        }
    }
    else {
        rawEntries.resize(idx + 1);
        if (!is_texel_buffer) {
            rawEntries[idx] = std::move(UpdateTemplateDataEntry{ VkDescriptorBufferInfo {
                 (VkBuffer)rsrc->Handle,
                 0u,
                 reinterpret_cast<const VkBufferCreateInfo*>(rsrc->Info)->size
            } });
        }
        else {
            rawEntries[idx] = std::move(UpdateTemplateDataEntry{ (VkBufferView)rsrc->ViewHandle });
        }
    }

}

void UpdateTemplateData::bindImageDescriptor(const size_t idx, VkDescriptorType type, VulkanResource* rsrc) {
    if (idx < rawEntries.size()) {
        auto& raw_entry = rawEntries[idx];
        if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            raw_entry.ImageInfo.imageView = (VkImageView)rsrc->ViewHandle;
            raw_entry.ImageInfo.sampler = (VkSampler)rsrc->Sampler;
        }
        else {
            raw_entry.ImageInfo.imageView = (VkImageView)rsrc->ViewHandle;
        }
    }
    else {
        rawEntries.resize(idx + 1); 
        const VkImageCreateInfo* img_create_info = reinterpret_cast<VkImageCreateInfo*>(rsrc->Info);
        if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            rawEntries[idx] = std::move(UpdateTemplateDataEntry{ VkDescriptorImageInfo{ (VkSampler)rsrc->Sampler, (VkImageView)rsrc->ViewHandle, imageLayoutFromUsage(img_create_info->usage) } });
        }
        else {
            rawEntries[idx] = std::move(UpdateTemplateDataEntry{ VkDescriptorImageInfo{ VK_NULL_HANDLE, (VkImageView)rsrc->ViewHandle, imageLayoutFromUsage(img_create_info->usage) } });
        }
    }
}

void UpdateTemplateData::bindSamplerDescriptor(const size_t idx, VulkanResource* rsrc) {
    if (idx < rawEntries.size()) {
        auto& raw_entry = rawEntries[idx];
        raw_entry.ImageInfo.sampler = (VkSampler)rsrc->Handle;
    }
    else {
        rawEntries.resize(idx + 1);
        rawEntries[idx] = std::move(UpdateTemplateDataEntry{ VkDescriptorImageInfo{ (VkSampler)rsrc->Handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED } });
    }
}
