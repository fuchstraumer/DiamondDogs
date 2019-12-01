#include "Descriptor.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorPool.hpp"
#include "vkAssert.hpp"
#include "RenderingContext.hpp"
#include "VkDebugUtils.hpp"
#include <cassert>

static std::atomic<size_t> gSetsAllocated{ 0u };
static std::atomic<size_t> gSetsDestroyed{ 0u };
static std::atomic<int64_t> gSetsAlive{ 0u };

Descriptor::Descriptor(const vpr::Device* _device, const st::descriptor_type_counts_t& rsrc_counts, size_t max_sets, DescriptorTemplate* _templ,
    std::unordered_map<std::string, size_t> binding_locs, const char* _name) : device{ _device }, maxSets{ uint32_t(max_sets) }, templ{ _templ }, setLayouts(max_sets, _templ->SetLayout()),
    bindingLocations{ binding_locs }, typeCounts{ rsrc_counts }, name{ _name }
{
    createPool();
}

Descriptor::Descriptor(const vpr::Device * _device, const st::descriptor_type_counts_t & rsrc_counts, size_t max_sets, DescriptorTemplate * _templ, std::unordered_map<std::string, size_t>&& binding_locations) : device{ _device }, maxSets{ uint32_t(max_sets) },
    templ{ _templ }, setLayouts(max_sets, _templ->SetLayout()), bindingLocations{ std::move(binding_locations) }, typeCounts{ rsrc_counts }
{
    createPool();
}

Descriptor::~Descriptor()
{
    Reset();
}

void Descriptor::Reset()
{
    std::lock_guard reset_lock(poolMutex);

    if (descriptorPools.size() == 1u && maxSets != 0u)
    {
        // when we're allocating one large descriptor pool, we're gonna check to see if we should shrink it a bit

        size_t low_load_factor_mark = availSets.size() / 2u;
        if (setContainerIdx <= low_load_factor_mark) {
            // if we only use half our allocated sets, we're probably over-allocating so let's reduce our maxSets quantity
            maxSets /= 2u;
            VkDescriptorSetLayout set_layout_handle = setLayouts.front();
            setLayouts.resize(maxSets, set_layout_handle);
        }

        // don't forget to destroy our single used set
        VkResult result = vkFreeDescriptorSets(device->vkHandle(), activePool->vkHandle(), static_cast<uint32_t>(availSets.size()), availSets.data());
        VkAssert(result);
        gSetsDestroyed.fetch_add(availSets.size());
        gSetsAlive = gSetsAlive - availSets.size();

        availSets.clear();
        descriptorPools.pop_back();

        assert(descriptorPools.empty());

        setContainerIdx = 0u;
        createPool();

        assert(usedSets.empty());

    }
    else if (availSets.size() == 0u && descriptorPools.size() == 0u)
    {
        // sets potentially not created this frame, but pool is being left "hot"
        return;
    }
    else
    {
        uint32_t used_sets = setContainerIdx;
        VkResult result = vkFreeDescriptorSets(device->vkHandle(), activePool->vkHandle(), static_cast<uint32_t>(availSets.size()), availSets.data());
        size_t sets_destroyed = availSets.size();
        VkAssert(result);
        descriptorPools.pop_back();
        availSets.clear();

        while (!descriptorPools.empty()) {
            auto& curr_pool = descriptorPools.back();
            auto& curr_sets = usedSets.back();
            used_sets += static_cast<uint32_t>(curr_sets.size());

            result = vkFreeDescriptorSets(device->vkHandle(), curr_pool->vkHandle(), static_cast<uint32_t>(curr_sets.size()), curr_sets.data());
            VkAssert(result);
            sets_destroyed += curr_sets.size();

            usedSets.pop_back();
            descriptorPools.pop_back();

        }

        assert(usedSets.empty());

        setContainerIdx = 0u;
        // Update max sets, so that we only allocate one descriptor pool next time.
        maxSets = used_sets;
        VkDescriptorSetLayout set_layout_handle = setLayouts.front();
        setLayouts.resize(maxSets, set_layout_handle);
        gSetsDestroyed.fetch_add(sets_destroyed);
        gSetsAlive = gSetsAlive - sets_destroyed;
        createPool();
    }

}

size_t Descriptor::TotalUsedSets() const
{
    return usedSets.size() + 1;
}

void Descriptor::BindResource(const char* _name, const VkDescriptorType type, const VulkanResource* rsrc)
{
    const size_t idx = bindingLocations.at(_name);
    templ->BindResourceToIdx(idx, type, rsrc);
}

void Descriptor::BindResourceToIdx(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc)
{
    templ->BindResourceToIdx(idx, type, rsrc);
}

void Descriptor::BindArrayResources(const size_t idx, const VkDescriptorType type, const size_t numResources, const VulkanResource** resources)
{
    templ->BindArrayResourcesToIdx(idx, type, numResources, resources);
}

void Descriptor::BindSingularArrayResourceToIdx(const size_t idx, const VkDescriptorType type, const size_t arrayIndex, const VulkanResource* resource)
{
    templ->BindSingularArrayResourceToIdx(idx, type, arrayIndex, resource);
}

void Descriptor::FillArrayRangeWithResource(const size_t idx, const VkDescriptorType type, const size_t arraySize, const VulkanResource* resource)
{
    templ->FillArrayRangeWithResource(idx, type, arraySize, resource);
}

size_t Descriptor::BindingLocation(const char * rsrc_name) const
{
    return bindingLocations.at(rsrc_name);
}

VkDescriptorSet Descriptor::fetchNewSet() noexcept
{
    // for single-set containers we need to just create a new pack now
    if ((setContainerIdx == (availSets.size() - 1u)) || (maxSets == 1u))
    {
        // expand capacity of spare sets
        std::lock_guard add_pool_guard(poolMutex);
        createPool();
        setContainerIdx = 0u;
    }
    return availSets[setContainerIdx.fetch_add(1u)];
}

void Descriptor::allocateSets()
{

    const VkDescriptorSetAllocateInfo alloc_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        activePool->vkHandle(),
        maxSets,
        setLayouts.data()
    };

    availSets.resize(maxSets, VK_NULL_HANDLE);
    VkResult result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, availSets.data());
    VkAssert(result);
    gSetsAllocated.fetch_add(availSets.size());
    gSetsAlive.fetch_add(availSets.size());

    if constexpr (VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
    {
        const std::string base_name = name + std::string("_Pool") + std::to_string(descriptorPools.size()) + std::string("_DescriptorSet");
        for (size_t i = 0; i < availSets.size(); ++i)
        {
            std::string curr_name = base_name + std::to_string(i);
            result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)availSets[i], VTF_DEBUG_OBJECT_NAME(curr_name.c_str()));
            VkAssert(result);
        }
    }

}

void Descriptor::createPool()
{
    if (!availSets.empty())
    {
        usedSets.emplace_back(std::move(availSets));
    }
    else if (maxSets == 0u)
    {
        maxSets = 1u;
        setLayouts.resize(1u, templ->SetLayout());
        return;
    }

    descriptorPools.emplace_back(std::make_unique<vpr::DescriptorPool>(device->vkHandle(), maxSets));
    activePool = descriptorPools.back().get();
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, typeCounts.UniformBuffers * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, typeCounts.UniformBuffersDynamic * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, typeCounts.StorageBuffers * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, typeCounts.StorageBuffersDynamic * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, typeCounts.StorageTexelBuffers * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, typeCounts.UniformTexelBuffers * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, typeCounts.StorageImages * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLER, typeCounts.Samplers * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, typeCounts.SampledImages * maxSets);
    activePool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, typeCounts.CombinedImageSamplers * maxSets);
    activePool->Create();
    if constexpr(VTF_USE_DEBUG_INFO && VTF_VALIDATION_ENABLED)
    {
        const std::string curr_name = name + std::string("_DescriptorPool_Num") + std::to_string(descriptorPools.size() - 1u);
        VkResult result = RenderingContext::SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, (uint64_t)descriptorPools.back()->vkHandle(), VTF_DEBUG_OBJECT_NAME(curr_name.c_str()));
        VkAssert(result);
    }
    allocateSets();
}

size_t Descriptor::highWaterMark() const noexcept
{
    // no memory order here because we are not doing this across threads
    size_t used_sets = static_cast<size_t>(setContainerIdx.load(std::memory_order_relaxed));
    for (const auto& set_vec : usedSets)
    {
        used_sets += set_vec.size();
    }
    return used_sets;
}
