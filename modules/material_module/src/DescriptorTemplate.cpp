#include "DescriptorTemplate.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSetLayout.hpp"
#include "vkAssert.hpp"
#include "VkDebugUtils.hpp"
#include <cassert>

DescriptorTemplate::DescriptorTemplate(std::string _name) : name(std::move(_name)) {
    auto& ctxt = RenderingContext::Get();
    descriptorSetLayout = std::make_unique<vpr::DescriptorSetLayout>(ctxt.Device()->vkHandle(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT);
    device = ctxt.Device();
}

DescriptorTemplate::~DescriptorTemplate() {
    vkDestroyDescriptorUpdateTemplate(device->vkHandle(), updateTemplate, nullptr);
    descriptorSetLayout.reset();
}

const UpdateTemplateData & DescriptorTemplate::UpdateData() const noexcept {
    return updateData;
}

void DescriptorTemplate::AddLayoutBinding(VkDescriptorSetLayoutBinding binding) {
    // can't add more bindings after init
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(binding);
    if (binding.descriptorCount <= 1u)
    {
        addUpdateEntry(binding.binding, VkDescriptorUpdateTemplateEntry{
            binding.binding,
            0,
            1,
            binding.descriptorType,
            sizeof(UpdateTemplateDataEntry) * binding.binding,
            0
            });
    }
    else
    {
        // we need to do some unique things to optimally setup update data for descriptor arrays
        addUpdateEntry(binding.binding, VkDescriptorUpdateTemplateEntry{
            binding.binding,
            0,
            binding.descriptorCount,
            binding.descriptorType,
            sizeof(UpdateTemplateDataEntry) * binding.binding,
            0
        });
    }
}

void DescriptorTemplate::BindResourceToIdx(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc) {
    updateData.BindResourceToIdx(idx, type, rsrc);
}

void DescriptorTemplate::BindArrayResourcesToIdx(const size_t idx, const VkDescriptorType type, const size_t num_descriptors, const VulkanResource** resources)
{
    updateData.BindArrayResourcesToIdx(idx, type, num_descriptors, resources);
}

void DescriptorTemplate::BindSingularArrayResourceToIdx(const size_t idx, const VkDescriptorType type, const size_t arrayIndex, const VulkanResource* resource)
{
    updateData.BindSingularArrayResourceToIdx(idx, type, arrayIndex, resource);
}

void DescriptorTemplate::FillArrayRangeWithResource(const size_t idx, const VkDescriptorType type, const size_t arraySize, const VulkanResource* resource)
{
    updateData.FillArrayRangeWithResource(idx, type, arraySize, resource);
}

VkDescriptorUpdateTemplate DescriptorTemplate::UpdateTemplate() const noexcept {
    if (!created) {
        createUpdateTemplate();
    }
    return updateTemplate;
}

VkDescriptorSetLayout DescriptorTemplate::SetLayout() const noexcept {
    if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
    {
        if (namedDescriptorSet)
        {
            return descriptorSetLayout->vkHandle();
        }

        const std::string set_layout_name = name + std::string("_DescriptorSetLayout");
        VkDescriptorSetLayout set_layout_handle = descriptorSetLayout->vkHandle();
        VkResult result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)set_layout_handle, VTF_DEBUG_OBJECT_NAME(set_layout_name.c_str()));
        VkAssert(result);
        namedDescriptorSet = true;
        return set_layout_handle;
    }
    else
    {
        return descriptorSetLayout->vkHandle();
    }
}

void DescriptorTemplate::UpdateSet(VkDescriptorSet set) {
    assert(updateData.Size() == updateEntries.size());
    vkUpdateDescriptorSetWithTemplate(device->vkHandle(), set, updateTemplate, updateData.Data());
}

void DescriptorTemplate::addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry) {
    if (updateEntries.empty() || idx >= updateEntries.size()) {
        updateEntries.resize(idx + 1);
    }
    updateEntries[idx] = std::move(entry);
}

void DescriptorTemplate::createUpdateTemplate() const {
    templateInfo.descriptorUpdateEntryCount = static_cast<uint32_t>(updateEntries.size());
    templateInfo.pDescriptorUpdateEntries = updateEntries.data();
    templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    templateInfo.descriptorSetLayout = descriptorSetLayout->vkHandle();
    templateInfo.pipelineBindPoint = VkPipelineBindPoint(0);
    templateInfo.pipelineLayout = VK_NULL_HANDLE;
    templateInfo.set = 0u;
    VkResult result = vkCreateDescriptorUpdateTemplate(device->vkHandle(), &templateInfo, nullptr, &updateTemplate);
    VkAssert(result);
    if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
    {
        const std::string template_object_name = name + std::string("_DescriptorTemplate");
        result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE, (uint64_t)updateTemplate, VTF_DEBUG_OBJECT_NAME(template_object_name.c_str()));
        VkAssert(result);
    }
}
