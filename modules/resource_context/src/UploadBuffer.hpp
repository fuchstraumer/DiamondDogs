#pragma once
#ifndef RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#define RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#include "Allocator.hpp"
#include "LogicalDevice.hpp"
#include "vkAssert.hpp"
#include "AllocationRequirements.hpp"
#include "Allocation.hpp"
#include <vulkan/vulkan.h>

constexpr static VkBufferCreateInfo staging_buffer_create_info{
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    nullptr,
    0,
    0,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VK_SHARING_MODE_EXCLUSIVE,
    0,
    nullptr
};

struct UploadBuffer {
    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;
    UploadBuffer(const vpr::Device* _device, vpr::Allocator* alloc, VkDeviceSize sz);
    UploadBuffer(UploadBuffer&& other) noexcept;
    UploadBuffer& operator=(UploadBuffer&& other) noexcept;
    void SetData(const void* data, size_t data_size, size_t offset);
    VkBuffer Buffer;
    vpr::Allocation alloc;
    const vpr::Device* device;
};

inline UploadBuffer::UploadBuffer(const vpr::Device * _device, vpr::Allocator * allocator, VkDeviceSize sz) : device(_device) {
    VkBufferCreateInfo create_info = staging_buffer_create_info;
    create_info.size = sz;
    VkResult result = vkCreateBuffer(device->vkHandle(), &create_info, nullptr, &Buffer);
    VkAssert(result);
    vpr::AllocationRequirements alloc_reqs;
    alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    allocator->AllocateForBuffer(Buffer, alloc_reqs, vpr::AllocationType::Buffer, alloc);
}

inline UploadBuffer::UploadBuffer(UploadBuffer && other) noexcept : Buffer(std::move(other.Buffer)), alloc(other.alloc), device(other.device) {
    other.Buffer = VK_NULL_HANDLE;
}

inline UploadBuffer& UploadBuffer::operator=(UploadBuffer && other) noexcept {
    Buffer = std::move(other.Buffer);
    alloc = other.alloc;
    device = other.device;
    other.Buffer = VK_NULL_HANDLE;
    return *this;
}

inline void UploadBuffer::SetData(const void* data, size_t data_size, size_t offset) {
    VkMappedMemoryRange mapped_memory{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.Memory(), alloc.Offset() + offset, VK_WHOLE_SIZE };
    void* mapped_address = nullptr;
    alloc.Map(data_size, offset, &mapped_address);
    memcpy(mapped_address, data, data_size);
    alloc.Unmap();
    vkFlushMappedMemoryRanges(device->vkHandle(), 1, &mapped_memory);
}

#endif //!RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
