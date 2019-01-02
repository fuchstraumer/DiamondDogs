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

class DescriptorTemplate {
public:

    DescriptorTemplate(std::string name);

    VkDescriptorUpdateTemplate UpdateTemplate() const noexcept;
    VkDescriptorSetLayout SetLayout() const noexcept;
    void UpdateSet(VkDescriptorSet set);

    void AddLayoutBinding(size_t idx, VkDescriptorType type);
    void AddLayoutBinding(VkDescriptorSetLayoutBinding binding);
    void BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource* rsrc);
    
private:

    void addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry);
    void createUpdateTemplate() const;

    mutable bool created;
    const std::string name;
    const vpr::Device* device;
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    mutable VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    mutable VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };

    UpdateTemplateData updateData;
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;

};

#endif //!DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
