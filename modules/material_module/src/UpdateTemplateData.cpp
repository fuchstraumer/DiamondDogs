#include "UpdateTemplateData.hpp"
#include "ResourceContext.hpp"
#include <stdexcept>

static VkImageLayout imageLayoutFromUsage(const VkImageUsageFlags usage_flags) {
    if (usage_flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else if (usage_flags & VK_IMAGE_USAGE_SAMPLED_BIT)
    {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    else
    {
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

UpdateTemplateData::UpdateTemplateData(const UpdateTemplateData & other) noexcept : rawEntries(other.rawEntries) {}

UpdateTemplateData::UpdateTemplateData(UpdateTemplateData && other) noexcept : rawEntries(std::move(other.rawEntries)) {}

UpdateTemplateData& UpdateTemplateData::operator=(const UpdateTemplateData & other) noexcept
{
    rawEntries = other.rawEntries;
    return *this;
}

UpdateTemplateData& UpdateTemplateData::operator=(UpdateTemplateData&& other) noexcept
{
    rawEntries = std::move(other.rawEntries);
    return *this;
}

void UpdateTemplateData::BindResourceToIdx(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc)
{

    if (idx > rawEntries.size())
    {
        rawEntries.resize(idx + 1, UpdateTemplateDataEntry((VkBufferView)VK_NULL_HANDLE));
    }

    switch (rsrc->Type)
    {
    case resource_type::BUFFER:
        bindBufferDescriptor(idx, type, rsrc);
        break;
    case resource_type::COMBINED_IMAGE_SAMPLER:
        [[fallthrough]];
    case resource_type::IMAGE:
        bindImageDescriptor(idx, type, rsrc);
        break;
    case resource_type::SAMPLER:
        bindSamplerDescriptor(idx, rsrc);
        break;
    default:
        throw std::domain_error("VulkanResource pointer had invalid type value!");
    }
}

void UpdateTemplateData::BindArrayResourcesToIdx(const size_t idx, const VkDescriptorType type, const size_t numDescriptors, VulkanResource** resources)
{
    // really the only optimization we can pull with this method without too much restructuring
    const size_t requiredSize = idx + numDescriptors;
    if (requiredSize > rawEntries.size())
    {
        rawEntries.resize(requiredSize);
    }

    for (size_t i = 0; i < numDescriptors; ++i)
    {
        const size_t actualIdx = idx + i;
        BindResourceToIdx(actualIdx, type, resources[i]);
    }
}

void UpdateTemplateData::BindSingularArrayResourceToIdx(const size_t idx, const size_t arrayIndex, const VkDescriptorType type, VulkanResource* resource)
{
    // we just lay our array descriptors out linearly
    const size_t finalIndex = idx + arrayIndex;
    if (finalIndex > rawEntries.size())
    {
        rawEntries.resize(finalIndex);
    }

    BindResourceToIdx(finalIndex, type, resource);
}

void UpdateTemplateData::FillArrayRangeWithResource(const size_t idx, const VkDescriptorType type, const size_t arraySize, const VulkanResource* resource)
{
    const size_t requiredSize = idx + arraySize;
    if (requiredSize > rawEntries.size())
    {
        rawEntries.resize(requiredSize);
    }

    UpdateTemplateDataEntry rsrcDescriptor;
    switch (type)
    {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        rsrcDescriptor = UpdateTemplateDataEntry{ VkDescriptorImageInfo{ (VkSampler)resource->Handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED } };
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        rsrcDescriptor = UpdateTemplateDataEntry{
            VkDescriptorImageInfo{ (VkSampler)resource->Sampler, (VkImageView)resource->ViewHandle,
            imageLayoutFromUsage(reinterpret_cast<VkImageCreateInfo*>(resource->Info)->usage) } };
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        [[fallthrough]];
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        rsrcDescriptor = UpdateTemplateDataEntry{
            VkDescriptorImageInfo{ VK_NULL_HANDLE, (VkImageView)resource->ViewHandle,
            imageLayoutFromUsage(reinterpret_cast<VkImageCreateInfo*>(resource->Info)->usage) } };
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        [[fallthrough]];
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        rsrcDescriptor = UpdateTemplateDataEntry((VkBufferView)resource->ViewHandle);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        [[fallthrough]];
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        [[falthrough]];
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        [[fallthrough]];
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        rsrcDescriptor = UpdateTemplateDataEntry{ VkDescriptorBufferInfo { (VkBuffer)resource->Handle, 0u,
            reinterpret_cast<const VkBufferCreateInfo*>(resource->Info)->size } };
        break;
    default:
        throw std::domain_error("Unsupported VkDescriptorType for binding to UpdateTemplateData!");
    };

    // Fill rawEntries with the given resource descriptor.
    std::fill(rawEntries.begin() + idx, rawEntries.begin() + idx + arraySize, rsrcDescriptor);
}

const void* UpdateTemplateData::Data() const noexcept
{
    return rawEntries.data();
}

size_t UpdateTemplateData::Size() const noexcept
{
    return rawEntries.size();
}

void UpdateTemplateData::bindBufferDescriptor(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc) noexcept
{
    const bool is_texel_buffer = (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) || (type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER);
    const VkBufferCreateInfo* bufferInfo = reinterpret_cast<const VkBufferCreateInfo*>(rsrc->Info);
    if (!is_texel_buffer)
    {
        rawEntries[idx] = UpdateTemplateDataEntry{ VkDescriptorBufferInfo { (VkBuffer)rsrc->Handle, 0u, bufferInfo->size } };
    }
    else
    {
        // texel buffers are just formatted views of regular buffers, so all we need to bind them is a buffer view handle
        rawEntries[idx] = UpdateTemplateDataEntry{ (VkBufferView)rsrc->ViewHandle };
    }
}

void UpdateTemplateData::bindImageDescriptor(const size_t idx, const VkDescriptorType type, const VulkanResource* rsrc) noexcept
{
    auto& raw_entry = rawEntries[idx];
    const VkImageCreateInfo* img_create_info = reinterpret_cast<VkImageCreateInfo*>(rsrc->Info);
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
    {
        raw_entry = UpdateTemplateDataEntry{ VkDescriptorImageInfo{ (VkSampler)rsrc->Sampler, (VkImageView)rsrc->ViewHandle, imageLayoutFromUsage(img_create_info->usage) } };
    }
    else
    {
        raw_entry = UpdateTemplateDataEntry{ VkDescriptorImageInfo{ VK_NULL_HANDLE, (VkImageView)rsrc->ViewHandle, imageLayoutFromUsage(img_create_info->usage) } };
    }
}

void UpdateTemplateData::bindSamplerDescriptor(const size_t idx, const VulkanResource* rsrc) noexcept {
    auto& raw_entry = rawEntries[idx];
    raw_entry = UpdateTemplateDataEntry{ VkDescriptorImageInfo{ (VkSampler)rsrc->Handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED } };
}
