#pragma once
#ifndef RESOURCE_CONTEXT_IMPL_HPP
#define RESOURCE_CONTEXT_IMPL_HPP
#include "ResourceTypes.hpp"
#include "TransferSystem.hpp"
#include "ResourceLoader.hpp"
#include "Allocator.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "vkAssert.hpp"
#include "UploadBuffer.hpp"
#include <vector>
#include <algorithm>
#include "easylogging++.h"
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

struct ResourceContextImpl
{

    void construct(vpr::Device * _device, vpr::PhysicalDevice * physical_device);
    void destroy();
    // called per-frame (at the start of the frame) to flush transfers and take care of resizing the containers when we're able to do so safely
    void update();

    VulkanResource* createBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    void setBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    VulkanResource* createImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    VulkanResource* createImageView(const VulkanResource* base_rsrc, const VkImageViewCreateInfo* view_info, void* user_data = nullptr);
    VulkanResource* createSampler(const VkSamplerCreateInfo* info, void* user_data = nullptr);
    void copyResourceContents(VulkanResource* src, VulkanResource* dst);
    // using uint64_t for allocator handle to keep this header clean. we should probably move this to an impl pointer
    void setBufferInitialDataHostOnly(VulkanResource * resource, const size_t num_data, const gpu_resource_data_t * initial_data, VmaAllocation& alloc, resource_usage _resource_usage);
    void setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, VmaAllocation& alloc);
    void setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data, VmaAllocation& alloc);
    vpr::AllocationRequirements getAllocReqs(resource_usage _resource_usage) const noexcept;
    VkFormatFeatureFlags featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept;
	void writeStatsJsonFile(const char* output_file);

    std::unordered_set<std::unique_ptr<VulkanResource>> resources;
    using resource_iter_t = decltype(resources)::iterator;

    void createBufferResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createImageResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createSamplerResourceCopy(VulkanResource* src, VulkanResource** dst);
    void createCombinedImageSamplerResourceCopy(VulkanResource * src, VulkanResource ** dest);

    void copyBufferContentsToBuffer(VulkanResource * src, VulkanResource * dst);
    void copyImageContentsToImage(VulkanResource * src, VulkanResource * dst);
    void copyBufferContentsToImage(VulkanResource * src, VulkanResource * dst);
    void copyImageContentsToBuffer(VulkanResource * src, VulkanResource * dst);

    void destroyResource(VulkanResource* rsrc);
    void* map(VulkanResource* resource, size_t size, size_t offset);
    void unmap(VulkanResource* resource, size_t size, size_t offset);
    void destroyBuffer(resource_iter_t iter);
    void destroyImage(resource_iter_t iter);
    void destroySampler(resource_iter_t iter);

    // Called every N frames (quantity to be adjusted, but at least 10) to see if we should resize the internal containers,
    // so we'll lock the mutex and resize. This way, we can get room back AND make sure we do so at a time where we won't invalidate iterators-in-use
    bool rehashContainers() noexcept;
    void reserveSpaceInContainers(size_t count);

    struct infoStorage {
        bool mayNeedRehash(const size_t headroom) const noexcept;
        void reserve(size_t count);
		void clear();
        std::unordered_map<VulkanResource*, resource_usage> resourceMemoryType;
		std::unordered_map<VulkanResource*, resource_creation_flags> resourceFlags;
        std::unordered_map<VulkanResource*, VkBufferCreateInfo> bufferInfos;
        std::unordered_map<VulkanResource*, VkBufferViewCreateInfo> bufferViewInfos;
        std::unordered_map<VulkanResource*, VkImageCreateInfo> imageInfos;
        std::unordered_map<VulkanResource*, VkImageViewCreateInfo> imageViewInfos;
        std::unordered_map<VulkanResource*, VkSamplerCreateInfo> samplerInfos;
    } resourceInfos;

    size_t maxContainerDelta{ 0u };
    size_t prevContainerMaxSize{ 0u };
    std::unordered_map<VulkanResource*, std::string> resourceNames;
    std::unordered_map<VulkanResource*, VmaAllocation> resourceAllocations;
    // Resources that depend on the key for their Image handle, but which are still independent views
    // of the key, go here. When key is destroyed, we have to destroy all the views too.
    std::unordered_multimap<VulkanResource*, VulkanResource*> imageViews;
    std::unordered_map<VulkanResource*, VmaAllocationInfo> allocInfos;
    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };
    std::shared_mutex containerMutex;
    const vpr::Device* device;

};

#endif //!RESOURCE_CONTEXT_IMPL_HPP
