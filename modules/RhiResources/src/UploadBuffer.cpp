#include "UploadBuffer.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "RhiSystem.hpp"
#include "vulkan/vulkan_core.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr static VkBufferCreateInfo k_DefaultStagingBufferCreateInfo
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

constexpr static VmaAllocationCreateInfo k_DefaultAllocationCreateInfo
{
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
    0,
    0u,
    UINT32_MAX,
    VK_NULL_HANDLE,
    nullptr
};

// buffer to image copies need the buffer offset to be a multiple of the texel block size (and 4, on a transfer queue).
// 16 covers every uncompressed format and every block-compressed one
constexpr static VkDeviceSize k_ImageCopyOffsetAlignment = 16u;

UploadBuffer::UploadBuffer(VmaAllocator allocator) :
    Allocator{ allocator }
{}

UploadBuffer::UploadBuffer(UploadBuffer&& other) noexcept :
    Buffer(other.Buffer),
    Allocation(other.Allocation),
    Allocator(other.Allocator),
    mappedPtr(other.mappedPtr),
    Size(other.Size)
{
    other.Buffer = VK_NULL_HANDLE;
    other.Allocation = VK_NULL_HANDLE;
}

UploadBuffer& UploadBuffer::operator=(UploadBuffer&& other) noexcept
{
    if (this != &other)
    {
        Buffer = other.Buffer;
        Allocation = other.Allocation;
        Allocator = other.Allocator;
        mappedPtr = other.mappedPtr;
        Size = other.Size;
        other.Buffer = VK_NULL_HANDLE;
        other.Allocation = VK_NULL_HANDLE;
    }
    return *this;
}

UploadBuffer::~UploadBuffer()
{
    if (Buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(Allocator, Buffer, Allocation);
    }
}

std::vector<VkBufferCopy> UploadBuffer::SetData(
    const InternalResourceDataContainer::BufferDataVector& data_vector)
{
    VkDeviceSize totalSize = 0;
    for (const auto& data : data_vector)
    {
        totalSize += static_cast<VkDeviceSize>(data.Size);
    }

    createAndAllocateBuffer(totalSize);
    std::vector<VkBufferCopy> bufferCopies(data_vector.size());
    VkDeviceSize offset = 0;
    for (size_t i = 0; i < data_vector.size(); ++i)
    {
        setDataAtOffset(data_vector[i].Data.get(), data_vector[i].Size, offset);
        bufferCopies[i].size = data_vector[i].Size;
        bufferCopies[i].dstOffset = offset;
        bufferCopies[i].srcOffset = offset;
        offset += data_vector[i].Size;
    }

    return bufferCopies;
}

std::vector<VkBufferImageCopy> UploadBuffer::SetData(
    const InternalResourceDataContainer::ImageDataVector& image_data_vector,
    VkImageAspectFlags aspect_mask)
{
    VkDeviceSize totalSize = 0;
    for (const auto& data : image_data_vector)
    {
        totalSize = ((totalSize + k_ImageCopyOffsetAlignment - 1u) / k_ImageCopyOffsetAlignment) * k_ImageCopyOffsetAlignment;
        totalSize += static_cast<VkDeviceSize>(data.Size);
    }

    createAndAllocateBuffer(totalSize);
    // fixed: this sized the vector to the element count and then also emplaced each copy, so every region was recorded
    // twice, the second time over the first. it also used the image's layer count for every element
    std::vector<VkBufferImageCopy> bufferImageCopies(image_data_vector.size());
    VkDeviceSize offset = 0;
    for (size_t i = 0; i < image_data_vector.size(); ++i)
    {
        offset = ((offset + k_ImageCopyOffsetAlignment - 1u) / k_ImageCopyOffsetAlignment) * k_ImageCopyOffsetAlignment;
        setDataAtOffset(image_data_vector[i].Data.get(), image_data_vector[i].Size, offset);
        VkBufferImageCopy& copy = bufferImageCopies[i];
        copy.bufferOffset = offset;
        copy.bufferRowLength = 0u;
        copy.bufferImageHeight = 0u;
        copy.imageSubresource.aspectMask = aspect_mask;
        copy.imageSubresource.baseArrayLayer = image_data_vector[i].ArrayLayer;
        copy.imageSubresource.layerCount = image_data_vector[i].NumLayers;
        copy.imageSubresource.mipLevel = image_data_vector[i].MipLevel;
        copy.imageOffset = VkOffset3D{ 0, 0, 0 };
        copy.imageExtent = VkExtent3D{ image_data_vector[i].Width, image_data_vector[i].Height, 1u };
        offset += static_cast<VkDeviceSize>(image_data_vector[i].Size);
    }

    return bufferImageCopies;
}

void UploadBuffer::createAndAllocateBuffer(VkDeviceSize size)
{
    VkBufferCreateInfo createInfo = k_DefaultStagingBufferCreateInfo;
    createInfo.size = size;
    VmaAllocationCreateInfo allocCreateInfo = k_DefaultAllocationCreateInfo;
    VmaAllocationInfo resultAllocInfo;
    VkResult result = vmaCreateBuffer(
        Allocator,
        &createInfo,
        &allocCreateInfo,
        &Buffer,
        &Allocation,
        &resultAllocInfo);
    assert(result == VK_SUCCESS);
    mappedPtr = resultAllocInfo.pMappedData;
    Size = size;

    if constexpr (RHI_SYSTEM_USE_DEBUG_INFO && RHI_SYSTEM_VALIDATION_ENABLED)
    {
        rhi::RhiSystem::SetObjectName(
            VK_OBJECT_TYPE_BUFFER,
            reinterpret_cast<uint64_t>(Buffer),
            "TransferSystem_UploadBuffer");
    }
}

void UploadBuffer::setDataAtOffset(const void* data, size_t data_size, size_t offset)
{
    assert((data_size + offset) <= Size);
    auto* destAddress = reinterpret_cast<std::byte*>(mappedPtr) + offset;
    std::memcpy(destAddress, data, data_size);
    // the allocation is host-visible but may not be coherent
    vmaFlushAllocation(Allocator, Allocation, offset, data_size);
}
