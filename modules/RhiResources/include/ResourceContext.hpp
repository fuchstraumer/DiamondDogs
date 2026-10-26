#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#define DIAMOND_DOGS_RESOURCE_CONTEXT_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>

namespace rhi
{
    class Device;
    class Instance;
}

/**
 * @brief Configuration structure for initializing a ResourceContext
 * 
 * Chosen so we can expand configuration options in the future without breaking API
 */
struct ResourceContextCreateInfo
{
    const rhi::Instance* Instance{ nullptr };   ///< The instance that created Device, used to create the allocator
    const rhi::Device* Device{ nullptr };       ///< The logical device to use for resource operations
    uint32_t VulkanApiVersion{ 0u };            ///< The packed API version the instance was created with (RhiSystem::GetVulkanApiVersion)
    bool ValidationEnabled{ false };            ///< Whether Vulkan validation layers are enabled
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
     * @brief Create a buffer, with a buffer view when `createInfo.ViewFormat` is set. If initial_data is provided,
     * the buffer is created in a transferring state and is not complete until the upload finishes. Host-visible
     * buffers are written directly; device-local buffers go through the transfer system.
     *
     * @param create_info Domain, usage, size and creation flags. `UserData` is the name string when the flags
     * contain `UserDataAsNameString`.
     * @param initial_data Initial data regions, written one after another from offset 0. Empty for none.
     * @return Shared pointer to reply object for tracking creation progress
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateBuffer(
        const BufferCreateInfo& create_info,
        std::span<const RhiBufferResourceData> initial_data);

    /**
     * @brief Create an image, with a view when `createInfo.ViewInfo` is set and a sampler when
     * `createInfo.SamplerInfo` is set (the resource type is then CombinedImageSampler). With initialData, the
     * transfer system uploads the data and moves the image to the layout that its usage asks for.
     *
     * @param create_info Domain, usage, format, extent and optional view and sampler
     * @param initial_data Initial data, one element per mip level and array layer range. Empty for none.
     * @return Shared pointer to reply object for tracking creation progress
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateImage(
        const ImageCreateInfo& create_info,
        std::span<const RhiImageResourceData> initial_data);

    /**
     * @brief Create a sampler
     *
     * @param create_info Sampler creation parameters
     * @param sampler_name Optional name for the sampler resource
     * @return Shared pointer to reply object for tracking creation progress
     *
     * @note Sampler creation is typically fast as it doesn't involve memory allocation
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CreateSampler(
        const SamplerCreateInfo& create_info,
        std::string_view sampler_name = {});

    /**
     * @brief Upload data to an existing buffer.
     * This operation may use staging buffers for GPU-only resources, briefly inflating memory usage. For
     * CPU-accessible buffers, data is written directly to mapped memory. Multiple data regions can be 
     * uploaded in a single operation.
     * 
     * @param buffer The target buffer resource to update
     * @param data The data regions to upload
     * @return Shared pointer to reply object for tracking transfer progress
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetBufferData(
        GraphicsResource buffer,
        std::span<const RhiBufferResourceData> data);

    /**
     * @brief Upload data to an existing image, including specific mip levels and array layers.
     * Uses staging buffers and performs layout transitions as needed.
     * 
     * @param image The target image resource to update
     * @param data The data regions to upload
     * @return Shared pointer to reply object for tracking transfer progress
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> SetImageData(
        GraphicsResource image,
        std::span<const RhiImageResourceData> data);
        
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
     * @param size The size of the region to map
     * @param offset The byte offset from the start of the buffer
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
     * @param src The source buffer to copy from
     * @param copy_contents Whether to copy the buffer's contents or just create an empty buffer with the same properties
     * @return Shared pointer to reply object containing the new buffer resource
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyBuffer(GraphicsResource src, bool copy_contents);

    /**
     * @brief Create a copy of an existing image
     * 
     * @param src The source image to copy from
     * @return Shared pointer to reply object containing the new image resource
     * 
     * @note Proper image layout transitions are handled automatically
     */
    [[nodiscard]] std::shared_ptr<GraphicsResourceReply> CopyImage(GraphicsResource src);
    
    /**
     * @brief Copy contents from one buffer to another existing buffer
     * 
     * @param src The source buffer to copy from
     * @param dst The destination buffer to copy to
     * @return Shared pointer to reply object for tracking transfer progress
     * 
     * @note The entire source buffer is copied to the destination buffer
     * @note Uses GPU-side copy operations for optimal performance
     * @note Either side may be an image: image to buffer copies mip 0 of every layer, tightly packed from offset 0
     *       (the readback path; the image needs TransferSource usage and the buffer CopyDestination), and buffer to
     *       image is the reverse
     */
    [[nodiscard]] std::shared_ptr<ResourceTransferReply> CopyBufferContents(
        GraphicsResource src,
        GraphicsResource dst);

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
