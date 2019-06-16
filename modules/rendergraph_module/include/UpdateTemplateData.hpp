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

    void BindResourceToIdx(size_t idx, VkDescriptorType type, VulkanResource* rsrc);
    void BindArrayResourceToIdx(const size_t idx, const size_t numDescriptors, VkDescriptorType type, VulkanResource** resources);
    const void* Data() const noexcept;
    size_t Size() const noexcept;

private:    
    
    void bindSamplerDescriptor(const size_t idx, VulkanResource* rsrc) noexcept;
    void bindBufferDescriptor(const size_t idx, VkDescriptorType type, VulkanResource* rsrc) noexcept;
    void bindImageDescriptor(const size_t idx, VkDescriptorType type, VulkanResource * rsrc) noexcept;
    std::vector<UpdateTemplateDataEntry> rawEntries;

};

#endif // !DIAMOND_DOGS_UPDATE_TEMPLATE_DATA_HPP
