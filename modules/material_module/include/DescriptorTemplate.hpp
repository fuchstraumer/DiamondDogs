#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
#define DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
#include "ForwardDecl.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vulkan/vulkan.h>
#include "UpdateTemplateData.hpp"

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

    DescriptorTemplate(std::string name);
    ~DescriptorTemplate();

    VkDescriptorUpdateTemplate UpdateTemplate() const noexcept;
    VkDescriptorSetLayout SetLayout() const noexcept;
    void UpdateSet(VkDescriptorSet set);

    const UpdateTemplateData& UpdateData() const noexcept;
    void AddLayoutBinding(VkDescriptorSetLayoutBinding binding);
    void BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource* rsrc);
    void BindArrayResourcesToIdx(const size_t idx, const size_t num_descriptors, VkDescriptorType type, VulkanResource** resources);

private:

    void addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry);
    void createUpdateTemplate() const;

    mutable bool created{ false };
    const std::string name;
    const vpr::Device* device;
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    mutable VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    mutable VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };

    UpdateTemplateData updateData;
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;
    mutable bool namedDescriptorSet{ false };

};

#endif //!DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
