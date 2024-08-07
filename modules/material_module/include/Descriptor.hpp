#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#define DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#include "ForwardDecl.hpp"
#include "DescriptorTemplate.hpp"
#include "DescriptorBinder.hpp"
#include "common/UtilityStructs.hpp"
#include "utility/tagged_bool.hpp"
#include <memory>
#include <atomic>
#include <vector>
#include <mutex>
#include <unordered_map>

struct VulkanResource;
class DescriptorPack;

struct DescriptorCreateInfo
{
    const vpr::Device* device{ nullptr };
    // Used to allocate slots for descriptors, so it must be accurate
    const st::descriptor_type_counts_t* rsrcCounts{ nullptr };
    // Sets initial count of descriptor sets, but will adjust dynamically as needed
    size_t maxSets;
    DescriptorTemplate* templ{ nullptr };
    const char* name{ nullptr };
    // If true, descriptor count will actually just be 1 and we won't dynamically spawn descriptor
    // sets as new resources are bound (since updating->use doesn't invalidate existing ones)
    bool updateAfterBind{ false };
};

/*
    The Descriptor is effectively a pool of VkDescriptorSets, and a more effective way to manage multiple DescriptorPools. It serves DescriptorBinder
    instances to clients, which are what is used to actually use and access VkDescriptorSet handles.

    The Descriptor is also "adaptive": the max_sets quantity in the constructor is used to construct a single descriptor set with that capacity of
    bindings (using the rsrc_counts structure to get the per-type count of bindings). If this quantity is exceeded, however, a new pool will be
    created. At the end of the frame, when Reset() is called, this total consumed quantity will be counted and used to construct a new root
    descriptor pool with enough room for as many descriptor sets were used. This will mean that in later frames, this allocation isn't required
    as much (if at all, once the ideal size if found).

    The pool will also shrink too, if required. It counts how many sets are actually bound over what it's current expected quantity is, and
    if that number is far less than what it has allocated it will begin shrinking the pool. If this is too much, the adaptive expansion will
    occur again. Ultimately, it should reach a good amount.

    Descriptors can also have their internal resources updated, which allows them to serve better as a true "template" and spawner for
    further VkDescriptorSets (i.e, we can update resources we expect to never change and leave frequently updated things to clients).
*/
class Descriptor {
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
public:

    Descriptor(const DescriptorCreateInfo& createInfo, std::unordered_map<std::string, size_t> bindingLocations);
    ~Descriptor();

    // frees all sets. call at the end of a frame, once all command buffers using this Descriptor have been consumed fully.
    void Reset();
    size_t TotalUsedSets() const;
    void BindResource(const char* name, const VkDescriptorType type, const VulkanResource* rsrc);
    void BindResourceToIdx(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc);
    // Bind multiple varying array resources at given index from resources** array
    void BindArrayResources(const size_t idx, const VkDescriptorType type, const size_t numResources, const VulkanResource** resources);
    // Update a singular resource in a descriptor array
    void BindSingularArrayResourceToIdx(const size_t idx, const VkDescriptorType type, const size_t arrayIndex, const VulkanResource* resource);
    // 
    void FillArrayRangeWithResource(const size_t idx, const VkDescriptorType type, const size_t arraySize, const VulkanResource* resource);
    size_t BindingLocation(const char* rsrc_name) const;
    bool AllowsUpdatingAfterBind() const noexcept;

private:
    friend class DescriptorBinder;
    friend class DescriptorPack;

    VkDescriptorSet fetchNewSet() noexcept;
    void allocateSets();
    void createPool();

    // Used when constructing frame-buffered sibling
    size_t highWaterMark() const noexcept;

    uint32_t maxSets{ 0u };
    const bool updateAfterBind{ false };
    const vpr::Device* device{ nullptr };
    DescriptorTemplate* templ{ nullptr };
    st::descriptor_type_counts_t typeCounts;
    std::vector<std::unique_ptr<vpr::DescriptorPool>> descriptorPools;
    vpr::DescriptorPool* activePool{ nullptr };
    std::atomic<uint32_t> setContainerIdx{ 0u };
    std::vector<VkDescriptorSet> availSets;
    std::vector<std::vector<VkDescriptorSet>> usedSets;
    std::mutex poolMutex;
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::unordered_map<std::string, size_t> bindingLocations;
    std::string name; // unused / left empty in optimized release builds
};

#endif // !DIAMOND_DOGS_DESCRIPTOR_SET_HPP
