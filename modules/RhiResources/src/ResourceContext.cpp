#include "ResourceContext.hpp"
#include "ResourceContextImpl.hpp"
#include "ResourceMessageTypesInternal.hpp"

ResourceContext::ResourceContext()
{
    impl = std::make_unique<ResourceContextImpl>();
}

ResourceContext::~ResourceContext()
{
    // fixed: the destructor did nothing, so the worker threads were still joinable when impl was destroyed (std::terminate)
    impl->destroy();
}

void ResourceContext::Initialize(const ResourceContextCreateInfo& create_info)
{
    impl->construct(create_info);
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateBuffer(
    const BufferCreateInfo& create_info,
    std::span<const RhiBufferResourceData> initial_data)
{
    CreateBufferMessage message;
    message.bufferInfo = create_info;
    if (!initial_data.empty())
    {
        message.initialData = InternalResourceDataContainer(initial_data);
    }

    // replies that may reach the transfer system own a timeline semaphore, which the transfer signals
    message.reply = std::make_shared<GraphicsResourceReply>(create_info.Domain, ResourceType::Buffer, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateImage(
    const ImageCreateInfo& create_info,
    std::span<const RhiImageResourceData> initial_data)
{
    CreateImageMessage message;
    message.imageInfo = create_info;
    if (!initial_data.empty())
    {
        message.initialData = InternalResourceDataContainer(initial_data);
    }

    const ResourceType resourceType = create_info.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    message.reply = std::make_shared<GraphicsResourceReply>(create_info.Domain, resourceType, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateSampler(
    const SamplerCreateInfo& create_info,
    void* user_data)
{
    CreateSamplerMessage message;
    message.samplerInfo = create_info;
    message.userData = user_data;
    message.reply = std::make_shared<GraphicsResourceReply>(ResourceDomain::GPUOnly, ResourceType::Sampler);
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::SetBufferData(
    GraphicsResource buffer,
    std::span<const RhiBufferResourceData> data)
{
    SetBufferDataMessage message(buffer, data);
    message.reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::SetImageData(
    GraphicsResource image,
    std::span<const RhiImageResourceData> data)
{
    SetImageDataMessage message(image, data);
    message.reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::FillBuffer(
    GraphicsResource buffer,
    uint32_t value,
    size_t offset,
    size_t size)
{
    FillResourceMessage message;
    message.resource = buffer;
    message.value = value;
    message.offset = offset;
    message.size = size;
    message.reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<PointerMessageReply> ResourceContext::MapBuffer(
    GraphicsResource buffer,
    size_t size,
    size_t offset)
{
    MapResourceMessage message;
    message.resource = buffer;
    message.size = size;
    message.offset = offset;
    message.reply = std::make_shared<PointerMessageReply>();
    std::shared_ptr<PointerMessageReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<MessageReply> ResourceContext::UnmapBuffer(
    GraphicsResource buffer,
    size_t size,
    size_t offset)
{
    UnmapResourceMessage message;
    message.resource = buffer;
    message.size = size;
    message.offset = offset;
    message.reply = std::make_shared<MessageReply>();
    std::shared_ptr<MessageReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CopyBuffer(
    GraphicsResource src,
    bool copy_contents)
{
    CopyResourceMessage message;
    message.sourceResource = src;
    message.copyContents = copy_contents;
    message.reply = std::make_shared<GraphicsResourceReply>(src.Domain, ResourceType::Buffer, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CopyImage(GraphicsResource src_image)
{
    // fixed: declared, never defined
    CopyResourceMessage message;
    message.sourceResource = src_image;
    message.copyContents = true;
    message.reply = std::make_shared<GraphicsResourceReply>(src_image.Domain, src_image.Type, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::CopyBufferContents(
    GraphicsResource src,
    GraphicsResource dst)
{
    CopyResourceContentsMessage message;
    message.sourceResource = src;
    message.destinationResource = dst;
    message.reply = std::make_shared<ResourceTransferReply>(impl->Device());
    std::shared_ptr<ResourceTransferReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<MessageReply> ResourceContext::DestroyResource(
    GraphicsResource resource)
{
    DestroyResourceMessage message;
    message.resource = resource;
    message.reply = std::make_shared<MessageReply>();
    std::shared_ptr<MessageReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}
