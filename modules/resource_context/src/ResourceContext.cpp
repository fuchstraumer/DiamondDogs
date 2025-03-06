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

std::shared_ptr<ResourceMessageReply<VulkanResource*>> ResourceContext::CreateBuffer(
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
        message.initialData = InternalResourceDataContainer(numData, initialData, initialData[0].DestinationQueueFamily);
    }

    message.resourceUsage = resourceUsage;
    message.flags = flags;
    message.userData = userData;
    message.reply = std::make_shared<ResourceMessageReply<VulkanResource*>>();
    std::shared_ptr<ResourceMessageReply<VulkanResource*>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<VulkanResource*>> ResourceContext::CreateImage(
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
        message.initialData = InternalResourceDataContainer(numData, initialData, initialData[0].DestinationQueueFamily);
    }

    message.resourceUsage = resourceUsage;
    message.flags = flags;
    message.userData = userData;
    message.reply = std::make_shared<ResourceMessageReply<VulkanResource*>>();
    std::shared_ptr<ResourceMessageReply<VulkanResource*>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::SetBufferData(
    VulkanResource* buffer,
    const gpu_resource_data_t* data,
    size_t numData)
{
    SetBufferDataMessage message(numData, data);
    message.destBuffer = buffer;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::SetImageData(
    VulkanResource* image,
    const gpu_image_resource_data_t* data,
    size_t numData)
{   
    SetImageDataMessage message(numData, data);
    message.image = image;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::FillBuffer(
    VulkanResource* buffer,
    uint32_t value,
    size_t offset,
    size_t size)
{   
    FillResourceMessage message;
    message.resource = buffer;
    message.value = value;
    message.offset = offset;
    message.size = size;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<void*>> ResourceContext::MapBuffer(
    VulkanResource* buffer,
    size_t size,
    size_t offset)
{
    MapResourceMessage message;
    message.resource = buffer;
    message.size = size;
    message.offset = offset;
    message.reply = std::make_shared<ResourceMessageReply<void*>>();
    std::shared_ptr<ResourceMessageReply<void*>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::UnmapBuffer(
    VulkanResource* buffer,
    size_t size,
    size_t offset)
{
    UnmapResourceMessage message;
    message.resource = buffer;
    message.size = size;
    message.offset = offset;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::CopyBuffer(
    VulkanResource* src,
    VulkanResource* dst)
{
    CopyResourceMessage message;
    message.src = src;
    message.dest = dst;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::CopyBufferContents(
    VulkanResource* src,
    VulkanResource* dst)
{
    CopyResourceContentsMessage message;
    message.src = src;
    message.dest = dst;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}

std::shared_ptr<ResourceMessageReply<bool>> ResourceContext::DestroyResource(
    VulkanResource* resource)
{
    DestroyResourceMessage message;
    message.resource = resource;
    message.reply = std::make_shared<ResourceMessageReply<bool>>();
    std::shared_ptr<ResourceMessageReply<bool>> reply = message.reply;

    impl->pushMessage(std::move(message));

    return reply;
}
