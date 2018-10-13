#include "Descriptor.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSetLayout.hpp"

Descriptor::Descriptor(std::string _name, vpr::DescriptorPool* _pool) : name(std::move(_name)), pool(_pool) {
    auto& ctxt = RenderingContext::Get();
    descriptorSetLayout = std::make_unique<vpr::DescriptorSetLayout>(ctxt.Device()->vkHandle());
}

void Descriptor::AddLayoutBinding(size_t idx, VkDescriptorType type) {
    descriptorSetLayout->AddDescriptorBinding(type, VK_SHADER_STAGE_ALL, idx);
    descriptorTypeMap.emplace(idx, type);
}

void Descriptor::AddLayoutBinding(const VkDescriptorSetLayoutBinding& binding) {
    descriptorSetLayout->AddDescriptorBinding(binding);
    descriptorTypeMap.emplace(binding.binding, binding.descriptorType);
}

void Descriptor::BindResourceToIdx(size_t idx, VulkanResource* rsrc) {

    if (!dirty) {
        dirty = true;
    }

    if (resourceBindings.count(rsrc) == 0) {
        addDescriptorBinding(idx, rsrc);
    }
    else {
        updateDescriptorBinding(idx, rsrc);
    }
}

void Descriptor::updateDescriptorBinding(const size_t idx, VulkanResource * rsrc) {
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
        rawEntries.emplace_back(entry);
    }
    else {
        rawEntries.emplace_back(rawDataEntry{ VkDescriptorBufferInfo {
             (VkBuffer)rsrc->Handle,
             0,
             VK_WHOLE_SIZE
        } });
    }
    updateEntries.emplace_back(VkDescriptorUpdateTemplateEntry{ 
        idx,
        0,
        1,
        type,
        sizeof(rawDataEntry) * rawEntries.size(),
        0 // size not required for single-descriptor entries
    });
}

void Descriptor::addSamplerDescriptor(const size_t idx, VulkanResource* rsrc) {
    rawEntries.emplace_back(rawDataEntry{});
    updateEntries.emplace_back(VkDescriptorUpdateTemplateEntry{
        idx,
        0,
        1,
        VK_DESCRIPTOR_TYPE_SAMPLER,
        sizeof(rawDataEntry) * rawEntries.size(),
        0
    });
}

void Descriptor::addImageDescriptor(const size_t idx, VulkanResource* rsrc) {
    VkDescriptorImageInfo image_info{
            VK_NULL_HANDLE,
            (VkImageView)rsrc->ViewHandle,
            VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkDescriptorType& type = descriptorTypeMap.at(idx);

    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        VulkanResource* sampler = reinterpret_cast<VulkanResource*>(rsrc->UserData);
        image_info.sampler = (VkSampler)sampler->Handle;
    }

    rawDataEntry entry;
    entry.ImageInfo = image_info;
    rawEntries.emplace_back(entry);

    updateEntries.emplace_back(VkDescriptorUpdateTemplateEntry{
        idx,
        0,
        1, 
        type,
        sizeof(rawDataEntry) * rawEntries.size(),
        0
    });
}

void Descriptor::createUpdateTemplate() {
    templateInfo.descriptorUpdateEntryCount = static_cast<uint32_t>(updateEntries.size());
    templateInfo.pDescriptorUpdateEntries = updateEntries.data();
    templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    templateInfo.descriptorSetLayout = descriptorSetLayout->vkHandle();
    templateInfo.pipelineBindPoint = VkPipelineBindPoint(0);
    templateInfo.pipelineLayout = VK_NULL_HANDLE;
    templateInfo.set = 0u;
    vkCreateDescriptorUpdateTemplate(RenderingContext::Get().Device()->vkHandle(), &templateInfo, nullptr, &updateTemplate);
}

