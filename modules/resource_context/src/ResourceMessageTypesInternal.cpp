#include "ResourceMessageTypesInternal.hpp"

SetBufferDataMessage::SetBufferDataMessage(size_t numData, const gpu_resource_data_t* data) noexcept :
    data{ InternalResourceDataContainer(numData, data) }
{}

SetBufferDataMessage::SetBufferDataMessage(GraphicsResource _destBuffer, InternalResourceDataContainer&& _data) noexcept :
    destBuffer{ _destBuffer },
    data{ std::move(_data) }
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

SetImageDataMessage::SetImageDataMessage(GraphicsResource _destImage, size_t numData, const gpu_image_resource_data_t* data) :
    destImage{ _destImage },
    data{ InternalResourceDataContainer(numData, data) }
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
