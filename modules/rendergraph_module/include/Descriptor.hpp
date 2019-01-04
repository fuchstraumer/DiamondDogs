#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#define DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#include "ForwardDecl.hpp"
#include <memory>
#include <vulkan/vulkan.h>
#include <atomic>
#include <stack>
#include <mutex>
#include "DescriptorTemplate.hpp"
#include "common/UtilityStructs.hpp"

struct VulkanResource;

namespace st {
    struct descriptor_type_counts_t;
}

class Descriptor {
public:

    Descriptor(const vpr::Device * _device, const st::descriptor_type_counts_t& rsrc_counts, size_t max_sets, const DescriptorTemplate* templ);
    ~Descriptor();

    // frees all sets
    void Reset();

    DescriptorHandle GetHandle();
    
private:
    friend class DescriptorHandle;

    VkDescriptorSet availSet() noexcept;
    void allocateSets();
    void createPool();

    size_t maxSets{ 0u };
    const vpr::Device* device{ nullptr };
    const DescriptorTemplate* templ{ nullptr };
    std::stack<std::unique_ptr<vpr::DescriptorPool>> descriptorPools;
    vpr::DescriptorPool* activePool{ nullptr };
    std::atomic<size_t> setContainerIdx;
    std::vector<VkDescriptorSet> availSets;
    std::stack<std::vector<VkDescriptorSet>> usedSets;
    st::descriptor_type_counts_t typeCounts;
    std::mutex poolMutex;
    std::vector<VkDescriptorSetLayout> setLayouts;

};

#endif // !DIAMOND_DOGS_DESCRIPTOR_SET_HPP
