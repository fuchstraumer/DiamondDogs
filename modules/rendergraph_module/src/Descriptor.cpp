#include "Descriptor.hpp"
#include "LogicalDevice.hpp"
#include "DescriptorPool.hpp"
#include "vkAssert.hpp"

Descriptor::Descriptor(const vpr::Device * _device, const st::descriptor_type_counts_t & rsrc_counts, size_t max_sets, DescriptorTemplate* _templ) : device{ _device }, maxSets{ uint32_t(max_sets) }, templ{ _templ },
    typeCounts{ rsrc_counts }, setLayouts(max_sets, _templ->SetLayout()) {
    createPool();
}

Descriptor::~Descriptor() {
    Reset();
}

void Descriptor::Reset() {
    std::lock_guard reset_lock(poolMutex);

    if (descriptorPools.size() == 1u) {
        // when we're allocating one large descriptor pool, we're gonna check to see if we should shrink it a bit

        size_t low_load_factor_mark = availSets.size() / 2u;
        if (setContainerIdx <= low_load_factor_mark) {
            // if we only use half our allocated sets, we're probably over-allocating so let's reduce our maxSets quantity
            maxSets /= 2u;
        }

        // don't forget to destroy our single used set
        VkResult result = vkFreeDescriptorSets(device->vkHandle(), activePool->vkHandle(), setContainerIdx, availSets.data());
        VkAssert(result);
        descriptorPools.pop();

        setContainerIdx = 0u;
        createPool();

    }
    else {
        uint32_t used_sets = setContainerIdx;
        VkResult result = vkFreeDescriptorSets(device->vkHandle(), activePool->vkHandle(), setContainerIdx, availSets.data());
        VkAssert(result);
        descriptorPools.pop();

        while (!descriptorPools.empty()) {
            auto& curr_pool = descriptorPools.top();
            auto& curr_sets = usedSets.top();
            used_sets += static_cast<uint32_t>(curr_sets.size());

            result = vkFreeDescriptorSets(device->vkHandle(), curr_pool->vkHandle(), maxSets, curr_sets.data());
            VkAssert(result);

            usedSets.pop();
            descriptorPools.pop();

        }

        setContainerIdx = 0u;
        // Update max sets, so that we only allocate one descriptor pool next time.
        maxSets = used_sets;
        createPool();
    }

}

void Descriptor::BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource* rsrc) {
    templ->BindResourceToIdx(idx, type, rsrc);
}

VkDescriptorSet Descriptor::fetchNewSet() noexcept {
    if (setContainerIdx == (availSets.size() - 1u)) {
        // expand capacity of spare sets
        std::lock_guard add_pool_guard(poolMutex);
        createPool();
        setContainerIdx = 0u;
    }
    return availSets[setContainerIdx.fetch_add(1u)];
}

void Descriptor::allocateSets() {
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
}

void Descriptor::createPool() {
    descriptorPools.emplace(std::make_unique<vpr::DescriptorPool>(device->vkHandle(), maxSets));
    activePool = descriptorPools.top().get();
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
    allocateSets();
    usedSets.emplace(std::move(availSets));
}
