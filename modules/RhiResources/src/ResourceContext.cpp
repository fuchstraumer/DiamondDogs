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
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace
{
    /** @brief Allocates the future for a copy between existing resources and pushes the message. A full table pushes nothing. */
    [[nodiscard]] rhi::OperationFuture PushCopyContents(
        ResourceContextImpl& impl,
        CopyResourceContentsMessage&& message,
        std::span<const rhi::TransferWait> waits);
    /** @brief Fails the future on the calling thread, with no message, when there are more waits than a message holds.
     *  Returns true when it did. The slot is still owned by this thread, so no one else writes it. */
    [[nodiscard]] bool RefuseTooManyWaits(ResourceContextImpl& impl, int32_t slot, std::span<const rhi::TransferWait> waits);
    [[nodiscard]] TransferWaitSet MakeWaitSet(std::span<const rhi::TransferWait> waits) noexcept;
}

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

    CreateBufferMessage message = MakeCreateBufferMessage(create_info, slot);

    if (!initial_data.empty())
    {
        message.InitialData = InternalResourceDataContainer(initial_data);
    }

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CreateImage(const ImageCreateInfo& create_info)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CreateImageMessage message = MakeCreateImageMessage(create_info, slot);

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CreateImage(
    const ImageCreateInfo& create_info,
    std::span<const RhiImageResourceData> initial_data,
    ImageUsageBits usage_after_upload)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    CreateImageMessage message = MakeCreateImageMessage(create_info, slot);
    message.UsageAfterUpload = usage_after_upload;

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
    std::span<const RhiBufferResourceData> data,
    std::span<const rhi::TransferWait> waits)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    if (RefuseTooManyWaits(*impl, slot, waits))
    {
        return future;
    }

    SetBufferDataMessage message(buffer, data);
    message.Waits = MakeWaitSet(waits);
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::SetImageData(
    GraphicsResource image,
    rhi::ImageUsageTransition usage,
    std::span<const RhiImageResourceData> data,
    std::span<const rhi::TransferWait> waits)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    if (RefuseTooManyWaits(*impl, slot, waits))
    {
        return future;
    }

    SetImageDataMessage message(image, data);
    message.Usage = usage;
    message.Waits = MakeWaitSet(waits);
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::FillBuffer(
    GraphicsResource buffer,
    uint32_t value,
    size_t offset,
    size_t size,
    std::span<const rhi::TransferWait> waits)
{
    rhi::OperationFuture future = impl->Futures().AllocateOperation();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    if (RefuseTooManyWaits(*impl, slot, waits))
    {
        return future;
    }

    FillResourceMessage message;
    message.Resource = buffer;
    message.Value = value;
    message.Offset = offset;
    message.Size = size;
    message.Waits = MakeWaitSet(waits);
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
    bool copy_contents,
    std::span<const rhi::TransferWait> waits)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    if (RefuseTooManyWaits(*impl, slot, waits))
    {
        return future;
    }

    CopyResourceMessage message;
    message.SourceResource = src;
    message.SourceIsImage = false;
    message.CopyContents = copy_contents;
    message.Waits = MakeWaitSet(waits);
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::ResourceFuture<GraphicsResource> ResourceContext::CopyImage(
    GraphicsResource src,
    rhi::ImageUsageTransition src_usage,
    ImageUsageBits copy_usage_after,
    std::span<const rhi::TransferWait> waits)
{
    rhi::ResourceFuture<GraphicsResource> future = impl->Futures().AllocateResource();
    const int32_t slot = rhi::FutureTable::SlotOf(future);
    if (rhi::FutureTable::IsFullTableSlot(slot))
    {
        return future;
    }

    if (RefuseTooManyWaits(*impl, slot, waits))
    {
        return future;
    }

    CopyResourceMessage message;
    message.SourceResource = src;
    message.SourceIsImage = true;
    message.SourceUsage = src_usage;
    message.CopyUsageAfter = copy_usage_after;
    message.CopyContents = true;
    message.Waits = MakeWaitSet(waits);
    message.Future = slot;

    impl->PushMessage(std::move(message));
    return future;
}

rhi::OperationFuture ResourceContext::CopyBufferContents(
    GraphicsResource src,
    GraphicsResource dst,
    std::span<const rhi::TransferWait> waits)
{
    CopyResourceContentsMessage message;
    message.Kind = CopyKind::BufferToBuffer;
    message.SourceResource = src;
    message.DestinationResource = dst;

    return PushCopyContents(*impl, std::move(message), waits);
}

rhi::OperationFuture ResourceContext::CopyImageToBuffer(
    GraphicsResource image,
    rhi::ImageUsageTransition image_usage,
    GraphicsResource buffer,
    std::span<const rhi::TransferWait> waits)
{
    CopyResourceContentsMessage message;
    message.Kind = CopyKind::ImageToBuffer;
    message.SourceResource = image;
    message.SourceUsage = image_usage;
    message.DestinationResource = buffer;

    return PushCopyContents(*impl, std::move(message), waits);
}

rhi::OperationFuture ResourceContext::CopyBufferToImage(
    GraphicsResource buffer,
    GraphicsResource image,
    rhi::ImageUsageTransition image_usage,
    std::span<const rhi::TransferWait> waits)
{
    CopyResourceContentsMessage message;
    message.Kind = CopyKind::BufferToImage;
    message.SourceResource = buffer;
    message.DestinationResource = image;
    message.DestinationUsage = image_usage;

    return PushCopyContents(*impl, std::move(message), waits);
}

rhi::OperationFuture ResourceContext::CopyImageContents(
    GraphicsResource src,
    rhi::ImageUsageTransition src_usage,
    GraphicsResource dst,
    rhi::ImageUsageTransition dst_usage,
    std::span<const rhi::TransferWait> waits)
{
    CopyResourceContentsMessage message;
    message.Kind = CopyKind::ImageToImage;
    message.SourceResource = src;
    message.SourceUsage = src_usage;
    message.DestinationResource = dst;
    message.DestinationUsage = dst_usage;

    return PushCopyContents(*impl, std::move(message), waits);
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

MemoryBudgetSnapshot ResourceContext::MemoryBudgets() const noexcept
{
    return impl->MemoryBudgets();
}

namespace
{
    rhi::OperationFuture PushCopyContents(
        ResourceContextImpl& impl,
        CopyResourceContentsMessage&& message,
        std::span<const rhi::TransferWait> waits)
    {
        rhi::OperationFuture future = impl.Futures().AllocateOperation();
        const int32_t slot = rhi::FutureTable::SlotOf(future);
        if (rhi::FutureTable::IsFullTableSlot(slot))
        {
            return future;
        }

        if (RefuseTooManyWaits(impl, slot, waits))
        {
            return future;
        }

        message.Waits = MakeWaitSet(waits);
        message.Future = slot;

        impl.PushMessage(std::move(message));
        return future;
    }

    bool RefuseTooManyWaits(ResourceContextImpl& impl, int32_t slot, std::span<const rhi::TransferWait> waits)
    {
        if (std::ssize(waits) <= rhi::k_MaxTransferWaits)
        {
            return false;
        }

        impl.Futures().Fail(slot, rhi::ResourceError::TooManyWaits);
        return true;
    }

    TransferWaitSet MakeWaitSet(std::span<const rhi::TransferWait> waits) noexcept
    {
        TransferWaitSet result;
        result.Count = static_cast<int32_t>(waits.size());
        std::ranges::copy(waits, result.Items.begin());
        return result;
    }
}
