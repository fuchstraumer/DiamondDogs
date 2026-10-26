#pragma once
#ifndef RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
#define RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>
#include <vulkan/vulkan_core.h>

// Users still call functions with the old API on the surface, but we translate to these internal structures
// so that we can place them in the message queue (type erased by being shoved in a variant, effectively)

// Because users just provide pointers to raw data, we need to take a copy of the data so that exiting
// the function once they finish enqueuing the message doesn't invalidate the data right before we use it.
// We'll free this as soon as it's uploaded to the GPU or a staging buffer
struct InternalResourceDataContainer
{

    struct BufferData
    {
        BufferData() noexcept;
        BufferData(const RhiBufferResourceData& _data);
        ~BufferData() = default;

        BufferData(const BufferData&) = delete;
        BufferData& operator=(const BufferData&) = delete;

        BufferData(BufferData&& other) noexcept;
        BufferData& operator=(BufferData&& other) noexcept;

        std::unique_ptr<std::byte[]> Data{ nullptr };
        size_t Size{ 0u };
        size_t Alignment{ 0u };
    };

    using BufferDataVector = std::vector<BufferData>;

    // Images have a bit more metadata, so we need this struct for them
    struct ImageData
    {
        ImageData() noexcept;
        ImageData(const RhiImageResourceData& _data);
        ~ImageData() = default;

        ImageData(const ImageData&) = delete;
        ImageData& operator=(const ImageData&) = delete;

        ImageData(ImageData&& other) noexcept;
        ImageData& operator=(ImageData&& other) noexcept;

        std::unique_ptr<std::byte[]> Data{ nullptr };
        // size of the actual image data, not width/height info
        size_t Size{ 0u };
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        uint32_t ArrayLayer{ 0u };
        uint32_t NumLayers{ 0u };
        uint32_t MipLevel{ 0u };
    };

    using ImageDataVector = std::vector<ImageData>;

    std::variant<BufferDataVector, ImageDataVector> DataVector;
    std::optional<uint32_t> NumLayers;

    InternalResourceDataContainer(std::span<const RhiBufferResourceData> data);

    InternalResourceDataContainer(std::span<const RhiImageResourceData> data);

    InternalResourceDataContainer() noexcept;
};

struct CreateBufferMessage
{
    BufferCreateInfo BufferInfo;
    std::optional<InternalResourceDataContainer> InitialData = std::nullopt;
    std::shared_ptr<GraphicsResourceReply> Reply{ nullptr };
};

// an image with a SamplerInfo becomes a CombinedImageSampler, which replaced
// CreateCombinedImageSamplerMessage
struct CreateImageMessage
{
    ImageCreateInfo ImageInfo;
    std::optional<InternalResourceDataContainer> InitialData = std::nullopt;
    std::shared_ptr<GraphicsResourceReply> Reply{ nullptr };
};

struct CreateSamplerMessage
{
    SamplerCreateInfo SamplerInfo;
    std::string_view Name;
    std::shared_ptr<GraphicsResourceReply> Reply{ nullptr };
};

struct SetBufferDataMessage
{
    SetBufferDataMessage() noexcept = default;
    SetBufferDataMessage(GraphicsResource _destBuffer, std::span<const RhiBufferResourceData> data) noexcept;
    SetBufferDataMessage(GraphicsResource _destBuffer,
                         InternalResourceDataContainer&& _data,
                         std::shared_ptr<ResourceTransferReply>&& reply) noexcept;
    SetBufferDataMessage(const SetBufferDataMessage&) = delete;
    SetBufferDataMessage& operator=(const SetBufferDataMessage&) = delete;
    SetBufferDataMessage(SetBufferDataMessage&& other) noexcept;
    SetBufferDataMessage& operator=(SetBufferDataMessage&& other) noexcept;

    GraphicsResource DestBuffer{ GraphicsResource::Null() };
    InternalResourceDataContainer Data;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct SetImageDataMessage
{
    SetImageDataMessage() noexcept = default;
    SetImageDataMessage(GraphicsResource _destImage, std::span<const RhiImageResourceData> data) noexcept;
    // for use when transferring rest of work to the transfer system
    SetImageDataMessage(GraphicsResource _destImage,
                        InternalResourceDataContainer&& _data,
                        std::shared_ptr<ResourceTransferReply>&& reply) noexcept;
    SetImageDataMessage(const SetImageDataMessage&) = delete;
    SetImageDataMessage& operator=(const SetImageDataMessage&) = delete;
    SetImageDataMessage(SetImageDataMessage&& other) noexcept;
    SetImageDataMessage& operator=(SetImageDataMessage&& other) noexcept;

    GraphicsResource DestImage{ GraphicsResource::Null() };
    InternalResourceDataContainer Data;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct FillResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    uint32_t Value{ 0 };
    size_t Offset{ 0 };
    size_t Size{ 0 };
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct MapResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    size_t Size{ 0 };
    size_t Offset{ 0 };
    std::shared_ptr<PointerMessageReply> Reply{ nullptr };
};

struct UnmapResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    size_t Size{ 0 };
    size_t Offset{ 0 };
    std::shared_ptr<MessageReply> Reply{ nullptr };
};

struct CopyResourceMessage
{
    GraphicsResource SourceResource{ GraphicsResource::Null() };
    // not copying contents just means using the source like a template I guess
    bool CopyContents = false;
    std::shared_ptr<GraphicsResourceReply> Reply{ nullptr };
};

struct CopyResourceContentsMessage
{
    GraphicsResource SourceResource{ GraphicsResource::Null() };
    GraphicsResource DestinationResource{ GraphicsResource::Null() };
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct DestroyResourceMessage
{
    GraphicsResource Resource{ GraphicsResource::Null() };
    std::shared_ptr<MessageReply> Reply{ nullptr };
};

/** @brief Tells the worker thread to return. Shutdown is a message so that the worker can block on the queue instead
 *  of polling a flag. */
struct ExitWorkerMessage
{
};

using ResourceMessagePayloadType = std::variant<CreateBufferMessage,
                                                CreateImageMessage,
                                                CreateSamplerMessage,
                                                SetBufferDataMessage,
                                                SetImageDataMessage,
                                                FillResourceMessage,
                                                MapResourceMessage,
                                                UnmapResourceMessage,
                                                CopyResourceMessage,
                                                CopyResourceContentsMessage,
                                                DestroyResourceMessage,
                                                ExitWorkerMessage>;

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
    // the layout image is in when transfer starts. UNDEFINED until the first upload, then the usage's layout
    VkImageLayout CurrentLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

struct TransferSystemSetBufferDataMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    InternalResourceDataContainer Data;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct TransferSystemSetImageDataMessage
{
    TransferSystemReqImageInfo ImageInfo;
    InternalResourceDataContainer Data;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct TransferSystemFillBufferMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    uint32_t Value{ 0u };
    size_t Offset{ 0u };
    size_t Size{ 0u };
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct TransferSystemCopyBufferToBufferMessage
{
    TransferSystemReqBufferInfo SrcBufferInfo;
    TransferSystemReqBufferInfo DstBufferInfo;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct TransferSystemCopyImageToImageMessage
{
    TransferSystemReqImageInfo SrcImageInfo;
    TransferSystemReqImageInfo DstImageInfo;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct TransferSystemCopyImageToBufferMessage
{
    TransferSystemReqImageInfo ImageInfo;
    TransferSystemReqBufferInfo BufferInfo;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

struct TransferSystemCopyBufferToImageMessage
{
    TransferSystemReqBufferInfo BufferInfo;
    TransferSystemReqImageInfo ImageInfo;
    std::shared_ptr<ResourceTransferReply> Reply{ nullptr };
};

using TransferPayloadType = std::variant<TransferSystemSetBufferDataMessage,
                                         TransferSystemSetImageDataMessage,
                                         TransferSystemFillBufferMessage,
                                         TransferSystemCopyBufferToBufferMessage,
                                         TransferSystemCopyImageToImageMessage,
                                         TransferSystemCopyImageToBufferMessage,
                                         TransferSystemCopyBufferToImageMessage>;

#endif // RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
