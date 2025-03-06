#include "UploadBuffer.hpp"
#include "LogicalDevice.hpp"

UploadBuffer::UploadBuffer(const vpr::Device * _device, VmaAllocator allocator) : device(_device), Allocator(allocator), Allocation(nullptr), Buffer(VK_NULL_HANDLE) {}

UploadBuffer::UploadBuffer(UploadBuffer&& other) noexcept : Buffer(std::move(other.Buffer)), Allocation(std::move(other.Allocation)), device(other.device), Allocator(std::move(other.Allocator))
{
    other.Buffer = VK_NULL_HANDLE;
}

UploadBuffer& UploadBuffer::operator=(UploadBuffer&& other) noexcept
{
    Buffer = std::move(other.Buffer);
    Allocation = std::move(other.Allocation);
    device = other.device;
    Allocator = std::move(other.Allocator);
    other.Buffer = VK_NULL_HANDLE;
    return *this;
}

void UploadBuffer::SetData(const void* data, size_t data_size, size_t offset)
{
    void* mapped_address = nullptr;
    assert((data_size + offset) <= Size);
    VkResult result = vmaMapMemory(Allocator, Allocation, &mapped_address);
    VkAssert(result);
    auto destAddress = reinterpret_cast<std::byte*>(mapped_address) + offset;
    auto dataAddr = reinterpret_cast<const std::byte*>(data);
    auto endAddr = dataAddr + data_size;
    std::copy(dataAddr, endAddr, destAddress);
    vmaUnmapMemory(Allocator, Allocation);
    vmaFlushAllocation(Allocator, Allocation, offset, data_size);
}
