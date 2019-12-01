#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
#define DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
#include "ForwardDecl.hpp"
#include "UpdateTemplateData.hpp"
#include <vulkan/vulkan_core.h>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

struct VulkanResource;

/*
    The DescriptorTemplate is a wrapper over a VkDescriptorUpdateTemplate and a VkDescriptorSetLayout. It
    is used to update (initialize, effectively) fresh VkDescriptorSets by the Descriptor class. This object
    is, unlike the others, thread and frame-static: meaning that copies and lifetimes don't have to be scoped
    or managed based on threads or frames. Resources bound to this will then be used by child binders used
    in actual drawcalls, potentially with only a few (or no) resources changed.
*/
class DescriptorTemplate {
public:

    DescriptorTemplate(std::string name, const bool updateAfterBind);
    ~DescriptorTemplate();

    VkDescriptorUpdateTemplate UpdateTemplate() const noexcept;
    VkDescriptorSetLayout SetLayout() const noexcept;
    void UpdateSet(VkDescriptorSet set);

    const UpdateTemplateData& UpdateData() const noexcept;
    void AddLayoutBinding(VkDescriptorSetLayoutBinding binding, const VkDescriptorBindingFlagsEXT bindingFlags);
    void BindResourceToIdx(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc);
    void BindArrayResourcesToIdx(const size_t idx, const VkDescriptorType type, const size_t num_descriptors, const VulkanResource** resources);
    void BindSingularArrayResourceToIdx(const size_t idx, const VkDescriptorType type, const size_t arrayIndex, const VulkanResource* resource);
    void FillArrayRangeWithResource(const size_t idx, const VkDescriptorType type, const size_t arraySize, const VulkanResource* resource);

private:

    void addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry);
    void createUpdateTemplate() const;

    mutable bool created{ false };
    const bool updateAfterBindEnabled{ false };
    const std::string name;
    const vpr::Device* device;
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    mutable VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    mutable VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };

    UpdateTemplateData updateData;
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;
    mutable bool namedDescriptorSet{ false };
    std::unordered_map<uint32_t, VkDescriptorBindingFlagsEXT> extFlags;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBindingFlagsCreateInfoEXT> extInfos;
};

#endif //!DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
