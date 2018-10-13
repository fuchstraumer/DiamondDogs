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

    VkDescriptorSet Handle();

private:

    union rawDataEntry {
        VkDescriptorBufferInfo BufferInfo;
        VkDescriptorImageInfo ImageInfo;
        VkBufferView BufferView;
    };

    void updateDescriptorBinding(const size_t idx, VulkanResource* rsrc);
    void addDescriptorBinding(const size_t idx, VulkanResource* rsrc);
    void addBufferDescriptor(const size_t idx, VulkanResource* rsrc);
    void addSamplerDescriptor(const size_t idx, VulkanResource* rsrc);
    void addImageDescriptor(const size_t idx, VulkanResource* rsrc);
    void createUpdateTemplate();

    friend class ShaderResourcePack;
    mutable bool dirty{ true };
    const std::string name;
    mutable VkDescriptorSet descriptorSet;
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    vpr::DescriptorPool* pool{ nullptr };
    VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };
    std::vector<rawDataEntry> rawEntries;
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;
    std::unordered_map<VulkanResource*, size_t> resourceBindings;
    std::unordered_map<size_t, VkDescriptorType> descriptorTypeMap;


};

#endif // !RENDERGRAPH_DESCRIPTOR_HPP
