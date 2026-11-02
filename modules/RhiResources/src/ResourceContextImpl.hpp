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

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <entt/entity/registry.hpp>

struct ResourceContextCreateInfo;
struct ResourceContextCounters;
enum class ResourceContextError : uint8_t;

namespace rhi
{
    class Device;
}

class ResourceContextImpl
{
public:

    [[nodiscard]] ResourceContextError Construct(const ResourceContextCreateInfo& create_info);
    void Destroy();

    /** @brief Adds a message - using by-value since we will always be moving this data */
    void PushMessage(ResourceMessagePayloadType message);
    void SetExitWorker();
    void StartWorker();

    /** @brief Returns the device, so that ResourceContext can create replies that wait on the transfer timeline. */
    [[nodiscard]] const rhi::Device* Device() const noexcept;
    /** @brief The transfer system's timeline semaphore, as a VkSemaphore cast to uint64_t. 0 before Construct. */
    [[nodiscard]] uint64_t TransferTimeline() const noexcept;
    [[nodiscard]] ResourceContextCounters Counters() const noexcept;

private:

    struct ResourceFlags
    {
        ResourceType Type;
        ResourceCreationFlags Flags;
        ResourceDomain Domain;
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

    // the last ticket of a transfer that reads or writes the entity. a destroy waits for it, queued or in flight
    struct LastTransferTicket
    {
        uint64_t Value{ 0u };
    };

    // a destroyed entity that waits for the GPU. the resolve functions refuse it, so no new work can name it
    struct Retiring
    {
    };

    // CompletedValue is read once per Tick. PendingValue is the highest value the owner reported
    struct ConsumerTimelineSlot
    {
        uint64_t Semaphore{ 0u };
        uint64_t PendingValue{ 0u };
        uint64_t CompletedValue{ 0u };
    };

    // a value of 0 waits for nothing. TimelineValues is indexed like consumerTimelines
    struct RetiringResource
    {
        entt::entity Entity{ entt::null };
        std::array<uint64_t, k_MaxConsumerTimelines> TimelineValues{};
        uint64_t TransferTicket{ 0u };
        std::shared_ptr<MessageReply> Reply{ nullptr };
    };

    void processMessages();
    void dispatchMessage(ResourceMessagePayloadType&& message);

    // one overload per alternative of ResourceMessagePayloadType, so that a message type without a handler fails to
    // compile instead of being dropped
    void processMessage(CreateBufferMessage&& message);
    void processMessage(CreateImageMessage&& message);
    void processMessage(CreateSamplerMessage&& message);
    void processMessage(SetBufferDataMessage&& message);
    void processMessage(SetImageDataMessage&& message);
    void processMessage(FillResourceMessage&& message);
    void processMessage(MapResourceMessage&& message);
    void processMessage(UnmapResourceMessage&& message);
    void processMessage(CopyResourceMessage&& message);
    void processMessage(CopyResourceContentsMessage&& message);
    void processMessage(DestroyResourceMessage&& message);
    void processMessage(ExitWorkerMessage&& message) noexcept;
    void processMessage(TickMessage&& message);
    void processMessage(RegisterConsumerTimelineMessage&& message);
    void processMessage(UnregisterConsumerTimelineMessage&& message);
    void processMessage(TransferProgressMessage&& message) noexcept;

    /** @brief Issues the next ticket, publishes it in the reply with the Transferring status, records it on every
     *  entity the transfer touches, and enqueues the transfer. The only path into the transfer system. */
    template<typename TransferMessage>
    void enqueueTransfer(TransferMessage&& message, std::span<const entt::entity> touched_entities);

    /** @brief Destroys every retiring resource whose timelines and transfer ticket have all passed. Cheap when nothing
     *  waits, so the worker calls it after every message. */
    void retireReady();
    [[nodiscard]] bool isReadyToRetire(const RetiringResource& entry, uint64_t completed_ticket) const noexcept;
    void retire(RetiringResource& entry);

    /** @brief Returns the entity that `resource` refers to, when that entity is alive, is a buffer, and its buffer
     *  handle matches the one in `resource`. Every message that takes a buffer from a caller resolves it here. */
    [[nodiscard]] std::optional<entt::entity> resolveBuffer(const GraphicsResource& resource) const;
    /** @brief Returns the entity that `resource` refers to, when that entity is alive, is an image, and its image
     *  handle matches the one in `resource`. */
    [[nodiscard]] std::optional<entt::entity> resolveImage(const GraphicsResource& resource) const;
    /** @brief Returns the entity that `resource` refers to, when it is a buffer, an image or a sampler, and its handle
     *  matches. Used by the messages that accept any kind of resource. */
    [[nodiscard]] std::optional<entt::entity> resolveResource(const GraphicsResource& resource) const;

    /** @brief Creates the entity and every API object that the create info asks for. Leaves the reply alone, so that a
     *  copy can follow creation before the reply completes. */
    [[nodiscard]] std::optional<GraphicsResource> createBufferEntity(const BufferCreateInfo& info, const DebugName& name);
    [[nodiscard]] std::optional<GraphicsResource> createImageEntity(const ImageCreateInfo& info, const DebugName& name);
    static void completeWithResource(GraphicsResourceReply& reply, const GraphicsResource& resource) noexcept;

    // each of these names its object after the entity's DebugName component, when the entity has one
    VkBuffer createBuffer(entt::entity new_entity, const BufferCreateInfo& buffer_info);
    VkBufferView createBufferView(entt::entity new_entity, const BufferCreateInfo& buffer_info);
    VkImage createImage(entt::entity new_entity, const ImageCreateInfo& image_info);
    VkImageView createImageView(entt::entity new_entity, const ImageCreateInfo& image_info);
    VkSampler createSampler(entt::entity new_entity, const SamplerCreateInfo& sampler_info, std::string_view suffix);

    /** @brief Returns true when the allocation landed in host-visible memory, so data can be written without the transfer system. */
    [[nodiscard]] bool allocationIsHostVisible(entt::entity entity) const;
    // in case of host-visible buffers (e.g. UBOs and such) we just handle it in the resource context instead of the transfer system
    void setBufferDataHostOnly(entt::entity entity, InternalResourceDataContainer& data_container);
    void enqueueBufferUpload(entt::entity entity, InternalResourceDataContainer&& data, std::shared_ptr<ResourceTransferReply>&& reply);
    void enqueueImageUpload(entt::entity entity, InternalResourceDataContainer&& data, std::shared_ptr<ResourceTransferReply>&& reply);
    [[nodiscard]] TransferSystemReqBufferInfo bufferTransferInfo(entt::entity entity) const;
    /** @brief Returns the image's transfer info, and records the layout that the transfer will leave the image in. */
    [[nodiscard]] TransferSystemReqImageInfo imageTransferInfo(entt::entity entity);
    void setObjectName(VkObjectType object_type, uint64_t handle, const DebugName& name) const;

    /** @brief Destroys every API object that the entity has, and the entity. Used for destroy messages, for creation
     *  failures part-way through, and for resources still alive at shutdown. */
    void destroyEntityResources(entt::entity entity);

    mwsrQueue<ResourceMessagePayloadType> messageQueue;
    std::thread workerThread;

    // worker thread only
    std::array<ConsumerTimelineSlot, k_MaxConsumerTimelines> consumerTimelines{};
    std::vector<RetiringResource> retiring;
    uint64_t lastIssuedTicket{ 0u };

    // written by the worker with relaxed stores, read by Counters()
    std::atomic<int64_t> messagesProcessed{ 0 };
    std::atomic<int64_t> consumerTimelineReads{ 0 };
    std::atomic<int64_t> resourcesRetired{ 0 };
    std::atomic<int64_t> retirementsPending{ 0 };

    VmaAllocator allocatorHandle{ VK_NULL_HANDLE };

    // Primarily used to contain our info structures and allocation structures
    entt::registry resourceRegistry;
    ResourceTransferSystem transferSystem;

    const rhi::Device* device = nullptr;
    // ResourceContext assumes VK_SHARING_MODE_CONCURRENT across these, so that the transfer queue needs no ownership transfers
    std::vector<uint32_t> queueFamilyIndices;
    bool validationEnabled{ false };
    bool deviceAddressesEnabled{ false };
};


#endif //!RESOURCE_CONTEXT_IMPL_HPP
