#include "UploadBuffer.hpp"
#include "Allocator.hpp"
#include "LogicalDevice.hpp"
#include "easylogging++.h"

UploadBuffer::UploadBuffer(const vpr::Device * _device, VmaAllocator allocator) : device(_device), Allocator(allocator) {}

UploadBuffer::UploadBuffer(UploadBuffer && other) noexcept : Buffer(std::move(other.Buffer)), Allocation(std::move(other.Allocation)), device(other.device), Allocator(std::move(other.Allocator)) {
    other.Buffer = VK_NULL_HANDLE;
}

UploadBuffer& UploadBuffer::operator=(UploadBuffer && other) noexcept {
    Buffer = std::move(other.Buffer);
    Allocation = std::move(other.Allocation);
    device = other.device;
	Allocator = std::move(other.Allocator);
    other.Buffer = VK_NULL_HANDLE;
    return *this;
}

void UploadBuffer::SetData(const void* data, size_t data_size, size_t offset) {
    void* mapped_address = nullptr;
	vmaMapMemory(Allocator, Allocation, &mapped_address);
    memcpy(mapped_address, data, data_size);
	vmaUnmapMemory(Allocator, Allocation);
	vmaFlushAllocation(Allocator, Allocation, offset, data_size);
}
