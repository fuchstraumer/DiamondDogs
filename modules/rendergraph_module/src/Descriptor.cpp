#include "Descriptor.hpp"
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
    else if (usage_flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

Descriptor::Descriptor(std::string _name, vpr::DescriptorPool* _pool) : name(std::move(_name)), pool(_pool) {
    auto& ctxt = RenderingContext::Get();
    descriptorSetLayout = std::make_unique<vpr::DescriptorSetLayout>(ctxt.Device()->vkHandle());
}

Descriptor::~Descriptor() {
    vkDestroyDescriptorUpdateTemplate(device->vkHandle(), updateTemplate, nullptr);
    VkResult result = vkFreeDescriptorSets(device->vkHandle(), pool->vkHandle(), 1, &descriptorSet);
    VkAssert(result);
}

void Descriptor::AddLayoutBinding(size_t idx, VkDescriptorType type) {
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(type, VK_SHADER_STAGE_ALL, idx);
    descriptorTypeMap.emplace(idx, type);
}

void Descriptor::AddLayoutBinding(const VkDescriptorSetLayoutBinding& binding) {
    // can't add more bindings after init
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(binding);
    descriptorTypeMap.emplace(binding.binding, binding.descriptorType);
}

void Descriptor::BindResourceToIdx(size_t idx, VulkanResource* rsrc) {

    if (!dirty) {
        dirty = true;
    }

    // no resource bound at that index yet
    if (descriptorTypeMap.count(idx) == 0) {
        // make sure we do have the ability to bind here, though (entry is zero if not added to layout binding)
        addDescriptorBinding(idx, rsrc);
    }
    else {
        assert(idx < resourceBindings.size());
        updateDescriptorBinding(idx, rsrc);
    }
   
}

void Descriptor::BindCombinedImageSampler(size_t idx, VulkanResource * img, VulkanResource * sampler) {
    if (!dirty) {
        dirty = true;
    }

    assert(descriptorTypeMap.at(idx) == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    if (descriptorTypeMap.count(idx) == 0) {
        addCombinedImageSamplerDescriptor(idx, img, sampler);
    }
    else {
        auto& raw_entry = rawEntries[idx];
        raw_entry.ImageInfo.imageView = (VkImageView)img->ViewHandle;
        raw_entry.ImageInfo.sampler = (VkSampler)sampler->Handle;
    }
}

VkDescriptorSet Descriptor::Handle() const noexcept {
    update();
    return descriptorSet;
}

VkDescriptorSetLayout Descriptor::SetLayout() const {
    return descriptorSetLayout->vkHandle();
}

void Descriptor::update() const {

    if (!created) {
        createDescriptorSet();
        createUpdateTemplate();
        created = true;
    }

    if (dirty) {
        vkUpdateDescriptorSetWithTemplate(device->vkHandle(), descriptorSet, updateTemplate, rawEntries.data());
        dirty = false;
    }
}

void Descriptor::updateDescriptorBinding(const size_t idx, VulkanResource * rsrc) {

    switch (rsrc->Type) {
    case resource_type::BUFFER:
        updateBufferDescriptor(idx, rsrc);
        break;
    case resource_type::IMAGE:
        updateImageDescriptor(idx, rsrc);
        break;
    case resource_type::SAMPLER:
        break;
    default:
        throw std::domain_error("VulkanResource pointer had invalid type value!");
    }

}

void Descriptor::updateBufferDescriptor(const size_t idx, VulkanResource* rsrc) {

    auto& raw_entry = rawEntries[idx];
    if (rsrc->ViewHandle != VK_NULL_HANDLE) {
        raw_entry.BufferView = (VkBufferView)rsrc->ViewHandle;
    }
    else {
        raw_entry.BufferInfo.buffer = (VkBuffer)rsrc->Handle;
    }

}

void Descriptor::updateImageDescriptor(const size_t idx, VulkanResource* rsrc) {

    auto& raw_entry = rawEntries[idx];
    raw_entry.ImageInfo.imageView = (VkImageView)rsrc->ViewHandle;

}

void Descriptor::addDescriptorBinding(const size_t idx, VulkanResource* rsrc) {


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
    default:
        throw std::domain_error("Invalid resource type when trying to add descriptor binding to Descriptor.");
    };

}

void Descriptor::addBufferDescriptor(const size_t idx, VulkanResource* rsrc) {
    const VkDescriptorType& type = descriptorTypeMap.at(idx);

    if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
        rawDataEntry entry;
        entry.BufferView = (VkBufferView)rsrc->ViewHandle;
        addRawEntry(idx, std::move(entry));
        rawEntries.emplace_back(entry);
    }
    else {
        addRawEntry(idx, rawDataEntry{ VkDescriptorBufferInfo {
             (VkBuffer)rsrc->Handle,
             0,
             VK_WHOLE_SIZE
        } });
    }

    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{ 
        idx,
        0,
        1,
        type,
        sizeof(rawDataEntry) * rawEntries.size(),
        0 // size not required for single-descriptor entries
    });

}

void Descriptor::addSamplerDescriptor(const size_t idx, VulkanResource* rsrc) {
    addRawEntry(idx, rawDataEntry{});
    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        idx,
        0,
        1,
        VK_DESCRIPTOR_TYPE_SAMPLER,
        sizeof(rawDataEntry) * rawEntries.size(),
        0
    });
}

void Descriptor::addImageDescriptor(const size_t idx, VulkanResource* rsrc) {
    const VkImageCreateInfo* img_create_info = reinterpret_cast<VkImageCreateInfo*>(rsrc->Info);

    rawDataEntry entry;
    entry.ImageInfo =
    VkDescriptorImageInfo{
        VK_NULL_HANDLE,
        (VkImageView)rsrc->ViewHandle,
        imageLayoutFromUsage(img_create_info->usage)
    };
    addRawEntry(idx, std::move(entry));

    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        idx,
        0,
        1, 
        descriptorTypeMap.at(idx),
        sizeof(rawDataEntry) * rawEntries.size(),
        0
    });

}

void Descriptor::addCombinedImageSamplerDescriptor(const size_t idx, VulkanResource* img, VulkanResource* sampler) {
    const VkImageCreateInfo* img_create_info = reinterpret_cast<VkImageCreateInfo*>(img->Info);

    rawDataEntry entry;
    entry.ImageInfo = 
    VkDescriptorImageInfo{
        (VkSampler)sampler->Handle,
        (VkImageView)img->ViewHandle,
        imageLayoutFromUsage(img_create_info->usage)
    };
    addRawEntry(idx, std::move(entry));

    addUpdateEntry(idx, VkDescriptorUpdateTemplateEntry{
        idx,
        0,
        1,
        descriptorTypeMap.at(idx),
        sizeof(rawDataEntry) * rawEntries.size(),
        0
    });

}

void Descriptor::addRawEntry(const size_t idx, rawDataEntry&& entry) {
    if (idx > rawEntries.size() - 1) {
        rawEntries.resize(idx + 1);
    }
    rawEntries[idx] = std::forward<rawDataEntry>(entry);
}

void Descriptor::addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry) {
    if (idx > updateEntries.size() - 1) {
        updateEntries.resize(idx + 1);
    }
    updateEntries[idx] = std::forward<VkDescriptorUpdateTemplateEntry>(entry);
}

void Descriptor::createUpdateTemplate() const {
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

void Descriptor::createDescriptorSet() const {
    VkDescriptorSetLayout layout = descriptorSetLayout->vkHandle();
    VkDescriptorSetAllocateInfo alloc_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        pool->vkHandle(),
        1,
        &layout
    };
    VkResult result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, &descriptorSet);
    VkAssert(result);
}

