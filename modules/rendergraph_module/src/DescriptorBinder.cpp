#include "DescriptorBinder.hpp"
#include "Descriptor.hpp"
#include "LogicalDevice.hpp"
#include <cassert>

DescriptorBinder::DescriptorBinder(size_t num_descriptors) {
    templHandles.reserve(num_descriptors);
}

DescriptorBinder::~DescriptorBinder() {}

void DescriptorBinder::BindResourceToIdx(Descriptor* descr, size_t idx, VkDescriptorType type, VulkanResource * rsrc) {
    if (updated) {
        // just gets fresh new handle, easiest method
        handle = parent.availSet();
        updated = false;
    }
    data.BindResourceToIdx(idx, type, rsrc);
}

void DescriptorBinder::Update() {
    if (updated) {
        return;
    }
    else {
        vkUpdateDescriptorSetWithTemplate(parent.device->vkHandle(), handle, templHandle, data.Data());
        updated = true;
    }
}

VkDescriptorSet DescriptorBinder::Handle() const noexcept {
    assert(updated);
    return handle;
}