#pragma once
#ifndef RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
#define RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include <vulkan/vulkan_core.h>
#include <memory>
#include <variant>
#include <optional>
#include <vector>
#include <limits>

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
        BufferData(const gpu_resource_data_t& _data);
        ~BufferData() = default;

        BufferData(const BufferData&) = delete;
        BufferData& operator=(const BufferData&) = delete;

        BufferData(BufferData&& other) noexcept;
        BufferData& operator=(BufferData&& other) noexcept;

        std::unique_ptr<std::byte[]> data;
        size_t size;
        size_t alignment;
    };
    using BufferDataVector = std::vector<BufferData>;
    // Images have a bit more metadata, so we need this struct for them
    struct ImageData
    {
        ImageData() noexcept;
        ImageData(const gpu_image_resource_data_t& _data);
        ~ImageData() = default;

        ImageData(const ImageData&) = delete;
        ImageData& operator=(const ImageData&) = delete;

        ImageData(ImageData&& other) noexcept;
        ImageData& operator=(ImageData&& other) noexcept;

        std::unique_ptr<std::byte[]> data;
        // size of the actual image data, not width/height info
        size_t size;
        uint32_t width;
        uint32_t height;
        uint32_t arrayLayer;
        uint32_t mipLevel;
    };
    using ImageDataVector = std::vector<ImageData>;
    
    std::variant<BufferDataVector, ImageDataVector> DataVector;
    std::optional<uint32_t> NumLayers;

    InternalResourceDataContainer(size_t numData, const gpu_resource_data_t* data);

    InternalResourceDataContainer(size_t numData, const gpu_image_resource_data_t* data);

    InternalResourceDataContainer() noexcept;
};

struct CreateBufferMessage
{
    VkBufferCreateInfo bufferInfo;
    std::optional<VkBufferViewCreateInfo> viewInfo = std::nullopt;
    std::optional<InternalResourceDataContainer> initialData = std::nullopt;
    resource_usage resourceUsage;
    resource_creation_flags flags;
    void* userData = nullptr;
    std::shared_ptr<GraphicsResourceReply> reply = nullptr;
};

struct CreateImageMessage
{
    VkImageCreateInfo imageInfo;
    std::optional<VkImageViewCreateInfo> viewInfo = std::nullopt;
    std::optional<InternalResourceDataContainer> initialData = std::nullopt;
    resource_usage resourceUsage;
    resource_creation_flags flags;
    void* userData = nullptr;
    std::shared_ptr<GraphicsResourceReply> reply = nullptr;
};

struct CreateCombinedImageSamplerMessage
{
    VkImageCreateInfo imageInfo;
    VkImageViewCreateInfo viewInfo;
    VkSamplerCreateInfo samplerInfo;
    std::optional<InternalResourceDataContainer> initialData = std::nullopt;
    resource_usage resourceUsage;
    resource_creation_flags flags;
    void* userData = nullptr;
    std::shared_ptr<GraphicsResourceReply> reply = nullptr;
};

struct CreateSamplerMessage
{
    VkSamplerCreateInfo samplerInfo;
    resource_creation_flags flags;
    void* userData = nullptr;
    std::shared_ptr<GraphicsResourceReply> reply = nullptr;
};

struct SetBufferDataMessage
{
    SetBufferDataMessage() noexcept = default;
    SetBufferDataMessage(
        GraphicsResource _destBuffer,
        size_t numData,
        const gpu_resource_data_t* data) noexcept;
    SetBufferDataMessage(
        GraphicsResource _destBuffer,
        InternalResourceDataContainer&& _data,
        std::shared_ptr<ResourceTransferReply>&& reply) noexcept;
    SetBufferDataMessage(const SetBufferDataMessage&) = delete;
    SetBufferDataMessage& operator=(const SetBufferDataMessage&) = delete;
    SetBufferDataMessage(SetBufferDataMessage&& other) noexcept;
    SetBufferDataMessage& operator=(SetBufferDataMessage&& other) noexcept;
    
    GraphicsResource destBuffer{ GraphicsResource::Null() };
    InternalResourceDataContainer data;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct SetImageDataMessage
{
    SetImageDataMessage() noexcept = default;
    SetImageDataMessage(
        GraphicsResource _destImage,
        size_t numData,
        const gpu_image_resource_data_t* data) noexcept;
    // for use when transferring rest of work to the transfer system
    SetImageDataMessage(
        GraphicsResource _destImage,
        InternalResourceDataContainer&& _data,
        std::shared_ptr<ResourceTransferReply>&& reply) noexcept;
    SetImageDataMessage(const SetImageDataMessage&) = delete;
    SetImageDataMessage& operator=(const SetImageDataMessage&) = delete;
    SetImageDataMessage(SetImageDataMessage&& other) noexcept;
    SetImageDataMessage& operator=(SetImageDataMessage&& other) noexcept;
    
    GraphicsResource destImage{ GraphicsResource::Null() };
    InternalResourceDataContainer data;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct FillResourceMessage
{
    GraphicsResource resource{ GraphicsResource::Null() };
    uint32_t value{ 0 };
    size_t offset{ 0 };
    size_t size{ 0 };
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct MapResourceMessage
{
    GraphicsResource resource{ GraphicsResource::Null() };
    size_t size{ 0 };
    size_t offset{ 0 };
    std::shared_ptr<PointerMessageReply> reply = nullptr;
};

struct UnmapResourceMessage
{
    GraphicsResource resource{ GraphicsResource::Null() };
    size_t size{ 0 };
    size_t offset{ 0 };
    std::shared_ptr<MessageReply> reply = nullptr;
};

struct CopyResourceMessage
{
    GraphicsResource sourceResource{ GraphicsResource::Null() };
    // not copying contents just means using the source like a template I guess
    bool copyContents = false;
    std::shared_ptr<GraphicsResourceReply> reply = nullptr;
};

struct CopyResourceContentsMessage
{
    GraphicsResource sourceResource{ GraphicsResource::Null() };
    GraphicsResource destinationResource{ GraphicsResource::Null() };
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct DestroyResourceMessage
{
    GraphicsResource resource{ GraphicsResource::Null() };
    std::shared_ptr<MessageReply> reply = nullptr;
};

using ResourceMessagePayloadType = std::variant<
    CreateBufferMessage,
    CreateImageMessage,
    CreateCombinedImageSamplerMessage,
    CreateSamplerMessage,
    SetBufferDataMessage,
    SetImageDataMessage,
    FillResourceMessage,
    MapResourceMessage,
    UnmapResourceMessage,
    CopyResourceMessage,
    CopyResourceContentsMessage,
    DestroyResourceMessage>;

// following message types are broadly the same, but feature extra info to reduce need for thread sync or safety concerns
// with transfer system.

struct TransferSystemReqBufferInfo
{
    VkBuffer bufferHandle{ VK_NULL_HANDLE };
    VkBufferCreateInfo createInfo;
    resource_usage resourceUsage{ resource_usage::InvalidResourceUsage };
    resource_creation_flags flags{ static_cast<resource_creation_flags>(std::numeric_limits<uint32_t>::max()) };
};

struct TransferSystemReqImageInfo
{
    VkImage imageHandle{ VK_NULL_HANDLE };
    VkImageCreateInfo createInfo;
    resource_usage resourceUsage{ resource_usage::InvalidResourceUsage };
    resource_creation_flags flags{ static_cast<resource_creation_flags>(std::numeric_limits<uint32_t>::max()) };
};

struct TransferSystemSetBufferDataMessage
{
    TransferSystemReqBufferInfo bufferInfo;
    InternalResourceDataContainer data;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct TransferSystemSetImageDataMessage
{
    TransferSystemReqImageInfo imageInfo;
    InternalResourceDataContainer data;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct TransferSystemFillBufferMessage
{
    TransferSystemReqBufferInfo bufferInfo;
    uint32_t value{ 0 };
    size_t offset{ 0 };
    size_t size{ 0 };
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct TransferSystemCopyBufferToBufferMessage
{
    TransferSystemReqBufferInfo srcBufferInfo;
    TransferSystemReqBufferInfo dstBufferInfo;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct TransferSystemCopyImageToImageMessage
{
    TransferSystemReqImageInfo srcImageInfo;
    TransferSystemReqImageInfo dstImageInfo;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct TransferSystemCopyImageToBufferMessage
{
    TransferSystemReqImageInfo imageInfo;
    TransferSystemReqBufferInfo bufferInfo;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct TransferSystemCopyBufferToImageMessage
{
    TransferSystemReqBufferInfo bufferInfo;
    TransferSystemReqImageInfo imageInfo;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

using TransferPayloadType = std::variant<
    TransferSystemSetBufferDataMessage,
    TransferSystemSetImageDataMessage,
    TransferSystemFillBufferMessage,
    TransferSystemCopyBufferToBufferMessage,
    TransferSystemCopyImageToImageMessage,
    TransferSystemCopyImageToBufferMessage,
    TransferSystemCopyBufferToImageMessage>;


#endif // RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
