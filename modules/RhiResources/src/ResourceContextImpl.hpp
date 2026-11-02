#pragma once
#include "ResourceFlags.hpp"
#include "entt/entity/fwd.hpp"
#include <cstdint>
#ifndef RESOURCE_CONTEXT_IMPL_HPP
#define RESOURCE_CONTEXT_IMPL_HPP
#include "ResourceTypes.hpp"
#include "FutureTable.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "TransferSystem.hpp"
#include "containers/mwsrQueue.hpp"

#include <array>
#include <atomic>
#include <expected>
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

    /** @brief Returns the future table. ResourceContext allocates a future from it for every call. */
    [[nodiscard]] rhi::FutureTable& Futures() noexcept;
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
        int32_t Future{ -1 };
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

    /** @brief Issues the next ticket, publishes it in the future with the Created state, records it on every entity
     *  the transfer touches, and enqueues the transfer with its waits. The only path into the transfer system. */
    template<typename TransferMessage>
    void enqueueTransfer(TransferMessage&& message, std::span<const entt::entity> touched_entities, const TransferWaitSet& waits);

    /** @brief Destroys every retiring resource whose timelines and transfer ticket have all passed. Cheap when nothing
     *  waits, so the worker calls it after every message. */
    void retireReady();
    [[nodiscard]] bool isReadyToRetire(const RetiringResource& entry, uint64_t completed_ticket) const noexcept;
    void retire(RetiringResource& entry);

    /** @brief Returns the entity that `resource` refers to, when that entity is alive, is a buffer, and its buffer
     *  handle matches the one in `resource`. Every message that takes a buffer from a caller resolves it here. */
    [[nodiscard]] std::expected<entt::entity, rhi::ResourceError> resolveBuffer(const GraphicsResource& resource) const;
    /** @brief Returns the entity that `resource` refers to, when that entity is alive, is an image, and its image
     *  handle matches the one in `resource`. */
    [[nodiscard]] std::expected<entt::entity, rhi::ResourceError> resolveImage(const GraphicsResource& resource) const;
    /** @brief Returns the entity that `resource` refers to, when it is a buffer, an image or a sampler, and its handle
     *  matches. Used by the messages that accept any kind of resource. */
    [[nodiscard]] std::expected<entt::entity, rhi::ResourceError> resolveResource(const GraphicsResource& resource) const;

    /** @brief Creates the entity and every API object that the create info asks for. Leaves the future alone, so that
     *  a copy can follow creation before the future completes. */
    [[nodiscard]] std::expected<GraphicsResource, rhi::ResourceError> createBufferEntity(const BufferCreateInfo& info, const DebugName& name);
    [[nodiscard]] std::expected<GraphicsResource, rhi::ResourceError> createImageEntity(const ImageCreateInfo& info, const DebugName& name);

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
    void enqueueBufferUpload(entt::entity entity, InternalResourceDataContainer&& data, const TransferWaitSet& waits, int32_t future_slot);
    void enqueueImageUpload(const TransferSystemReqImageInfo& image_info, entt::entity entity, InternalResourceDataContainer&& data, const TransferWaitSet& waits, int32_t future_slot);
    [[nodiscard]] TransferSystemReqBufferInfo bufferTransferInfo(entt::entity entity) const;
    /** @brief Returns the image's transfer info. The layouts come from the caller's usages, and the context keeps no
     *  layout state of its own. */
    [[nodiscard]] TransferSystemReqImageInfo imageTransferInfo(entt::entity entity, rhi::ImageUsageTransition usage) const;
    /** @brief Returns the transfer info for an image that no transfer or GPU work has touched: it starts UNDEFINED. */
    [[nodiscard]] TransferSystemReqImageInfo newImageTransferInfo(entt::entity entity, ImageUsageBits usage_after) const;
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
    // declared before the transfer system, so that it is destroyed after the last thread that writes it
    rhi::FutureTable futures;
    ResourceTransferSystem transferSystem;

    const rhi::Device* device = nullptr;
    // ResourceContext assumes VK_SHARING_MODE_CONCURRENT across these, so that the transfer queue needs no ownership transfers
    std::vector<uint32_t> queueFamilyIndices;
    bool validationEnabled{ false };
    bool deviceAddressesEnabled{ false };
};


#endif //!RESOURCE_CONTEXT_IMPL_HPP
