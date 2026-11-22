#pragma once
#ifndef RESOURCE_CONTEXT_IMPL_HPP
#define RESOURCE_CONTEXT_IMPL_HPP
#include "FutureTable.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiContextWebGpu.hpp"
#include "RhiResourceFlags.hpp"

#include <atomic>
#include <cstdint>
#include <expected>

#include <entt/entity/registry.hpp>
#include <webgpu/webgpu_cpp.h>

struct ResourceContextCreateInfo;
struct ResourceContextCounters;
struct MemoryBudgetSnapshot;
enum class ResourceContextError : uint8_t;

/** @brief The WebGPU resource context. It has no worker thread: PushMessage runs each message on the calling thread, so
 *  every future is final when the call that made it returns. */
class ResourceContextImpl
{
public:
    [[nodiscard]] ResourceContextError Construct(const ResourceContextCreateInfo& create_info);
    void Destroy();

    void PushMessage(ResourceMessagePayloadType message);

    [[nodiscard]] rhi::FutureTable& Futures() noexcept;
    [[nodiscard]] ResourceContextCounters Counters() const noexcept;
    [[nodiscard]] MemoryBudgetSnapshot MemoryBudgets() const noexcept;

private:
    struct ResourceFlags
    {
        ResourceType Type;
        ResourceCreationFlags Flags;
        ResourceDomain Domain;
    };

    void processMessage(CreateBufferMessage&& message);
    void processMessage(CreateImageMessage&& message);
    void processMessage(CreateSamplerMessage&& message);
    void processMessage(CreateMemoryHeapMessage&& message);
    void processMessage(CreatePlacedBufferMessage&& message);
    void processMessage(CreatePlacedImageMessage&& message);
    void processMessage(SetBufferDataMessage&& message);
    void processMessage(SetImageDataMessage&& message);
    void processMessage(FillResourceMessage&& message);
    void processMessage(MapResourceMessage&& message);
    void processMessage(UnmapResourceMessage&& message);
    void processMessage(CopyResourceMessage&& message);
    void processMessage(CopyResourceContentsMessage&& message);
    void processMessage(TransferSetMessage&& message);
    void processMessage(DestroyResourceMessage&& message);
    void processMessage(ExitWorkerMessage&& message) noexcept;
    void processMessage(TickMessage&& message);
    void processMessage(RegisterConsumerTimelineMessage&& message);
    void processMessage(UnregisterConsumerTimelineMessage&& message);
    void processMessage(TransferProgressMessage&& message) noexcept;

    /** @brief Fails the future with UnsupportedUsage. WebGPU has no path for the call. */
    void failUnsupported(int32_t future_slot) noexcept;

    [[nodiscard]] std::expected<entt::entity, rhi::ResourceError> resolveBuffer(const GraphicsResource& resource) const;
    [[nodiscard]] std::expected<entt::entity, rhi::ResourceError> resolveImage(const GraphicsResource& resource) const;
    [[nodiscard]] std::expected<entt::entity, rhi::ResourceError> resolveResource(const GraphicsResource& resource) const;

    [[nodiscard]] std::expected<GraphicsResource, rhi::ResourceError> createBufferEntity(const BufferCreateInfo& info,
                                                                                           const DebugName& name);
    [[nodiscard]] std::expected<GraphicsResource, rhi::ResourceError> createImageEntity(const ImageCreateInfo& info,
                                                                                          const DebugName& name);
    [[nodiscard]] std::expected<wgpu::Sampler, rhi::ResourceError> makeSampler(const SamplerCreateInfo& info,
                                                                                const DebugName& name) const;
    [[nodiscard]] std::expected<wgpu::TextureView, rhi::ResourceError> makeTextureView(const wgpu::Texture& texture,
                                                                                        const ImageCreateInfo& info,
                                                                                        const DebugName& name) const;

    void writeBufferPieces(const wgpu::Buffer& buffer, const InternalResourceDataContainer::BufferDataVector& pieces) const;
    void writeImagePieces(const wgpu::Texture& texture,
                          const ImageCreateInfo& info,
                          const InternalResourceDataContainer::ImageDataVector& pieces) const;
    [[nodiscard]] rhi::ResourceError copyBufferToBuffer(const CopyResourceContentsMessage& message);

    void destroyEntityResources(entt::entity entity);

    wgpu::Device device;
    wgpu::Queue queue;
    entt::registry resourceRegistry;
    rhi::FutureTable futures;

    std::atomic<int64_t> messagesProcessed{ 0 };
    std::atomic<int64_t> resourcesRetired{ 0 };
    bool validationEnabled{ false };
    bool deviceAddressesEnabled{ false };
};

#endif // !RESOURCE_CONTEXT_IMPL_HPP
