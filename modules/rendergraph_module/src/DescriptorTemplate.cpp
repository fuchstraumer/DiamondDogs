#include "DescriptorTemplate.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSetLayout.hpp"
#include "vkAssert.hpp"
#include <cassert>

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

DescriptorTemplate::DescriptorTemplate(std::string _name) : name(std::move(_name)) {
    auto& ctxt = RenderingContext::Get();
    descriptorSetLayout = std::make_unique<vpr::DescriptorSetLayout>(ctxt.Device()->vkHandle());
    device = ctxt.Device();
}

DescriptorTemplate::~DescriptorTemplate() {
    vkDestroyDescriptorUpdateTemplate(device->vkHandle(), updateTemplate, nullptr);
    throw std::runtime_error("re-integrate destruction of everything else you dolt");
}

void DescriptorTemplate::AddLayoutBinding(size_t idx, VkDescriptorType type) {
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(type, VK_SHADER_STAGE_ALL, uint32_t(idx));
    descriptorTypeMap.emplace(std::move(idx), std::move(type));
}

void DescriptorTemplate::AddLayoutBinding(VkDescriptorSetLayoutBinding binding) {
    // can't add more bindings after init
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(binding);
    descriptorTypeMap.emplace(binding.binding, binding.descriptorType);
}

void DescriptorTemplate::BindResourceToIdx(size_t idx, VulkanResource* rsrc) {

    if (!dirty) {
        dirty = true;
    }

    // no resource bound at that index yet
    if (createdBindings.count(idx) == 0) {
        // make sure we do have the ability to bind here, though (entry is zero if not added to layout binding)
        addDescriptorBinding(idx, rsrc);
    }
    else {
        updateDescriptorBinding(idx, rsrc);
    }

}

VkDescriptorUpdateTemplate DescriptorTemplate::UpdateTemplate() const noexcept {
    if (!created) {
        createUpdateTemplate();
    }
    return updateTemplate;
}

VkDescriptorSetLayout DescriptorTemplate::SetLayout() const {
    return descriptorSetLayout->vkHandle();
}

void DescriptorTemplate::UpdateSet(VkDescriptorSet set) {
    vkUpdateDescriptorSetWithTemplate(device->vkHandle(), set, updateTemplate, rawEntries.data());
}

void DescriptorTemplate::updateDescriptorBinding(const size_t idx, VulkanResource * rsrc) {

    switch (rsrc->Type) {
    case resource_type::BUFFER:
        updateBufferDescriptor(idx, rsrc);
        break;
    case resource_type::IMAGE:
        updateImageDescriptor(idx, rsrc);
        break;
    case resource_type::SAMPLER:
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        updateImageDescriptor(idx, rsrc);
        break;
    default:
        throw std::domain_error("VulkanResource pointer had invalid type value!");
    }

}

void DescriptorTemplate::updateBufferDescriptor(const size_t idx, VulkanResource* rsrc) {

    auto& raw_entry = rawEntries[idx];
    if (rsrc->ViewHandle != VK_NULL_HANDLE) {
        raw_entry = rawDataEntry((VkBufferView)rsrc->ViewHandle);
    }
    else {
        raw_entry.BufferInfo.buffer = (VkBuffer)rsrc->Handle;
        raw_entry.BufferInfo.range = reinterpret_cast<const VkBufferCreateInfo*>(rsrc->Info)->size;
    }

}

void DescriptorTemplate::updateImageDescriptor(const size_t idx, VulkanResource* rsrc) {

    auto& raw_entry = rawEntries[idx];
    raw_entry.ImageInfo.imageView = (VkImageView)rsrc->ViewHandle;

}

void DescriptorTemplate::addDescriptorBinding(const size_t idx, VulkanResource* rsrc) {

    switch (rsrc->Type) {
    case resource_type::BUFFER:
        addBufferDescriptor(idx, rsrc);
        break;
    case resource_type::SAMPLER:
        addSamplerDescriptor(idx, rsrc);
        break;
    case resource_type::IMAGE:
        addImageDescriptor(idx, rsrc);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        addCombinedImageSamplerDescriptor(idx, rsrc, rsrc->Sampler);
    default:
        throw std::domain_error("Invalid resource type when trying to add descriptor binding to DescriptorTemplate.");
    };

    createdBindings.emplace(idx);
}

void DescriptorTemplate::addBufferDescriptor(const size_t idx, VulkanResource* rsrc) {
    const VkDescriptorType& type = descriptorTypeMap.at(idx);

    if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
        addRawEntry(idx, rawDataEntry((VkBufferView)rsrc->ViewHandle));
    }
    else {
        const uint32_t range = reinterpret_cast<const VkBufferCreateInfo*>(rsrc->Info)->size;
        /*
        constexpr uint32_t MAX_UNIFORM_BUFFER_RANGE_MIN = 16384u;
        constexpr uint32_t MAX_STORAGE_BUFFER_RANGE_MIN = 134217728u;
        uint32_t range = (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ? MAX_UNIFORM_BUFFER_RANGE_MIN : MAX_STORAGE_BUFFER_RANGE_MIN;
        */
        addRawEntry(idx, rawDataEntry{ VkDescriptorBufferInfo {
             (VkBuffer)rsrc->Handle,
             0,
             range
        } });
    }

    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        uint32_t(idx),
        0,
        1,
        type,
        sizeof(rawDataEntry) * idx,
        0 // size not required for single-descriptor entries
        });

}

void DescriptorTemplate::addSamplerDescriptor(const size_t idx, VulkanResource* rsrc) {
    addRawEntry(idx, rawDataEntry{});
    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        uint32_t(idx),
        0,
        1,
        VK_DESCRIPTOR_TYPE_SAMPLER,
        sizeof(rawDataEntry) * idx,
        0
        });
}

void DescriptorTemplate::addImageDescriptor(const size_t idx, VulkanResource* rsrc) {
    const VkImageCreateInfo* img_create_info = reinterpret_cast<VkImageCreateInfo*>(rsrc->Info);

    addRawEntry(idx, rawDataEntry{
        VkDescriptorImageInfo{
            VK_NULL_HANDLE,
            (VkImageView)rsrc->ViewHandle,
            imageLayoutFromUsage(img_create_info->usage)
        }
        });

    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        uint32_t(idx),
        0,
        1,
        descriptorTypeMap.at(idx),
        sizeof(rawDataEntry) * idx,
        0
        });

}

void DescriptorTemplate::addCombinedImageSamplerDescriptor(const size_t idx, VulkanResource* img, VulkanResource* sampler) {
    const VkImageCreateInfo* img_create_info = reinterpret_cast<VkImageCreateInfo*>(img->Info);

    addRawEntry(idx, rawDataEntry{ VkDescriptorImageInfo{
        (VkSampler)sampler->Handle,
        (VkImageView)img->ViewHandle,
        imageLayoutFromUsage(img_create_info->usage)
    } });

    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        uint32_t(idx),
        0,
        1,
        descriptorTypeMap.at(idx),
        sizeof(rawDataEntry) * idx,
        0
        });

    createdBindings.emplace(idx);
}

void DescriptorTemplate::addRawEntry(const size_t idx, rawDataEntry&& entry) {
    if (rawEntries.empty() || idx >= rawEntries.size()) {
        rawEntries.resize(idx + 1);
    }
    rawEntries[idx] = std::move(entry);
}

void DescriptorTemplate::addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry) {
    if (updateEntries.empty() || idx >= updateEntries.size()) {
        updateEntries.resize(idx + 1);
    }
    updateEntries[idx] = std::move(entry);
}

void DescriptorTemplate::createUpdateTemplate() const {
    templateInfo.descriptorUpdateEntryCount = static_cast<uint32_t>(updateEntries.size());
    assert(updateEntries.size() == rawEntries.size());
    templateInfo.pDescriptorUpdateEntries = updateEntries.data();
    templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    templateInfo.descriptorSetLayout = descriptorSetLayout->vkHandle();
    templateInfo.pipelineBindPoint = VkPipelineBindPoint(0);
    templateInfo.pipelineLayout = VK_NULL_HANDLE;
    templateInfo.set = 0u;
    vkCreateDescriptorUpdateTemplate(RenderingContext::Get().Device()->vkHandle(), &templateInfo, nullptr, &updateTemplate);
}
