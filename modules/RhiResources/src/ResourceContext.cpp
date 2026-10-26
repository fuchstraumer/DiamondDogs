#include "ResourceContext.hpp"
#include "ResourceContextImpl.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

ResourceContext::ResourceContext()
{
    impl = std::make_unique<ResourceContextImpl>();
}

ResourceContext::~ResourceContext()
{
    // fixed: the destructor did nothing, so the worker threads were still joinable when impl was destroyed (std::terminate)
    impl->Destroy();
}

void ResourceContext::Initialize(const ResourceContextCreateInfo& create_info)
{
    impl->Construct(create_info);
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateBuffer(
    const BufferCreateInfo& create_info,
    std::span<const RhiBufferResourceData> initial_data)
{
    CreateBufferMessage message;
    message.BufferInfo = create_info;
    if (!initial_data.empty())
    {
        message.InitialData = InternalResourceDataContainer(initial_data);
    }

    // replies that may reach the transfer system own a timeline semaphore, which the transfer signals
    message.Reply = std::make_shared<GraphicsResourceReply>(create_info.Domain, ResourceType::Buffer, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateImage(
    const ImageCreateInfo& create_info,
    std::span<const RhiImageResourceData> initial_data)
{
    CreateImageMessage message;
    message.ImageInfo = create_info;
    if (!initial_data.empty())
    {
        message.InitialData = InternalResourceDataContainer(initial_data);
    }

    const ResourceType resourceType = create_info.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    message.Reply = std::make_shared<GraphicsResourceReply>(create_info.Domain, resourceType, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateSampler(
    const SamplerCreateInfo& create_info,
    std::string_view sampler_name)
{
    CreateSamplerMessage message;
    message.SamplerInfo = create_info;
    message.Name = sampler_name;
    message.Reply = std::make_shared<GraphicsResourceReply>(ResourceDomain::GPUOnly, ResourceType::Sampler);
    std::shared_ptr<GraphicsResourceReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::SetBufferData(
    GraphicsResource buffer,
    std::span<const RhiBufferResourceData> data)
{
    SetBufferDataMessage message(buffer, data);
    message.Reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::SetImageData(
    GraphicsResource image,
    std::span<const RhiImageResourceData> data)
{
    SetImageDataMessage message(image, data);
    message.Reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::FillBuffer(
    GraphicsResource buffer,
    uint32_t value,
    size_t offset,
    size_t size)
{
    FillResourceMessage message;
    message.Resource = buffer;
    message.Value = value;
    message.Offset = offset;
    message.Size = size;
    message.Reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<PointerMessageReply> ResourceContext::MapBuffer(
    GraphicsResource buffer,
    size_t size,
    size_t offset)
{
    MapResourceMessage message;
    message.Resource = buffer;
    message.Size = size;
    message.Offset = offset;
    message.Reply = std::make_shared<PointerMessageReply>();
    std::shared_ptr<PointerMessageReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<MessageReply> ResourceContext::UnmapBuffer(
    GraphicsResource buffer,
    size_t size,
    size_t offset)
{
    UnmapResourceMessage message;
    message.Resource = buffer;
    message.Size = size;
    message.Offset = offset;
    message.Reply = std::make_shared<MessageReply>();
    std::shared_ptr<MessageReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CopyBuffer(
    GraphicsResource src,
    bool copy_contents)
{
    CopyResourceMessage message;
    message.SourceResource = src;
    message.CopyContents = copy_contents;
    message.Reply = std::make_shared<GraphicsResourceReply>(src.Domain, ResourceType::Buffer, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CopyImage(GraphicsResource src_image)
{
    // fixed: declared, never defined
    CopyResourceMessage message;
    message.SourceResource = src_image;
    message.CopyContents = true;
    message.Reply = std::make_shared<GraphicsResourceReply>(src_image.Domain, src_image.Type, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::CopyBufferContents(
    GraphicsResource src,
    GraphicsResource dst)
{
    CopyResourceContentsMessage message;
    message.SourceResource = src;
    message.DestinationResource = dst;
    message.Reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}

std::shared_ptr<MessageReply> ResourceContext::DestroyResource(
    GraphicsResource resource)
{
    DestroyResourceMessage message;
    message.Resource = resource;
    message.Reply = std::make_shared<MessageReply>();
    std::shared_ptr<MessageReply> reply = message.Reply;

    impl->PushMessage(std::move(message));

    return reply;
}
