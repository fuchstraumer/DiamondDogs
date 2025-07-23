#include "ResourceMessageTypesInternal.hpp"


InternalResourceDataContainer::BufferData::BufferData(const gpu_resource_data_t& _data) :
    data{ std::make_unique<std::byte[]>(_data.DataSize) },
    size{ _data.DataSize },
    alignment{ _data.DataAlignment }
{
    std::memcpy(data.get(), _data.Data, _data.DataSize);
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

InternalResourceDataContainer::ImageData::ImageData(const gpu_image_resource_data_t& _data) :
    data{ std::make_unique<std::byte[]>(_data.DataSize) },
    size{ _data.DataSize },
    width{ _data.Width },
    height{ _data.Height },
    arrayLayer{ _data.ArrayLayer },
    mipLevel{ _data.MipLevel }
{
    std::memcpy(data.get(), _data.Data, _data.DataSize);
}

InternalResourceDataContainer::ImageData::ImageData(ImageData&& other) noexcept :
    data{ std::move(other.data) },
    size{ other.size },
    width{ other.width },
    height{ other.height },
    arrayLayer{ other.arrayLayer },
    mipLevel{ other.mipLevel }
{}

InternalResourceDataContainer::ImageData::ImageData() noexcept :
    data{ nullptr },
    size{ 0 },
    width{ 0 },
    height{ 0 },
    arrayLayer{ 0 },
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
    mipLevel = other.mipLevel;
    return *this;
}

InternalResourceDataContainer::InternalResourceDataContainer() noexcept :
    NumLayers{ std::nullopt }
{}

InternalResourceDataContainer::InternalResourceDataContainer(size_t numData, const gpu_image_resource_data_t* data)
{
    NumLayers = data[0].NumLayers;
    ImageDataVector image_data(numData);
    for (size_t i = 0; i < numData; ++i)
    {
        image_data[i] = ImageData(data[i]);
    }
    DataVector = std::move(image_data);
}

InternalResourceDataContainer::InternalResourceDataContainer(size_t numData, const gpu_resource_data_t* data) :
    NumLayers{ std::nullopt }
{
    BufferDataVector buffer_data(numData);
    for (size_t i = 0; i < numData; ++i)
    {
        buffer_data[i] = BufferData(data[i]);
    }
    DataVector = std::move(buffer_data);
}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, InternalResourceDataContainer&& _data, std::shared_ptr<ResourceTransferReply>&& reply) noexcept :
    destBuffer{ _destBuffer },
    data{ std::move(_data) },
    reply{ std::move(reply) }
{}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, size_t numData, const gpu_resource_data_t* data) noexcept :
    destBuffer{ _destBuffer },
    data{ InternalResourceDataContainer(numData, data) }
{}

SetBufferDataMessage::SetBufferDataMessage(SetBufferDataMessage&& other) noexcept :
    destBuffer{ other.destBuffer },
    data{ std::move(other.data) }
{}

SetBufferDataMessage& SetBufferDataMessage::operator=(SetBufferDataMessage&& other) noexcept
{
    if (this != &other)
    {
        destBuffer = other.destBuffer;
        data = std::move(other.data);
    }
    return *this;
}

SetImageDataMessage::SetImageDataMessage(SetImageDataMessage&& other) noexcept :
    destImage{ other.destImage },
    data{ std::move(other.data) }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, size_t numData, const gpu_image_resource_data_t* data) noexcept :
    destImage{ _destImage },
    data{ InternalResourceDataContainer(numData, data) }
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
    }
    return *this;
}
