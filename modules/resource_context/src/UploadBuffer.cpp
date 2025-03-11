#include "UploadBuffer.hpp"
#include "LogicalDevice.hpp"
#include <vk_mem_alloc.h>

constexpr static VkBufferCreateInfo k_defaultStagingBufferCreateInfo
{
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    nullptr,
    0,
    4096u,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VK_SHARING_MODE_EXCLUSIVE,
    0,
    nullptr
};

constexpr static VmaAllocationCreateInfo k_defaultAllocationCreateInfo
{
    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    VMA_MEMORY_USAGE_AUTO,
    0,
    0u,
    UINT32_MAX,
    VK_NULL_HANDLE,
    nullptr
};

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

UploadBuffer::~UploadBuffer()
{
    vmaDestroyBuffer(Allocator, Buffer, Allocation);
}

std::vector<VkBufferCopy> UploadBuffer::SetData(const InternalResourceDataContainer::BufferDataVector& dataVector)
{
    VkDeviceSize total_size = 0;
    for (const auto& data : dataVector)
    {
        total_size += static_cast<VkDeviceSize>(data.size);
    }

    Size = total_size;

    createAndAllocateBuffer(total_size);
    std::vector<VkBufferCopy> buffer_copies(dataVector.size());
    VkDeviceSize offset = 0;
    for (size_t i = 0; i < dataVector.size(); ++i)
    {
        setDataAtOffset(dataVector[i].data.get(), dataVector[i].size, offset);
        buffer_copies[i].size = dataVector[i].size;
        buffer_copies[i].dstOffset = offset;
        buffer_copies[i].srcOffset = offset;
        offset += dataVector[i].size;
    }

    return buffer_copies;
}

std::vector<VkBufferImageCopy> UploadBuffer::SetData(
    const InternalResourceDataContainer::ImageDataVector& imageDataVector,
    const uint32_t numLayers)
{
    VkDeviceSize total_size = 0;
    for (const auto& data : imageDataVector)
    {
        total_size += static_cast<VkDeviceSize>(data.size);
    }

    Size = total_size;

    createAndAllocateBuffer(total_size);
    std::vector<VkBufferImageCopy> buffer_image_copies(imageDataVector.size());
    VkDeviceSize offset = 0;
    for (size_t i = 0; i < imageDataVector.size(); ++i)
    {
        setDataAtOffset(imageDataVector[i].data.get(), imageDataVector[i].size, offset);
        VkBufferImageCopy& copy = buffer_image_copies[i];
        copy.bufferOffset = offset;
        copy.bufferRowLength = 0u;
        copy.bufferImageHeight = 0u;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.baseArrayLayer = imageDataVector[i].arrayLayer;
        copy.imageSubresource.layerCount = numLayers;
        copy.imageSubresource.mipLevel = imageDataVector[i].mipLevel;
        copy.imageOffset = VkOffset3D{ 0, 0, 0 };
        copy.imageExtent = VkExtent3D{ imageDataVector[i].width, imageDataVector[i].height, 1u };
        buffer_image_copies.emplace_back(std::move(copy));
        offset += static_cast<VkDeviceSize>(imageDataVector[i].size);
    }

    return buffer_image_copies;
}

void UploadBuffer::createAndAllocateBuffer(VkDeviceSize size)
{
    VkBufferCreateInfo create_info = k_defaultStagingBufferCreateInfo;
    create_info.size = size;
    VmaAllocationCreateInfo alloc_create_info = k_defaultAllocationCreateInfo;
    VkResult result = vmaCreateBuffer(
        Allocator,
        &create_info,
        &alloc_create_info,
        &Buffer,
        &Allocation,
        nullptr
    );
    VkAssert(result);
    Size = size;
}

void UploadBuffer::setDataAtOffset(const void* data, size_t data_size, size_t offset)
{
    assert((data_size + offset) <= Size);
    auto destAddress = reinterpret_cast<std::byte*>(mappedPtr) + offset;
    std::memcpy(destAddress, data, data_size);
}
