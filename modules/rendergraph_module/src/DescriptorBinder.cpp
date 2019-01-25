#include "DescriptorBinder.hpp"
#include "Descriptor.hpp"
#include "LogicalDevice.hpp"
#include <cassert>
#include <algorithm>

DescriptorBinder::DescriptorBinder(size_t num_descriptors, VkPipelineLayout layout) : parentDescriptors(num_descriptors), templHandles(num_descriptors), setHandles(num_descriptors, VK_NULL_HANDLE), 
    updateTemplData(num_descriptors), pipelineLayout(std::move(layout)), dirtySets(num_descriptors, true) {
    descriptorIdxMap.reserve(num_descriptors);
}

DescriptorBinder::~DescriptorBinder() {}

void DescriptorBinder::AddDescriptor(size_t descr_idx, Descriptor* descr) {
    descriptorIdxMap.emplace(descr, descr_idx);
    parentDescriptors[descr_idx] = descr;
    templHandles[descr_idx] = descr->templ->UpdateTemplate();
    updateTemplData[descr_idx] = descr->templ->UpdateData();
    setHandles[descr_idx] = descr->fetchNewSet();
}

void DescriptorBinder::BindResourceToIdx(const std::string& descr, size_t rsrc_idx, VkDescriptorType type, VulkanResource * rsrc) {
    size_t idx = descriptorIdxMap.at(descr);
    updateTemplData[idx].BindResourceToIdx(rsrc_idx, type, rsrc);
    dirtySets[idx] = true;
}

void DescriptorBinder::Update() {

    for (auto iter = std::find(dirtySets.begin(), dirtySets.end(), true); iter != dirtySets.end() && *iter == true; ++iter) {
        size_t idx = std::distance(dirtySets.begin(), iter);
        VkDescriptorSet handle = parentDescriptors[idx]->fetchNewSet();
        vkUpdateDescriptorSetWithTemplate(parentDescriptors[idx]->device->vkHandle(), handle, templHandles[idx], updateTemplData[idx].Data());
        setHandles[idx] = handle;
        *iter = false;
    }

}

void DescriptorBinder::Bind(VkCommandBuffer cmd, VkPipelineBindPoint bind_point) {
    vkCmdBindDescriptorSets(cmd, bind_point, pipelineLayout, 0, setHandles.size(), setHandles.data(), 0, nullptr);
}

void DescriptorBinder::BindSingle(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const std::string & descr) {
    size_t dscr_idx = descriptorIdxMap.at(descr);
    // dscr_idx is offset to "first_set", which is first one we intend to bind: only one to be re-bound here
    vkCmdBindDescriptorSets(cmd, bind_point, pipelineLayout, dscr_idx, 1, setHandles.data(), 0, nullptr);
}
