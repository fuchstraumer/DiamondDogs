#pragma once
#ifndef RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#define RESOURCE_CONTEXT_UPLOAD_BUFFER_HPP
#include "ImageDataFormats.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/** @brief Where the staged bytes of one transfer go: a buffer, the offset in it where the transfer's range starts, and
 *  the mapped pointer to that offset. The staging ring and a dedicated UploadBuffer both make one. */
struct StagingDestination
{
    VkBuffer Buffer{ VK_NULL_HANDLE };
    uint64_t Offset{ 0u };
    std::span<std::byte> Mapped;
};

/** @brief Returns the bytes that a buffer upload stages. The pieces are packed, because the destination buffer takes
 *  them one after another from its offset 0. */
[[nodiscard]] uint64_t StagedBufferSize(const InternalResourceDataContainer::BufferDataVector& data_vector) noexcept;

/** @brief Returns the bytes that an image upload stages, with the start of each piece aligned to `alignment`. */
[[nodiscard]] uint64_t StagedImageSize(const InternalResourceDataContainer::ImageDataVector& image_data_vector, uint64_t alignment) noexcept;

/** @brief Returns the alignment that the buffer offset of an image copy needs: the least common multiple of the texel
 *  block size of the format and 4. A 12-byte texel such as RGB32F is why this is not a power of two. */
[[nodiscard]] uint64_t ImageStagingAlignment(const ImageFormat& format) noexcept;

/** @brief Copies each piece into `destination` and returns one copy region per piece. The destination must have
 *  StagedBufferSize bytes. */
[[nodiscard]] std::vector<VkBufferCopy> WriteBufferData(
    const StagingDestination& destination,
    const InternalResourceDataContainer::BufferDataVector& data_vector);

/** @brief Copies each piece into `destination` and returns one copy region per piece. The destination must have
 *  StagedImageSize bytes, and its offset must be a multiple of `alignment`. */
[[nodiscard]] std::vector<VkBufferImageCopy> WriteImageData(
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
