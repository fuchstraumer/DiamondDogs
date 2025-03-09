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

// Users still call functions with the old API on the surface, but we translate to these internal structures
// so that we can place them in the message queue (type erased by being shoved in a variant, effectively)

// Because users just provide pointers to raw data, we need to take a copy of the data so that exiting 
// the function once they finish enqueuing the message doesn't invalidate the data right before we use it.
// We'll free this as soon as it's uploaded to the GPU or a staging buffer
struct InternalResourceDataContainer
{

    struct BufferData
    {
        BufferData() noexcept : data{nullptr}, size{0}, alignment{0}
        {}

        BufferData(const BufferData&) = delete;
        BufferData& operator=(const BufferData&) = delete;

        BufferData(BufferData&& other) noexcept :
            data{std::move(other.data)},
            size{other.size},
            alignment{other.alignment} 
        {}

        BufferData& operator=(BufferData&& other) noexcept
        {
            data = std::move(other.data);
            size = other.size;
            alignment = other.alignment;
            return *this;
        }

        ~BufferData() = default;

        BufferData(const gpu_resource_data_t& _data) :
            data{std::make_unique<std::byte[]>(_data.DataSize)},
            size{_data.DataSize},
            alignment{_data.DataAlignment}
        {
            std::memcpy(data.get(), _data.Data, _data.DataSize);
        }

        std::unique_ptr<std::byte[]> data;
        size_t size;
        size_t alignment;
    };
    using BufferDataVector = std::vector<BufferData>;
    // Images have a bit more metadata, so we need this struct for them
    struct ImageData
    {
        ImageData() noexcept : data{nullptr}, size{0}, width{0}, height{0}, arrayLayer{0}, mipLevel{0} {}
        ImageData(const ImageData&) = delete;
        ImageData& operator=(const ImageData&) = delete;

        ImageData(ImageData&& other) noexcept :
            data{std::move(other.data)},
            size{other.size},
            width{other.width},
            height{other.height},
            arrayLayer{other.arrayLayer},
            mipLevel{other.mipLevel}
        {}

        ImageData& operator=(ImageData&& other) noexcept
        {
            data = std::move(other.data);
            size = other.size;
            width = other.width;
            height = other.height;
            arrayLayer = other.arrayLayer;
            mipLevel = other.mipLevel;
            return *this;
        }

        ~ImageData() = default;

        ImageData(const gpu_image_resource_data_t& _data) :
            data{std::make_unique<std::byte[]>(_data.DataSize)},
            size{_data.DataSize},
            width{_data.Width},
            height{_data.Height},
            arrayLayer{_data.ArrayLayer},
            mipLevel{_data.MipLevel}
        {
            std::memcpy(data.get(), _data.Data, _data.DataSize);
        }

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

    InternalResourceDataContainer(size_t numData, const gpu_resource_data_t* data) :
        NumLayers{ std::nullopt }
    {
        BufferDataVector buffer_data(numData);
        for (size_t i = 0; i < numData; ++i)
        {
            buffer_data[i] = BufferData(data[i]);
        }
        DataVector = std::move(buffer_data);
    }

    InternalResourceDataContainer(size_t numData, const gpu_image_resource_data_t* data)
    {
        NumLayers = data[0].NumLayers;
        ImageDataVector image_data(numData);
        for (size_t i = 0; i < numData; ++i)
        {
            image_data[i] = ImageData(data[i]);
        }
        DataVector = std::move(image_data);
    }
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
    SetBufferDataMessage(size_t numData, const gpu_resource_data_t* data) :
        data{ InternalResourceDataContainer(numData, data) }
    {}
    GraphicsResource destBuffer{ GraphicsResource::Null() };
    InternalResourceDataContainer data;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct SetImageDataMessage
{
    SetImageDataMessage(size_t numData, const gpu_image_resource_data_t* data) :
        data{ InternalResourceDataContainer(numData, data) }
    {}
    GraphicsResource image{ GraphicsResource::Null() };
    InternalResourceDataContainer data;
    std::shared_ptr<ResourceTransferReply> reply = nullptr;
};

struct FillResourceMessage
{
    GraphicsResource resource{ GraphicsResource::Null() };
    uint32_t value{ 0 };
    size_t offset{ 0 };
    size_t size{ 0 };
    std::shared_ptr<MessageReply> reply = nullptr;
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
    GraphicsResource src{ GraphicsResource::Null() };
    GraphicsResource dest{ GraphicsResource::Null() };
    std::shared_ptr<GraphicsResourceReply> reply = nullptr;
};

struct CopyResourceContentsMessage
{
    GraphicsResource src{ GraphicsResource::Null() };
    GraphicsResource dest{ GraphicsResource::Null() };
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


#endif // RESOURCE_MESSAGE_TYPES_INTERNAL_HPP
