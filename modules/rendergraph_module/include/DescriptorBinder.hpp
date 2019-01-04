#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
#define DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
#include "UpdateTemplateData.hpp"
#include <vector>
#include <unordered_set>

struct VulkanResource;
class Descriptor;


class DescriptorBinder {
public:

    ~DescriptorBinder();

    void BindResourceToIdx(Descriptor* descr, size_t idx, VkDescriptorType type, VulkanResource* rsrc);
    void Update();
    VkDescriptorSet Handle() const noexcept;

private:
    DescriptorBinder(size_t num_descriptors);
    DescriptorBinder& addDescriptor(class Descriptor& parent, VkDescriptorSet _handle);

    friend class Descriptor;
    std::vector<Descriptor*> parentDescriptors;
    std::unordered_map<Descriptor*, VkDescriptorUpdateTemplate> templHandles;
    std::unordered_map<Descriptor*, VkDescriptorSet> setHandles;
    std::unordered_map<Descriptor*, UpdateTemplateData> updateTemplData;
    bool updated{ false };
};

#endif // !DIAMOND_DOGS_DESCRIPTOR_BINDER_HPP
