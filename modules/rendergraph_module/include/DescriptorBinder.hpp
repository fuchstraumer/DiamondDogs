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

    DescriptorBinder() = default;
    DescriptorBinder(const DescriptorBinder& other) noexcept;
    DescriptorBinder& operator=(const DescriptorBinder& other) noexcept;
    DescriptorBinder(DescriptorBinder&& other) noexcept = default;
    DescriptorBinder& operator=(DescriptorBinder&& other) noexcept = default;

    DescriptorBinder(size_t num_descriptors, VkPipelineLayout layout);
    ~DescriptorBinder();
    void BindResourceToIdx(const std::string& descr, size_t rsrc_idx, VkDescriptorType type, VulkanResource* rsrc);
    void Update();
    void Bind(VkCommandBuffer cmd, VkPipelineBindPoint bind_point);
    // Especially useful with copied binders: if copied binder has only a single different set, use this
    // to re-bind *just* that single set.
    void BindSingle(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const std::string& descr);

private:
    friend class Descriptor;
    friend class DescriptorPack;
    void AddDescriptor(size_t descr_idx, std::string name, Descriptor* descr);
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    std::unordered_map<std::string, size_t> descriptorIdxMap;
    std::vector<Descriptor*> parentDescriptors;
    std::vector<VkDescriptorUpdateTemplate> templHandles;
    std::vector<VkDescriptorSet> setHandles;
    std::vector<UpdateTemplateData> updateTemplData;
    std::vector<bool> dirtySets;
};

#endif // !DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
