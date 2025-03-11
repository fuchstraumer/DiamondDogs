#include "ResourceContext.hpp"
#include "ResourceContextImpl.hpp"
#include "ResourceMessageTypesInternal.hpp"

ResourceContext::ResourceContext()
{
    impl = std::make_unique<ResourceContextImpl>();
}

ResourceContext::~ResourceContext()
{
}

void ResourceContext::Initialize(const ResourceContextCreateInfo& createInfo)
{
    impl->construct(createInfo);
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateBuffer(
    const VkBufferCreateInfo& createInfo,
    const VkBufferViewCreateInfo* viewCreateInfo,
    const gpu_resource_data_t* initialData,
    size_t numData,
    resource_usage resourceUsage,
    resource_creation_flags flags,
    void* userData)
{
    CreateBufferMessage message;
    message.bufferInfo = createInfo;
    message.viewInfo = viewCreateInfo ? std::optional<VkBufferViewCreateInfo>(*viewCreateInfo) : std::nullopt;
    if (numData > 0)
    {
        message.initialData = InternalResourceDataContainer(numData, initialData);
    }

    message.resourceUsage = resourceUsage;
    message.flags = flags;
    message.userData = userData;
    message.reply = std::make_shared<GraphicsResourceReply>();
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateImage(
    const VkImageCreateInfo& createInfo,
    const VkImageViewCreateInfo* viewCreateInfo,
    const gpu_image_resource_data_t* initialData,
    size_t numData,
    resource_usage resourceUsage,
    resource_creation_flags flags,
    void* userData) 
{
    CreateImageMessage message;
    message.imageInfo = createInfo;
    message.viewInfo = viewCreateInfo ? std::optional<VkImageViewCreateInfo>(*viewCreateInfo) : std::nullopt;
    if (numData > 0)
    {
        message.initialData = InternalResourceDataContainer(numData, initialData);
    }

    message.resourceUsage = resourceUsage;
    message.flags = flags;
    message.userData = userData;
    message.reply = std::make_shared<GraphicsResourceReply>();
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<GraphicsResourceReply> ResourceContext::CreateSampler(
    const VkSamplerCreateInfo& createInfo,
    void* userData)
{
    CreateSamplerMessage message;
    message.samplerInfo = createInfo;
    message.userData = userData;
    message.reply = std::make_shared<GraphicsResourceReply>();
    std::shared_ptr<GraphicsResourceReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::SetBufferData(
    GraphicsResource buffer,
    const gpu_resource_data_t* data,
    size_t numData)
{
    SetBufferDataMessage message(buffer, numData, data);
    message.reply = std::make_shared<ResourceTransferReply>();
    std::shared_ptr<ResourceTransferReply> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceTransferReply> ResourceContext::SetImageData(
    GraphicsResource image,
    const gpu_image_resource_data_t* data,
    size_t numData)
{   
    SetImageDataMessage message(image, numData, data);
    message.reply = std::make_shared<ResourceTransferReply>();
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
    message.reply = std::make_shared<ResourceTransferReply>();
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
    message.reply = std::make_shared<GraphicsResourceReply>();
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
    message.reply = std::make_shared<ResourceTransferReply>();
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
