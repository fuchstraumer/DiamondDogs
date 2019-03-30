#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ForwardDecl.hpp"
#include "Allocation.hpp"
#include "AllocationRequirements.hpp"
#include "ResourceTypes.hpp"
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <vulkan/vulkan.h>
#include <mutex>

struct VmaAllocationInfo;

class ResourceContext {
    ResourceContext(const ResourceContext&) = delete;
    ResourceContext& operator=(const ResourceContext&) = delete;
    // Resource context is bound to a single device and uses underlying physical device resources
    ResourceContext();
    ~ResourceContext();
public:

    static ResourceContext& Get();

    void Construct(vpr::Device* device, vpr::PhysicalDevice* physical_device);

    VulkanResource* CreateBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    void SetBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    void FillBuffer(VulkanResource* dest_buffer, const uint32_t value, const size_t offset, const size_t fill_size);
    VulkanResource* CreateImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    VulkanResource* CreateImageView(const VulkanResource* base_rsrc, const VkImageViewCreateInfo* view_info, void* user_data = nullptr);
    void SetImageData(VulkanResource* image, const size_t num_data, const gpu_image_resource_data_t* data);
    VulkanResource* CreateSampler(const VkSamplerCreateInfo* info, void* user_data = nullptr);
    VulkanResource* CreateCombinedImageSampler(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const VkSamplerCreateInfo* sampler_info, 
        const size_t num_data, const gpu_image_resource_data_t* initial_data, const resource_usage _resource_usage, const resource_creation_flags _flags, void* user_data = nullptr);
    VulkanResource* CreateResourceCopy(VulkanResource* src);
    void CopyResource(VulkanResource* src, VulkanResource** dest);
    void CopyResourceContents(VulkanResource* src, VulkanResource* dest);
    void DestroyResource(VulkanResource* resource);

    void* MapResourceMemory(VulkanResource* resource, size_t size, size_t offset);
    void UnmapResourceMemory(VulkanResource* resource, size_t size, size_t offset);

    // Call at start of frame
    void Update();

    void Destroy();
private:

    void setBufferInitialDataHostOnly(VulkanResource * resource, const size_t num_data, const gpu_resource_data_t * initial_data, uint64_t& alloc, resource_usage _resource_usage);
    void setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, uint64_t& alloc);
    void setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data, uint64_t& alloc);
    vpr::AllocationRequirements getAllocReqs(resource_usage _resource_usage) const noexcept;
    VkFormatFeatureFlags featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept;

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

    void destroyResource(resource_iter_t iter);
    void destroyBuffer(resource_iter_t iter);
    void destroyImage(resource_iter_t iter);
    void destroySampler(resource_iter_t iter);


    struct infoStorage {
        std::unordered_map<VulkanResource*, resource_usage> resourceMemoryType;
        std::unordered_map<VulkanResource*, VkBufferCreateInfo> bufferInfos;
        std::unordered_map<VulkanResource*, VkBufferViewCreateInfo> bufferViewInfos;
        std::unordered_map<VulkanResource*, VkImageCreateInfo> imageInfos;
        std::unordered_map<VulkanResource*, VkImageViewCreateInfo> imageViewInfos;
        std::unordered_map<VulkanResource*, VkSamplerCreateInfo> samplerInfos;
    } resourceInfos;

    std::unordered_map<VulkanResource*, std::string> resourceNames;
    std::unordered_map<VulkanResource*, uint64_t> resourceAllocations;
    // Resources that depend on the key for their Image handle, but which are still independent views
    // of the key, go here. When key is destroyed, we have to destroy all the views too.
    std::unordered_multimap<VulkanResource*, VulkanResource*> imageViews;
    std::unordered_map<uint64_t, std::unique_ptr<VmaAllocationInfo>> allocInfos;
    uint64_t allocatorHandle{ VK_NULL_HANDLE };
    std::shared_mutex containerMutex;
    const vpr::Device* device;

};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
