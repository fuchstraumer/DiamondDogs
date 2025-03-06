#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include <vulkan/vulkan.h>
#include <memory>

// Chosen so we can expand configuration options in the future without breaking API
struct ResourceContextCreateInfo
{
    vpr::Device* logicalDevice;
    vpr::PhysicalDevice* physicalDevice;
    bool validationEnabled;
};

class ResourceContextImpl;

class ResourceContext
{
public:
    ResourceContext();
    ~ResourceContext();
    ResourceContext(const ResourceContext&) = delete;
    ResourceContext& operator=(const ResourceContext&) = delete;

    void Initialize(const ResourceContextCreateInfo& createInfo);

    [[nodiscard]] std::shared_ptr<ResourceMessageReply<VulkanResource*>> CreateBuffer(
        const VkBufferCreateInfo& createInfo,
        const VkBufferViewCreateInfo* viewCreateInfo = nullptr,
        const gpu_resource_data_t* initialData = nullptr,
        size_t numData = 0,
        resource_usage resourceUsage = resource_usage::GPUOnly,
        resource_creation_flags flags = 0,
        void* userData = nullptr);

    [[nodiscard]] std::shared_ptr<ResourceMessageReply<VulkanResource*>> CreateImage(
        const VkImageCreateInfo& createInfo,
        const VkImageViewCreateInfo* viewCreateInfo = nullptr,
        const gpu_image_resource_data_t* initialData = nullptr,
        size_t numData = 0,
        resource_usage resourceUsage = resource_usage::GPUOnly,
        resource_creation_flags flags = 0,
        void* userData = nullptr);

    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> SetBufferData(
        VulkanResource* buffer,
        const gpu_resource_data_t* data,
        size_t numData);

    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> SetImageData(
        VulkanResource* image,
        const gpu_image_resource_data_t* data,
        size_t numData);
        
    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> FillBuffer(
        VulkanResource* buffer,
        uint32_t value,
        size_t offset,
        size_t size);
        
    [[nodiscard]] std::shared_ptr<ResourceMessageReply<void*>> MapBuffer(
        VulkanResource* buffer,
        size_t size,
        size_t offset);
        
    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> UnmapBuffer(
        VulkanResource* buffer, 
        size_t size,
        size_t offset);

    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> CopyBuffer(
        VulkanResource* srcBuffer,
        VulkanResource* destBuffer);
        
    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> CopyBufferContents(
        VulkanResource* srcBuffer,
        VulkanResource* destBuffer);

    [[nodiscard]] std::shared_ptr<ResourceMessageReply<bool>> DestroyResource(
        VulkanResource* resource);    

private:
    std::unique_ptr<ResourceContextImpl> impl;
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
