#pragma once
#ifndef RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#define RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#include "ImageDataFormats.hpp"
#include "containers/SmallVector.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <cstddef>
#include <cstdint>
#include <span>

/** @brief Where the staged bytes of one transfer go: a buffer, the offset in it where the transfer's range starts, and
 *  the mapped pointer to that offset. The staging ring and a dedicated UploadBuffer both make one. */
struct StagingDestination
{
    VkBuffer Buffer{ VK_NULL_HANDLE };
    uint64_t Offset{ 0u };
    std::span<std::byte> Mapped;
};

/** @brief Returns the bytes that a buffer upload stages. The pieces are packed in the staging range, one after
 *  another. Where each piece lands in the destination buffer is its `DestinationOffset`, and it does not change the
 *  staged size. */
[[nodiscard]] uint64_t StagedBufferSize(const InternalResourceDataContainer::BufferDataVector& data_vector) noexcept;

/** @brief Returns the bytes that an image upload stages, with the start of each piece aligned to `alignment`. */
[[nodiscard]] uint64_t StagedImageSize(const InternalResourceDataContainer::ImageDataVector& image_data_vector, uint64_t alignment) noexcept;

/** @brief Returns the bytes of one texel block of the format: the texel size, or the size of one 4 by 4 block for a
 *  block-compressed format. */
[[nodiscard]] uint64_t ImageTexelBlockBytes(const ImageFormat& format) noexcept;

/** @brief Returns the edge of a texel block in texels: 4 for a block-compressed format, and 1 for every other format. */
[[nodiscard]] int32_t ImageTexelBlockEdge(const ImageFormat& format) noexcept;

/** @brief Returns the alignment that the buffer offset of an image copy needs: the least common multiple of the texel
 *  block size of the format and 4. A 12-byte texel such as RGB32F is why this is not a power of two. */
[[nodiscard]] uint64_t ImageStagingAlignment(const ImageFormat& format) noexcept;

/** @brief Copies each piece into `destination`, packed one after another, and returns one copy region per piece. The
 *  source offset of a region is the place of the piece in staging, and its destination offset is the `DestinationOffset`
 *  of the piece. The destination must have StagedBufferSize bytes. */
[[nodiscard]] foundation::SmallVector<VkBufferCopy, 16> WriteBufferData(
    const StagingDestination& destination,
    const InternalResourceDataContainer::BufferDataVector& data_vector);

/** @brief Copies each piece into `destination` and returns one copy region per piece. The destination must have
 *  StagedImageSize bytes, and its offset must be a multiple of `alignment`. Each piece is tightly packed in staging.
 *  The worker set the depth of every piece. */
[[nodiscard]] foundation::SmallVector<VkBufferImageCopy, 16> WriteImageData(
    const StagingDestination& destination,
    const InternalResourceDataContainer::ImageDataVector& image_data_vector,
    VkImageAspectFlags aspect_mask,
    uint64_t alignment);

/** @brief A staging buffer of its own, for an upload that is larger than the staging ring. */
struct UploadBuffer
{
    UploadBuffer(VmaAllocator alloc);
    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;
    UploadBuffer(UploadBuffer&& other) noexcept;
    UploadBuffer& operator=(UploadBuffer&& other) noexcept;
    ~UploadBuffer();

    /** @brief Creates the mapped buffer. Returns false when the allocation fails. */
    [[nodiscard]] bool Create(VkDeviceSize size);
    [[nodiscard]] StagingDestination Destination() const noexcept;
    /** @brief Flushes the whole buffer, because the memory may not be coherent. */
    void Flush() const;

    VkBuffer Buffer{ VK_NULL_HANDLE };
    VmaAllocation Allocation{ VK_NULL_HANDLE };
    VmaAllocator Allocator{ VK_NULL_HANDLE };
    std::span<std::byte> Mapped;
    VkDeviceSize Size{ 0u };
};

#endif //!RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
