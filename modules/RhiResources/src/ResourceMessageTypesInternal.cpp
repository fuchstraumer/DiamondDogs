#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

DebugName::DebugName(std::string_view name) noexcept
{
    // one byte stays free for the terminator, which the zero-initialized array already provides
    Length = static_cast<int8_t>(std::min(name.size(), static_cast<size_t>(k_Capacity - 1)));
    std::memcpy(Text.data(), name.data(), static_cast<size_t>(Length));
}

DebugName DebugName::WithSuffix(std::string_view suffix) const noexcept
{
    DebugName result = *this;
    const int8_t suffixLength = static_cast<int8_t>(std::min(suffix.size(), static_cast<size_t>(k_Capacity - 1 - Length)));
    std::memcpy(result.Text.data() + Length, suffix.data(), static_cast<size_t>(suffixLength));
    result.Length = static_cast<int8_t>(Length + suffixLength);
    return result;
}

std::string_view DebugName::View() const noexcept
{
    return std::string_view(Text.data(), static_cast<size_t>(Length));
}

const char* DebugName::CString() const noexcept
{
    return Text.data();
}

bool DebugName::Empty() const noexcept
{
    return Length == 0;
}


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

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, InternalResourceDataContainer&& _data, int32_t future_slot) noexcept :
    DestBuffer{ _destBuffer },
    Data{ std::move(_data) },
    Future{ future_slot }
{}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, std::span<const RhiBufferResourceData> data) noexcept :
    DestBuffer{ _destBuffer },
    Data{ InternalResourceDataContainer(data) }
{}

// fixed: the move constructors and assignments dropped the reply (now the future slot), so the reply that SetBufferData
// returned never completed
SetBufferDataMessage::SetBufferDataMessage(SetBufferDataMessage&& other) noexcept :
    DestBuffer{ other.DestBuffer },
    Data{ std::move(other.Data) },
    Waits{ other.Waits },
    Future{ other.Future }
{}

SetBufferDataMessage& SetBufferDataMessage::operator=(SetBufferDataMessage&& other) noexcept
{
    if (this != &other)
    {
        DestBuffer = other.DestBuffer;
        Data = std::move(other.Data);
        Waits = other.Waits;
        Future = other.Future;
    }
    return *this;
}

SetImageDataMessage::SetImageDataMessage(SetImageDataMessage&& other) noexcept :
    DestImage{ other.DestImage },
    Data{ std::move(other.Data) },
    Usage{ other.Usage },
    Waits{ other.Waits },
    Future{ other.Future }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, std::span<const RhiImageResourceData> data) noexcept :
    DestImage{ _destImage },
    Data{ InternalResourceDataContainer(data) }
{}

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, InternalResourceDataContainer&& _data, int32_t future_slot) noexcept :
    DestImage{ _destImage },
    Data{ std::move(_data) },
    Future{ future_slot }
{}

SetImageDataMessage& SetImageDataMessage::operator=(SetImageDataMessage&& other) noexcept
{
    if (this != &other)
    {
        DestImage = other.DestImage;
        Data = std::move(other.Data);
        Usage = other.Usage;
        Waits = other.Waits;
        Future = other.Future;
    }
    return *this;
}

CreateBufferMessage MakeCreateBufferMessage(const BufferCreateInfo& create_info, int32_t future_slot)
{
    CreateBufferMessage message;
    message.BufferInfo = create_info;
    message.Name = DebugName(create_info.Name);
    message.BufferInfo.Name = {};
    message.Future = future_slot;
    return message;
}

CreateImageMessage MakeCreateImageMessage(const ImageCreateInfo& create_info, int32_t future_slot)
{
    CreateImageMessage message;
    message.ImageInfo = create_info;
    message.Name = DebugName(create_info.Name);
    message.ImageInfo.Name = {};
    message.Future = future_slot;

    // the sampler takes its name from the image, so its own view is never read
    if (message.ImageInfo.SamplerInfo.has_value())
    {
        message.ImageInfo.SamplerInfo->Name = {};
    }

    return message;
}
