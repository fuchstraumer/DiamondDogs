#pragma once
#ifndef RESOURCE_CONTEXT_IMPL_HPP
#define RESOURCE_CONTEXT_IMPL_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "TransferSystem.hpp"
#include "ResourceLoader.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "vkAssert.hpp"
#include "UploadBuffer.hpp"
#include "VkDebugUtils.hpp"
#include "containers/mwsrQueue.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <entt/entt.hpp>

struct ResourceContextCreateInfo;

class ResourceContextImpl
{
public:

    void construct(const ResourceContextCreateInfo& createInfo);
    void destroy();
    // locks current thread and forces it to wait for all pending operations to complete, unlike how it used to be (required per-frame ticks)
    void update();

    void pushMessage(ResourceMessagePayloadType message);
    void setExitWorker();
    void startWorker();

private:

    struct ResourceFlags
    {
        resource_type type;
        resource_creation_flags flags;
        queue_family_flags queueFamilyFlags;
        resource_usage resourceUsage;
    };

    // just a tagged component, effectively, so I don't have to store std::string itself in the registry
    struct ResourceDebugName : public std::string
    {
    };

    // Templated to work with std::visit, so that the callsite isn't std::variant branching hellsite
    template<typename T>
    void processMessage(T&& message);
    template<>
    void processMessage<CreateBufferMessage>(CreateBufferMessage&& message);
    template<>
    void processMessage<CreateImageMessage>(CreateImageMessage&& message);
    template<>
    void processMessage<CreateCombinedImageSamplerMessage>(CreateCombinedImageSamplerMessage&& message);
    template<>
    void processMessage<CreateSamplerMessage>(CreateSamplerMessage&& message);
    template<>
    void processMessage<SetBufferDataMessage>(SetBufferDataMessage&& message);
    template<>
    void processMessage<SetImageDataMessage>(SetImageDataMessage&& message);
    template<>
    void processMessage<FillResourceMessage>(FillResourceMessage&& message);
    template<>
    void processMessage<MapResourceMessage>(MapResourceMessage&& message);
    template<>
    void processMessage<UnmapResourceMessage>(UnmapResourceMessage&& message);
    template<>
    void processMessage<CopyResourceMessage>(CopyResourceMessage&& message);
    template<>
    void processMessage<CopyResourceContentsMessage>(CopyResourceContentsMessage&& message);
    template<>
    void processMessage<DestroyResourceMessage>(DestroyResourceMessage&& message);

    // I don't want to blow up the header implementing the above functions, so they're redeclared here explicitly to be implemented in the .cpp. Sorry :(
    void processCreateBufferMessage(CreateBufferMessage&& message);
    void processCreateImageMessage(CreateImageMessage&& message);
    void processCreateCombinedImageSamplerMessage(CreateCombinedImageSamplerMessage&& message);
    void processCreateSamplerMessage(CreateSamplerMessage&& message);
    void processSetBufferDataMessage(SetBufferDataMessage&& message);
    void processSetImageDataMessage(SetImageDataMessage&& message);
    void processFillResourceMessage(FillResourceMessage&& message);
    void processMapResourceMessage(MapResourceMessage&& message);
    void processUnmapResourceMessage(UnmapResourceMessage&& message);
    void processCopyResourceMessage(CopyResourceMessage&& message);
    void processCopyResourceContentsMessage(CopyResourceContentsMessage&& message);
    void processDestroyResourceMessage(DestroyResourceMessage&& message);

    void processMessages();

    VkBuffer createBuffer(
        entt::entity new_entity,
        VkBufferCreateInfo&& buffer_info,
        const resource_creation_flags _flags,
        const resource_usage _resource_usage,
        void* user_data_ptr,
        bool has_initial_data);

    VkBufferView createBufferView(
        entt::entity new_entity,
        VkBufferViewCreateInfo&& view_info,
        const resource_creation_flags _flags,
        void* user_data_ptr);

    void setBufferData(
        entt::entity new_entity,
        VkBuffer buffer_handle,
        InternalResourceDataContainer& dataContainer,
        resource_usage _resource_usage);

    void setBufferDataHostOnly(entt::entity new_entity, InternalResourceDataContainer& dataContainer);

    void setBufferDataUploadBuffer(entt::entity new_entity, InternalResourceDataContainer& dataContainer);

    void fillBuffer(entt::entity new_entity, VkBuffer buffer_handle, uint32_t value, size_t offset, size_t size);

    VkImage createImage(
        entt::entity new_entity,
        VkImageCreateInfo&& image_info,
        const resource_creation_flags _flags,
        const resource_usage _resource_usage,
        void* user_data_ptr,
        bool has_initial_data);
 
    VkImageView createImageView(
        entt::entity new_entity,
        const VkImageViewCreateInfo& view_info,
        const resource_creation_flags _flags);

    void setImageData(
        entt::entity new_entity,
        VkImage image_handle,
        const VkImageCreateInfo& image_info,
        InternalResourceDataContainer& dataContainer,
        const resource_creation_flags _flags);

    VkSampler createSampler(
        entt::entity new_entity,
        const VkSamplerCreateInfo& sampler_info,
        const resource_creation_flags _flags,
        void* user_data_ptr);

    void setBufferData(GraphicsResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    GraphicsResource* createSampler(const VkSamplerCreateInfo* info, const resource_creation_flags _flags, void* user_data = nullptr);
    void copyResourceContents(GraphicsResource* src, GraphicsResource* dst);
    void setImageInitialData(GraphicsResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data);

    MessageReply::Status destroyBuffer(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyImage(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroySampler(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyBufferView(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyImageView(entt::entity entity, GraphicsResource resource);
    MessageReply::Status destroyCombinedImageSampler(entt::entity entity, GraphicsResource resource);

    VkFormatFeatureFlags featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept;
    
    void writeStatsJsonFile(const char* output_file);

    std::unordered_set<std::unique_ptr<GraphicsResource>> resources;

    void createBufferResourceCopy(GraphicsResource* src, GraphicsResource** dst);
    void createImageResourceCopy(GraphicsResource* src, GraphicsResource** dst);
    void createSamplerResourceCopy(GraphicsResource* src, GraphicsResource** dst);
    void createCombinedImageSamplerResourceCopy(GraphicsResource * src, GraphicsResource ** dest);

    void copyBufferContentsToBuffer(GraphicsResource* src, GraphicsResource* dst);
    void copyImageContentsToImage(GraphicsResource* src, GraphicsResource* dst, const VkImageSubresourceRange& src_range, const VkImageSubresourceRange& dst_range, const std::vector<VkImageCopy>& image_copies);
    void copyBufferContentsToImage(GraphicsResource* src, GraphicsResource* dst, const VkDeviceSize src_offset, const VkImageSubresourceRange& dst_range, const std::vector<VkBufferImageCopy>& copy_params);
    void copyImageContentsToBuffer(GraphicsResource* src, GraphicsResource* dst);

    void destroyResource(GraphicsResource* rsrc);
    void* map(GraphicsResource* resource, size_t size, size_t offset);
    void unmap(GraphicsResource* resource, size_t size, size_t offset);
    void destroyBuffer(resource_iter_t iter);
    void destroyImage(resource_iter_t iter);
    void destroySampler(resource_iter_t iter);

    mwsrQueue<ResourceMessagePayloadType> messageQueue;
    std::thread workerThread;
    std::atomic<bool> shouldExitWorker{ false };

    std::unordered_map<GraphicsResource*, std::string> resourceNames;
    std::unordered_map<GraphicsResource*, VmaAllocation> resourceAllocations;
    // Resources that depend on the key for their Image handle, but which are still independent views
    // of the key, go here. When key is destroyed, we have to destroy all the views too.
    std::unordered_multimap<GraphicsResource*, GraphicsResource*> imageViews;
    std::unordered_map<GraphicsResource*, VmaAllocationInfo> allocInfos;
    vpr::VkDebugUtilsFunctions vkDebugFns;
    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };

    // Primarily used to contain our info structures and allocation structures
    entt::registry resourceRegistry;
    ResourceTransferSystem transferSystem;

    const vpr::Device* device = nullptr;
    bool validationEnabled{ false };

};

template<>
inline void ResourceContextImpl::processMessage(CreateBufferMessage&& message)
{
    processCreateBufferMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(CreateImageMessage&& message)
{
    processCreateImageMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(CreateCombinedImageSamplerMessage&& message)
{
    processCreateCombinedImageSamplerMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(CreateSamplerMessage&& message)
{
    processCreateSamplerMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(SetBufferDataMessage&& message)
{
    processSetBufferDataMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(SetImageDataMessage&& message)
{
    processSetImageDataMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(FillResourceMessage&& message)
{
    processFillResourceMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(MapResourceMessage&& message)
{
    processMapResourceMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(UnmapResourceMessage&& message)
{
    processUnmapResourceMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(CopyResourceMessage&& message)
{
    processCopyResourceMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(CopyResourceContentsMessage&& message)
{
    processCopyResourceContentsMessage(std::move(message));
}

template<>
inline void ResourceContextImpl::processMessage(DestroyResourceMessage&& message)
{
    processDestroyResourceMessage(std::move(message));
}

#endif //!RESOURCE_CONTEXT_IMPL_HPP
