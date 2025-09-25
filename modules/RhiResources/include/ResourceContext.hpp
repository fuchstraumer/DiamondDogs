#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ForwardDecl.hpp"
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include <vulkan/vulkan.h>
#include <memory>

/**
 * @brief Configuration structure for initializing a ResourceContext
 * 
 * Chosen so we can expand configuration options in the future without breaking API
 */
struct ResourceContextCreateInfo
{
    vpr::Device* logicalDevice;        ///< The Vulkan logical device to use for resource operations
    vpr::PhysicalDevice* physicalDevice; ///< The Vulkan physical device for memory allocation
    bool validationEnabled;            ///< Whether Vulkan validation layers are enabled
};

/// Semaphore value used to signal when transfer operations are complete
constexpr static uint64_t k_TransferCompleteSemaphoreValue = 2u;

class ResourceContextImpl;

/**
 * @brief Main interface for asynchronous GPU resource management
 * 
 * ResourceContext provides a high-level, thread-safe interface for creating and managing
 * Vulkan resources such as buffers, images, and samplers. All operations are asynchronous
 * and return reply objects that can be used to track completion status, and internal state
 * and resources are managed using an ECS (ENTT, to be specific).
 */
class ResourceContext
{
public:
    /**
     * @brief Default constructor
     * 
     * Creates an uninitialized ResourceContext. You must call Initialize() before using any other methods.
     */
    ResourceContext();
    
    /**
     * @brief Destructor
     * 
     * Automatically waits for all pending operations to complete and cleans up resources.
     */
    ~ResourceContext();
    
    /// Copy constructor deleted - ResourceContext is not copyable
    ResourceContext(const ResourceContext&) = delete;
    /// Copy assignment deleted - ResourceContext is not copyable
    ResourceContext& operator=(const ResourceContext&) = delete;

    /**
     * @brief Initialize the ResourceContext with the given configuration
     * 
     * @param createInfo Configuration structure containing device pointers and options
     * 
     * @note Must be called before any other operations, as this starts background worker threads for processing resource operations
     */
    void Initialize(const ResourceContextCreateInfo& createInfo);

    /**
     * @brief Create a Vulkan buffer with optional buffer view and initial data. If initialData
     * is provided, the buffer will be created in a transferring state and will not be complete
     * until this is finished. Additionally, buffer views are only created if viewCreateInfo is provided.
     * 
     * @param createInfo Vulkan buffer creation parameters
     * @param viewCreateInfo Optional buffer view creation parameters (can be nullptr)
     * @param initialData Optional array of initial data to upload to the buffer
     * @param numData Number of elements in the initialData array
     * @param resourceUsage Memory usage pattern for the buffer (GPU/CPU accessibility)
     * @param flags Additional creation flags controlling behavior
     * @param userData Optional user data pointer associated with this resource
     * @return Shared pointer to reply object for tracking creation progress
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateBuffer(
        const BufferCreateInfo& createInfo,
        const RhiBufferResourceData* initialData = nullptr,
        size_t numData = 0,
        void* userData = nullptr);

    /**
     * @brief Create a Vulkan image with optional image view and initial data.
     * Image views are only created if viewCreateInfo is provided, and layout transitions
     * will also be handled automatically if initialData is provided.
     * 
     * @param createInfo Vulkan image creation parameters
     * @param viewCreateInfo Optional image view creation parameters (can be nullptr)
     * @param initialData Optional array of initial data to upload to the image
     * @param numData Number of elements in the initialData array
     * @param resourceUsage Memory usage pattern for the image (GPU/CPU accessibility)
     * @param flags Additional creation flags controlling behavior
     * @param userData Optional user data pointer associated with this resource
     * @return Shared pointer to reply object for tracking creation progress
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateImage(
        const VkImageCreateInfo& createInfo,
        const VkImageViewCreateInfo* viewCreateInfo = nullptr,
        const RhiImageResourceData* initialData = nullptr,
        size_t numData = 0,
        resource_usage resourceUsage = resource_usage::GPUOnly,
        resource_creation_flags flags = 0,
        void* userData = nullptr);

    /**
     * @brief Create a Vulkan sampler
     * 
     * @param createInfo Vulkan sampler creation parameters
     * @param userData Optional user data pointer associated with this resource
     * @return Shared pointer to reply object for tracking creation progress
     * 
     * @note Sampler creation is typically fast as it doesn't involve memory allocation
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateSampler(
        const VkSamplerCreateInfo& createInfo,
        void* userData = nullptr);

    /**
     * @brief Upload data to an existing buffer.
     * This operation may use staging buffers for GPU-only resources, briefly inflating memory usage. For
     * CPU-accessible buffers, data is written directly to mapped memory. Multiple data regions can be 
     * uploaded in a single operation.
     * 
     * @param buffer The target buffer resource to update
     * @param data Array of data structures containing the data to upload
     * @param numData Number of elements in the data array
     * @return Shared pointer to reply object for tracking transfer progress
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetBufferData(
        GraphicsResource buffer,
        const RhiBufferResourceData* data,
        size_t numData);

    /**
     * @brief Upload data to an existing image, including specific mip levels and array layers.
     * Uses staging buffers and performs layout transitions as needed.
     * 
     * @param image The target image resource to update
     * @param data Array of image data structures containing the data to upload
     * @param numData Number of elements in the data array
     * @return Shared pointer to reply object for tracking transfer progress
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetImageData(
        GraphicsResource image,
        const RhiImageResourceData* data,
        size_t numData);
        
    /**
     * @brief Fill a buffer region with a repeating 32-bit value
     * 
     * @param buffer The target buffer resource to fill
     * @param value The 32-bit value to repeat throughout the region
     * @param offset Byte offset from the start of the buffer
     * @param size Number of bytes to fill (must be multiple of 4)
     * @return Shared pointer to reply object for tracking transfer progress
     * 
     * @note This is implemented using vkCmdFillBuffer for optimal performance, so the size parameter must be a multiple of 4 bytes
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> FillBuffer(
        GraphicsResource buffer,
        uint32_t value,
        size_t offset,
        size_t size);
        
    /**
     * @brief Map a buffer region into CPU-accessible memory
     * 
     * @param buffer The buffer resource to map
     * @param size Number of bytes to map
     * @param offset Byte offset from the start of the buffer
     * @return Shared pointer to reply object containing the mapped pointer
     * 
     * @note Only works with CPU-accessible buffers (CPUOnly, CPUToGPU, GPUToCPU usage) and multiple overlapping map operations are not supported
     */
    [[nodiscard]] std::shared_ptr<PointerMessageReply> MapBuffer(
        GraphicsResource buffer,
        size_t size,
        size_t offset);
        
    /**
     * @brief Unmap a previously mapped buffer region
     * 
     * @param buffer The buffer resource to unmap
     * @param size Number of bytes that were mapped
     * @param offset Byte offset that was mapped
     * @return Shared pointer to reply object for tracking completion
     */
    [[nodiscard]] std::shared_ptr<MessageReply> UnmapBuffer(
        GraphicsResource buffer, 
        size_t size,
        size_t offset);

    /**
     * @brief Create a copy of an existing buffer
     * 
     * @param srcBuffer The source buffer to copy from
     * @param copyContents Whether to copy the buffer's contents or just create an empty buffer with the same properties
     * @return Shared pointer to reply object containing the new buffer resource
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyBuffer(GraphicsResource srcBuffer, bool copyContents);

    /**
     * @brief Create a copy of an existing image
     * 
     * @param srcImage The source image to copy from
     * @return Shared pointer to reply object containing the new image resource
     * 
     * @note Proper image layout transitions are handled automatically
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyImage(GraphicsResource srcImage);
    
    /**
     * @brief Copy contents from one buffer to another existing buffer
     * 
     * @param srcBuffer The source buffer to copy from
     * @param destBuffer The destination buffer to copy to
     * @return Shared pointer to reply object for tracking transfer progress
     * 
     * @note The entire source buffer is copied to the destination buffer
     * @note Uses GPU-side copy operations for optimal performance
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> CopyBufferContents(
        GraphicsResource srcBuffer,
        GraphicsResource destBuffer);

    /**
     * @brief Destroy a graphics resource and free its memory
     * 
     * @param resource The resource to destroy
     * @return Shared pointer to reply object for tracking completion
     * 
     * @note All associated Vulkan objects (buffer/image, view, sampler) are destroyed, with memory returned to the allocator
     * @note It's safe to destroy resources that are still being used by in-flight GPU commands
     */
    [[nodiscard]] std::shared_ptr<MessageReply> DestroyResource(
        GraphicsResource resource);

private:
    std::unique_ptr<ResourceContextImpl> impl;
};

#endif //!DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
