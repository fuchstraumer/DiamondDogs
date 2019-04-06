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
    descriptorSetLayout = std::make_unique<vpr::DescriptorSetLayout>(ctxt.Device()->vkHandle());
    device = ctxt.Device();
}

DescriptorTemplate::~DescriptorTemplate() {
    vkDestroyDescriptorUpdateTemplate(device->vkHandle(), updateTemplate, nullptr);
    descriptorSetLayout.reset();
}

const UpdateTemplateData & DescriptorTemplate::UpdateData() const noexcept {
    return updateData;
}

void DescriptorTemplate::AddLayoutBinding(size_t idx, VkDescriptorType type) {
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(type, VK_SHADER_STAGE_ALL, uint32_t(idx)); 
    addUpdateEntry(uint32_t(idx), VkDescriptorUpdateTemplateEntry{
        uint32_t(idx),
        0,
        1,
        type,
        sizeof(UpdateTemplateDataEntry) * idx,
        0
    });
}

void DescriptorTemplate::AddLayoutBinding(VkDescriptorSetLayoutBinding binding) {
    // can't add more bindings after init
    assert(!created);
    descriptorSetLayout->AddDescriptorBinding(binding);
    addUpdateEntry(binding.binding, VkDescriptorUpdateTemplateEntry{
        binding.binding,
        0,
        1,
        binding.descriptorType,
        sizeof(UpdateTemplateDataEntry) * binding.binding,
        0
    });
}

void DescriptorTemplate::BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource* rsrc) {
    updateData.BindResourceToIdx(idx, type, rsrc);
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
		auto SetObjectNameFn = device->DebugUtilsHandler().vkSetDebugUtilsObjectName;
		const VkDevice device_handle = device->vkHandle();
		const std::string set_layout_name = name + std::string("_DescriptorSetLayout");
		VkDescriptorSetLayout set_layout_handle = descriptorSetLayout->vkHandle();
		const VkDebugUtilsObjectNameInfoEXT set_layout_name_info{
			VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			nullptr,
			VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
			(uint64_t)set_layout_handle,
			set_layout_name.c_str()
		};
		SetObjectNameFn(device_handle, &set_layout_name_info);
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
    vkCreateDescriptorUpdateTemplate(RenderingContext::Get().Device()->vkHandle(), &templateInfo, nullptr, &updateTemplate);
	if constexpr (VTF_VALIDATION_ENABLED && VTF_USE_DEBUG_INFO)
	{
		auto SetObjectNameFn = device->DebugUtilsHandler().vkSetDebugUtilsObjectName;
		const VkDevice device_handle = device->vkHandle();
		const std::string template_object_name = name + std::string("_DescriptorTemplate");
		const VkDebugUtilsObjectNameInfoEXT template_name_info{
			VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			nullptr,
			VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE,
			(uint64_t)updateTemplate,
			template_object_name.c_str()
		};
		SetObjectNameFn(device_handle, &template_name_info);
	}
}
