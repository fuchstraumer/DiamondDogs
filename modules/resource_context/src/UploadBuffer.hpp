#pragma once
#ifndef RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#define RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#include "ForwardDecl.hpp"
#include "vkAssert.hpp"
#include "AllocationRequirements.hpp"
#include "Allocation.hpp"
#include <vulkan/vulkan.h>

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

#endif //!RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
