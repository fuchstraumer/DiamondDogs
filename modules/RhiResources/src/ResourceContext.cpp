#include "ResourceContext.hpp"
#include "FutureTable.hpp"
#include "ResourceContextImpl.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceMessageTypesInternal.hpp"
#include "ResourceTypes.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

std::string_view ToString(ResourceContextError error) noexcept
{
    switch (error)
    {
    case ResourceContextError::Invalid:
        return "Invalid";
    case ResourceContextError::Success:
        return "Success";
    case ResourceContextError::AlreadyInitialized:
        return "AlreadyInitialized";
    case ResourceContextError::InvalidCreateInfo:
        return "InvalidCreateInfo";
    case ResourceContextError::AllocatorCreationFailed:
        return "AllocatorCreationFailed";
    case ResourceContextError::TransferSetupFailed:
        return "TransferSetupFailed";
    }

    return "Unknown ResourceContextError";
}

ResourceContext::ResourceContext()
{
    impl = std::make_unique<ResourceContextImpl>();
}

ResourceContext::~ResourceContext()
{
    // fixed: the destructor did nothing, so the worker threads were still joinable when impl was destroyed (std::terminate)
    impl->Destroy();
}

ResourceContextError ResourceContext::Initialize(const ResourceContextCreateInfo& create_info)
{
    return impl->Construct(create_info);
}

// every call allocates its future first. a full table gives a future that is already Failed, and then the call sends
// no message, because no slot exists for the worker to write

rhi::ResourceFuture<GraphicsResource> ResourceContext::CreateBuffer(
    const BufferCreateInfo& create_info,
    std::span<const RhiBufferResourceData> initial_data)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CreateBufferMessage message;
    message.BufferInfo = create_info;
    message.Name = DebugName(create_info.Name);
    message.BufferInfo.Name = {};
    message.Future = slot;

    if (!initial_data.empty())
    {
        message.InitialData = InternalResourceDataContainer(initial_data);
    }

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CreateImage(
    const ImageCreateInfo& create_info,
    std::span<const RhiImageResourceData> initial_data)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CreateImageMessage message;
    message.ImageInfo = create_info;
    message.Name = DebugName(create_info.Name);
    message.ImageInfo.Name = {};
    message.Future = slot;

    // the sampler takes its name from the image, so its own view is never read
    if (message.ImageInfo.SamplerInfo.has_value())
    {
        message.ImageInfo.SamplerInfo->Name = {};
    }

    if (!initial_data.empty())
    {
        message.InitialData = InternalResourceDataContainer(initial_data);
    }

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CreateSampler(const SamplerCreateInfo& create_info)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CreateSamplerMessage message;
    message.SamplerInfo = create_info;
    message.Name = DebugName(create_info.Name);
    message.SamplerInfo.Name = {};
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::SetBufferData(
    GraphicsResource buffer,
    std::span<const RhiBufferResourceData> data)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    SetBufferDataMessage message(buffer, data);
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::SetImageData(
    GraphicsResource image,
    std::span<const RhiImageResourceData> data)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    SetImageDataMessage message(image, data);
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::FillBuffer(
    GraphicsResource buffer,
    uint32_t value,
    size_t offset,
    size_t size)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    FillResourceMessage message;
    message.Resource = buffer;
    message.Value = value;
    message.Offset = offset;
    message.Size = size;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<void*> ResourceContext::MapBuffer(
    GraphicsResource buffer,
    size_t size,
    size_t offset)
{
    rhi::ResourceFuture<void*> future = impl->Futures().AllocatePointer();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    MapResourceMessage message;
    message.Resource = buffer;
    message.Size = size;
    message.Offset = offset;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::UnmapBuffer(
    GraphicsResource buffer,
    size_t size,
    size_t offset)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    UnmapResourceMessage message;
    message.Resource = buffer;
    message.Size = size;
    message.Offset = offset;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CopyBuffer(
    GraphicsResource src,
    bool copy_contents)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CopyResourceMessage message;
    message.SourceResource = src;
    message.CopyContents = copy_contents;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CopyImage(GraphicsResource src_image)
{
    // fixed: declared, never defined
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CopyResourceMessage message;
    message.SourceResource = src_image;
    message.CopyContents = true;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::CopyBufferContents(
    GraphicsResource src,
    GraphicsResource dst)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CopyResourceContentsMessage message;
    message.SourceResource = src;
    message.DestinationResource = dst;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::DestroyResource(
    GraphicsResource resource)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    DestroyResourceMessage message;
    message.Resource = resource;
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::RegisterConsumerTimeline(rhi::TimelineSemaphoreHandle timeline)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    impl->PushMessage(RegisterConsumerTimelineMessage{ timeline.Get(), slot });
    return future;
}

rhi::OperationFuture ResourceContext::UnregisterConsumerTimeline(rhi::TimelineSemaphoreHandle timeline)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    impl->PushMessage(UnregisterConsumerTimelineMessage{ timeline.Get(), slot });
    return future;
}

void ResourceContext::Tick(std::span<const ConsumerTimelineValue> pending_values)
{
    TickMessage message;
    const size_t count = std::min(pending_values.size(), message.Points.size());
    for (size_t i = 0; i < count; ++i)
    {
        message.Points[i] = ConsumerTimelinePoint{ pending_values[i].Timeline.Get(), pending_values[i].PendingValue };
    }

    message.Count = static_cast<int32_t>(count);
    impl->PushMessage(std::move(message));
}

rhi::FutureState ResourceContext::Wait(const rhi::OperationFuture& future, uint64_t timeout_ns) const
{
    // the future's own table, so that a future of another context still waits correctly
    const rhi::FutureTable* table = rhi::FutureTable::TableOf(future);
    if (table == nullptr)
    {
        return rhi::FutureState::Invalid;
    }

    return table->Wait(rhi::FutureTable::SlotOf(future), timeout_ns);
}

ResourceContextCounters ResourceContext::Counters() const noexcept
{
    return impl->Counters();
}
