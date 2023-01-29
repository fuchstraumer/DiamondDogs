#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include <vulkan/vulkan.h>
#include <memory>

struct VmaAllocationInfo;
struct ResourceContextImpl;

class ResourceContext {
    ResourceContext(const ResourceContext&) = delete;
    ResourceContext& operator=(const ResourceContext&) = delete;
    // Resource context is bound to a single device and uses underlying physical device resources
    ResourceContext();
    ~ResourceContext();
public:

    static ResourceContext& Get();

    void Initialize(vpr::Device* device, vpr::PhysicalDevice* physical_device, bool validation_enabled);
    // Call at start of frame
    void Update();
    void Destroy();

    VulkanResource* CreateBuffer(
        const VkBufferCreateInfo* info,
        const VkBufferViewCreateInfo* view_info,
        const size_t num_data,
        const gpu_resource_data_t* initial_data,
        const resource_usage _resource_usage,
        const resource_creation_flags _flags,
        void* user_data = nullptr);
    void SetBufferData(VulkanResource* dest_buffer, const size_t num_data, const gpu_resource_data_t* data);
    void FillBuffer(VulkanResource* dest_buffer, const uint32_t value, const size_t offset, const size_t fill_size);
    VulkanResource* CreateImage(
        const VkImageCreateInfo* info,
        const VkImageViewCreateInfo* view_info,
        const size_t num_data,
        const gpu_image_resource_data_t* initial_data,
        const resource_usage _resource_usage,
        const resource_creation_flags _flags,
        void* user_data = nullptr);
    VulkanResource* CreateImageView(const VulkanResource* base_rsrc, const VkImageViewCreateInfo* view_info, void* user_data = nullptr);
    void SetImageData(VulkanResource* image, const size_t num_data, const gpu_image_resource_data_t* data);
    VulkanResource* CreateSampler(const VkSamplerCreateInfo* info, const resource_creation_flags _flags, void* user_data = nullptr);
    VulkanResource* CreateCombinedImageSampler(
        const VkImageCreateInfo* info,
        const VkImageViewCreateInfo* view_info,
        const VkSamplerCreateInfo* sampler_info,
        const size_t num_data,
        const gpu_image_resource_data_t* initial_data,
        const resource_usage _resource_usage,
        const resource_creation_flags _flags,
        void* user_data = nullptr);
    VulkanResource* CreateResourceCopy(VulkanResource* src);
    void CopyResource(VulkanResource* src, VulkanResource** dest);
    void CopyResourceContents(VulkanResource* src, VulkanResource* dest);
    void DestroyResource(VulkanResource* resource);
    void WriteMemoryStatsFile(const char* output_file);
    bool ResourceInTransferQueue(VulkanResource* rsrc);

    void* MapResourceMemory(VulkanResource* resource, size_t size, size_t offset);
    void UnmapResourceMemory(VulkanResource* resource, size_t size, size_t offset);


private:
    std::unique_ptr<ResourceContextImpl> impl;
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
