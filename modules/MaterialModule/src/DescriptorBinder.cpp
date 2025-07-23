#include "DescriptorBinder.hpp"
#include "Descriptor.hpp"
#include "LogicalDevice.hpp"
#include <cassert>
#include <algorithm>

DescriptorBinder::DescriptorBinder(const DescriptorBinder & other) noexcept : pipelineLayout(other.pipelineLayout), descriptorIdxMap(other.descriptorIdxMap), parentDescriptors(other.parentDescriptors),
    templHandles(other.templHandles), setHandles(other.setHandles), updateTemplData(other.updateTemplData), dirtySets(other.dirtySets) {}

DescriptorBinder::DescriptorBinder(size_t num_descriptors, VkPipelineLayout layout) : parentDescriptors(num_descriptors), templHandles(num_descriptors), setHandles(num_descriptors, VK_NULL_HANDLE),
    updateTemplData(num_descriptors), pipelineLayout(std::move(layout)), dirtySets(num_descriptors, true) {
    descriptorIdxMap.reserve(num_descriptors);
}

DescriptorBinder::~DescriptorBinder() {}

void DescriptorBinder::AddDescriptor(size_t descr_idx, std::string name, Descriptor* descr)
{
    descriptorIdxMap.emplace(std::move(name), descr_idx);
    parentDescriptors[descr_idx] = descr;
    templHandles[descr_idx] = descr->templ->UpdateTemplate();
    updateTemplData[descr_idx] = descr->templ->UpdateData();
    setHandles[descr_idx] = descr->fetchNewSet();
}

void DescriptorBinder::BindResourceToIdx(const std::string& descr, size_t rsrc_idx, VkDescriptorType type, VulkanResource * rsrc)
{
    size_t idx = descriptorIdxMap.at(descr);
    updateTemplData[idx].BindResourceToIdx(rsrc_idx, type, rsrc);
    dirtySets[idx] = true;
}

void DescriptorBinder::BindResource(const char* descr, const char* rsrc_name, VkDescriptorType type, VulkanResource* rsrc)
{
    const size_t descr_idx = descriptorIdxMap.at(descr);
    const Descriptor* parentDescriptor = parentDescriptors[descr_idx];
    const size_t resource_idx = parentDescriptor->bindingLocations.at(rsrc_name);
    updateTemplData[descr_idx].BindResourceToIdx(resource_idx, type, rsrc);
    dirtySets[descr_idx] = true;
}

void DescriptorBinder::Update()
{

    for (auto iter = std::find(dirtySets.begin(), dirtySets.end(), true); iter != dirtySets.end(); ++iter)
    {
        if (*iter == false)
        {
            continue;
        }
        size_t idx = std::distance(dirtySets.begin(), iter);
        VkDescriptorSet setHandle{ VK_NULL_HANDLE };
        if (!parentDescriptors[idx]->AllowsUpdatingAfterBind())
        {
            setHandle = parentDescriptors[idx]->fetchNewSet();
        }
        else
        {
            setHandle = parentDescriptors[idx]->availSets.back();
        }
        vkUpdateDescriptorSetWithTemplate(parentDescriptors[idx]->device->vkHandle(), setHandle, templHandles[idx], updateTemplData[idx].Data());
        setHandles[idx] = setHandle;
        *iter = false;
    }

}

void DescriptorBinder::Bind(VkCommandBuffer cmd, VkPipelineBindPoint bind_point)
{
    if (std::any_of(dirtySets.begin(), dirtySets.end(), [](const bool& v) { return v; }))
    {
        Update();
    }

    vkCmdBindDescriptorSets(cmd, bind_point, pipelineLayout, 0, static_cast<uint32_t>(setHandles.size()), setHandles.data(), 0, nullptr);
}

void DescriptorBinder::BindSingle(VkCommandBuffer cmd, VkPipelineBindPoint bind_point, const std::string & descr)
{
    size_t dscr_idx = descriptorIdxMap.at(descr);

    if (dirtySets[dscr_idx])
    {
        Update();
    }

    // dscr_idx is offset to "first_set", which is first one we intend to bind: only one to be re-bound here
    vkCmdBindDescriptorSets(cmd, bind_point, pipelineLayout, static_cast<uint32_t>(dscr_idx), 1u, setHandles.data(), 0, nullptr);
}
