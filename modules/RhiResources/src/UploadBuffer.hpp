#pragma once
#ifndef RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#define RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#include "ResourceMessageTypesInternal.hpp"
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <vector>

struct UploadBuffer
{
    UploadBuffer(VmaAllocator alloc);
    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;
    UploadBuffer(UploadBuffer&& other) noexcept;
    UploadBuffer& operator=(UploadBuffer&& other) noexcept;
    ~UploadBuffer();
    std::vector<VkBufferCopy> SetData(
        const InternalResourceDataContainer::BufferDataVector& dataVector);
    std::vector<VkBufferImageCopy> SetData(
        const InternalResourceDataContainer::ImageDataVector& imageDataVector,
        VkImageAspectFlags aspect_mask);
    VkBuffer Buffer{ VK_NULL_HANDLE };
    VmaAllocation Allocation{ VK_NULL_HANDLE };
    VmaAllocator Allocator{ VK_NULL_HANDLE };
    void* mappedPtr{ nullptr };
    VkDeviceSize Size{ 0u };
private:
    void createAndAllocateBuffer(VkDeviceSize size);
    void setDataAtOffset(const void* data, size_t data_size, size_t offset);
};

#endif //!RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
