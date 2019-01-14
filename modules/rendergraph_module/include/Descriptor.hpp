#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#define DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#include "ForwardDecl.hpp"
#include "DescriptorTemplate.hpp"
#include "DescriptorBinder.hpp"
#include "common/UtilityStructs.hpp"
#include <memory>
#include <atomic>
#include <stack>
#include <mutex>

struct VulkanResource;

namespace st {
    struct descriptor_type_counts_t;
}

/*
    The Descriptor is effectively a pool VkDescriptorSets, and a more effective way to manage multiple DescriptorPools. It serves DescriptorBinder
    instances to clients, which are what is used to actually use and access VkDescriptorSet handles. 

    The Descriptor is also "adaptive": the max_sets quantity in the constructor is used to construct a single descriptor set with that capacity of
    bindings (using the rsrc_counts structure to get the per-type count of bindings). If this quantity is exceeded, however, a new pool will be 
    created. At the end of the frame, when Reset() is called, this total consumed quantity will be counted and used to construct a new root 
    descriptor pool with enough room for as many descriptor sets were used. This will mean that in later frames, this allocation isn't required
    as much (if at all, once the ideal size if found).

    The pool will also shrink too, if required. It counts how many sets are actually bound over what it's current expected quantity is, and 
    if that number is far less than what it has allocated it will begin shrinking the pool. If this is too much, the adaptive expansion will
    occur again. Ultimately, it should reach a good amount.
*/
class Descriptor {
public:

    /*
        max_sets is used to set how many sets are initially allocated, but if this number is exceeded a new pool will be created
    */
    Descriptor(const vpr::Device * _device, const st::descriptor_type_counts_t& rsrc_counts, size_t max_sets, const DescriptorTemplate* templ);
    ~Descriptor();

    // frees all sets. call at the end of a frame, once all command buffers using this Descriptor have been consumed fully.
    void Reset();
    size_t TotalUsedSets() const;

    
private:
    friend class DescriptorBinder;

    VkDescriptorSet fetchNewSet() noexcept;
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
