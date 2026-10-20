#include "ResourceMessageTypesInternal.hpp"
#include <cstring>


InternalResourceDataContainer::BufferData::BufferData(const RhiBufferResourceData& _data) :
    data{ std::make_unique<std::byte[]>(_data.Data.size()) },
    size{ _data.Data.size() },
    alignment{ _data.DataAlignment }
{
    std::memcpy(data.get(), _data.Data.data(), _data.Data.size());
}

InternalResourceDataContainer::BufferData::BufferData(BufferData&& other) noexcept :
    data{ std::move(other.data) },
    size{ other.size },
    alignment{ other.alignment }
{}

InternalResourceDataContainer::BufferData::BufferData() noexcept : data{ nullptr }, size{ 0 }, alignment{ 0 }
{}

InternalResourceDataContainer::InternalResourceDataContainer::BufferData& InternalResourceDataContainer::BufferData::operator=(BufferData&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    data = std::move(other.data);
    size = other.size;
    alignment = other.alignment;
    return *this;
}

InternalResourceDataContainer::ImageData::ImageData(const RhiImageResourceData& _data) :
    data{ std::make_unique<std::byte[]>(_data.Data.size()) },
    size{ _data.Data.size() },
    width{ _data.Width },
    height{ _data.Height },
    arrayLayer{ _data.ArrayLayer },
    numLayers{ _data.NumLayers },
    mipLevel{ _data.MipLevel }
{
    std::memcpy(data.get(), _data.Data.data(), _data.Data.size());
}

InternalResourceDataContainer::ImageData::ImageData(ImageData&& other) noexcept :
    data{ std::move(other.data) },
    size{ other.size },
    width{ other.width },
    height{ other.height },
    arrayLayer{ other.arrayLayer },
    numLayers{ other.numLayers },
    mipLevel{ other.mipLevel }
{}

InternalResourceDataContainer::ImageData::ImageData() noexcept :
    data{ nullptr },
    size{ 0 },
    width{ 0 },
    height{ 0 },
    arrayLayer{ 0 },
    numLayers{ 1 },
    mipLevel{ 0 }
{}

InternalResourceDataContainer::InternalResourceDataContainer::ImageData& InternalResourceDataContainer::ImageData::operator=(ImageData&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    data = std::move(other.data);
    size = other.size;
    width = other.width;
    height = other.height;
    arrayLayer = other.arrayLayer;
    numLayers = other.numLayers;
    mipLevel = other.mipLevel;
    return *this;
}

InternalResourceDataContainer::InternalResourceDataContainer() noexcept :
    NumLayers{ std::nullopt }
{}

InternalResourceDataContainer::InternalResourceDataContainer(std::span<const RhiImageResourceData> data)
{
    NumLayers = data[0].NumLayers;
    ImageDataVector imageData(data.size());
    for (size_t i = 0; i < data.size(); ++i)
    {
        imageData[i] = ImageData(data[i]);
    }
    DataVector = std::move(imageData);
}

InternalResourceDataContainer::InternalResourceDataContainer(std::span<const RhiBufferResourceData> data) :
    NumLayers{ std::nullopt }
{
    BufferDataVector bufferData(data.size());
    for (size_t i = 0; i < data.size(); ++i)
    {
        bufferData[i] = BufferData(data[i]);
    }
    DataVector = std::move(bufferData);
}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, InternalResourceDataContainer&& _data, std::shared_ptr<ResourceTransferReply>&& reply) noexcept :
    destBuffer{ _destBuffer },
    data{ std::move(_data) },
    reply{ std::move(reply) }
{}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, std::span<const RhiBufferResourceData> data) noexcept :
    destBuffer{ _destBuffer },
    data{ InternalResourceDataContainer(data) }
{}

// fixed: the move constructors and assignments dropped the reply, so the reply that SetBufferData returned never completed
SetBufferDataMessage::SetBufferDataMessage(SetBufferDataMessage&& other) noexcept :
    destBuffer{ other.destBuffer },
    data{ std::move(other.data) },
    reply{ std::move(other.reply) }
{}

SetBufferDataMessage& SetBufferDataMessage::operator=(SetBufferDataMessage&& other) noexcept
{
    if (this != &other)
    {
        destBuffer = other.destBuffer;
        data = std::move(other.data);
        reply = std::move(other.reply);
    }
    return *this;
}

SetImageDataMessage::SetImageDataMessage(SetImageDataMessage&& other) noexcept :
    destImage{ other.destImage },
    data{ std::move(other.data) },
    reply{ std::move(other.reply) }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, std::span<const RhiImageResourceData> data) noexcept :
    destImage{ _destImage },
    data{ InternalResourceDataContainer(data) }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, InternalResourceDataContainer&& _data, std::shared_ptr<ResourceTransferReply>&& reply) noexcept :
    destImage{ _destImage },
    data{ std::move(_data) },
    reply{ std::move(reply) }
{}

SetImageDataMessage& SetImageDataMessage::operator=(SetImageDataMessage&& other) noexcept
{
    if (this != &other)
    {
        destImage = other.destImage;
        data = std::move(other.data);
        reply = std::move(other.reply);
    }
    return *this;
}
