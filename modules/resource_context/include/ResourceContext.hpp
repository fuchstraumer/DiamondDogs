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

constexpr static uint64_t k_TransferCompleteSemaphoreValue = 2u;

class ResourceContextImpl;

class ResourceContext
{
public:
    ResourceContext();
    ~ResourceContext();
    ResourceContext(const ResourceContext&) = delete;
    ResourceContext& operator=(const ResourceContext&) = delete;

    void Initialize(const ResourceContextCreateInfo& createInfo);

    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateBuffer(
        const VkBufferCreateInfo& createInfo,
        const VkBufferViewCreateInfo* viewCreateInfo = nullptr,
        const gpu_resource_data_t* initialData = nullptr,
        size_t numData = 0,
        resource_usage resourceUsage = resource_usage::GPUOnly,
        resource_creation_flags flags = 0,
        void* userData = nullptr);

    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateImage(
        const VkImageCreateInfo& createInfo,
        const VkImageViewCreateInfo* viewCreateInfo = nullptr,
        const gpu_image_resource_data_t* initialData = nullptr,
        size_t numData = 0,
        resource_usage resourceUsage = resource_usage::GPUOnly,
        resource_creation_flags flags = 0,
        void* userData = nullptr);

    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateSampler(
        const VkSamplerCreateInfo& createInfo,
        void* userData = nullptr);

    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetBufferData(
        GraphicsResource buffer,
        const gpu_resource_data_t* data,
        size_t numData);

    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetImageData(
        GraphicsResource image,
        const gpu_image_resource_data_t* data,
        size_t numData);
        
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> FillBuffer(
        GraphicsResource buffer,
        uint32_t value,
        size_t offset,
        size_t size);
        
    [[nodiscard]] std::shared_ptr<PointerMessageReply> MapBuffer(
        GraphicsResource buffer,
        size_t size,
        size_t offset);
        
    [[nodiscard]] std::shared_ptr<MessageReply> UnmapBuffer(
        GraphicsResource buffer, 
        size_t size,
        size_t offset);

    // Creates a copy of the source buffer and returns it in the reply
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyBuffer(GraphicsResource srcBuffer, bool copyContents);

    // Creates a copy of the source image and returns it in the reply
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyImage(GraphicsResource srcImage);
    
    // Copies the contents of the source buffer to the destination buffer, but does not create a new buffer
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> CopyBufferContents(
        GraphicsResource srcBuffer,
        GraphicsResource destBuffer);

    [[nodiscard]] std::shared_ptr<MessageReply> DestroyResource(
        GraphicsResource resource);

private:
    std::unique_ptr<ResourceContextImpl> impl;
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
