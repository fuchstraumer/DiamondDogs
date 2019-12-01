#pragma once
#ifndef DIAMOND_DOGS_UPDATE_TEMPLATE_DATA_HPP
#define DIAMOND_DOGS_UPDATE_TEMPLATE_DATA_HPP
#include <vulkan/vulkan.h>
#include <utility>
#include <vector>

struct VulkanResource;

union UpdateTemplateDataEntry {
    UpdateTemplateDataEntry() = default;
    explicit UpdateTemplateDataEntry(VkDescriptorBufferInfo&& info) noexcept : BufferInfo(std::move(info)) {}
    explicit UpdateTemplateDataEntry(VkDescriptorImageInfo&& info) noexcept: ImageInfo(std::move(info)) {}
    explicit UpdateTemplateDataEntry(VkBufferView view) noexcept : BufferView{ view } {}
    VkDescriptorBufferInfo BufferInfo;
    VkDescriptorImageInfo ImageInfo;
    VkBufferView BufferView;
};

class UpdateTemplateData {
public:

    UpdateTemplateData() = default;
    UpdateTemplateData(const UpdateTemplateData& other) noexcept;
    UpdateTemplateData(UpdateTemplateData&& other) noexcept;
    UpdateTemplateData& operator=(const UpdateTemplateData& other) noexcept;
    UpdateTemplateData& operator=(UpdateTemplateData&& other) noexcept;

    void BindResourceToIdx(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc);
    void BindArrayResourcesToIdx(const size_t idx, const VkDescriptorType type, const size_t numDescriptors, VulkanResource** resources);
    void BindSingularArrayResourceToIdx(const size_t idx, const size_t arrayIndex, const VkDescriptorType type, VulkanResource* resource);
    // Useful for filling bindless arrays with starting resources
    void FillArrayRangeWithResource(const size_t idx, const VkDescriptorType type, const size_t arraySize, const VulkanResource* resource);
    const void* Data() const noexcept;
    size_t Size() const noexcept;

private:

    void bindSamplerDescriptor(const size_t idx, const VulkanResource* rsrc) noexcept;
    void bindBufferDescriptor(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc) noexcept;
    void bindImageDescriptor(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc) noexcept;
    std::vector<UpdateTemplateDataEntry> rawEntries;

};

#endif // !DIAMOND_DOGS_UPDATE_TEMPLATE_DATA_HPP
