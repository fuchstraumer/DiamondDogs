#pragma once
#ifndef RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#define RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#include "ForwardDecl.hpp"
#include "vkAssert.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct UploadBuffer
{
    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;
    UploadBuffer(const vpr::Device* _device, VmaAllocator alloc);
    UploadBuffer(UploadBuffer&& other) noexcept;
    UploadBuffer& operator=(UploadBuffer&& other) noexcept;
    void SetData(const void* data, size_t data_size, size_t offset);
    VkBuffer Buffer{ VK_NULL_HANDLE };
    VmaAllocation Allocation{ VK_NULL_HANDLE };
    VmaAllocator Allocator{ VK_NULL_HANDLE };
    const vpr::Device* device{ nullptr };
    const void* mappedPtr{ nullptr };
    VkDeviceSize Size{ 0u };
};

#endif //!RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
