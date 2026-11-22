#pragma once
#ifndef DIAMOND_DOGS_TRANSFER_SYSTEM_MESSAGES_INTERNAL_HPP
#define DIAMOND_DOGS_TRANSFER_SYSTEM_MESSAGES_INTERNAL_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <variant>
#include <vulkan/vulkan_core.h>

// following message types are broadly the same, but feature extra info to reduce need for thread sync or
// safety concerns with transfer system.

struct TransferSystemReqBufferInfo
{
    VkBuffer BufferHandle{ VK_NULL_HANDLE };
    BufferCreateInfo CreateInfo;
};

struct TransferSystemReqImageInfo
{
    VkImage ImageHandle{ VK_NULL_HANDLE };
    ImageCreateInfo CreateInfo;
    // the layout the image is in when the transfer starts, and the layout the transfer leaves it in. the worker
    // derives both from the caller's usages, and the transfer thread uses them as they come
    VkImageLayout BeforeLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout AfterLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

struct TransferSystemSetBufferDataMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    InternalResourceDataContainer Data;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemSetImageDataMessage
{
    TransferSystemReqImageInfo ImageInfo;
    InternalResourceDataContainer Data;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemFillBufferMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    uint32_t Value{ 0u };
    size_t Offset{ 0u };
    size_t Size{ 0u };
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyBufferToBufferMessage
{
    TransferSystemReqBufferInfo SrcBufferInfo;
    TransferSystemReqBufferInfo DstBufferInfo;
    // the worker checked every region, and the block is never empty
    CopyRegionBlock Regions;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyImageToImageMessage
{
    TransferSystemReqImageInfo SrcImageInfo;
    TransferSystemReqImageInfo DstImageInfo;
    // the worker checked every region, and the block is never empty
    ImageCopyRegionBlock Regions;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyImageToBufferMessage
{
    TransferSystemReqImageInfo ImageInfo;
    TransferSystemReqBufferInfo BufferInfo;
    // the worker checked every region, and the block is never empty
    BufferImageRegionBlock Regions;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

struct TransferSystemCopyBufferToImageMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    TransferSystemReqImageInfo ImageInfo;
    // the worker checked every region, and the block is never empty
    BufferImageRegionBlock Regions;
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits;
};

/** @brief One op of a transfer set after the worker resolved and checked it: the API handles, the aspects and the layouts
 *  that the transfer thread uses as they are. A side that the kind does not use has null handles. The source buffer of an
 *  upload is the staging buffer, which only the transfer thread knows, so it is not here. */
struct ResolvedTransferOp
{
    TransferOpKind Kind{ TransferOpKind::Invalid };
    VkBuffer SourceBuffer{ VK_NULL_HANDLE };
    VkBuffer DestinationBuffer{ VK_NULL_HANDLE };
    VkImage SourceImage{ VK_NULL_HANDLE };
    VkImage DestinationImage{ VK_NULL_HANDLE };
    // every aspect that the format has, as a barrier lists them
    VkImageAspectFlags SourceAspect{ 0u };
    VkImageAspectFlags DestinationAspect{ 0u };
    VkImageLayout SourceBeforeLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout SourceAfterLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout DestinationBeforeLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout DestinationAfterLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
    BufferCopyRegion Buffer;
    BufferImageRegion BufferImage;
    ImageCopyRegion Image;
    uint32_t FillValue{ 0u };
};

/** @brief A transfer set for the transfer thread. The worker checked every op. `Set` is kept for its range ends, and
 *  `Data` is the bytes of the uploads: the transfer thread stages them once, and the ops read them by offset.
 *  `StagingAlignment` is a multiple of the buffer offset alignment of every image upload in the set. */
struct TransferSystemTransferSetMessage // 144 -> 152
{
    std::unique_ptr<ResolvedTransferOp[]> Ops;
    int32_t OpCount{ 0 };
    TransferSetBlock Set;
    ByteBlock Data;
    uint64_t StagingAlignment{ 1u };
    int32_t Future{ -1 };
    uint64_t Ticket{ 0u };
    TransferWaitSet Waits{};
};

/** @brief Tells the transfer thread to return, so that it can block on an empty queue instead of polling a flag. */
struct TransferSystemExitMessage
{
};

using TransferPayloadType = std::variant<TransferSystemSetBufferDataMessage,
                                         TransferSystemSetImageDataMessage,
                                         TransferSystemFillBufferMessage,
                                         TransferSystemCopyBufferToBufferMessage,
                                         TransferSystemCopyImageToImageMessage,
                                         TransferSystemCopyImageToBufferMessage,
                                         TransferSystemCopyBufferToImageMessage,
                                         TransferSystemTransferSetMessage,
                                         TransferSystemExitMessage>;

#endif // !DIAMOND_DOGS_TRANSFER_SYSTEM_MESSAGES_INTERNAL_HPP
