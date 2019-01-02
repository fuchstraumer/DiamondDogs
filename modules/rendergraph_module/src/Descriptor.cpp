#include "Descriptor.hpp"

void Descriptor::BindResourceToIdx(size_t idx, VulkanResource* rsrc) {
    // no resource bound at that index yet
    if (createdBindings.count(idx) == 0) {
        // make sure we do have the ability to bind here, though (entry is zero if not added to layout binding)
        addDescriptorBinding(idx, rsrc);
    }
    else {
        updateDescriptorBinding(idx, rsrc);
    }

}