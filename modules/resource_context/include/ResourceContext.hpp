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

class ResourceContext {
    ResourceContext(const ResourceContext&) = delete;
    ResourceContext& operator=(const ResourceContext&) = delete;
public:
    // Resource context is bound to a single device and uses underlying physical device resources
    ResourceContext(vpr::Device* device, vpr::PhysicalDevice* physical_device);
    ~ResourceContext();

    VulkanResource* CreateBuffer(const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const memory_type _memory_type, void* user_data = nullptr);
    VulkanResource* CreateNamedBuffer(const char* name, const VkBufferCreateInfo* info, const VkBufferViewCreateInfo* view_info, const size_t num_data, const gpu_resource_data_t* initial_data, const memory_type _memory_type, void* user_data = nullptr);
    void SetBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    void FillBuffer(VulkanResource* dest_buffer, const uint32_t value, const size_t offset, const size_t fill_size);
    VulkanResource* CreateImage(const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const memory_type _memory_type, void* user_data = nullptr);
    VulkanResource* CreateNamedImage(const char* name, const VkImageCreateInfo* info, const VkImageViewCreateInfo* view_info, const size_t num_data, const gpu_image_resource_data_t* initial_data, const memory_type _memory_type, void* user_data = nullptr);
    void SetImageData(VulkanResource* image, const size_t num_data, const gpu_image_resource_data_t* data);
    VulkanResource* CreateSampler(const VkSamplerCreateInfo* info, void* user_data = nullptr);
    VulkanResource* CreateResourceCopy(VulkanResource* src);
    void CopyResource(VulkanResource* src, VulkanResource* dest);
    void DestroyResource(VulkanResource* resource);

    void* MapResourceMemory(VulkanResource* resource, size_t size = 0, size_t offset = 0);
    void UnmapResourceMemory(VulkanResource* resource);

    // Call at start of frame
    void Update();
    // Call at end of frame
    void FlushStagingBuffers();

    void Destroy();
private:

    void setBufferInitialDataHostOnly(VulkanResource * resource, const size_t num_data, const gpu_resource_data_t * initial_data, vpr::Allocation& alloc, memory_type _memory_type);
    void setBufferInitialDataUploadBuffer(VulkanResource* resource, const size_t num_data, const gpu_resource_data_t* initial_data, vpr::Allocation& alloc);
    void setImageInitialData(VulkanResource* resource, const size_t num_data, const gpu_image_resource_data_t* initial_data, vpr::Allocation & alloc);
    vpr::AllocationRequirements getAllocReqs(memory_type _memory_type) const noexcept;
    VkFormatFeatureFlags featureFlagsFromUsage(const VkImageUsageFlags flags) const noexcept;


    std::unordered_set<std::unique_ptr<VulkanResource>> resources;
    using resource_iter_t = decltype(resources)::iterator;

    void destroyResource(resource_iter_t iter);
    void destroyBuffer(resource_iter_t iter);
    void destroyImage(resource_iter_t iter);
    void destroySampler(resource_iter_t iter);


    struct infoStorage {
        std::unordered_map<VulkanResource*, memory_type> resourceMemoryType;
        std::unordered_map<VulkanResource*, VkBufferCreateInfo> bufferInfos;
        std::unordered_map<VulkanResource*, VkBufferViewCreateInfo> bufferViewInfos;
        std::unordered_map<VulkanResource*, VkImageCreateInfo> imageInfos;
        std::unordered_map<VulkanResource*, VkImageViewCreateInfo> imageViewInfos;
        std::unordered_map<VulkanResource*, VkSamplerCreateInfo> samplerInfos;
    } resourceInfos;

    std::unordered_map<VulkanResource*, std::string> resourceNames;
    std::unordered_map<VulkanResource*, vpr::Allocation> resourceAllocations;
    std::unordered_map<VulkanResource*, VkMappedMemoryRange> mappedRanges;
    std::unordered_map<VulkanResource*, const void*> referencedLoaderAddresses;
    std::unique_ptr<vpr::Allocator> allocator;
    std::mutex containerMutex;
    const vpr::Device* device;

};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
