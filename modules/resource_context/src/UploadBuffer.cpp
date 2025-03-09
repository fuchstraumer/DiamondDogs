#include "UploadBuffer.hpp"
#include "LogicalDevice.hpp"

UploadBuffer::UploadBuffer(const vpr::Device * _device, VmaAllocator allocator) :
    device{ _device },
    Allocator{ allocator },
    Allocation{ nullptr },
    Buffer{ VK_NULL_HANDLE },
    mappedPtr{ nullptr },
    Size{ 0u }
{}

UploadBuffer::UploadBuffer(UploadBuffer&& other) noexcept :
    Buffer(std::move(other.Buffer)),
    Allocation(std::move(other.Allocation)),
    device(other.device),
    Allocator(std::move(other.Allocator)),
    mappedPtr(other.mappedPtr),
    Size(other.Size)
{
    other.Buffer = VK_NULL_HANDLE;
}

UploadBuffer& UploadBuffer::operator=(UploadBuffer&& other) noexcept
{
    if (this != &other)
    {
        Buffer = std::move(other.Buffer);
        Allocation = std::move(other.Allocation);
        device = other.device;
        Allocator = std::move(other.Allocator);
        mappedPtr = other.mappedPtr;
        Size = other.Size;
        other.Buffer = VK_NULL_HANDLE;
    }
    return *this;
}

void UploadBuffer::SetData(const void* data, size_t data_size, size_t offset)
{
    assert((data_size + offset) <= Size);
    auto destAddress = reinterpret_cast<std::byte*>(mappedPtr) + offset;
    auto dataAddr = reinterpret_cast<const std::byte*>(data);
    auto endAddr = dataAddr + data_size;
    std::copy(dataAddr, endAddr, destAddress);
    vmaUnmapMemory(Allocator, Allocation);
    vmaFlushAllocation(Allocator, Allocation, offset, data_size);
}
