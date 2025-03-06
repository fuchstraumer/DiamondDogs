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
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <vulkan/vulkan.h>
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include <vk_mem_alloc.h>

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

    template<typename T>
    void processMessage(T&& message);
    template<>
    void processMessage<CreateBufferMessage>(CreateBufferMessage&& message);
    template<>
    void processMessage<CreateImageMessage>(CreateImageMessage&& message);
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

    void processMessages();
 
    VulkanResource* createBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    void setBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    VulkanResource* createImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    VulkanResource* createImageView(const VulkanResource* base_rsrc, const VkImageViewCreateInfo* view_info, void* user_data = nullptr);
    VulkanResource* createSampler(const VkSamplerCreateInfo* info, const resource_creation_flags _flags, void* user_data = nullptr);
    void copyResourceContents(VulkanResource* src, VulkanResource* dst);
    void setBufferInitialDataHostOnly(VulkanResource * resource, InternalResourceDataContainer& dataContainer, resource_usage _resource_usage);
    void setBufferInitialDataUploadBuffer(VulkanResource* resource, InternalResourceDataContainer& dataContainer);
    void setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data);
    VkFormatFeatureFlags featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept;
    
    void writeStatsJsonFile(const char* output_file);

    std::unordered_set<std::unique_ptr<VulkanResource>> resources;
    using resource_iter_t = decltype(resources)::iterator;

    void createBufferResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createImageResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createSamplerResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createCombinedImageSamplerResourceCopy(VulkanResource * src, VulkanResource ** dest);

    void copyBufferContentsToBuffer(VulkanResource* src, VulkanResource* dst);
    void copyImageContentsToImage(VulkanResource* src, VulkanResource* dst, const VkImageSubresourceRange& src_range, const VkImageSubresourceRange& dst_range, const std::vector<VkImageCopy>& image_copies);
    void copyBufferContentsToImage(VulkanResource* src, VulkanResource* dst, const VkDeviceSize src_offset, const VkImageSubresourceRange& dst_range, const std::vector<VkBufferImageCopy>& copy_params);
    void copyImageContentsToBuffer(VulkanResource* src, VulkanResource* dst);

    void destroyResource(VulkanResource* rsrc);
    void* map(VulkanResource* resource, size_t size, size_t offset);
    void unmap(VulkanResource* resource, size_t size, size_t offset);
    void destroyBuffer(resource_iter_t iter);
    void destroyImage(resource_iter_t iter);
    void destroySampler(resource_iter_t iter);

    mwsrQueue<ResourceMessagePayloadType> messageQueue;
    std::thread workerThread;
    std::atomic<bool> shouldExitWorker{ false };

    std::unordered_map<VulkanResource*, std::string> resourceNames;
    std::unordered_map<VulkanResource*, VmaAllocation> resourceAllocations;
    // Resources that depend on the key for their Image handle, but which are still independent views
    // of the key, go here. When key is destroyed, we have to destroy all the views too.
    std::unordered_multimap<VulkanResource*, VulkanResource*> imageViews;
    std::unordered_map<VulkanResource*, VmaAllocationInfo> allocInfos;
    vpr::VkDebugUtilsFunctions vkDebugFns;
    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };
    std::shared_mutex containerMutex;
    const vpr::Device* device;
    bool validationEnabled{ false };

    struct infoStorage
    {
        void clear();
        std::unordered_map<VulkanResource*, resource_usage> resourceMemoryType;
        std::unordered_map<VulkanResource*, resource_creation_flags> resourceFlags;
        std::unordered_map<VulkanResource*, VkBufferCreateInfo> bufferInfos;
        std::unordered_map<VulkanResource*, VkBufferViewCreateInfo> bufferViewInfos;
        std::unordered_map<VulkanResource*, VkImageCreateInfo> imageInfos;
        std::unordered_map<VulkanResource*, VkImageViewCreateInfo> imageViewInfos;
        std::unordered_map<VulkanResource*, VkSamplerCreateInfo> samplerInfos;
    } resourceInfos;

};

template<>
inline void ResourceContextImpl::processMessage(CreateBufferMessage&& message)
{

}

template<>
inline void ResourceContextImpl::processMessage(CreateImageMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(SetBufferDataMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(SetImageDataMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(FillResourceMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(MapResourceMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(UnmapResourceMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(CopyResourceMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(CopyResourceContentsMessage&& message)
{
}

template<>
inline void ResourceContextImpl::processMessage(DestroyResourceMessage&& message)
{
}

#endif //!RESOURCE_CONTEXT_IMPL_HPP
