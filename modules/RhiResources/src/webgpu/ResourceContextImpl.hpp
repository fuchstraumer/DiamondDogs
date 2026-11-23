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
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <variant>

#include <entt/entity/registry.hpp>
#include <webgpu/webgpu_cpp.h>

struct ResourceContextCreateInfo;
struct ResourceContextCounters;
struct MemoryBudgetSnapshot;
enum class ResourceContextError : uint8_t;

/** @brief The WebGPU resource context. It has no worker thread: PushMessage runs each message on the calling thread,
 *  and Tick pumps GPU completion callbacks and drains deferred upload work within the byte and time budgets. */
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
    static constexpr uint64_t k_UploadChunkBytes = 256ull * 1024ull;

    // written by the OnSubmittedWorkDone callback, read by Tick. AllowProcessEvents runs the callback on the thread that
    // calls ProcessEvents, so the fields are atomic
    struct CompletionRecord
    {
        std::atomic<uint64_t> CompletedSerial{ 0u };
        std::atomic<bool> DeviceLost{ false };
    };

    struct InFlightFuture
    {
        int32_t FutureSlot{ -1 };
        uint64_t Serial{ 0u };
    };

    struct BufferUploadOp
    {
        wgpu::Buffer Buffer;
        InternalResourceDataContainer::BufferDataVector Pieces;
        int32_t PieceIndex{ 0 };
        uint64_t ByteOffset{ 0u };
        int32_t FutureSlot{ -1 };
        bool IsCreate{ false };
        bool PublishedResource{ false };
        GraphicsResource Resource{};
        ResourceDescription Description{};
    };

    struct ImageUploadOp
    {
        wgpu::Texture Texture;
        ImageCreateInfo Info{};
        InternalResourceDataContainer::ImageDataVector Pieces;
        int32_t PieceIndex{ 0 };
        int32_t SliceIndex{ 0 };
        int32_t BlockRowOffset{ 0 };
        int32_t FutureSlot{ -1 };
        bool IsCreate{ false };
        bool PublishedResource{ false };
        GraphicsResource Resource{};
        ResourceDescription Description{};
    };

    struct BufferCopyOp
    {
        wgpu::Buffer SourceBuffer;
        wgpu::Buffer DestinationBuffer;
        CopyRegionBlock Regions;
        uint64_t TotalBytes{ 0u };
        int32_t FutureSlot{ -1 };
    };

    struct DestroyOp
    {
        entt::entity Entity{ entt::null };
        int32_t FutureSlot{ -1 };
    };

    using DeferredOp = std::variant<BufferUploadOp, ImageUploadOp, BufferCopyOp, DestroyOp>;

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
    void submitBufferCopy(const wgpu::Buffer& source_buffer,
                          const wgpu::Buffer& destination_buffer,
                          std::span<const BufferCopyRegion> regions) const;
    void recordInFlightFuture(int32_t future_slot);
    void processCompletedWork();
    void registerWorkDoneCallback();
    void drainDeferredOps(std::chrono::nanoseconds upload_time_slice);
    [[nodiscard]] uint64_t executeBufferUploadChunk(BufferUploadOp& op, uint64_t max_bytes, bool& finished);
    [[nodiscard]] uint64_t executeImageUploadChunk(ImageUploadOp& op, uint64_t max_bytes, bool& finished);
    void failAllPendingWork(rhi::ResourceError error);

    void destroyEntityResources(entt::entity entity);

    wgpu::Instance instance;
    wgpu::Device device;
    wgpu::Queue queue;
    entt::registry resourceRegistry;
    rhi::FutureTable futures;
    std::deque<DeferredOp> deferredOps;
    std::deque<InFlightFuture> inFlightFutures;
    std::shared_ptr<CompletionRecord> completionRecord;
    uint64_t uploadBytesPerTick{ 0u };
    uint64_t remainingUploadBudget{ 0u };
    uint64_t currentSerial{ 1u };
    bool wroteThisTick{ false };
#ifndef NDEBUG
    bool loggedZeroTimeSlice{ false };
#endif

    std::atomic<int64_t> messagesProcessed{ 0 };
    std::atomic<int64_t> bytesStaged{ 0 };
    std::atomic<int64_t> resourcesRetired{ 0 };
    std::atomic<int64_t> uploadOpsDeferred{ 0 };
    std::atomic<int64_t> uploadTicksAtBudget{ 0 };
    std::atomic<int64_t> uploadTicksAtTimeSlice{ 0 };
    bool validationEnabled{ false };
    bool deviceAddressesEnabled{ false };
};

#endif // !RESOURCE_CONTEXT_IMPL_HPP
