#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#define DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#include "ForwardDecl.hpp"
#include <memory>
#include <vulkan/vulkan.h>
#include "DescriptorTemplate.hpp"

struct VulkanResource;

namespace st {
    struct descriptor_type_counts_t;
}

struct DescriptorHandle {
    DescriptorHandle(const DescriptorHandle&) = delete;
    DescriptorHandle& operator=(const DescriptorHandle&) = delete;
    ~DescriptorHandle();
    void BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource * rsrc);
    void Update();
    VkDescriptorSet Handle() const noexcept;

private:
    DescriptorHandle(class Descriptor& parent, VkDescriptorSet&& _handle);
    friend class Descriptor;
    Descriptor& parent;
    VkDescriptorUpdateTemplate templHandle;
    VkDescriptorSet handle;
    UpdateTemplateData data;
};

class Descriptor {
public:

    Descriptor(const vpr::Device * _device, const st::descriptor_type_counts_t& rsrc_counts, size_t max_sets, const DescriptorTemplate* templ);
    ~Descriptor();

    // frees all sets
    void Reset();
    // frees sets moved to "usedSets"
    void Trim();

    DescriptorHandle GetHandle();
    
private:
    friend class DescriptorHandle;

    void allocateSets();
    void createPool(const st::descriptor_type_counts_t& rsrc_counts);

    size_t maxSets{ 0u };
    const vpr::Device* device;
    const DescriptorTemplate* templ;
    std::unique_ptr<vpr::DescriptorPool> descriptorPool;
    std::vector<VkDescriptorSet> availSets;
    std::vector<VkDescriptorSet> usedSets;

};

#endif // !DIAMOND_DOGS_DESCRIPTOR_SET_HPP
