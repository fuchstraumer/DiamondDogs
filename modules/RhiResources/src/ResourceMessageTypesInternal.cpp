#include "ResourceMessageTypesInternal.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>


InternalResourceDataContainer::BufferData::BufferData(const RhiBufferResourceData& _data) :
    Data{ std::make_unique<std::byte[]>(_data.Data.size()) },
    Size{ _data.Data.size() },
    Alignment{ _data.DataAlignment }
{
    std::memcpy(Data.get(), _data.Data.data(), _data.Data.size());
}

InternalResourceDataContainer::BufferData::BufferData(BufferData&& other) noexcept :
    Data{ std::move(other.Data) },
    Size{ other.Size },
    Alignment{ other.Alignment }
{}

InternalResourceDataContainer::BufferData::BufferData() noexcept : Data{ nullptr }, Size{ 0 }, Alignment{ 0 }
{}

InternalResourceDataContainer::InternalResourceDataContainer::BufferData& InternalResourceDataContainer::BufferData::operator=(BufferData&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    Data = std::move(other.Data);
    Size = other.Size;
    Alignment = other.Alignment;
    return *this;
}

InternalResourceDataContainer::ImageData::ImageData(const RhiImageResourceData& _data) :
    Data{ std::make_unique<std::byte[]>(_data.Data.size()) },
    Size{ _data.Data.size() },
    Width{ _data.Width },
    Height{ _data.Height },
    ArrayLayer{ _data.ArrayLayer },
    NumLayers{ _data.NumLayers },
    MipLevel{ _data.MipLevel }
{
    std::memcpy(Data.get(), _data.Data.data(), _data.Data.size());
}

InternalResourceDataContainer::ImageData::ImageData(ImageData&& other) noexcept :
    Data{ std::move(other.Data) },
    Size{ other.Size },
    Width{ other.Width },
    Height{ other.Height },
    ArrayLayer{ other.ArrayLayer },
    NumLayers{ other.NumLayers },
    MipLevel{ other.MipLevel }
{}

InternalResourceDataContainer::ImageData::ImageData() noexcept :
    Data{ nullptr },
    Size{ 0 },
    Width{ 0 },
    Height{ 0 },
    ArrayLayer{ 0 },
    NumLayers{ 1 },
    MipLevel{ 0 }
{}

InternalResourceDataContainer::InternalResourceDataContainer::ImageData& InternalResourceDataContainer::ImageData::operator=(ImageData&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    Data = std::move(other.Data);
    Size = other.Size;
    Width = other.Width;
    Height = other.Height;
    ArrayLayer = other.ArrayLayer;
    NumLayers = other.NumLayers;
    MipLevel = other.MipLevel;
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
    DestBuffer{ _destBuffer },
    Data{ std::move(_data) },
    Reply{ std::move(reply) }
{}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, std::span<const RhiBufferResourceData> data) noexcept :
    DestBuffer{ _destBuffer },
    Data{ InternalResourceDataContainer(data) }
{}

// fixed: the move constructors and assignments dropped the reply, so the reply that SetBufferData returned never completed
SetBufferDataMessage::SetBufferDataMessage(SetBufferDataMessage&& other) noexcept :
    DestBuffer{ other.DestBuffer },
    Data{ std::move(other.Data) },
    Reply{ std::move(other.Reply) }
{}

SetBufferDataMessage& SetBufferDataMessage::operator=(SetBufferDataMessage&& other) noexcept
{
    if (this != &other)
    {
        DestBuffer = other.DestBuffer;
        Data = std::move(other.Data);
        Reply = std::move(other.Reply);
    }
    return *this;
}

SetImageDataMessage::SetImageDataMessage(SetImageDataMessage&& other) noexcept :
    DestImage{ other.DestImage },
    Data{ std::move(other.Data) },
    Reply{ std::move(other.Reply) }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, std::span<const RhiImageResourceData> data) noexcept :
    DestImage{ _destImage },
    Data{ InternalResourceDataContainer(data) }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, InternalResourceDataContainer&& _data, std::shared_ptr<ResourceTransferReply>&& reply) noexcept :
    DestImage{ _destImage },
    Data{ std::move(_data) },
    Reply{ std::move(reply) }
{}

SetImageDataMessage& SetImageDataMessage::operator=(SetImageDataMessage&& other) noexcept
{
    if (this != &other)
    {
        DestImage = other.DestImage;
        Data = std::move(other.Data);
        Reply = std::move(other.Reply);
    }
    return *this;
}
