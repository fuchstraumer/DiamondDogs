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
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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
[[nodiscard]] bool BufferPiecesAreValid(const BufferPieceVector& pieces, uint64_t buffer_size);
[[nodiscard]] uint64_t TotalBufferUploadBytes(const BufferPieceVector& pieces) noexcept;
[[nodiscard]] bool BufferCopyRegionsAreValid(std::span<const BufferCopyRegion> regions,
                                             uint64_t source_size,
                                             uint64_t destination_size,
                                             bool same_buffer);
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
[[nodiscard]] bool ImagePiecesAreValid(const ImagePieceVector& pieces, const ImageCreateInfo& info, bool require_full_mip);
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

    if (create_info.MaxFutures < 2 || create_info.StagingRingSize < k_MinStagingRingSize)
    {
        return ResourceContextError::InvalidCreateInfo;
    }

    futures.Initialize(create_info.MaxFutures, rhi::TimelineSemaphoreHandle{}, nullptr);

    pendingUploads.clear();
    remainingUploadBudget = k_UploadBytesPerTick;
    aliveToken = std::make_shared<bool>(true);
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

    if (aliveToken)
    {
        *aliveToken = false;
    }

    while (!pendingUploads.empty())
    {
        futures.Fail(pendingUploads.front().FutureSlot, rhi::ResourceError::ResourceRetiring);
        pendingUploads.pop_front();
    }

    resourceRegistry.clear();
    queue = wgpu::Queue{};
    device = wgpu::Device{};
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
    counters.ResourcesRetired = resourcesRetired.load(std::memory_order_relaxed);
    counters.FuturesLive = futures.LiveCount();
    counters.FutureTableFull = futures.FullCount();
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

        if (pendingUploads.empty() && totalBytes <= remainingUploadBudget)
        {
            writeBufferPieces(buffer, *pieces);
            remainingUploadBudget -= totalBytes;
            futures.CompleteWithResource(message.Future, *created, description);
        }
        else
        {
            PendingUpload upload{};
            upload.Buffer = buffer;
            upload.Pieces = std::move(*pieces);
            upload.PieceIndex = 0;
            upload.ByteOffset = 0u;
            upload.FutureSlot = message.Future;
            upload.IsCreate = true;
            upload.Resource = *created;
            upload.Description = description;
            pendingUploads.push_back(std::move(upload));
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

    const ImagePieceVector* pieces = nullptr;

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

    ImageUsageBits initialUsage = ImageUsageBits::Invalid;

    if (pieces != nullptr)
    {
        const entt::entity entity = entt::entity(created->EntityHandle);
        const wgpu::Texture& texture = resourceRegistry.get<wgpu::Texture>(entity);
        writeImagePieces(texture, message.ImageInfo, *pieces);
        initialUsage = message.UsageAfterUpload;
    }

    futures.CompleteWithResource(message.Future, *created, DescribeImage(message.ImageInfo, created->Type, initialUsage));
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

    if (pendingUploads.empty() && totalBytes <= remainingUploadBudget)
    {
        writeBufferPieces(buffer, *pieces);
        remainingUploadBudget -= totalBytes;
        futures.Complete(message.Future);
        return;
    }

    PendingUpload upload{};
    upload.Buffer = buffer;
    upload.Pieces = std::move(*pieces);
    upload.PieceIndex = 0;
    upload.ByteOffset = 0u;
    upload.FutureSlot = message.Future;
    upload.IsCreate = false;
    pendingUploads.push_back(std::move(upload));
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
    const ImagePieceVector* pieces = std::get_if<ImagePieceVector>(&message.Data.DataVector);

    if (pieces == nullptr || !ImagePiecesAreValid(*pieces, info, discarded))
    {
        futures.Fail(message.Future, rhi::ResourceError::InvalidRegion);
        return;
    }

    writeImagePieces(texture, info, *pieces);
    futures.Complete(message.Future);
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

    const rhi::ResourceError error = copyBufferToBuffer(message);

    if (error != rhi::ResourceError::Success)
    {
        futures.Fail(message.Future, error);
        return;
    }

    futures.Complete(message.Future);
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

    destroyEntityResources(*entity);
    resourcesRetired.fetch_add(1, std::memory_order_relaxed);
    futures.Complete(message.Future);
}

void ResourceContextImpl::processMessage([[maybe_unused]] ExitWorkerMessage&& message) noexcept
{
}

void ResourceContextImpl::processMessage([[maybe_unused]] TickMessage&& message)
{
    remainingUploadBudget = k_UploadBytesPerTick;
    drainPendingUploads();
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
        queue.WriteBuffer(buffer, piece.DestinationOffset, piece.Data.get(), piece.Size);
    }
}

void ResourceContextImpl::drainPendingUploads()
{
    while (!pendingUploads.empty())
    {
        PendingUpload& upload = pendingUploads.front();

        while (upload.PieceIndex < static_cast<int32_t>(std::ssize(upload.Pieces)))
        {
            const InternalResourceDataContainer::BufferData& piece = upload.Pieces[upload.PieceIndex];
            const uint64_t pieceSize = static_cast<uint64_t>(piece.Size);

            if (upload.ByteOffset >= pieceSize)
            {
                ++upload.PieceIndex;
                upload.ByteOffset = 0u;
                continue;
            }

            const uint64_t remainingInPiece = pieceSize - upload.ByteOffset;

            if (remainingInPiece >= 4u)
            {
                const uint64_t rawChunk = std::min(remainingInPiece, remainingUploadBudget);
                const uint64_t alignedChunk = (rawChunk / 4u) * 4u;

                if (alignedChunk == 0u)
                {
                    return;
                }

                queue.WriteBuffer(upload.Buffer,
                                  piece.DestinationOffset + upload.ByteOffset,
                                  piece.Data.get() + upload.ByteOffset,
                                  static_cast<size_t>(alignedChunk));
                upload.ByteOffset += alignedChunk;
                remainingUploadBudget -= alignedChunk;
            }
            else
            {
                if (remainingUploadBudget < 4u)
                {
                    return;
                }

                std::array<std::byte, 4> tail{};
                std::copy_n(piece.Data.get() + upload.ByteOffset, static_cast<size_t>(remainingInPiece), tail.data());
                queue.WriteBuffer(upload.Buffer,
                                  piece.DestinationOffset + upload.ByteOffset,
                                  tail.data(),
                                  4u);
                upload.ByteOffset = pieceSize;
                remainingUploadBudget -= 4u;
            }

            if (upload.ByteOffset >= pieceSize)
            {
                ++upload.PieceIndex;
                upload.ByteOffset = 0u;
            }
        }

        if (upload.IsCreate)
        {
            futures.CompleteWithResource(upload.FutureSlot, upload.Resource, upload.Description);
        }
        else
        {
            futures.Complete(upload.FutureSlot);
        }

        pendingUploads.pop_front();
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

rhi::ResourceError ResourceContextImpl::copyBufferToBuffer(const CopyResourceContentsMessage& message)
{
    const std::expected<entt::entity, rhi::ResourceError> source = resolveBuffer(message.SourceResource);

    if (!source.has_value())
    {
        return source.error();
    }

    const std::expected<entt::entity, rhi::ResourceError> destination = resolveBuffer(message.DestinationResource);

    if (!destination.has_value())
    {
        return destination.error();
    }

    const wgpu::Buffer sourceBuffer = resourceRegistry.get<wgpu::Buffer>(*source);
    const wgpu::Buffer destinationBuffer = resourceRegistry.get<wgpu::Buffer>(*destination);
    const bool sourceCanRead = (sourceBuffer.GetUsage() & wgpu::BufferUsage::CopySrc) != wgpu::BufferUsage::None;
    const bool destinationCanWrite = (destinationBuffer.GetUsage() & wgpu::BufferUsage::CopyDst) != wgpu::BufferUsage::None;

    if (!sourceCanRead || !destinationCanWrite)
    {
        return rhi::ResourceError::MissingTransferUsage;
    }

    const uint64_t sourceSize = sourceBuffer.GetSize();
    const uint64_t destinationSize = destinationBuffer.GetSize();
    const std::span<const BufferCopyRegion> regions = message.Regions.View();
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    if (regions.empty())
    {
        if (sourceSize > destinationSize)
        {
            return rhi::ResourceError::CopySizeMismatch;
        }

        encoder.CopyBufferToBuffer(sourceBuffer, 0u, destinationBuffer, 0u, sourceSize);
    }
    else
    {
        const bool sameBuffer = *source == *destination;

        if (!BufferCopyRegionsAreValid(regions, sourceSize, destinationSize, sameBuffer))
        {
            return rhi::ResourceError::InvalidRegion;
        }

        for (const BufferCopyRegion& region : regions)
        {
            encoder.CopyBufferToBuffer(sourceBuffer, region.SourceOffset, destinationBuffer, region.DestinationOffset, region.Size);
        }
    }

    const wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
    return rhi::ResourceError::Success;
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

bool BufferPiecesAreValid(const BufferPieceVector& pieces, uint64_t buffer_size)
{
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    ranges.reserve(pieces.size());

    for (const InternalResourceDataContainer::BufferData& piece : pieces)
    {
        if (!BufferRangeIsValid(piece.DestinationOffset, piece.Size, buffer_size))
        {
            return false;
        }

        ranges.emplace_back(piece.DestinationOffset, piece.DestinationOffset + piece.Size);
    }

    std::ranges::sort(ranges);

    for (size_t index = 1; index < ranges.size(); ++index)
    {
        if (ranges[index].first < ranges[index - 1].second)
        {
            return false;
        }
    }

    return true;
}

bool BufferCopyRegionsAreValid(std::span<const BufferCopyRegion> regions,
                               uint64_t source_size,
                               uint64_t destination_size,
                               bool same_buffer)
{
    for (const BufferCopyRegion& region : regions)
    {
        const bool sourceValid = BufferRangeIsValid(region.SourceOffset, region.Size, source_size);
        const bool destinationValid = BufferRangeIsValid(region.DestinationOffset, region.Size, destination_size);

        if (region.Size == 0u || !sourceValid || !destinationValid)
        {
            return false;
        }
    }

    if (!same_buffer)
    {
        return true;
    }

    for (const BufferCopyRegion& first : regions)
    {
        for (const BufferCopyRegion& second : regions)
        {
            const bool sourceHitsDestination = first.SourceOffset < second.DestinationOffset + second.Size &&
                                               second.DestinationOffset < first.SourceOffset + first.Size;

            if (sourceHitsDestination)
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

bool ImagePiecesAreValid(const ImagePieceVector& pieces, const ImageCreateInfo& info, bool require_full_mip)
{
    const FormatRow* row = FindFormatRow(info.Format);

    if (row == nullptr)
    {
        return false;
    }

    return std::ranges::all_of(pieces, [&info, row, require_full_mip](const InternalResourceDataContainer::ImageData& piece)
    {
        return ImagePieceIsValid(piece, info, *row, require_full_mip);
    });
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

} // namespace
