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

void ResourceContext::Initialize(const ResourceContextCreateInfo& createInfo)
{
    impl->construct(createInfo);
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateBuffer(
    const BufferCreateInfo& createInfo,
    std::span<const RhiBufferResourceData> initialData)
{
    CreateBufferMessage message;
    message.bufferInfo = createInfo;
    if (!initialData.empty())
    {
        message.initialData = InternalResourceDataContainer(initialData);
    }

    // replies that may reach the transfer system own a timeline semaphore, which the transfer signals
    message.reply = std::make_shared<GraphicsResourceReply>(createInfo.Domain, ResourceType::Buffer, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateImage(
    const ImageCreateInfo& createInfo,
    std::span<const RhiImageResourceData> initialData)
{
    CreateImageMessage message;
    message.imageInfo = createInfo;
    if (!initialData.empty())
    {
        message.initialData = InternalResourceDataContainer(initialData);
    }

    const ResourceType resourceType = createInfo.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    message.reply = std::make_shared<GraphicsResourceReply>(createInfo.Domain, resourceType, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateSampler(
    const SamplerCreateInfo& createInfo,
    void* userData)
{
    CreateSamplerMessage message;
    message.samplerInfo = createInfo;
    message.userData = userData;
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
    bool copyContents)
{
    CopyResourceMessage message;
    message.sourceResource = src;
    message.copyContents = copyContents;
    message.reply = std::make_shared<GraphicsResourceReply>(src.Domain, ResourceType::Buffer, impl->Device());
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CopyImage(GraphicsResource srcImage)
{
    // fixed: declared, never defined
    CopyResourceMessage message;
    message.sourceResource = srcImage;
    message.copyContents = true;
    message.reply = std::make_shared<GraphicsResourceReply>(srcImage.Domain, srcImage.Type, impl->Device());
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
