#pragma once
#include "ResourceFlags.hpp"
#include "entt/entity/fwd.hpp"
#include <cstdint>
#ifndef RESOURCE_CONTEXT_IMPL_HPP
#define RESOURCE_CONTEXT_IMPL_HPP
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "TransferSystem.hpp"
#include "containers/mwsrQueue.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <entt/entity/registry.hpp>

struct ResourceContextCreateInfo;

namespace rhi
{
    class Device;
}

class ResourceContextImpl
{
public:

    void construct(const ResourceContextCreateInfo& create_info);
    void destroy();
    /**
     * @brief Locks current thread and forces it to wait for all pending operations to complete, unlike how it used to be (required per-frame ticks).
     * Useful for when you want to flush all operations, e.g, swapchain events or key engine boundary events.
    */
    void update();

    /** @brief Adds a message - using by-value since we will always be moving this data */
    void pushMessage(ResourceMessagePayloadType message);
    void setExitWorker();
    void startWorker();

    /** @brief Returns the device, so that ResourceContext can create replies that own a timeline semaphore. */
    [[nodiscard]] const rhi::Device* Device() const noexcept;

private:

    struct ResourceFlags
    {
        ResourceType type;
        ResourceCreationFlags flags;
        ResourceDomain domain;
    };

    // counts the vmaMapMemory calls that MapBuffer made and UnmapBuffer has not undone, so destroy can undo them.
    // VMA asserts when a mapped allocation is destroyed
    struct UserMapCount
    {
        int32_t Count{ 0 };
    };

    // the layout that the transfer system last left an image in. images start UNDEFINED
    struct TrackedImageLayout
    {
        VkImageLayout Layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    };

    void processMessages();
    void processMessage(ResourceMessagePayloadType&& message);

    // I don't want to blow up the header implementing the above functions, so they're redeclared here explicitly to be implemented in the .cpp. Sorry :(
    void processCreateBufferMessage(CreateBufferMessage&& message);
    void processCreateImageMessage(CreateImageMessage&& message);
    void processCreateSamplerMessage(CreateSamplerMessage&& message);
    void processSetBufferDataMessage(SetBufferDataMessage&& message);
    void processSetImageDataMessage(SetImageDataMessage&& message);
    void processFillResourceMessage(FillResourceMessage&& message);
    void processMapResourceMessage(MapResourceMessage&& message);
    void processUnmapResourceMessage(UnmapResourceMessage&& message);
    void processCopyResourceMessage(CopyResourceMessage&& message);
    void processCopyResourceContentsMessage(CopyResourceContentsMessage&& message);
    void processDestroyResourceMessage(DestroyResourceMessage&& message);

    /** @brief Creates the entity and every API object that the create info asks for. Leaves the reply alone, so that a
     *  copy can follow creation before the reply completes. */
    [[nodiscard]] std::optional<GraphicsResource> createBufferEntity(const BufferCreateInfo& info);
    [[nodiscard]] std::optional<GraphicsResource> createImageEntity(const ImageCreateInfo& info);
    static void completeWithResource(GraphicsResourceReply& reply, const GraphicsResource& resource) noexcept;

    VkBuffer createBuffer(entt::entity new_entity, const BufferCreateInfo& buffer_info);
    VkBufferView createBufferView(entt::entity new_entity, const BufferCreateInfo& buffer_info);
    VkImage createImage(entt::entity new_entity, const ImageCreateInfo& image_info);
    VkImageView createImageView(entt::entity new_entity, const ImageCreateInfo& image_info);
    VkSampler createSampler(entt::entity new_entity, const SamplerCreateInfo& sampler_info, ResourceCreationFlags flags, void* user_data_ptr);

    /** @brief Returns true when the allocation landed in host-visible memory, so data can be written without the transfer system. */
    [[nodiscard]] bool allocationIsHostVisible(entt::entity entity) const;
    // in case of host-visible buffers (e.g. UBOs and such) we just handle it in the resource context instead of the transfer system
    void setBufferDataHostOnly(entt::entity entity, InternalResourceDataContainer& data_container);
    void enqueueBufferUpload(entt::entity entity, InternalResourceDataContainer&& data, std::shared_ptr<ResourceTransferReply>&& reply);
    void enqueueImageUpload(entt::entity entity, InternalResourceDataContainer&& data, std::shared_ptr<ResourceTransferReply>&& reply);
    [[nodiscard]] TransferSystemReqBufferInfo bufferTransferInfo(entt::entity entity) const;
    /** @brief Returns the image's transfer info, and records the layout that the transfer will leave the image in. */
    [[nodiscard]] TransferSystemReqImageInfo imageTransferInfo(entt::entity entity);
    void setObjectName(VkObjectType object_type, uint64_t handle, const std::string& name) const;

    /** @brief Destroys every API object that the entity has, and the entity. Used for destroy messages, for creation
     *  failures part-way through, and for resources still alive at shutdown. */
    void destroyEntityResources(entt::entity entity);

    void writeStatsJsonFile(const char* output_file);

    mwsrQueue<ResourceMessagePayloadType> messageQueue;
    std::thread workerThread;

    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };

    // Primarily used to contain our info structures and allocation structures
    entt::registry resourceRegistry;
    ResourceTransferSystem transferSystem;

    const rhi::Device* device = nullptr;
    // ResourceContext assumes VK_SHARING_MODE_CONCURRENT across these, so that the transfer queue needs no ownership transfers
    std::vector<uint32_t> queueFamilyIndices;
    bool validationEnabled{ false };
};


#endif //!RESOURCE_CONTEXT_IMPL_HPP
