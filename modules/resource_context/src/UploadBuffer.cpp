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

UploadBuffer::UploadBuffer(const vpr::Device * _device, VmaAllocator allocator, VmaPool pool, VkDeviceSize sz) : device(_device), Allocator(allocator) {

	VmaAllocationCreateInfo alloc_create_info
	{
		0,
		VMA_MEMORY_USAGE_CPU_ONLY,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
		0u,
		UINT32_MAX,
		pool,
		nullptr
	};

    VkBufferCreateInfo create_info = staging_buffer_create_info;
    create_info.size = sz;

	VkResult result = vmaCreateBuffer(allocator, &create_info, &alloc_create_info, &Buffer, &Allocation, nullptr);
	VkAssert(result);
}

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
