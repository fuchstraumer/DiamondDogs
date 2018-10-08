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

    void BindResourceAtIdx(std::string rsrc_name, size_t idx, VkDescriptorType type, VulkanResource* name);

    VkDescriptorSet Handle();

private:

    friend class ShaderResourcePack;
    mutable bool dirty{ true };
    const std::string name;
    mutable std::unique_ptr<vpr::DescriptorSet> descriptorSet{ nullptr };
    std::unique_ptr<vpr::DescriptorSetLayout> descriptorSetLayout{ nullptr };
    vpr::DescriptorPool* pool{ nullptr };
    VkDescriptorUpdateTemplate updateTemplate{ VK_NULL_HANDLE };
    VkDescriptorUpdateTemplateCreateInfo templateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, nullptr };
    std::vector<VkDescriptorUpdateTemplateEntry> updateEntries;
    std::unordered_map<std::string, size_t> resourceBindings;

};

#endif // !RENDERGRAPH_DESCRIPTOR_HPP
