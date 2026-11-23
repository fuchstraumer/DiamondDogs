#include "ResourceContextWebGpu.hpp"
#include "ResourceContextImpl.hpp"
#include "FutureTable.hpp"
#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include "RhiContextWebGpu.hpp"
#include "RhiHandle.hpp"
#include "RhiResourceFlags.hpp"
#include "entt/entity/fwd.hpp"
#include "timing/MonotonicClock.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <webgpu/webgpu.h>
#ifndef NDEBUG
#include <print>
#endif
#include <span>
#include <utility>
#include <variant>
#include <webgpu/webgpu_cpp.h>

namespace
{

struct FormatRow
{
    ImageComponentFormats Component;
    ImageDataType Data;
    wgpu::TextureFormat Format;
    uint32_t BlockBytes;
};

/** The layout of one upload piece, in the rows of its format. The texels of one layer are tightly packed. */
struct PieceLayout
{
    uint32_t CopyWidth{ 0u };
    uint32_t CopyHeight{ 0u };
    uint32_t CopyDepth{ 0u };
    uint32_t RowBytes{ 0u };
    uint32_t Rows{ 0u };
    uint64_t LayerBytes{ 0u };
};

using BufferPieceVector = InternalResourceDataContainer::BufferDataVector;
using ImagePieceVector = InternalResourceDataContainer::ImageDataVector;

[[nodiscard]] bool HasAnyBufferBits(BufferUsageBits value, BufferUsageBits mask) noexcept;
[[nodiscard]] bool HasAnyImageBits(ImageUsageBits value, ImageUsageBits mask) noexcept;
[[nodiscard]] uint64_t RoundUpToFour(uint64_t value) noexcept;
[[nodiscard]] uint64_t PackHandle(const void* handle) noexcept;
[[nodiscard]] bool BufferUsageIsSupported(BufferUsageBits usage) noexcept;
[[nodiscard]] wgpu::BufferUsage ToWgpuBufferUsage(BufferUsageBits usage, ResourceDomain domain, bool has_initial_data) noexcept;
[[nodiscard]] bool BufferRangeIsValid(uint64_t offset, uint64_t size, uint64_t buffer_size) noexcept;
[[nodiscard]] bool BufferPiecesAreValid(const BufferPieceVector& pieces, uint64_t buffer_size) noexcept;
[[nodiscard]] uint64_t TotalBufferUploadBytes(const BufferPieceVector& pieces) noexcept;
[[nodiscard]] uint64_t TotalImageUploadBytes(const ImagePieceVector& pieces, const ImageCreateInfo& info) noexcept;
[[nodiscard]] uint64_t TotalCopyBytes(std::span<const BufferCopyRegion> regions) noexcept;
[[nodiscard]] bool BufferCopyRegionsAreValid(std::span<const BufferCopyRegion> regions,
                                             uint64_t source_size,
                                             uint64_t destination_size,
                                             bool same_buffer) noexcept;
[[nodiscard]] bool ImageUsageIsSupported(ImageUsageBits usage) noexcept;
[[nodiscard]] wgpu::TextureUsage ToWgpuTextureUsage(ImageUsageBits usage) noexcept;
[[nodiscard]] bool UsageIsSubset(ImageUsageBits usage, ImageUsageBits created_usage) noexcept;
[[nodiscard]] bool TransitionIsValid(ImageUsageBits created_usage, ImageUsageBits before, ImageUsageBits after) noexcept;
[[nodiscard]] const FormatRow* FindFormatRow(const ImageFormat& format) noexcept;
[[nodiscard]] wgpu::TextureDimension ToWgpuTextureDimension(ImageType type) noexcept;
[[nodiscard]] std::optional<wgpu::TextureViewDimension> ToWgpuViewDimension(ImageViewType type) noexcept;
[[nodiscard]] wgpu::AddressMode ToWgpuAddressMode(SamplerCreateInfo::AddressMode mode) noexcept;
[[nodiscard]] wgpu::FilterMode ToWgpuFilterMode(ImageFilteringMode mode) noexcept;
[[nodiscard]] wgpu::MipmapFilterMode ToWgpuMipmapFilterMode(ImageFilteringMode mode) noexcept;
[[nodiscard]] wgpu::CompareFunction ToWgpuCompareFunction(ImageCompareOp op) noexcept;
[[nodiscard]] uint32_t MipExtent(uint32_t base_extent, int32_t mip_level) noexcept;
[[nodiscard]] uint32_t MipDepth(const ImageCreateInfo& info, int32_t mip_level) noexcept;
[[nodiscard]] PieceLayout ComputePieceLayout(const InternalResourceDataContainer::ImageData& piece,
                                             const ImageCreateInfo& info,
                                             const FormatRow& row) noexcept;
[[nodiscard]] bool ImagePieceIsValid(const InternalResourceDataContainer::ImageData& piece,
                                     const ImageCreateInfo& info,
                                     const FormatRow& row,
                                     bool require_full_mip) noexcept;
[[nodiscard]] bool ImagePiecesAreValid(const ImagePieceVector& pieces, const ImageCreateInfo& info, bool require_full_mip) noexcept;
[[nodiscard]] ResourceDescription DescribeBuffer(const BufferCreateInfo& info) noexcept;
[[nodiscard]] ResourceDescription DescribeImage(const ImageCreateInfo& info,
                                                ResourceType type,
                                                ImageUsageBits initial_usage) noexcept;

} // namespace

ResourceContextError ResourceContextImpl::Construct(const ResourceContextCreateInfo& create_info)
{
    if (device)
    {
        return ResourceContextError::AlreadyInitialized;
    }

    if (create_info.Context == nullptr)
    {
        return ResourceContextError::InvalidCreateInfo;
    }

    const rhi::WebGpuRhiContextView rhiView = rhi::UnwrapWebGpuRhiContext(*create_info.Context);

    if (!rhiView.Device || !rhiView.Queue)
    {
        return ResourceContextError::InvalidCreateInfo;
    }

    if (create_info.MaxFutures < 2 || create_info.StagingRingSize < k_MinStagingRingSize ||
        create_info.UploadBytesPerTick == 0u || create_info.UploadBytesPerTick > create_info.StagingRingSize)
    {
        return ResourceContextError::InvalidCreateInfo;
    }

    futures.Initialize(create_info.MaxFutures, rhi::TimelineSemaphoreHandle{}, nullptr);

    deferredOps.clear();
    inFlightFutures.clear();
    uploadBytesPerTick = create_info.UploadBytesPerTick;
    remainingUploadBudget = uploadBytesPerTick;
    currentSerial = 1u;
    wroteThisTick = false;
#ifndef NDEBUG
    loggedZeroTimeSlice = false;
#endif
    completionRecord = std::make_shared<CompletionRecord>();
    instance = rhiView.Instance;
    queue = rhiView.Queue;
    validationEnabled = rhiView.ValidationEnabled;
    deviceAddressesEnabled = create_info.DeviceAddressesEnabled;
    device = rhiView.Device;
    return ResourceContextError::Success;
}

void ResourceContextImpl::Destroy()
{
    if (!device)
    {
        return;
    }

    failAllPendingWork(rhi::ResourceError::ResourceRetiring);
    completionRecord.reset();

    for (const entt::entity entity : resourceRegistry.view<ResourceFlags>())
    {
        if (wgpu::Buffer* buffer = resourceRegistry.try_get<wgpu::Buffer>(entity))
        {
            buffer->Destroy();
        }

        if (wgpu::Texture* texture = resourceRegistry.try_get<wgpu::Texture>(entity))
        {
            texture->Destroy();
        }
    }

    resourceRegistry.clear();
    queue = wgpu::Queue{};
    device = wgpu::Device{};
    instance = wgpu::Instance{};
}

void ResourceContextImpl::PushMessage(ResourceMessagePayloadType message)
{
    std::visit([this](auto&& alternative) { processMessage(std::forward<decltype(alternative)>(alternative)); },
               std::move(message));
    messagesProcessed.fetch_add(1, std::memory_order_relaxed);
}

rhi::FutureTable& ResourceContextImpl::Futures() noexcept
{
    return futures;
}

ResourceContextCounters ResourceContextImpl::Counters() const noexcept
{
    ResourceContextCounters counters{};
    counters.MessagesProcessed = messagesProcessed.load(std::memory_order_relaxed);
    counters.BytesStaged = bytesStaged.load(std::memory_order_relaxed);
    counters.ResourcesRetired = resourcesRetired.load(std::memory_order_relaxed);
    counters.FuturesLive = futures.LiveCount();
    counters.FutureTableFull = futures.FullCount();
    counters.UploadOpsDeferred = uploadOpsDeferred.load(std::memory_order_relaxed);
    counters.UploadTicksAtBudget = uploadTicksAtBudget.load(std::memory_order_relaxed);
    counters.UploadTicksAtTimeSlice = uploadTicksAtTimeSlice.load(std::memory_order_relaxed);
    return counters;
}

MemoryBudgetSnapshot ResourceContextImpl::MemoryBudgets() const noexcept
{
    return MemoryBudgetSnapshot{};
}

void ResourceContextImpl::processMessage(CreateBufferMessage&& message)
{
    const bool hostDomain = message.BufferInfo.Domain == ResourceDomain::CPUOnly ||
                            message.BufferInfo.Domain == ResourceDomain::CPUToGPU;

    if (message.InitialData.has_value() && !hostDomain)
    {
        message.BufferInfo.Usage |= BufferUsageBits::TransferDestination;
    }

    BufferPieceVector* pieces = nullptr;

    if (message.InitialData.has_value())
    {
        pieces = std::get_if<BufferPieceVector>(&message.InitialData->DataVector);

        if (pieces == nullptr || !BufferPiecesAreValid(*pieces, message.BufferInfo.Size))
        {
            futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
            return;
        }
    }

    const std::expected<GraphicsResource, rhi::ResourceError> created = createBufferEntity(message.BufferInfo, message.Name);

    if (!created.has_value())
    {
        futures.Fail(message.Future, created.error());
        return;
    }

    const ResourceDescription description = DescribeBuffer(message.BufferInfo);

    if (pieces != nullptr)
    {
        const entt::entity entity = entt::entity(created->EntityHandle);
        const wgpu::Buffer& buffer = resourceRegistry.get<wgpu::Buffer>(entity);
        const uint64_t totalBytes = TotalBufferUploadBytes(*pieces);

        if (deferredOps.empty() && totalBytes <= remainingUploadBudget)
        {
            writeBufferPieces(buffer, *pieces);
            remainingUploadBudget -= totalBytes;
            bytesStaged.fetch_add(static_cast<int64_t>(totalBytes), std::memory_order_relaxed);
            futures.StoreResource(message.Future, *created, description);
            recordInFlightFuture(message.Future);
        }
        else
        {
            BufferUploadOp bufferOp
            {
                .Buffer = buffer,
                .Pieces = std::move(*pieces),
                .PieceIndex = 0,
                .ByteOffset = 0u,
                .FutureSlot = message.Future,
                .IsCreate = true,
                .PublishedResource = false,
                .Resource = *created,
                .Description = description
            };
            deferredOps.emplace_back(std::move(bufferOp));
            uploadOpsDeferred.fetch_add(1, std::memory_order_relaxed);
        }

        return;
    }

    futures.CompleteWithResource(message.Future, *created, description);
}

void ResourceContextImpl::processMessage(CreateImageMessage&& message)
{
    if (message.InitialData.has_value())
    {
        message.ImageInfo.Usage |= ImageUsageBits::TransferDestination;
    }

    const bool createUsesDiscard =
        HasAnyImageBits(message.ImageInfo.Usage, ImageUsageBits::Discard) ||
        (message.InitialData.has_value() && HasAnyImageBits(message.UsageAfterUpload, ImageUsageBits::Discard));

    if (createUsesDiscard)
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidCreateInfo);
        return;
    }

    ImagePieceVector* pieces = nullptr;

    if (message.InitialData.has_value())
    {
        const bool usageIsValid = message.UsageAfterUpload != ImageUsageBits::Invalid &&
                                  UsageIsSubset(message.UsageAfterUpload, message.ImageInfo.Usage);

        if (!usageIsValid)
        {
            futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
            return;
        }

        pieces = std::get_if<ImagePieceVector>(&message.InitialData->DataVector);

        if (pieces == nullptr || !ImagePiecesAreValid(*pieces, message.ImageInfo, false))
        {
            futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
            return;
        }
    }

    const std::expected<GraphicsResource, rhi::ResourceError> created = createImageEntity(message.ImageInfo, message.Name);

    if (!created.has_value())
    {
        futures.Fail(message.Future, created.error());
        return;
    }

    if (pieces != nullptr)
    {
        const entt::entity entity = entt::entity(created->EntityHandle);
        const wgpu::Texture& texture = resourceRegistry.get<wgpu::Texture>(entity);
        const ResourceDescription description = DescribeImage(message.ImageInfo, created->Type, message.UsageAfterUpload);
        const uint64_t totalBytes = TotalImageUploadBytes(*pieces, message.ImageInfo);

        if (deferredOps.empty() && totalBytes <= remainingUploadBudget)
        {
            writeImagePieces(texture, message.ImageInfo, *pieces);
            remainingUploadBudget -= totalBytes;
            bytesStaged.fetch_add(static_cast<int64_t>(totalBytes), std::memory_order_relaxed);
            futures.StoreResource(message.Future, *created, description);
            recordInFlightFuture(message.Future);
        }
        else
        {
            ImageUploadOp imageOp
            {
                .Texture = texture,
                .Info = message.ImageInfo,
                .Pieces = std::move(*pieces),
                .PieceIndex = 0,
                .SliceIndex = 0,
                .BlockRowOffset = 0,
                .FutureSlot = message.Future,
                .IsCreate = true,
                .PublishedResource = false,
                .Resource = *created,
                .Description = description
            };
            deferredOps.emplace_back(std::move(imageOp));
            uploadOpsDeferred.fetch_add(1, std::memory_order_relaxed);
        }

        return;
    }

    const ResourceDescription description = DescribeImage(message.ImageInfo, created->Type, ImageUsageBits::Invalid);
    futures.CompleteWithResource(message.Future, *created, description);
}

void ResourceContextImpl::processMessage(CreateSamplerMessage&& message)
{
    const std::expected<wgpu::Sampler, rhi::ResourceError> sampler = makeSampler(message.SamplerInfo, message.Name);

    if (!sampler.has_value())
    {
        futures.Fail(message.Future, sampler.error());
        return;
    }

    const entt::entity entity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(entity, ResourceType::Sampler, ResourceCreationFlags::None, ResourceDomain::GPUOnly);
    resourceRegistry.emplace<wgpu::Sampler>(entity, *sampler);

    GraphicsResource resource{};
    resource.Domain = ResourceDomain::GPUOnly;
    resource.Type = ResourceType::Sampler;
    resource.EntityHandle = static_cast<uint32_t>(entity);
    resource.VkSamplerHandle = PackHandle(sampler->Get());

    ResourceDescription description{};
    description.Type = ResourceType::Sampler;
    description.Domain = ResourceDomain::GPUOnly;
    futures.CompleteWithResource(message.Future, resource, description);
}

void ResourceContextImpl::processMessage(CreateMemoryHeapMessage&& message)
{
    failUnsupported(message.Future);
}

void ResourceContextImpl::processMessage(CreatePlacedBufferMessage&& message)
{
    failUnsupported(message.Create.Future);
}

void ResourceContextImpl::processMessage(CreatePlacedImageMessage&& message)
{
    failUnsupported(message.Create.Future);
}

void ResourceContextImpl::processMessage(SetBufferDataMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveBuffer(message.DestBuffer);

    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    const wgpu::Buffer& buffer = resourceRegistry.get<wgpu::Buffer>(*entity);
    const BufferCreateInfo& info = resourceRegistry.get<BufferCreateInfo>(*entity);

    if ((buffer.GetUsage() & wgpu::BufferUsage::CopyDst) == wgpu::BufferUsage::None)
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    BufferPieceVector* pieces = std::get_if<BufferPieceVector>(&message.Data.DataVector);

    if (pieces == nullptr || !BufferPiecesAreValid(*pieces, info.Size))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    const uint64_t totalBytes = TotalBufferUploadBytes(*pieces);

    if (deferredOps.empty() && totalBytes <= remainingUploadBudget)
    {
        writeBufferPieces(buffer, *pieces);
        remainingUploadBudget -= totalBytes;
        bytesStaged.fetch_add(static_cast<int64_t>(totalBytes), std::memory_order_relaxed);
        recordInFlightFuture(message.Future);
        return;
    }

    BufferUploadOp op{};
    op.Buffer = buffer;
    op.Pieces = std::move(*pieces);
    op.PieceIndex = 0;
    op.ByteOffset = 0u;
    op.FutureSlot = message.Future;
    op.IsCreate = false;
    op.PublishedResource = false;
    deferredOps.emplace_back(std::move(op));
    uploadOpsDeferred.fetch_add(1, std::memory_order_relaxed);
}

void ResourceContextImpl::processMessage(SetImageDataMessage&& message)
{
    const std::expected<entt::entity, rhi::ResourceError> entity = resolveImage(message.DestImage);

    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    const wgpu::Texture& texture = resourceRegistry.get<wgpu::Texture>(*entity);
    const ImageCreateInfo& info = resourceRegistry.get<ImageCreateInfo>(*entity);

    if (!HasAnyImageBits(info.Usage, ImageUsageBits::TransferDestination))
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    if (!TransitionIsValid(info.Usage, message.Usage.Before, message.Usage.After))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidUsageTransition);
        return;
    }

    const bool discarded = message.Usage.Before == ImageUsageBits::Discard;
    ImagePieceVector* pieces = std::get_if<ImagePieceVector>(&message.Data.DataVector);

    if (pieces == nullptr || !ImagePiecesAreValid(*pieces, info, discarded))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    const uint64_t totalBytes = TotalImageUploadBytes(*pieces, info);

    if (deferredOps.empty() && totalBytes <= remainingUploadBudget)
    {
        writeImagePieces(texture, info, *pieces);
        remainingUploadBudget -= totalBytes;
        bytesStaged.fetch_add(static_cast<int64_t>(totalBytes), std::memory_order_relaxed);
        recordInFlightFuture(message.Future);
        return;
    }

    ImageUploadOp op{};
    op.Texture = texture;
    op.Info = info;
    op.Pieces = std::move(*pieces);
    op.PieceIndex = 0;
    op.SliceIndex = 0;
    op.BlockRowOffset = 0;
    op.FutureSlot = message.Future;
    op.IsCreate = false;
    op.PublishedResource = false;
    deferredOps.emplace_back(std::move(op));
    uploadOpsDeferred.fetch_add(1, std::memory_order_relaxed);
}

void ResourceContextImpl::processMessage(FillResourceMessage&& message)
{
    failUnsupported(message.Future);
}

void ResourceContextImpl::processMessage(MapResourceMessage&& message)
{
    failUnsupported(message.Future);
}

void ResourceContextImpl::processMessage(UnmapResourceMessage&& message)
{
    failUnsupported(message.Future);
}

void ResourceContextImpl::processMessage(CopyResourceMessage&& message)
{
    failUnsupported(message.Future);
}

void ResourceContextImpl::processMessage(CopyResourceContentsMessage&& message)
{
    if (message.Kind != CopyKind::BufferToBuffer)
    {
        futures.Fail(message.Future, rhi::ResourceError::UnsupportedCopy);
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> source = resolveBuffer(message.SourceResource);

    if (!source.has_value())
    {
        futures.Fail(message.Future, source.error());
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> destination = resolveBuffer(message.DestinationResource);

    if (!destination.has_value())
    {
        futures.Fail(message.Future, destination.error());
        return;
    }

    const wgpu::Buffer sourceBuffer = resourceRegistry.get<wgpu::Buffer>(*source);
    const wgpu::Buffer destinationBuffer = resourceRegistry.get<wgpu::Buffer>(*destination);
    const bool sourceCanRead = (sourceBuffer.GetUsage() & wgpu::BufferUsage::CopySrc) != wgpu::BufferUsage::None;
    const bool destinationCanWrite = (destinationBuffer.GetUsage() & wgpu::BufferUsage::CopyDst) != wgpu::BufferUsage::None;

    if (!sourceCanRead || !destinationCanWrite)
    {
        futures.Fail(message.Future, rhi::ResourceError::MissingTransferUsage);
        return;
    }

    const uint64_t sourceSize = sourceBuffer.GetSize();
    const uint64_t destinationSize = destinationBuffer.GetSize();
    const std::span<const BufferCopyRegion> regions = message.Regions.View();

    if (regions.empty())
    {
        if (sourceSize > destinationSize)
        {
            futures.Fail(message.Future, rhi::ResourceError::CopySizeMismatch);
            return;
        }

        const BufferCopyRegion wholeSource{ 0u, 0u, sourceSize };
        message.Regions = CopyRegionBlock(std::span<const BufferCopyRegion>(&wholeSource, 1u));
    }
    else
    {
        const bool sameBuffer = *source == *destination;

        if (!BufferCopyRegionsAreValid(regions, sourceSize, destinationSize, sameBuffer))
        {
            futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
            return;
        }
    }

    const std::span<const BufferCopyRegion> copyRegions = message.Regions.View();
    const uint64_t totalBytes = TotalCopyBytes(copyRegions);

    if (deferredOps.empty() && totalBytes <= remainingUploadBudget)
    {
        submitBufferCopy(sourceBuffer, destinationBuffer, copyRegions);
        remainingUploadBudget -= totalBytes;
        bytesStaged.fetch_add(static_cast<int64_t>(totalBytes), std::memory_order_relaxed);
        recordInFlightFuture(message.Future);
        return;
    }

    BufferCopyOp copyOp
    {
        .SourceBuffer = sourceBuffer,
        .DestinationBuffer = destinationBuffer,
        .Regions = std::move(message.Regions),
        .TotalBytes = totalBytes,
        .FutureSlot = message.Future
    };
    deferredOps.emplace_back(std::move(copyOp));
    uploadOpsDeferred.fetch_add(1, std::memory_order_relaxed);
}

void ResourceContextImpl::processMessage(TransferSetMessage&& message)
{
    failUnsupported(message.Future);
}

void ResourceContextImpl::processMessage(DestroyResourceMessage&& message)
{
    if (message.HasAfter && (message.After.Semaphore.Get() == 0u || message.After.Value == 0u))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidWait);
        return;
    }

    const std::expected<entt::entity, rhi::ResourceError> entity = resolveResource(message.Resource);

    if (!entity.has_value())
    {
        futures.Fail(message.Future, entity.error());
        return;
    }

    if (deferredOps.empty())
    {
        destroyEntityResources(*entity);
        resourcesRetired.fetch_add(1, std::memory_order_relaxed);
        futures.Complete(message.Future);
        return;
    }

    deferredOps.emplace_back(DestroyOp{ .Entity = *entity, .FutureSlot = message.Future });
}

void ResourceContextImpl::processMessage([[maybe_unused]] ExitWorkerMessage&& message) noexcept
{
}

void ResourceContextImpl::processMessage(TickMessage&& message)
{
    processCompletedWork();

    if (completionRecord && completionRecord->DeviceLost.load(std::memory_order_acquire))
    {
        registerWorkDoneCallback();
        return;
    }

    remainingUploadBudget = uploadBytesPerTick;

    if (message.UploadTimeSlice.count() <= 0)
    {
#ifndef NDEBUG
        if (!loggedZeroTimeSlice)
        {
            loggedZeroTimeSlice = true;
            std::println(stderr,
                         "[ResourceContext] Tick called with non-positive upload_time_slice ({} ns): upload progress is stopped for this tick",
                         message.UploadTimeSlice.count());
        }
#endif
        registerWorkDoneCallback();
        return;
    }

    drainDeferredOps(message.UploadTimeSlice);
    registerWorkDoneCallback();
}

void ResourceContextImpl::processMessage(RegisterConsumerTimelineMessage&& message)
{
    futures.Complete(message.Future);
}

void ResourceContextImpl::processMessage(UnregisterConsumerTimelineMessage&& message)
{
    futures.Complete(message.Future);
}

void ResourceContextImpl::processMessage([[maybe_unused]] TransferProgressMessage&& message) noexcept
{
}

void ResourceContextImpl::failUnsupported(int32_t future_slot) noexcept
{
    futures.Fail(future_slot, rhi::ResourceError::UnsupportedUsage);
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveBuffer(const GraphicsResource& resource) const
{
    const entt::entity entity = entt::entity(resource.EntityHandle);

    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    const wgpu::Buffer* buffer = resourceRegistry.try_get<wgpu::Buffer>(entity);

    if (buffer == nullptr)
    {
        return std::unexpected(rhi::ResourceError::WrongResourceKind);
    }

    if (PackHandle(buffer->Get()) != resource.VkHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    return entity;
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveImage(const GraphicsResource& resource) const
{
    const entt::entity entity = entt::entity(resource.EntityHandle);

    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    const wgpu::Texture* texture = resourceRegistry.try_get<wgpu::Texture>(entity);

    if (texture == nullptr)
    {
        return std::unexpected(rhi::ResourceError::WrongResourceKind);
    }

    if (PackHandle(texture->Get()) != resource.VkHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    return entity;
}

std::expected<entt::entity, rhi::ResourceError> ResourceContextImpl::resolveResource(const GraphicsResource& resource) const
{
    if (resource.Type == ResourceType::Buffer)
    {
        return resolveBuffer(resource);
    }

    if (resource.Type == ResourceType::Image || resource.Type == ResourceType::CombinedImageSampler)
    {
        return resolveImage(resource);
    }

    if (resource.Type != ResourceType::Sampler)
    {
        return std::unexpected(rhi::ResourceError::UnsupportedCopy);
    }

    const entt::entity entity = entt::entity(resource.EntityHandle);

    if (!resourceRegistry.valid(entity))
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    const wgpu::Sampler* sampler = resourceRegistry.try_get<wgpu::Sampler>(entity);

    if (sampler == nullptr || PackHandle(sampler->Get()) != resource.VkSamplerHandle)
    {
        return std::unexpected(rhi::ResourceError::StaleResource);
    }

    return entity;
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::createBufferEntity(const BufferCreateInfo& info,
                                                                                              const DebugName& name)
{
    if (info.Domain == ResourceDomain::Invalid || info.Size == 0u)
    {
        return std::unexpected(rhi::ResourceError::InvalidCreateInfo);
    }

    if (!BufferUsageIsSupported(info.Usage))
    {
        return std::unexpected(rhi::ResourceError::UnsupportedUsage);
    }

    wgpu::BufferDescriptor descriptor{};
    descriptor.size = RoundUpToFour(static_cast<uint64_t>(info.Size));
    descriptor.usage = ToWgpuBufferUsage(info.Usage, info.Domain, false);

    if (!name.Empty())
    {
        descriptor.label = wgpu::StringView{ name.View() };
    }

    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);

    if (!buffer)
    {
        return std::unexpected(rhi::ResourceError::AllocationFailed);
    }

    const entt::entity entity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(entity, ResourceType::Buffer, info.Flags, info.Domain);
    resourceRegistry.emplace<BufferCreateInfo>(entity, info);
    resourceRegistry.emplace<wgpu::Buffer>(entity, buffer);

    GraphicsResource resource{};
    resource.Domain = info.Domain;
    resource.Type = ResourceType::Buffer;
    resource.EntityHandle = static_cast<uint32_t>(entity);
    resource.VkHandle = PackHandle(buffer.Get());
    return resource;
}

std::expected<GraphicsResource, rhi::ResourceError> ResourceContextImpl::createImageEntity(const ImageCreateInfo& info,
                                                                                             const DebugName& name)
{
    const FormatRow* row = FindFormatRow(info.Format);

    if (info.Domain == ResourceDomain::Invalid || info.Width == 0u || row == nullptr)
    {
        return std::unexpected(rhi::ResourceError::InvalidCreateInfo);
    }

    if (!ImageUsageIsSupported(info.Usage))
    {
        return std::unexpected(rhi::ResourceError::UnsupportedUsage);
    }

    wgpu::TextureDescriptor descriptor{};
    descriptor.dimension = ToWgpuTextureDimension(info.Type);
    descriptor.format = row->Format;
    descriptor.size.width = info.Width;
    descriptor.size.height = std::max(1u, info.Height);
    descriptor.size.depthOrArrayLayers = info.Type == ImageType::Type3D ? std::max(1u, info.Depth) : std::max(1u, info.ArrayLayers);
    descriptor.mipLevelCount = std::max(1u, info.MipLevels);
    descriptor.sampleCount = std::max(1u, info.SampleCount);
    descriptor.usage = ToWgpuTextureUsage(info.Usage);

    if (!name.Empty())
    {
        descriptor.label = wgpu::StringView{ name.View() };
    }

    wgpu::Texture texture = device.CreateTexture(&descriptor);

    if (!texture)
    {
        return std::unexpected(rhi::ResourceError::AllocationFailed);
    }

    GraphicsResource resource{};
    resource.Domain = info.Domain;
    resource.Type = info.SamplerInfo.has_value() ? ResourceType::CombinedImageSampler : ResourceType::Image;
    resource.VkHandle = PackHandle(texture.Get());

    wgpu::TextureView view;

    if (info.ViewInfo.has_value())
    {
        const std::expected<wgpu::TextureView, rhi::ResourceError> madeView = makeTextureView(texture, info, name);

        if (!madeView.has_value())
        {
            texture.Destroy();
            return std::unexpected(madeView.error());
        }

        view = *madeView;
        resource.VkViewHandle = PackHandle(view.Get());
    }

    wgpu::Sampler sampler;

    if (info.SamplerInfo.has_value())
    {
        const DebugName samplerName = name.WithSuffix(".sampler");
        const std::expected<wgpu::Sampler, rhi::ResourceError> madeSampler = makeSampler(*info.SamplerInfo, samplerName);

        if (!madeSampler.has_value())
        {
            texture.Destroy();
            return std::unexpected(madeSampler.error());
        }

        sampler = *madeSampler;
        resource.VkSamplerHandle = PackHandle(sampler.Get());
    }

    const entt::entity entity = resourceRegistry.create();
    resourceRegistry.emplace<ResourceFlags>(entity, resource.Type, info.Flags, info.Domain);
    resourceRegistry.emplace<ImageCreateInfo>(entity, info);
    resourceRegistry.emplace<wgpu::Texture>(entity, texture);

    if (view)
    {
        resourceRegistry.emplace<wgpu::TextureView>(entity, view);
    }

    if (sampler)
    {
        resourceRegistry.emplace<wgpu::Sampler>(entity, sampler);
    }

    resource.EntityHandle = static_cast<uint32_t>(entity);
    return resource;
}

std::expected<wgpu::Sampler, rhi::ResourceError> ResourceContextImpl::makeSampler(const SamplerCreateInfo& info,
                                                                                   const DebugName& name) const
{
    wgpu::SamplerDescriptor descriptor{};
    descriptor.addressModeU = ToWgpuAddressMode(info.AddressU);
    descriptor.addressModeV = ToWgpuAddressMode(info.AddressV);
    descriptor.addressModeW = ToWgpuAddressMode(info.AddressW);
    descriptor.magFilter = ToWgpuFilterMode(info.MagFilter);
    descriptor.minFilter = ToWgpuFilterMode(info.MinFilter);
    descriptor.mipmapFilter = ToWgpuMipmapFilterMode(info.MipFilter);
    descriptor.lodMinClamp = info.MinLod;
    descriptor.lodMaxClamp = std::min(info.MaxLod, 32.0f);

    if (info.CompareEnabled)
    {
        descriptor.compare = ToWgpuCompareFunction(info.CompareOp);
    }

    const bool allLinear = descriptor.magFilter == wgpu::FilterMode::Linear &&
                           descriptor.minFilter == wgpu::FilterMode::Linear &&
                           descriptor.mipmapFilter == wgpu::MipmapFilterMode::Linear;

    if (info.AnisotropyEnabled && allLinear)
    {
        const float clampedAnisotropy = std::clamp(info.MaxAnisotropy, 1.0f, 16.0f);
        descriptor.maxAnisotropy = static_cast<uint16_t>(clampedAnisotropy);
    }

    if (!name.Empty())
    {
        descriptor.label = wgpu::StringView{ name.View() };
    }

    wgpu::Sampler sampler = device.CreateSampler(&descriptor);

    if (!sampler)
    {
        return std::unexpected(rhi::ResourceError::SamplerCreationFailed);
    }

    return sampler;
}

std::expected<wgpu::TextureView, rhi::ResourceError> ResourceContextImpl::makeTextureView(const wgpu::Texture& texture,
                                                                                           const ImageCreateInfo& info,
                                                                                           const DebugName& name) const
{
    const ImageViewCreateInfo& viewInfo = *info.ViewInfo;
    const std::optional<wgpu::TextureViewDimension> dimension = ToWgpuViewDimension(viewInfo.ViewType);

    if (!dimension.has_value())
    {
        return std::unexpected(rhi::ResourceError::ViewCreationFailed);
    }

    const FormatRow* row = FindFormatRow(viewInfo.FormatOverride.value_or(info.Format));

    if (row == nullptr)
    {
        return std::unexpected(rhi::ResourceError::ViewCreationFailed);
    }

    wgpu::TextureViewDescriptor descriptor{};
    descriptor.format = row->Format;
    descriptor.dimension = *dimension;
    descriptor.baseMipLevel = viewInfo.Range.BaseMipLevel;
    descriptor.mipLevelCount = viewInfo.Range.LevelCount == 0u ? wgpu::kMipLevelCountUndefined : viewInfo.Range.LevelCount;
    descriptor.baseArrayLayer = viewInfo.Range.BaseArrayLayer;
    descriptor.arrayLayerCount = viewInfo.Range.LayerCount == 0u ? wgpu::kArrayLayerCountUndefined : viewInfo.Range.LayerCount;

    if (*dimension == wgpu::TextureViewDimension::e3D)
    {
        descriptor.baseArrayLayer = 0u;
        descriptor.arrayLayerCount = 1u;
    }

    if (viewInfo.Range.AspectMask == ImageAspectFlags::Depth)
    {
        descriptor.aspect = wgpu::TextureAspect::DepthOnly;
    }
    else if (viewInfo.Range.AspectMask == ImageAspectFlags::Stencil)
    {
        descriptor.aspect = wgpu::TextureAspect::StencilOnly;
    }

    const DebugName viewName = name.WithSuffix(".view");

    if (!viewName.Empty())
    {
        descriptor.label = wgpu::StringView{ viewName.View() };
    }

    wgpu::TextureView view = texture.CreateView(&descriptor);

    if (!view)
    {
        return std::unexpected(rhi::ResourceError::ViewCreationFailed);
    }

    return view;
}

void ResourceContextImpl::writeBufferPieces(const wgpu::Buffer& buffer, const BufferPieceVector& pieces) const
{
    for (const InternalResourceDataContainer::BufferData& piece : pieces)
    {
        assert((piece.Size % 4u) == 0u && (piece.DestinationOffset % 4u) == 0u);
        queue.WriteBuffer(buffer, piece.DestinationOffset, piece.Data.get(), piece.Size);
    }
}

void ResourceContextImpl::writeImagePieces(const wgpu::Texture& texture,
                                           const ImageCreateInfo& info,
                                           const ImagePieceVector& pieces) const
{
    const FormatRow* row = FindFormatRow(info.Format);
    const bool is3D = info.Type == ImageType::Type3D;

    for (const InternalResourceDataContainer::ImageData& piece : pieces)
    {
        const PieceLayout layout = ComputePieceLayout(piece, info, *row);

        wgpu::TexelCopyBufferLayout bufferLayout{};
        bufferLayout.offset = 0u;
        bufferLayout.bytesPerRow = layout.RowBytes;
        bufferLayout.rowsPerImage = layout.Rows;

        wgpu::Extent3D writeSize{};
        writeSize.width = layout.CopyWidth;
        writeSize.height = layout.CopyHeight;
        writeSize.depthOrArrayLayers = layout.CopyDepth;

        const int32_t layerCount = is3D ? 1 : std::max(1, piece.NumLayers);

        for (int32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            wgpu::TexelCopyTextureInfo destination{};
            destination.texture = texture;
            destination.mipLevel = static_cast<uint32_t>(piece.MipLevel);
            destination.origin.x = static_cast<uint32_t>(piece.X);
            destination.origin.y = static_cast<uint32_t>(piece.Y);
            destination.origin.z = is3D ? static_cast<uint32_t>(piece.Z) : static_cast<uint32_t>(piece.ArrayLayer + layerIndex);

            const uint64_t layerOffset = static_cast<uint64_t>(layerIndex) * layout.LayerBytes;
            const std::byte* layerData = piece.Data.get() + layerOffset;
            queue.WriteTexture(&destination, layerData, static_cast<size_t>(layout.LayerBytes), &bufferLayout, &writeSize);
        }
    }
}

void ResourceContextImpl::submitBufferCopy(const wgpu::Buffer& source_buffer,
                                           const wgpu::Buffer& destination_buffer,
                                           std::span<const BufferCopyRegion> regions) const
{
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    for (const BufferCopyRegion& region : regions)
    {
        encoder.CopyBufferToBuffer(source_buffer, region.SourceOffset, destination_buffer, region.DestinationOffset, region.Size);
    }

    const wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1u, &commands);
}

void ResourceContextImpl::recordInFlightFuture(int32_t future_slot)
{
    wroteThisTick = true;
    futures.SetTransferring(future_slot, currentSerial);
    futures.SetSubmitted(future_slot);
    inFlightFutures.push_back(InFlightFuture{ future_slot, currentSerial });
}

void ResourceContextImpl::processCompletedWork()
{
    if (instance)
    {
        instance.ProcessEvents();
    }

    if (!completionRecord)
    {
        return;
    }

    if (completionRecord->DeviceLost.load(std::memory_order_acquire))
    {
        failAllPendingWork(rhi::ResourceError::SubmitFailed);
        return;
    }

    const uint64_t completedSerial = completionRecord->CompletedSerial.load(std::memory_order_acquire);

    while (!inFlightFutures.empty() && inFlightFutures.front().Serial <= completedSerial)
    {
        futures.Complete(inFlightFutures.front().FutureSlot);
        inFlightFutures.pop_front();
    }
}

void ResourceContextImpl::registerWorkDoneCallback()
{
    if (!wroteThisTick)
    {
        return;
    }

    const std::shared_ptr<CompletionRecord> record = completionRecord;
    const uint64_t serial = currentSerial;

    const auto onWorkDone = [record, serial](wgpu::QueueWorkDoneStatus status, wgpu::StringView)
    {
        if (status != wgpu::QueueWorkDoneStatus::Success)
        {
            record->DeviceLost.store(true, std::memory_order_release);
            return;
        }

        uint64_t observed = record->CompletedSerial.load(std::memory_order_relaxed);

        while (observed < serial &&
               !record->CompletedSerial.compare_exchange_weak(observed,
                                                              serial,
                                                              std::memory_order_release,
                                                              std::memory_order_relaxed))
        {
        }
    };

    queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowProcessEvents, onWorkDone);
    wroteThisTick = false;
    ++currentSerial;
}

void ResourceContextImpl::drainDeferredOps(std::chrono::nanoseconds upload_time_slice)
{
    if (deferredOps.empty())
    {
        return;
    }

    const bool hasTimeSlice = upload_time_slice != k_NoUploadTimeSlice;
    const int64_t sliceNs = upload_time_slice.count();
    const int64_t startTime = foundation::MonotonicNanoseconds();
    bool wroteChunk = false;

    while (!deferredOps.empty())
    {
        DeferredOp& frontOp = deferredOps.front();

        if (DestroyOp* destroyOp = std::get_if<DestroyOp>(&frontOp))
        {
            if (resourceRegistry.valid(destroyOp->Entity))
            {
                destroyEntityResources(destroyOp->Entity);
            }

            resourcesRetired.fetch_add(1, std::memory_order_relaxed);
            futures.Complete(destroyOp->FutureSlot);
            deferredOps.pop_front();
            continue;
        }

        if (wroteChunk)
        {
            if (hasTimeSlice && (foundation::MonotonicNanoseconds() - startTime) >= sliceNs)
            {
                uploadTicksAtTimeSlice.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (remainingUploadBudget == 0u)
            {
                uploadTicksAtBudget.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        const uint64_t chunkBudget = remainingUploadBudget > 0u ? std::min(remainingUploadBudget, k_UploadChunkBytes) : k_UploadChunkBytes;

        if (BufferUploadOp* bufferOp = std::get_if<BufferUploadOp>(&frontOp))
        {
            if (bufferOp->IsCreate && !bufferOp->PublishedResource)
            {
                futures.StoreResource(bufferOp->FutureSlot, bufferOp->Resource, bufferOp->Description);
                futures.SetTransferring(bufferOp->FutureSlot, currentSerial);
                bufferOp->PublishedResource = true;
            }

            bool finished = false;
            const uint64_t bytesWritten = executeBufferUploadChunk(*bufferOp, chunkBudget, finished);
            wroteChunk = true;
            wroteThisTick = true;
            bytesStaged.fetch_add(static_cast<int64_t>(bytesWritten), std::memory_order_relaxed);
            remainingUploadBudget = bytesWritten >= remainingUploadBudget ? 0u : (remainingUploadBudget - bytesWritten);

            if (finished)
            {
                const int32_t futureSlot = bufferOp->FutureSlot;
                deferredOps.pop_front();
                recordInFlightFuture(futureSlot);
            }

            continue;
        }

        if (ImageUploadOp* imageOp = std::get_if<ImageUploadOp>(&frontOp))
        {
            if (imageOp->IsCreate && !imageOp->PublishedResource)
            {
                futures.StoreResource(imageOp->FutureSlot, imageOp->Resource, imageOp->Description);
                futures.SetTransferring(imageOp->FutureSlot, currentSerial);
                imageOp->PublishedResource = true;
            }

            bool finished = false;
            const uint64_t bytesWritten = executeImageUploadChunk(*imageOp, chunkBudget, finished);
            wroteChunk = true;
            wroteThisTick = true;
            bytesStaged.fetch_add(static_cast<int64_t>(bytesWritten), std::memory_order_relaxed);
            remainingUploadBudget = bytesWritten >= remainingUploadBudget ? 0u : (remainingUploadBudget - bytesWritten);

            if (finished)
            {
                const int32_t futureSlot = imageOp->FutureSlot;
                deferredOps.pop_front();
                recordInFlightFuture(futureSlot);
            }

            continue;
        }

        if (BufferCopyOp* copyOp = std::get_if<BufferCopyOp>(&frontOp))
        {
            if (wroteChunk && copyOp->TotalBytes > remainingUploadBudget)
            {
                uploadTicksAtBudget.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            submitBufferCopy(copyOp->SourceBuffer, copyOp->DestinationBuffer, copyOp->Regions.View());
            wroteChunk = true;
            wroteThisTick = true;
            const uint64_t bytesCopied = copyOp->TotalBytes;
            bytesStaged.fetch_add(static_cast<int64_t>(bytesCopied), std::memory_order_relaxed);
            remainingUploadBudget = bytesCopied >= remainingUploadBudget ? 0u : (remainingUploadBudget - bytesCopied);

            const int32_t futureSlot = copyOp->FutureSlot;
            deferredOps.pop_front();
            recordInFlightFuture(futureSlot);
            continue;
        }
    }

    if (wroteChunk && hasTimeSlice && (foundation::MonotonicNanoseconds() - startTime) >= sliceNs)
    {
        uploadTicksAtTimeSlice.fetch_add(1, std::memory_order_relaxed);
    }
    else if (wroteChunk && remainingUploadBudget == 0u)
    {
        uploadTicksAtBudget.fetch_add(1, std::memory_order_relaxed);
    }
}

uint64_t ResourceContextImpl::executeBufferUploadChunk(BufferUploadOp& op, uint64_t max_bytes, bool& finished)
{
    const int32_t pieceCount = static_cast<int32_t>(std::ssize(op.Pieces));

    while (op.PieceIndex < pieceCount)
    {
        const InternalResourceDataContainer::BufferData& piece = op.Pieces[static_cast<size_t>(op.PieceIndex)];
        assert((piece.Size % 4u) == 0u && (piece.DestinationOffset % 4u) == 0u);
        const uint64_t pieceSize = static_cast<uint64_t>(piece.Size);

        if (op.ByteOffset >= pieceSize)
        {
            ++op.PieceIndex;
            op.ByteOffset = 0u;
            continue;
        }

        const uint64_t remainingInPiece = pieceSize - op.ByteOffset;
        assert((remainingInPiece % 4u) == 0u);
        const uint64_t rawChunk = std::min(remainingInPiece, max_bytes);
        const uint64_t alignedChunk = std::max<uint64_t>(4u, (rawChunk / 4u) * 4u);
        const uint64_t chunkToWrite = std::min(remainingInPiece, alignedChunk);

        queue.WriteBuffer(op.Buffer,
                          piece.DestinationOffset + op.ByteOffset,
                          piece.Data.get() + op.ByteOffset,
                          static_cast<size_t>(chunkToWrite));
        op.ByteOffset += chunkToWrite;

        while (op.PieceIndex < pieceCount)
        {
            const uint64_t currentPieceSize = static_cast<uint64_t>(op.Pieces[static_cast<size_t>(op.PieceIndex)].Size);

            if (op.ByteOffset < currentPieceSize)
            {
                break;
            }

            ++op.PieceIndex;
            op.ByteOffset = 0u;
        }

        finished = (op.PieceIndex >= pieceCount);
        return chunkToWrite;
    }

    finished = true;
    return 0u;
}

uint64_t ResourceContextImpl::executeImageUploadChunk(ImageUploadOp& op, uint64_t max_bytes, bool& finished)
{
    const FormatRow* row = FindFormatRow(op.Info.Format);
    const bool is3D = op.Info.Type == ImageType::Type3D;
    const uint32_t blockEdge = IsCompressedFormat(op.Info.Format) ? 4u : 1u;
    const int32_t pieceCount = static_cast<int32_t>(std::ssize(op.Pieces));

    while (op.PieceIndex < pieceCount)
    {
        const InternalResourceDataContainer::ImageData& piece = op.Pieces[static_cast<size_t>(op.PieceIndex)];
        const PieceLayout layout = ComputePieceLayout(piece, op.Info, *row);
        const int32_t sliceCount = is3D ? static_cast<int32_t>(layout.CopyDepth) : std::max(1, piece.NumLayers);
        const int32_t totalBlockRows = static_cast<int32_t>(layout.Rows);

        if (op.SliceIndex >= sliceCount)
        {
            ++op.PieceIndex;
            op.SliceIndex = 0;
            op.BlockRowOffset = 0;
            continue;
        }

        if (op.BlockRowOffset >= totalBlockRows)
        {
            ++op.SliceIndex;
            op.BlockRowOffset = 0;
            continue;
        }

        const int32_t remainingBlockRows = totalBlockRows - op.BlockRowOffset;
        const uint64_t rowBytes = static_cast<uint64_t>(layout.RowBytes);
        const uint64_t maxRowsByBudget = rowBytes > 0u ? (max_bytes / rowBytes) : static_cast<uint64_t>(remainingBlockRows);
        const int32_t bandBlockRows = static_cast<int32_t>(std::clamp<uint64_t>(maxRowsByBudget, 1u, static_cast<uint64_t>(remainingBlockRows)));
        const uint32_t bandTexelHeight = static_cast<uint32_t>(bandBlockRows) * blockEdge;
        const uint64_t bandBytes = static_cast<uint64_t>(bandBlockRows) * rowBytes;

        wgpu::TexelCopyBufferLayout bufferLayout{};
        bufferLayout.offset = 0u;
        bufferLayout.bytesPerRow = layout.RowBytes;
        bufferLayout.rowsPerImage = static_cast<uint32_t>(bandBlockRows);

        wgpu::Extent3D writeSize{};
        writeSize.width = layout.CopyWidth;
        writeSize.height = bandTexelHeight;
        writeSize.depthOrArrayLayers = 1u;

        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = op.Texture;
        destination.mipLevel = static_cast<uint32_t>(piece.MipLevel);
        destination.origin.x = static_cast<uint32_t>(piece.X);
        destination.origin.y = static_cast<uint32_t>(piece.Y) + (static_cast<uint32_t>(op.BlockRowOffset) * blockEdge);
        destination.origin.z = is3D ? static_cast<uint32_t>(piece.Z + op.SliceIndex) : static_cast<uint32_t>(piece.ArrayLayer + op.SliceIndex);

        const uint64_t sliceByteSize = static_cast<uint64_t>(totalBlockRows) * rowBytes;
        const uint64_t dataOffset = (static_cast<uint64_t>(op.SliceIndex) * sliceByteSize) +
                                    (static_cast<uint64_t>(op.BlockRowOffset) * rowBytes);
        const std::byte* bandData = piece.Data.get() + dataOffset;

        queue.WriteTexture(&destination, bandData, static_cast<size_t>(bandBytes), &bufferLayout, &writeSize);
        op.BlockRowOffset += bandBlockRows;

        if (op.BlockRowOffset >= totalBlockRows)
        {
            op.BlockRowOffset = 0;
            ++op.SliceIndex;
        }

        while (op.PieceIndex < pieceCount)
        {
            const InternalResourceDataContainer::ImageData& currentPiece = op.Pieces[static_cast<size_t>(op.PieceIndex)];
            const PieceLayout currentLayout = ComputePieceLayout(currentPiece, op.Info, *row);
            const int32_t currentSlices = is3D ? static_cast<int32_t>(currentLayout.CopyDepth) : std::max(1, currentPiece.NumLayers);

            if (op.SliceIndex < currentSlices)
            {
                break;
            }

            ++op.PieceIndex;
            op.SliceIndex = 0;
            op.BlockRowOffset = 0;
        }

        finished = (op.PieceIndex >= pieceCount);
        return bandBytes;
    }

    finished = true;
    return 0u;
}

void ResourceContextImpl::failAllPendingWork(rhi::ResourceError error)
{
    while (!inFlightFutures.empty())
    {
        futures.Fail(inFlightFutures.front().FutureSlot, error);
        inFlightFutures.pop_front();
    }

    while (!deferredOps.empty())
    {
        DeferredOp& frontOp = deferredOps.front();

        if (const BufferUploadOp* bufferOp = std::get_if<BufferUploadOp>(&frontOp))
        {
            futures.Fail(bufferOp->FutureSlot, error);
        }
        else if (const ImageUploadOp* imageOp = std::get_if<ImageUploadOp>(&frontOp))
        {
            futures.Fail(imageOp->FutureSlot, error);
        }
        else if (const BufferCopyOp* copyOp = std::get_if<BufferCopyOp>(&frontOp))
        {
            futures.Fail(copyOp->FutureSlot, error);
        }
        else if (const DestroyOp* destroyOp = std::get_if<DestroyOp>(&frontOp))
        {
            futures.Fail(destroyOp->FutureSlot, error);
        }

        deferredOps.pop_front();
    }
}

void ResourceContextImpl::destroyEntityResources(entt::entity entity)
{
    if (wgpu::Buffer* buffer = resourceRegistry.try_get<wgpu::Buffer>(entity))
    {
        buffer->Destroy();
    }

    if (wgpu::Texture* texture = resourceRegistry.try_get<wgpu::Texture>(entity))
    {
        texture->Destroy();
    }

    resourceRegistry.destroy(entity);
}

namespace rhi
{

wgpu::Buffer UnwrapWebGpuBuffer(const GraphicsResource& resource) noexcept
{
    if (resource.Type != ResourceType::Buffer || resource.VkHandle == 0u)
    {
        return wgpu::Buffer{};
    }

    return wgpu::Buffer{ reinterpret_cast<WGPUBuffer>(static_cast<uintptr_t>(resource.VkHandle)) };
}

wgpu::Texture UnwrapWebGpuTexture(const GraphicsResource& resource) noexcept
{
    const bool isImage = resource.Type == ResourceType::Image || resource.Type == ResourceType::CombinedImageSampler;

    if (!isImage || resource.VkHandle == 0u)
    {
        return wgpu::Texture{};
    }

    return wgpu::Texture{ reinterpret_cast<WGPUTexture>(static_cast<uintptr_t>(resource.VkHandle)) };
}

wgpu::TextureView UnwrapWebGpuTextureView(const GraphicsResource& resource) noexcept
{
    if (resource.VkViewHandle == 0u)
    {
        return wgpu::TextureView{};
    }

    return wgpu::TextureView{ reinterpret_cast<WGPUTextureView>(static_cast<uintptr_t>(resource.VkViewHandle)) };
}

wgpu::Sampler UnwrapWebGpuSampler(const GraphicsResource& resource) noexcept
{
    if (resource.VkSamplerHandle == 0u)
    {
        return wgpu::Sampler{};
    }

    return wgpu::Sampler{ reinterpret_cast<WGPUSampler>(static_cast<uintptr_t>(resource.VkSamplerHandle)) };
}

} // namespace rhi

namespace
{

bool HasAnyBufferBits(BufferUsageBits value, BufferUsageBits mask) noexcept
{
    return (value & mask) != BufferUsageBits::Invalid;
}

bool HasAnyImageBits(ImageUsageBits value, ImageUsageBits mask) noexcept
{
    return (value & mask) != ImageUsageBits::Invalid;
}

uint64_t RoundUpToFour(uint64_t value) noexcept
{
    return (value + 3u) & ~uint64_t{ 3u };
}

uint64_t PackHandle(const void* handle) noexcept
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
}

bool BufferUsageIsSupported(BufferUsageBits usage) noexcept
{
    const BufferUsageBits unsupported = BufferUsageBits::AllRayTracingUsage | BufferUsageBits::AllMicromap |
                                        BufferUsageBits::TransformFeedback | BufferUsageBits::TransformFeedbackCounter |
                                        BufferUsageBits::GeneratedCommandsInput | BufferUsageBits::GeneratedCommandsPreprocess |
                                        BufferUsageBits::ConditionalRendering | BufferUsageBits::DeviceAddress;
    return !HasAnyBufferBits(usage, unsupported);
}

wgpu::BufferUsage ToWgpuBufferUsage(BufferUsageBits usage, ResourceDomain domain, bool has_initial_data) noexcept
{
    wgpu::BufferUsage result = wgpu::BufferUsage::None;

    if (HasAnyBufferBits(usage, BufferUsageBits::AllShaderUBO))
    {
        result |= wgpu::BufferUsage::Uniform;
    }

    if (HasAnyBufferBits(usage, BufferUsageBits::AllShaderStorage))
    {
        result |= wgpu::BufferUsage::Storage;
    }

    if (HasAnyBufferBits(usage, BufferUsageBits::IndexInput))
    {
        result |= wgpu::BufferUsage::Index;
    }

    if (HasAnyBufferBits(usage, BufferUsageBits::VertexInput))
    {
        result |= wgpu::BufferUsage::Vertex;
    }

    if (HasAnyBufferBits(usage, BufferUsageBits::DrawIndirect | BufferUsageBits::DispatchIndirect))
    {
        result |= wgpu::BufferUsage::Indirect;
    }

    const bool gpuBindable = result != wgpu::BufferUsage::None;
    const bool hostDomain = domain == ResourceDomain::CPUOnly || domain == ResourceDomain::CPUToGPU;

    if (HasAnyBufferBits(usage, BufferUsageBits::TransferSource))
    {
        result |= wgpu::BufferUsage::CopySrc;
    }

    if (HasAnyBufferBits(usage, BufferUsageBits::TransferDestination) || has_initial_data || hostDomain)
    {
        result |= wgpu::BufferUsage::CopyDst;
    }

    const bool readbackOnly = domain == ResourceDomain::GPUToCPU && !gpuBindable &&
                              (result & wgpu::BufferUsage::CopySrc) == wgpu::BufferUsage::None;

    if (readbackOnly)
    {
        result |= wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    }

    if (result == wgpu::BufferUsage::None)
    {
        result = wgpu::BufferUsage::CopyDst;
    }

    return result;
}

bool BufferRangeIsValid(uint64_t offset, uint64_t size, uint64_t buffer_size) noexcept
{
    const bool aligned = offset % 4u == 0u && size % 4u == 0u;
    return aligned && offset <= buffer_size && size <= buffer_size - offset;
}

bool BufferPiecesAreValid(const BufferPieceVector& pieces, uint64_t buffer_size) noexcept
{
    const int32_t pieceCount = static_cast<int32_t>(std::ssize(pieces));

    if (pieceCount == 0)
    {
        return false;
    }

    for (int32_t firstIndex = 0; firstIndex < pieceCount; ++firstIndex)
    {
        const InternalResourceDataContainer::BufferData& first = pieces[static_cast<size_t>(firstIndex)];

        if (first.Size == 0u || !BufferRangeIsValid(first.DestinationOffset, first.Size, buffer_size))
        {
            return false;
        }

        for (int32_t secondIndex = firstIndex + 1; secondIndex < pieceCount; ++secondIndex)
        {
            const InternalResourceDataContainer::BufferData& second = pieces[static_cast<size_t>(secondIndex)];
            const bool overlaps = first.DestinationOffset < second.DestinationOffset + second.Size &&
                                  second.DestinationOffset < first.DestinationOffset + first.Size;

            if (overlaps)
            {
                return false;
            }
        }
    }

    return true;
}

bool BufferCopyRegionsAreValid(std::span<const BufferCopyRegion> regions,
                               uint64_t source_size,
                               uint64_t destination_size,
                               bool same_buffer) noexcept
{
    const int32_t regionCount = static_cast<int32_t>(std::ssize(regions));

    for (int32_t firstIndex = 0; firstIndex < regionCount; ++firstIndex)
    {
        const BufferCopyRegion& first = regions[static_cast<size_t>(firstIndex)];
        const bool sourceValid = BufferRangeIsValid(first.SourceOffset, first.Size, source_size);
        const bool destinationValid = BufferRangeIsValid(first.DestinationOffset, first.Size, destination_size);

        if (first.Size == 0u || !sourceValid || !destinationValid)
        {
            return false;
        }

        for (int32_t secondIndex = 0; secondIndex < regionCount; ++secondIndex)
        {
            const BufferCopyRegion& second = regions[static_cast<size_t>(secondIndex)];
            const bool destinationsOverlap = firstIndex < secondIndex &&
                                             first.DestinationOffset < second.DestinationOffset + second.Size &&
                                             second.DestinationOffset < first.DestinationOffset + first.Size;
            const bool sourceHitsDestination = same_buffer &&
                                               first.SourceOffset < second.DestinationOffset + second.Size &&
                                               second.DestinationOffset < first.SourceOffset + first.Size;

            if (destinationsOverlap || sourceHitsDestination)
            {
                return false;
            }
        }
    }

    return true;
}

bool ImageUsageIsSupported(ImageUsageBits usage) noexcept
{
    const ImageUsageBits unsupported = ImageUsageBits::AllRayTracingStage | ImageUsageBits::ShadingRateRead |
                                       ImageUsageBits::FragmentDensityMapRead;
    return !HasAnyImageBits(usage, unsupported);
}

wgpu::TextureUsage ToWgpuTextureUsage(ImageUsageBits usage) noexcept
{
    wgpu::TextureUsage result = wgpu::TextureUsage::None;
    const ImageUsageBits sampledBits = ImageUsageBits::AllShaderSRV | ImageUsageBits::SampledImage;
    const ImageUsageBits attachmentBits = ImageUsageBits::RenderTargetRead | ImageUsageBits::RenderTargetWrite |
                                          ImageUsageBits::DepthRead | ImageUsageBits::DepthWrite |
                                          ImageUsageBits::StencilRead | ImageUsageBits::StencilWrite |
                                          ImageUsageBits::DepthStencilRead | ImageUsageBits::DepthStencilWrite |
                                          ImageUsageBits::InputAttachmentRead;

    if (HasAnyImageBits(usage, sampledBits))
    {
        result |= wgpu::TextureUsage::TextureBinding;
    }

    if (HasAnyImageBits(usage, ImageUsageBits::AllShaderUAV))
    {
        result |= wgpu::TextureUsage::StorageBinding;
    }

    if (HasAnyImageBits(usage, attachmentBits))
    {
        result |= wgpu::TextureUsage::RenderAttachment;
    }

    if (HasAnyImageBits(usage, ImageUsageBits::TransferSource))
    {
        result |= wgpu::TextureUsage::CopySrc;
    }

    if (HasAnyImageBits(usage, ImageUsageBits::TransferDestination))
    {
        result |= wgpu::TextureUsage::CopyDst;
    }

    if (result == wgpu::TextureUsage::None)
    {
        result = wgpu::TextureUsage::TextureBinding;
    }

    return result;
}

bool UsageIsSubset(ImageUsageBits usage, ImageUsageBits created_usage) noexcept
{
    return (usage & created_usage) == usage;
}

bool TransitionIsValid(ImageUsageBits created_usage, ImageUsageBits before, ImageUsageBits after) noexcept
{
    if (before == ImageUsageBits::Invalid || after == ImageUsageBits::Invalid)
    {
        return false;
    }

    const bool beforeIsValid = before == ImageUsageBits::Discard || UsageIsSubset(before, created_usage);
    return beforeIsValid && UsageIsSubset(after, created_usage);
}

const FormatRow* FindFormatRow(const ImageFormat& format) noexcept
{
    static constexpr FormatRow table[] =
    {
        { ImageComponentFormats::R8, ImageDataType::UNorm, wgpu::TextureFormat::R8Unorm, 1u },
        { ImageComponentFormats::R8, ImageDataType::SNorm, wgpu::TextureFormat::R8Snorm, 1u },
        { ImageComponentFormats::R8, ImageDataType::UInt, wgpu::TextureFormat::R8Uint, 1u },
        { ImageComponentFormats::R8, ImageDataType::SInt, wgpu::TextureFormat::R8Sint, 1u },
        { ImageComponentFormats::R16, ImageDataType::UNorm, wgpu::TextureFormat::R16Unorm, 2u },
        { ImageComponentFormats::R16, ImageDataType::SNorm, wgpu::TextureFormat::R16Snorm, 2u },
        { ImageComponentFormats::R16, ImageDataType::UInt, wgpu::TextureFormat::R16Uint, 2u },
        { ImageComponentFormats::R16, ImageDataType::SInt, wgpu::TextureFormat::R16Sint, 2u },
        { ImageComponentFormats::R16, ImageDataType::Float, wgpu::TextureFormat::R16Float, 2u },
        { ImageComponentFormats::R32, ImageDataType::UInt, wgpu::TextureFormat::R32Uint, 4u },
        { ImageComponentFormats::R32, ImageDataType::SInt, wgpu::TextureFormat::R32Sint, 4u },
        { ImageComponentFormats::R32, ImageDataType::Float, wgpu::TextureFormat::R32Float, 4u },
        { ImageComponentFormats::RG8, ImageDataType::UNorm, wgpu::TextureFormat::RG8Unorm, 2u },
        { ImageComponentFormats::RG8, ImageDataType::SNorm, wgpu::TextureFormat::RG8Snorm, 2u },
        { ImageComponentFormats::RG8, ImageDataType::UInt, wgpu::TextureFormat::RG8Uint, 2u },
        { ImageComponentFormats::RG8, ImageDataType::SInt, wgpu::TextureFormat::RG8Sint, 2u },
        { ImageComponentFormats::RG16, ImageDataType::UNorm, wgpu::TextureFormat::RG16Unorm, 4u },
        { ImageComponentFormats::RG16, ImageDataType::SNorm, wgpu::TextureFormat::RG16Snorm, 4u },
        { ImageComponentFormats::RG16, ImageDataType::UInt, wgpu::TextureFormat::RG16Uint, 4u },
        { ImageComponentFormats::RG16, ImageDataType::SInt, wgpu::TextureFormat::RG16Sint, 4u },
        { ImageComponentFormats::RG16, ImageDataType::Float, wgpu::TextureFormat::RG16Float, 4u },
        { ImageComponentFormats::RG32, ImageDataType::UInt, wgpu::TextureFormat::RG32Uint, 8u },
        { ImageComponentFormats::RG32, ImageDataType::SInt, wgpu::TextureFormat::RG32Sint, 8u },
        { ImageComponentFormats::RG32, ImageDataType::Float, wgpu::TextureFormat::RG32Float, 8u },
        { ImageComponentFormats::RGBA8, ImageDataType::UNorm, wgpu::TextureFormat::RGBA8Unorm, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::sRGB, wgpu::TextureFormat::RGBA8UnormSrgb, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::SNorm, wgpu::TextureFormat::RGBA8Snorm, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::UInt, wgpu::TextureFormat::RGBA8Uint, 4u },
        { ImageComponentFormats::RGBA8, ImageDataType::SInt, wgpu::TextureFormat::RGBA8Sint, 4u },
        { ImageComponentFormats::BGRA8, ImageDataType::UNorm, wgpu::TextureFormat::BGRA8Unorm, 4u },
        { ImageComponentFormats::BGRA8, ImageDataType::sRGB, wgpu::TextureFormat::BGRA8UnormSrgb, 4u },
        { ImageComponentFormats::RGBA16, ImageDataType::UNorm, wgpu::TextureFormat::RGBA16Unorm, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::SNorm, wgpu::TextureFormat::RGBA16Snorm, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::UInt, wgpu::TextureFormat::RGBA16Uint, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::SInt, wgpu::TextureFormat::RGBA16Sint, 8u },
        { ImageComponentFormats::RGBA16, ImageDataType::Float, wgpu::TextureFormat::RGBA16Float, 8u },
        { ImageComponentFormats::RGBA32, ImageDataType::UInt, wgpu::TextureFormat::RGBA32Uint, 16u },
        { ImageComponentFormats::RGBA32, ImageDataType::SInt, wgpu::TextureFormat::RGBA32Sint, 16u },
        { ImageComponentFormats::RGBA32, ImageDataType::Float, wgpu::TextureFormat::RGBA32Float, 16u },
        { ImageComponentFormats::A2B10G10R10, ImageDataType::UNorm, wgpu::TextureFormat::RGB10A2Unorm, 4u },
        { ImageComponentFormats::A2B10G10R10, ImageDataType::UInt, wgpu::TextureFormat::RGB10A2Uint, 4u },
        { ImageComponentFormats::D16, ImageDataType::UNorm, wgpu::TextureFormat::Depth16Unorm, 2u },
        { ImageComponentFormats::D24, ImageDataType::UNorm, wgpu::TextureFormat::Depth24Plus, 4u },
        { ImageComponentFormats::D32, ImageDataType::Float, wgpu::TextureFormat::Depth32Float, 4u },
        { ImageComponentFormats::D24S8, ImageDataType::UNorm, wgpu::TextureFormat::Depth24PlusStencil8, 4u },
        { ImageComponentFormats::D32S8, ImageDataType::Float, wgpu::TextureFormat::Depth32FloatStencil8, 8u },
        { ImageComponentFormats::S8, ImageDataType::UInt, wgpu::TextureFormat::Stencil8, 1u },
        { ImageComponentFormats::BC1, ImageDataType::UNorm, wgpu::TextureFormat::BC1RGBAUnorm, 8u },
        { ImageComponentFormats::BC1, ImageDataType::sRGB, wgpu::TextureFormat::BC1RGBAUnormSrgb, 8u },
        { ImageComponentFormats::BC2, ImageDataType::UNorm, wgpu::TextureFormat::BC2RGBAUnorm, 16u },
        { ImageComponentFormats::BC2, ImageDataType::sRGB, wgpu::TextureFormat::BC2RGBAUnormSrgb, 16u },
        { ImageComponentFormats::BC3, ImageDataType::UNorm, wgpu::TextureFormat::BC3RGBAUnorm, 16u },
        { ImageComponentFormats::BC3, ImageDataType::sRGB, wgpu::TextureFormat::BC3RGBAUnormSrgb, 16u },
        { ImageComponentFormats::BC4, ImageDataType::UNorm, wgpu::TextureFormat::BC4RUnorm, 8u },
        { ImageComponentFormats::BC4, ImageDataType::SNorm, wgpu::TextureFormat::BC4RSnorm, 8u },
        { ImageComponentFormats::BC5, ImageDataType::UNorm, wgpu::TextureFormat::BC5RGUnorm, 16u },
        { ImageComponentFormats::BC5, ImageDataType::SNorm, wgpu::TextureFormat::BC5RGSnorm, 16u },
        { ImageComponentFormats::BC6H, ImageDataType::UFloat, wgpu::TextureFormat::BC6HRGBUfloat, 16u },
        { ImageComponentFormats::BC6H, ImageDataType::Float, wgpu::TextureFormat::BC6HRGBFloat, 16u },
        { ImageComponentFormats::BC7, ImageDataType::UNorm, wgpu::TextureFormat::BC7RGBAUnorm, 16u },
        { ImageComponentFormats::BC7, ImageDataType::sRGB, wgpu::TextureFormat::BC7RGBAUnormSrgb, 16u },
    };

    for (const FormatRow& row : table)
    {
        if (row.Component == format.ComponentFormat && row.Data == format.DataType)
        {
            return &row;
        }
    }

    return nullptr;
}

wgpu::TextureDimension ToWgpuTextureDimension(ImageType type) noexcept
{
    switch (type)
    {
    case ImageType::Type1D:
        return wgpu::TextureDimension::e1D;
    case ImageType::Type3D:
        return wgpu::TextureDimension::e3D;
    default:
        return wgpu::TextureDimension::e2D;
    }
}

std::optional<wgpu::TextureViewDimension> ToWgpuViewDimension(ImageViewType type) noexcept
{
    switch (type)
    {
    case ImageViewType::Type1D:
        return wgpu::TextureViewDimension::e1D;
    case ImageViewType::Type2D:
        return wgpu::TextureViewDimension::e2D;
    case ImageViewType::Type2DArray:
        return wgpu::TextureViewDimension::e2DArray;
    case ImageViewType::Type3D:
        return wgpu::TextureViewDimension::e3D;
    case ImageViewType::Cube:
        return wgpu::TextureViewDimension::Cube;
    case ImageViewType::CubeArray:
        return wgpu::TextureViewDimension::CubeArray;
    default:
        return std::nullopt;
    }
}

wgpu::AddressMode ToWgpuAddressMode(SamplerCreateInfo::AddressMode mode) noexcept
{
    switch (mode)
    {
    case SamplerCreateInfo::AddressMode::MirroredRepeat:
        return wgpu::AddressMode::MirrorRepeat;
    case SamplerCreateInfo::AddressMode::Repeat:
        return wgpu::AddressMode::Repeat;
    default:
        return wgpu::AddressMode::ClampToEdge;
    }
}

wgpu::FilterMode ToWgpuFilterMode(ImageFilteringMode mode) noexcept
{
    if (mode == ImageFilteringMode::Nearest)
    {
        return wgpu::FilterMode::Nearest;
    }

    return wgpu::FilterMode::Linear;
}

wgpu::MipmapFilterMode ToWgpuMipmapFilterMode(ImageFilteringMode mode) noexcept
{
    if (mode == ImageFilteringMode::Nearest)
    {
        return wgpu::MipmapFilterMode::Nearest;
    }

    return wgpu::MipmapFilterMode::Linear;
}

wgpu::CompareFunction ToWgpuCompareFunction(ImageCompareOp op) noexcept
{
    switch (op)
    {
    case ImageCompareOp::Never:
        return wgpu::CompareFunction::Never;
    case ImageCompareOp::Less:
        return wgpu::CompareFunction::Less;
    case ImageCompareOp::Equal:
        return wgpu::CompareFunction::Equal;
    case ImageCompareOp::LessOrEqual:
        return wgpu::CompareFunction::LessEqual;
    case ImageCompareOp::Greater:
        return wgpu::CompareFunction::Greater;
    case ImageCompareOp::NotEqual:
        return wgpu::CompareFunction::NotEqual;
    case ImageCompareOp::GreaterOrEqual:
        return wgpu::CompareFunction::GreaterEqual;
    case ImageCompareOp::Always:
        return wgpu::CompareFunction::Always;
    default:
        return wgpu::CompareFunction::Undefined;
    }
}

uint32_t MipExtent(uint32_t base_extent, int32_t mip_level) noexcept
{
    return std::max(1u, base_extent >> static_cast<uint32_t>(mip_level));
}

uint32_t MipDepth(const ImageCreateInfo& info, int32_t mip_level) noexcept
{
    if (info.Type != ImageType::Type3D)
    {
        return 1u;
    }

    return MipExtent(info.Depth, mip_level);
}

PieceLayout ComputePieceLayout(const InternalResourceDataContainer::ImageData& piece,
                               const ImageCreateInfo& info,
                               const FormatRow& row) noexcept
{
    const uint32_t blockEdge = IsCompressedFormat(info.Format) ? 4u : 1u;
    const uint32_t width = static_cast<uint32_t>(piece.Width);
    const uint32_t height = static_cast<uint32_t>(piece.Height);

    PieceLayout layout{};
    layout.CopyWidth = (width + blockEdge - 1u) / blockEdge * blockEdge;
    layout.CopyHeight = (height + blockEdge - 1u) / blockEdge * blockEdge;
    layout.CopyDepth = piece.Depth > 0 ? static_cast<uint32_t>(piece.Depth) : MipDepth(info, piece.MipLevel);

    if (info.Type != ImageType::Type3D)
    {
        layout.CopyDepth = 1u;
    }

    layout.RowBytes = layout.CopyWidth / blockEdge * row.BlockBytes;
    layout.Rows = layout.CopyHeight / blockEdge;
    layout.LayerBytes = static_cast<uint64_t>(layout.RowBytes) * layout.Rows * layout.CopyDepth;
    return layout;
}

bool ImagePieceIsValid(const InternalResourceDataContainer::ImageData& piece,
                       const ImageCreateInfo& info,
                       const FormatRow& row,
                       bool require_full_mip) noexcept
{
    const int64_t mipCount = std::max<int64_t>(1, info.MipLevels);
    const int64_t layerCount = std::max<int64_t>(1, info.ArrayLayers);
    const bool is3D = info.Type == ImageType::Type3D;

    if (piece.MipLevel < 0 || piece.MipLevel >= mipCount || piece.NumLayers < 1 || piece.ArrayLayer < 0)
    {
        return false;
    }

    if (piece.ArrayLayer + piece.NumLayers > layerCount && !is3D)
    {
        return false;
    }

    if (is3D && (piece.ArrayLayer != 0 || piece.NumLayers != 1))
    {
        return false;
    }

    if (piece.X < 0 || piece.Y < 0 || piece.Z < 0 || piece.Width <= 0 || piece.Height <= 0)
    {
        return false;
    }

    const int64_t mipWidth = MipExtent(info.Width, piece.MipLevel);
    const int64_t mipHeight = MipExtent(std::max(1u, info.Height), piece.MipLevel);
    const int64_t mipDepth = MipDepth(info, piece.MipLevel);
    const int64_t pieceDepth = piece.Depth > 0 ? piece.Depth : mipDepth;
    const int64_t endX = static_cast<int64_t>(piece.X) + piece.Width;
    const int64_t endY = static_cast<int64_t>(piece.Y) + piece.Height;
    const int64_t endZ = static_cast<int64_t>(piece.Z) + (is3D ? pieceDepth : 1);

    if (endX > mipWidth || endY > mipHeight || endZ > mipDepth)
    {
        return false;
    }

    if (IsCompressedFormat(info.Format))
    {
        const bool startsOnBlock = piece.X % 4 == 0 && piece.Y % 4 == 0;
        const bool endsOnBlock = (endX % 4 == 0 || endX == mipWidth) && (endY % 4 == 0 || endY == mipHeight);

        if (!startsOnBlock || !endsOnBlock)
        {
            return false;
        }
    }

    if (require_full_mip)
    {
        const bool coversMip = piece.X == 0 && piece.Y == 0 && piece.Z == 0 && endX == mipWidth && endY == mipHeight &&
                               (!is3D || pieceDepth == mipDepth);

        if (!coversMip)
        {
            return false;
        }
    }

    const PieceLayout layout = ComputePieceLayout(piece, info, row);
    const uint64_t neededBytes = layout.LayerBytes * static_cast<uint64_t>(piece.NumLayers);
    return piece.Size >= neededBytes && piece.Data != nullptr;
}

bool ImagePiecesAreValid(const ImagePieceVector& pieces, const ImageCreateInfo& info, bool require_full_mip) noexcept
{
    if (pieces.empty())
    {
        return false;
    }

    const FormatRow* row = FindFormatRow(info.Format);

    if (row == nullptr)
    {
        return false;
    }

    const bool is3D = info.Type == ImageType::Type3D;
    const int32_t pieceCount = static_cast<int32_t>(std::ssize(pieces));

    for (int32_t firstIndex = 0; firstIndex < pieceCount; ++firstIndex)
    {
        const InternalResourceDataContainer::ImageData& first = pieces[static_cast<size_t>(firstIndex)];

        if (!ImagePieceIsValid(first, info, *row, require_full_mip))
        {
            return false;
        }

        const int64_t firstMipDepth = MipDepth(info, first.MipLevel);
        const int64_t firstDepth = is3D ? (first.Depth > 0 ? first.Depth : firstMipDepth) : 1;

        for (int32_t secondIndex = firstIndex + 1; secondIndex < pieceCount; ++secondIndex)
        {
            const InternalResourceDataContainer::ImageData& second = pieces[static_cast<size_t>(secondIndex)];

            if (first.MipLevel != second.MipLevel)
            {
                continue;
            }

            const int64_t secondMipDepth = MipDepth(info, second.MipLevel);
            const int64_t secondDepth = is3D ? (second.Depth > 0 ? second.Depth : secondMipDepth) : 1;
            const bool layersOverlap = first.ArrayLayer < static_cast<int64_t>(second.ArrayLayer) + second.NumLayers &&
                                       second.ArrayLayer < static_cast<int64_t>(first.ArrayLayer) + first.NumLayers;
            const bool xOverlaps = first.X < static_cast<int64_t>(second.X) + second.Width &&
                                   second.X < static_cast<int64_t>(first.X) + first.Width;
            const bool yOverlaps = first.Y < static_cast<int64_t>(second.Y) + second.Height &&
                                   second.Y < static_cast<int64_t>(first.Y) + first.Height;
            const bool zOverlaps = first.Z < static_cast<int64_t>(second.Z) + secondDepth &&
                                   second.Z < static_cast<int64_t>(first.Z) + firstDepth;

            if (layersOverlap && xOverlaps && yOverlaps && zOverlaps)
            {
                return false;
            }
        }
    }

    return true;
}

ResourceDescription DescribeBuffer(const BufferCreateInfo& info) noexcept
{
    ResourceDescription description{};
    description.Type = ResourceType::Buffer;
    description.Domain = info.Domain;
    description.BufferUsage = info.Usage;
    description.ByteSize = static_cast<uint64_t>(info.Size);
    return description;
}

ResourceDescription DescribeImage(const ImageCreateInfo& info, ResourceType type, ImageUsageBits initial_usage) noexcept
{
    ResourceDescription description{};
    description.Type = type;
    description.Domain = info.Domain;
    description.ImageUsage = info.Usage;
    description.Dimensions = info.Type;
    description.Format = info.Format;
    description.Width = static_cast<int32_t>(info.Width);
    description.Height = static_cast<int32_t>(info.Height);
    description.Depth = static_cast<int32_t>(info.Depth);
    description.MipLevels = static_cast<int32_t>(info.MipLevels);
    description.ArrayLayers = static_cast<int32_t>(info.ArrayLayers);
    description.SampleCount = static_cast<int32_t>(info.SampleCount);
    description.InitialUsage = initial_usage;
    return description;
}

uint64_t TotalBufferUploadBytes(const BufferPieceVector& pieces) noexcept
{
    uint64_t totalBytes = 0u;

    for (const InternalResourceDataContainer::BufferData& piece : pieces)
    {
        totalBytes += static_cast<uint64_t>(piece.Size);
    }

    return totalBytes;
}

uint64_t TotalImageUploadBytes(const ImagePieceVector& pieces, const ImageCreateInfo& info) noexcept
{
    const FormatRow* row = FindFormatRow(info.Format);

    if (row == nullptr)
    {
        return 0u;
    }

    const bool is3D = info.Type == ImageType::Type3D;
    uint64_t totalBytes = 0u;

    for (const InternalResourceDataContainer::ImageData& piece : pieces)
    {
        const PieceLayout layout = ComputePieceLayout(piece, info, *row);
        const int32_t layerCount = is3D ? 1 : std::max(1, piece.NumLayers);
        totalBytes += layout.LayerBytes * static_cast<uint64_t>(layerCount);
    }

    return totalBytes;
}

uint64_t TotalCopyBytes(std::span<const BufferCopyRegion> regions) noexcept
{
    uint64_t totalBytes = 0u;

    for (const BufferCopyRegion& region : regions)
    {
        totalBytes += region.Size;
    }

    return totalBytes;
}

} // namespace
