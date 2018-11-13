#pragma once
#ifndef RENDERGRAPH_DESCRIPTOR_HPP
#define RENDERGRAPH_DESCRIPTOR_HPP
#include "ForwardDecl.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <string_view>

class ShaderResourcePack;
struct VulkanResource;

class Descriptor {
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
public:

    Descriptor(std::string name, vpr::DescriptorPool* _pool);
    ~Descriptor();

    void AddLayoutBinding(size_t idx, VkDescriptorType type);
    void AddLayoutBinding(const VkDescriptorSetLayoutBinding& binding);
    void BindResourceToIdx(size_t idx, VulkanResource * rsrc);
    void BindCombinedImageSampler(size_t idx, VulkanResource* img, VulkanResource* sampler);

    VkDescriptorSet Handle() const noexcept;
    VkDescriptorSetLayout SetLayout() const;

private:

    union rawDataEntry {
        VkDescriptorBufferInfo BufferInfo;
        VkDescriptorImageInfo ImageInfo;
        VkBufferView BufferView;
    };

    void update() const;

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
    void createDescriptorSet() const;

    friend class ShaderResourcePack;
    mutable bool dirty{ true };
    mutable bool created{ false };
    const std::string name;
    const vpr::Device* device;
    mutable VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    vpr::DescriptorPool* pool{ nullptr };
    mutable VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    mutable VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };
    std::vector<rawDataEntry> rawEntries;
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;
    std::unordered_map<VulkanResource*, size_t> resourceBindings;
    std::unordered_map<size_t, VkDescriptorType> descriptorTypeMap;


};

#endif // !RENDERGRAPH_DESCRIPTOR_HPP
