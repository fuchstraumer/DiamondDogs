#include "UploadBuffer.hpp"
#include "ImageDataFormats.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "RhiSystem.hpp"
#include "vulkan/vulkan_core.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>

namespace
{
    [[nodiscard]] uint64_t AlignUp(uint64_t value, uint64_t alignment) noexcept;
    [[nodiscard]] uint64_t TexelBlockBytes(ImageComponentFormats format) noexcept;
}

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

// a buffer to image copy on a transfer queue needs the buffer offset to be a multiple of 4, besides the texel block size
constexpr static uint64_t k_TransferQueueOffsetAlignment = 4u;

uint64_t StagedBufferSize(const InternalResourceDataContainer::BufferDataVector& data_vector) noexcept
{
    uint64_t totalSize = 0u;
    for (const InternalResourceDataContainer::BufferData& data : data_vector)
    {
        totalSize += static_cast<uint64_t>(data.Size);
    }

    return totalSize;
}

uint64_t StagedImageSize(const InternalResourceDataContainer::ImageDataVector& image_data_vector, uint64_t alignment) noexcept
{
    uint64_t totalSize = 0u;
    for (const InternalResourceDataContainer::ImageData& data : image_data_vector)
    {
        totalSize = AlignUp(totalSize, alignment) + static_cast<uint64_t>(data.Size);
    }

    return totalSize;
}

uint64_t ImageStagingAlignment(const ImageFormat& format) noexcept
{
    return std::lcm(TexelBlockBytes(format.ComponentFormat), k_TransferQueueOffsetAlignment);
}

std::vector<VkBufferCopy> WriteBufferData(
    const StagingDestination& destination,
    const InternalResourceDataContainer::BufferDataVector& data_vector)
{
    std::vector<VkBufferCopy> bufferCopies(data_vector.size());
    uint64_t offset = 0u;

    for (auto&& [index, data] : std::views::enumerate(data_vector))
    {
        const std::span<std::byte> target = destination.Mapped.subspan(offset, data.Size);

        std::memcpy(target.data(), data.Data.get(), data.Size);

        VkBufferCopy& copy = bufferCopies[static_cast<size_t>(index)];
        copy.srcOffset = destination.Offset + offset;
        copy.dstOffset = offset;
        copy.size = data.Size;
        offset += static_cast<uint64_t>(data.Size);
    }

    return bufferCopies;
}

std::vector<VkBufferImageCopy> WriteImageData(
    const StagingDestination& destination,
    const InternalResourceDataContainer::ImageDataVector& image_data_vector,
    VkImageAspectFlags aspect_mask,
    uint64_t alignment)
{
    // fixed: this sized the vector to the element count and then also emplaced each copy, so every region was recorded
    // twice, the second time over the first. it also used the image's layer count for every element
    std::vector<VkBufferImageCopy> bufferImageCopies(image_data_vector.size());
    uint64_t offset = 0u;

    for (auto&& [index, data] : std::views::enumerate(image_data_vector))
    {
        offset = AlignUp(offset, alignment);

        const std::span<std::byte> target = destination.Mapped.subspan(offset, data.Size);

        std::memcpy(target.data(), data.Data.get(), data.Size);

        VkBufferImageCopy& copy = bufferImageCopies[static_cast<size_t>(index)];
        copy.bufferOffset = destination.Offset + offset;
        copy.bufferRowLength = 0u;
        copy.bufferImageHeight = 0u;
        copy.imageSubresource.aspectMask = aspect_mask;
        copy.imageSubresource.baseArrayLayer = data.ArrayLayer;
        copy.imageSubresource.layerCount = data.NumLayers;
        copy.imageSubresource.mipLevel = data.MipLevel;
        copy.imageOffset = VkOffset3D{ 0, 0, 0 };
        copy.imageExtent = VkExtent3D{ data.Width, data.Height, 1u };
        offset += static_cast<uint64_t>(data.Size);
    }

    return bufferImageCopies;
}

UploadBuffer::UploadBuffer(VmaAllocator allocator) :
    Allocator{ allocator }
{}

UploadBuffer::UploadBuffer(UploadBuffer&& other) noexcept :
    Buffer(other.Buffer),
    Allocation(other.Allocation),
    Allocator(other.Allocator),
    Mapped(other.Mapped),
    Size(other.Size)
{
    other.Buffer = VK_NULL_HANDLE;
    other.Allocation = VK_NULL_HANDLE;
    other.Mapped = std::span<std::byte>{};
}

UploadBuffer& UploadBuffer::operator=(UploadBuffer&& other) noexcept
{
    if (this != &other)
    {
        Buffer = other.Buffer;
        Allocation = other.Allocation;
        Allocator = other.Allocator;
        Mapped = other.Mapped;
        Size = other.Size;
        other.Buffer = VK_NULL_HANDLE;
        other.Allocation = VK_NULL_HANDLE;
        other.Mapped = std::span<std::byte>{};
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

bool UploadBuffer::Create(VkDeviceSize size)
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

    if (result != VK_SUCCESS)
    {
        Buffer = VK_NULL_HANDLE;
        Allocation = VK_NULL_HANDLE;
        return false;
    }

    std::byte* mappedPtr = static_cast<std::byte*>(resultAllocInfo.pMappedData);
    Mapped = std::span<std::byte>{ mappedPtr, static_cast<size_t>(size) };

    Size = size;

    if constexpr (RHI_SYSTEM_USE_DEBUG_INFO && RHI_SYSTEM_VALIDATION_ENABLED)
    {
        rhi::RhiSystem::SetObjectName(
            VK_OBJECT_TYPE_BUFFER,
            reinterpret_cast<uint64_t>(Buffer),
            "TransferSystem_UploadBuffer");
    }

    return true;
}

StagingDestination UploadBuffer::Destination() const noexcept
{
    return StagingDestination{ Buffer, 0u, Mapped };
}

void UploadBuffer::Flush() const
{
    vmaFlushAllocation(Allocator, Allocation, 0u, Size);
}

namespace
{
    uint64_t AlignUp(uint64_t value, uint64_t alignment) noexcept
    {
        const uint64_t remainder = value % alignment;
        return remainder == 0u ? value : value + (alignment - remainder);
    }

    uint64_t TexelBlockBytes(ImageComponentFormats format) noexcept
    {
        switch (format)
        {
        case ImageComponentFormats::R8:
            return 1u;
        case ImageComponentFormats::R16:
            return 2u;
        case ImageComponentFormats::R32:
            return 4u;
        case ImageComponentFormats::RG8:
            return 2u;
        case ImageComponentFormats::RG16:
            return 4u;
        case ImageComponentFormats::RG32:
            return 8u;
        case ImageComponentFormats::RGB8:
            return 3u;
        case ImageComponentFormats::RGB16:
            return 6u;
        case ImageComponentFormats::RGB32:
            return 12u;
        case ImageComponentFormats::RGBA8:
            return 4u;
        case ImageComponentFormats::BGRA8:
            return 4u;
        case ImageComponentFormats::RGBA16:
            return 8u;
        case ImageComponentFormats::RGBA32:
            return 16u;
        case ImageComponentFormats::A2R10G10B10:
            return 4u;
        case ImageComponentFormats::A2B10G10R10:
            return 4u;
        case ImageComponentFormats::R5G6B5:
            return 2u;
        case ImageComponentFormats::R5G5B5A1:
            return 2u;
        case ImageComponentFormats::R4G4B4A4:
            return 2u;
        case ImageComponentFormats::R64:
            return 8u;
        case ImageComponentFormats::RG64:
            return 16u;
        case ImageComponentFormats::RGB64:
            return 24u;
        case ImageComponentFormats::RGBA64:
            return 32u;
        case ImageComponentFormats::D16:
            return 2u;
        case ImageComponentFormats::D24:
            return 4u;
        case ImageComponentFormats::D32:
            return 4u;
        case ImageComponentFormats::D24S8:
            return 4u;
        case ImageComponentFormats::D32S8:
            return 4u;
        case ImageComponentFormats::S8:
            return 1u;
        case ImageComponentFormats::BC1:
            return 8u;
        case ImageComponentFormats::BC2:
            return 16u;
        case ImageComponentFormats::BC3:
            return 16u;
        case ImageComponentFormats::BC4:
            return 8u;
        case ImageComponentFormats::BC5:
            return 16u;
        case ImageComponentFormats::BC6H:
            return 16u;
        case ImageComponentFormats::BC7:
            return 16u;
        case ImageComponentFormats::Invalid:
        case ImageComponentFormats::Count:
            break;
        }

        // an invalid format fails at the image create, so the least common multiple falls back to 4
        return 1u;
    }
}
