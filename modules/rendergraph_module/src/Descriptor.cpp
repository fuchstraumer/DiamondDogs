#include "Descriptor.hpp"
#include "LogicalDevice.hpp"
#include "vpr/DescriptorPool.hpp"
#include "common/UtilityStructs.hpp"
#include "vkAssert.hpp"

DescriptorHandle::DescriptorHandle(Descriptor& _parent, VkDescriptorSet&& _handle) : parent(_parent), templHandle(parent.templ->UpdateTemplate()), handle(std::move(_handle)),
    data(parent.templ->UpdateData()) {} // starts out with initial data of parent

DescriptorHandle::~DescriptorHandle() {
    parent.usedSets.emplace_back(handle);
}

void DescriptorHandle::BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource * rsrc) {
    data.BindResourceToIdx(idx, type, rsrc);
}

void DescriptorHandle::Update() {
    vkUpdateDescriptorSetWithTemplate(parent.device->vkHandle(), handle, templHandle, data.Data());
}

VkDescriptorSet DescriptorHandle::Handle() const noexcept {
    return handle;
}

Descriptor::Descriptor(const vpr::Device * _device, const st::descriptor_type_counts_t & rsrc_counts, size_t max_sets, const DescriptorTemplate* _templ) : device(_device), maxSets{ max_sets }, templ{ _templ } {
    createPool(rsrc_counts);
}

Descriptor::~Descriptor() {
    Reset();
}

void Descriptor::Reset() {
    Trim();
    if (!availSets.empty()) {
        vkFreeDescriptorSets(device->vkHandle(), descriptorPool->vkHandle(), static_cast<uint32_t>(availSets.size()), availSets.data());
        availSets.clear();
    }
}

void Descriptor::Trim() {
    if (usedSets.empty()) {
        return;
    }
    vkFreeDescriptorSets(device->vkHandle(), descriptorPool->vkHandle(), static_cast<uint32_t>(usedSets.size()), usedSets.data());
    usedSets.clear();
}

DescriptorHandle Descriptor::GetHandle() {
    if (availSets.empty()) {
        allocateSets();
    }
    VkDescriptorSet result_handle = availSets.back();
    availSets.pop_back();
    return DescriptorHandle(*this, std::move(result_handle));
}

void Descriptor::allocateSets() {
    const VkDescriptorSetLayout layout = templ->SetLayout();
    std::vector<VkDescriptorSetLayout> set_layouts(maxSets, templ->SetLayout());
    const VkDescriptorSetAllocateInfo alloc_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        descriptorPool->vkHandle(),
        maxSets,
        set_layouts.data()
    };
    availSets.resize(maxSets, VK_NULL_HANDLE);
    VkResult result = vkAllocateDescriptorSets(device->vkHandle(), &alloc_info, availSets.data());
    VkAssert(result);
    usedSets.reserve(maxSets);
}

void Descriptor::createPool(const st::descriptor_type_counts_t & rsrc_counts) {
    descriptorPool = std::make_unique<vpr::DescriptorPool>(device->vkHandle(), maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, rsrc_counts.UniformBuffers * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, rsrc_counts.UniformBuffersDynamic * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, rsrc_counts.StorageBuffers * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, rsrc_counts.StorageBuffersDynamic * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, rsrc_counts.StorageTexelBuffers * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, rsrc_counts.UniformTexelBuffers * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, rsrc_counts.StorageImages * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLER, rsrc_counts.Samplers * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, rsrc_counts.SampledImages * maxSets);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rsrc_counts.CombinedImageSamplers * maxSets);
    descriptorPool->Create();
}
