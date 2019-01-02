#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
#define DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
#include <string>
#include <vulkan/vulkan.h>
#include "ForwardDecl.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct VulkanResource;

class DescriptorTemplate {
public:

    DescriptorTemplate(std::string name);

    VkDescriptorUpdateTemplate UpdateTemplate() const noexcept;
    VkDescriptorSetLayout SetLayout() const noexcept;
    void UpdateSet(VkDescriptorSet set);

    void AddLayoutBinding(size_t idx, VkDescriptorType type);
    void AddLayoutBinding(VkDescriptorSetLayoutBinding binding);
    void BindResourceToIdx(size_t idx, VulkanResource* rsrc);
    
private:

    union rawDataEntry {
        rawDataEntry() = default;
        rawDataEntry(VkDescriptorBufferInfo&& info) : BufferInfo(std::forward<VkDescriptorBufferInfo>(info)) {}
        rawDataEntry(VkDescriptorImageInfo&& info) : ImageInfo(std::forward<VkDescriptorImageInfo>(info)) {}
        rawDataEntry(VkBufferView view) : BufferView{ view } {}
        VkDescriptorBufferInfo BufferInfo;
        VkDescriptorImageInfo ImageInfo;
        VkBufferView BufferView;
    };

    void updateDescriptorBinding(const size_t idx, VulkanResource* rsrc);
    void updateBufferDescriptor(const size_t idx, VulkanResource * rsrc);
    void updateImageDescriptor(const size_t idx, VulkanResource * rsrc);
    void addDescriptorBinding(const size_t idx, VulkanResource* rsrc);
    void addBufferDescriptor(const size_t idx, VulkanResource* rsrc);
    void addSamplerDescriptor(const size_t idx, VulkanResource* rsrc);
    void addImageDescriptor(const size_t idx, VulkanResource* rsrc);
    void addCombinedImageSamplerDescriptor(const size_t idx, VulkanResource * img, VulkanResource * sampler);
    void addRawEntry(const size_t idx, rawDataEntry&& entry);
    void addUpdateEntry(const size_t idx, VkDescriptorUpdateTemplateEntry&& entry);
    void createUpdateTemplate() const;

    mutable bool created;
    const std::string name;
    const vpr::Device* device;
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    mutable VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    mutable VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };
    std::vector<rawDataEntry> rawEntries;
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;
    std::unordered_map<VulkanResource*, size_t> resourceBindings;
    std::unordered_map<size_t, VkDescriptorType> descriptorTypeMap;
    std::unordered_set<size_t> createdBindings;

};

#endif //!DIAMOND_DOGS_DESCRIPTOR_TEMPLATE_HPP
