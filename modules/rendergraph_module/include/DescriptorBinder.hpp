#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
#define DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
#include "UpdateTemplateData.hpp"
#include <vector>
#include <unordered_map>

struct VulkanResource;
class Descriptor;

class DescriptorBinder {
public:

    DescriptorBinder(size_t num_descriptors, VkPipelineLayout layout);
    ~DescriptorBinder();

    void AddDescriptor(size_t descr_idx, Descriptor* descr);
    void BindResourceToIdx(Descriptor* descr, size_t rsrc_idx, VkDescriptorType type, VulkanResource* rsrc);
    void Update();
    void Bind(VkCommandBuffer cmd, VkPipelineBindPoint bind_point);

private:
    friend class Descriptor;
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    std::unordered_map<Descriptor*, size_t> descriptorIdxMap;
    std::vector<Descriptor*> parentDescriptors;
    std::vector<VkDescriptorUpdateTemplate> templHandles;
    std::vector<VkDescriptorSet> setHandles;
    std::vector<UpdateTemplateData> updateTemplData;
    std::vector<bool> dirtySets;
};

#endif // !DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
