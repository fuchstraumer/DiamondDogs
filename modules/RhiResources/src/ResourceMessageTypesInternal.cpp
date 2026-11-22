#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
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


template<typename Region>
RegionBlock<Region>::RegionBlock(std::span<const Region> regions) :
    Items{ std::make_unique<Region[]>(regions.size()) },
    Count{ static_cast<int32_t>(regions.size()) }
{
    std::ranges::copy(regions, Items.get());
}

template<typename Region>
std::span<const Region> RegionBlock<Region>::View() const noexcept
{
    if (Items == nullptr)
    {
        return {};
    }

    return std::span<const Region>(Items.get(), static_cast<size_t>(Count));
}

template struct RegionBlock<BufferCopyRegion>;
template struct RegionBlock<BufferImageRegion>;
template struct RegionBlock<ImageCopyRegion>;

TransferSetBlock::TransferSetBlock(std::span<const TransferSetOp> ops, std::span<const int32_t> range_ends) :
    OpCount{ static_cast<int32_t>(ops.size()) },
    RangeCount{ static_cast<int32_t>(range_ends.size()) }
{
    // the op is 8-byte aligned and its size is a multiple of 8, so the range ends that follow the ops are 4-byte aligned
    static_assert(alignof(TransferSetOp) >= alignof(int32_t));
    static_assert(sizeof(TransferSetOp) % alignof(int32_t) == 0u);
    static_assert(std::is_trivially_copyable_v<TransferSetOp>);

    const size_t opBytes = ops.size() * sizeof(TransferSetOp);
    const size_t rangeBytes = range_ends.size() * sizeof(int32_t);
    Storage = std::make_unique<std::byte[]>(opBytes + rangeBytes);
    std::memcpy(Storage.get(), ops.data(), opBytes);
    std::memcpy(Storage.get() + opBytes, range_ends.data(), rangeBytes);
}

std::span<const TransferSetOp> TransferSetBlock::Ops() const noexcept
{
    if (Storage == nullptr)
    {
        return {};
    }

    return std::span<const TransferSetOp>(reinterpret_cast<const TransferSetOp*>(Storage.get()), static_cast<size_t>(OpCount));
}

std::span<const int32_t> TransferSetBlock::RangeEnds() const noexcept
{
    if (Storage == nullptr)
    {
        return {};
    }

    const size_t opBytes = static_cast<size_t>(OpCount) * sizeof(TransferSetOp);
    return std::span<const int32_t>(reinterpret_cast<const int32_t*>(Storage.get() + opBytes), static_cast<size_t>(RangeCount));
}

ByteBlock::ByteBlock(std::span<const std::byte> bytes) :
    Size{ static_cast<uint64_t>(bytes.size()) }
{
    if (bytes.empty())
    {
        return;
    }

    Items = std::make_unique<std::byte[]>(bytes.size());
    std::memcpy(Items.get(), bytes.data(), bytes.size());
}

std::span<const std::byte> ByteBlock::View() const noexcept
{
    if (Items == nullptr)
    {
        return {};
    }

    return std::span<const std::byte>(Items.get(), static_cast<size_t>(Size));
}

InternalResourceDataContainer::BufferData::BufferData(const RhiBufferResourceData& _data) :
    Data{ std::make_unique<std::byte[]>(_data.Data.size()) },
    Size{ _data.Data.size() },
    DestinationOffset{ _data.DestinationOffset }
{
    std::memcpy(Data.get(), _data.Data.data(), _data.Data.size());
}

InternalResourceDataContainer::BufferData::BufferData(BufferData&& other) noexcept :
    Data{ std::move(other.Data) },
    Size{ other.Size },
    DestinationOffset{ other.DestinationOffset }
{}

InternalResourceDataContainer::BufferData::BufferData() noexcept : Data{ nullptr }
{}

InternalResourceDataContainer::InternalResourceDataContainer::BufferData& InternalResourceDataContainer::BufferData::operator=(BufferData&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    Data = std::move(other.Data);
    Size = other.Size;
    DestinationOffset = other.DestinationOffset;
    return *this;
}

InternalResourceDataContainer::ImageData::ImageData(const RhiImageResourceData& _data) :
    Data{ std::make_unique<std::byte[]>(_data.Data.size()) },
    Size{ _data.Data.size() },
    Width{ _data.Width },
    Height{ _data.Height },
    Depth{ _data.Depth },
    X{ _data.X },
    Y{ _data.Y },
    Z{ _data.Z },
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
    Depth{ other.Depth },
    X{ other.X },
    Y{ other.Y },
    Z{ other.Z },
    ArrayLayer{ other.ArrayLayer },
    NumLayers{ other.NumLayers },
    MipLevel{ other.MipLevel }
{}

InternalResourceDataContainer::ImageData::ImageData() noexcept :
    Data{ nullptr },
    NumLayers{ 1 }
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
    Depth = other.Depth;
    X = other.X;
    Y = other.Y;
    Z = other.Z;
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
    if (!data.empty())
    {
        NumLayers = static_cast<uint32_t>(data[0].NumLayers);
    }

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
