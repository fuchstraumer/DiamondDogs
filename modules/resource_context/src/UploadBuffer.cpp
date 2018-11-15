#include "UploadBuffer.hpp"
#include "Allocator.hpp"
#include "LogicalDevice.hpp"
#include "easylogging++.h"
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

VkDeviceSize UploadBuffer::NonCoherentAtomSize = 0;

UploadBuffer::UploadBuffer(const vpr::Device * _device, vpr::Allocator * allocator, VkDeviceSize sz) : device(_device), alloc{} {
    VkBufferCreateInfo create_info = staging_buffer_create_info;
    create_info.size = sz;
    VkResult result = vkCreateBuffer(device->vkHandle(), &create_info, nullptr, &Buffer);
    VkAssert(result);
    vpr::AllocationRequirements alloc_reqs;
    alloc_reqs.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    allocator->AllocateForBuffer(Buffer, alloc_reqs, vpr::AllocationType::Buffer, alloc);
}

UploadBuffer::UploadBuffer(UploadBuffer && other) noexcept : Buffer(std::move(other.Buffer)), alloc(std::move(other.alloc)), device(other.device) {
    other.Buffer = VK_NULL_HANDLE;
}

UploadBuffer& UploadBuffer::operator=(UploadBuffer && other) noexcept {
    Buffer = std::move(other.Buffer);
    alloc = std::move(other.alloc);
    device = other.device;
    other.Buffer = VK_NULL_HANDLE;
    return *this;
}

void UploadBuffer::SetData(const void* data, size_t data_size, size_t offset) {
    void* mapped_address = nullptr;
    size_t map_size = data_size;
    if ((map_size % NonCoherentAtomSize) != 0) {
        map_size += map_size % NonCoherentAtomSize;
    }
    if ((offset % NonCoherentAtomSize) != 0) {
        LOG(ERROR) << "Offset was less than non-coherent atom size: can't safely make this valid without changing memory being copied into!";
        throw std::runtime_error("Invalid copy offset quantity!");
    }
    alloc.Map(data_size, offset, &mapped_address);
    memcpy(mapped_address, data, data_size);
    alloc.Unmap();
    // Use VK_WHOLE_SIZE here, as it specifies from mapping to end of region.
    VkMappedMemoryRange mapped_memory{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, alloc.Memory(), alloc.Offset() + offset, VK_WHOLE_SIZE };
    vkFlushMappedMemoryRanges(device->vkHandle(), 1, &mapped_memory);
}
