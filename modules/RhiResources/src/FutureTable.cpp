#include "FutureTable.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

#include <vulkan/vulkan_core.h>

namespace
{
    // the futures of a full table all share this slot. it stays Failed, and it is never freed
    constexpr int32_t k_FullTableSlot = 0;
    constexpr uint32_t k_NoFreeSlot = 0xFFFF'FFFFu;

    // the bits of FutureSlot::Word
    constexpr uint64_t k_StateMask = 0xFFu;
    constexpr int32_t k_ErrorShift = 8;
    constexpr uint64_t k_ErrorMask = 0xFFu << k_ErrorShift;
    constexpr uint64_t k_SubmittedBit = 1u << 16;
    constexpr uint64_t k_ReleasedBit = 1u << 17;

    [[nodiscard]] uint64_t PackStateAndError(rhi::FutureState state, rhi::ResourceError error) noexcept;
    [[nodiscard]] rhi::FutureState StateOf(uint64_t word) noexcept;
    [[nodiscard]] rhi::ResourceError ErrorOf(uint64_t word) noexcept;
    [[nodiscard]] bool IsFinal(rhi::FutureState state) noexcept;
    [[nodiscard]] uint64_t PackFreeHead(uint32_t tag, uint32_t slot_index) noexcept;
    [[nodiscard]] uint32_t FreeHeadTag(uint64_t head) noexcept;
    [[nodiscard]] uint32_t FreeHeadSlot(uint64_t head) noexcept;
}

namespace rhi
{

    std::string_view ToString(ResourceError error) noexcept
    {
        switch (error)
        {
        case ResourceError::Invalid:
            return "Invalid";
        case ResourceError::Success:
            return "Success";
        case ResourceError::FutureTableFull:
            return "FutureTableFull";
        case ResourceError::StaleResource:
            return "StaleResource";
        case ResourceError::ResourceRetiring:
            return "ResourceRetiring";
        case ResourceError::WrongResourceKind:
            return "WrongResourceKind";
        case ResourceError::InvalidCreateInfo:
            return "InvalidCreateInfo";
        case ResourceError::UnsupportedUsage:
            return "UnsupportedUsage";
        case ResourceError::MissingTransferUsage:
            return "MissingTransferUsage";
        case ResourceError::InvalidFillRange:
            return "InvalidFillRange";
        case ResourceError::CopySizeMismatch:
            return "CopySizeMismatch";
        case ResourceError::CopyShapeMismatch:
            return "CopyShapeMismatch";
        case ResourceError::UnsupportedCopy:
            return "UnsupportedCopy";
        case ResourceError::NotHostVisible:
            return "NotHostVisible";
        case ResourceError::MapFailed:
            return "MapFailed";
        case ResourceError::AllocationFailed:
            return "AllocationFailed";
        case ResourceError::ViewCreationFailed:
            return "ViewCreationFailed";
        case ResourceError::SamplerCreationFailed:
            return "SamplerCreationFailed";
        case ResourceError::StagingFailed:
            return "StagingFailed";
        case ResourceError::RecordingFailed:
            return "RecordingFailed";
        case ResourceError::SubmitFailed:
            return "SubmitFailed";
        case ResourceError::InvalidTimeline:
            return "InvalidTimeline";
        case ResourceError::TimelineAlreadyRegistered:
            return "TimelineAlreadyRegistered";
        case ResourceError::NoFreeTimelineSlot:
            return "NoFreeTimelineSlot";
        case ResourceError::TimelineNotRegistered:
            return "TimelineNotRegistered";
        case ResourceError::InvalidUsageTransition:
            return "InvalidUsageTransition";
        case ResourceError::TooManyWaits:
            return "TooManyWaits";
        case ResourceError::InvalidWait:
            return "InvalidWait";
        case ResourceError::WaitOnHostWrite:
            return "WaitOnHostWrite";
        }

        return "Unknown ResourceError";
    }

    OperationFuture::OperationFuture() noexcept = default;

    OperationFuture::OperationFuture(FutureTable* _table, int32_t slot_index) noexcept :
        table{ _table },
        slotIndex{ slot_index }
    {}

    OperationFuture::~OperationFuture()
    {
        if (table != nullptr)
        {
            table->Release(slotIndex);
        }
    }

    OperationFuture::OperationFuture(OperationFuture&& other) noexcept :
        table{ std::exchange(other.table, nullptr) },
        slotIndex{ std::exchange(other.slotIndex, -1) }
    {}

    OperationFuture& OperationFuture::operator=(OperationFuture&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        if (table != nullptr)
        {
            table->Release(slotIndex);
        }

        table = std::exchange(other.table, nullptr);
        slotIndex = std::exchange(other.slotIndex, -1);
        return *this;
    }

    FutureState OperationFuture::State() const noexcept
    {
        if (table == nullptr)
        {
            return FutureState::Invalid;
        }

        return table->State(slotIndex);
    }

    ResourceError OperationFuture::Error() const noexcept
    {
        if (table == nullptr)
        {
            return ResourceError::Invalid;
        }

        return table->Error(slotIndex);
    }

    GpuWait OperationFuture::Completion() const noexcept
    {
        if (table == nullptr)
        {
            return GpuWait{};
        }

        return table->Completion(slotIndex);
    }

    bool OperationFuture::IsDone() const noexcept
    {
        return IsFinal(State());
    }

    template<typename T>
    ResourceFuture<T>::ResourceFuture() noexcept = default;

    template<typename T>
    ResourceFuture<T>::ResourceFuture(FutureTable* _table, int32_t slot_index) noexcept :
        OperationFuture(_table, slot_index)
    {}

    template<>
    const GraphicsResource& ResourceFuture<GraphicsResource>::Get() const noexcept
    {
        return table->Resource(slotIndex);
    }

    template<>
    void* const& ResourceFuture<void*>::Get() const noexcept
    {
        return table->Pointer(slotIndex);
    }

    template class ResourceFuture<GraphicsResource>;
    template class ResourceFuture<void*>;

    FutureTable::FutureTable() noexcept = default;

    FutureTable::~FutureTable() = default;

    void FutureTable::Initialize(int32_t _capacity, VkDevice _device, VkSemaphore transfer_timeline)
    {
        capacity = _capacity;
        device = _device;
        transferTimeline = transfer_timeline;
        slots = std::make_unique<FutureSlot[]>(static_cast<size_t>(capacity));
        slots[k_FullTableSlot].Word.store(PackStateAndError(FutureState::Failed, ResourceError::FutureTableFull), std::memory_order_relaxed);
        freeHead.store(PackFreeHead(0u, k_NoFreeSlot), std::memory_order_relaxed);

        // pushed from the top down, so that the first allocations take the lowest slots
        for (int32_t index = capacity - 1; index > k_FullTableSlot; --index)
        {
            pushFreeSlot(index);
        }
    }

    OperationFuture FutureTable::AllocateOperation() noexcept
    {
        return OperationFuture(this, allocateSlot());
    }

    ResourceFuture<GraphicsResource> FutureTable::AllocateResource() noexcept
    {
        return ResourceFuture<GraphicsResource>(this, allocateSlot());
    }

    ResourceFuture<void*> FutureTable::AllocatePointer() noexcept
    {
        return ResourceFuture<void*>(this, allocateSlot());
    }

    int32_t FutureTable::SlotOf(const OperationFuture& future) noexcept
    {
        return future.slotIndex;
    }

    const FutureTable* FutureTable::TableOf(const OperationFuture& future) noexcept
    {
        return future.table;
    }

    bool FutureTable::IsFullTableSlot(int32_t slot_index) noexcept
    {
        return slot_index == k_FullTableSlot;
    }

    void FutureTable::StoreResource(int32_t slot_index, const GraphicsResource& resource) noexcept
    {
        slots[slot_index].Resource = resource;
    }

    void FutureTable::SetTransferring(int32_t slot_index, uint64_t ticket) noexcept
    {
        // the release in publish() makes the ticket and the stored resource visible with the state
        slots[slot_index].TicketOrNextFree.store(ticket, std::memory_order_relaxed);
        publish(slot_index, FutureState::Created, ResourceError::Success);
    }

    void FutureTable::SetSubmitted(int32_t slot_index) noexcept
    {
        slots[slot_index].Word.fetch_or(k_SubmittedBit, std::memory_order_release);
    }

    void FutureTable::Complete(int32_t slot_index) noexcept
    {
        publish(slot_index, FutureState::Complete, ResourceError::Success);
    }

    void FutureTable::CompleteWithResource(int32_t slot_index, const GraphicsResource& resource) noexcept
    {
        slots[slot_index].Resource = resource;
        publish(slot_index, FutureState::Complete, ResourceError::Success);
    }

    void FutureTable::CompleteWithPointer(int32_t slot_index, void* pointer) noexcept
    {
        slots[slot_index].Pointer = pointer;
        publish(slot_index, FutureState::Complete, ResourceError::Success);
    }

    void FutureTable::Fail(int32_t slot_index, ResourceError error) noexcept
    {
        publish(slot_index, FutureState::Failed, error);
    }

    FutureState FutureTable::State(int32_t slot_index) const noexcept
    {
        return StateOf(slots[slot_index].Word.load(std::memory_order_acquire));
    }

    ResourceError FutureTable::Error(int32_t slot_index) const noexcept
    {
        const uint64_t word = slots[slot_index].Word.load(std::memory_order_acquire);
        if (StateOf(word) != FutureState::Failed)
        {
            return ResourceError::Success;
        }

        return ErrorOf(word);
    }

    GpuWait FutureTable::Completion(int32_t slot_index) const noexcept
    {
        const FutureSlot& slot = slots[slot_index];
        const uint64_t word = slot.Word.load(std::memory_order_acquire);
        const uint64_t ticket = slot.TicketOrNextFree.load(std::memory_order_relaxed);
        if (ticket == 0u)
        {
            return GpuWait{};
        }

        return GpuWait
        {
            TimelineSemaphoreHandle(reinterpret_cast<uint64_t>(transferTimeline)),
            ticket,
            (word & k_SubmittedBit) != 0u
        };
    }

    const GraphicsResource& FutureTable::Resource(int32_t slot_index) const noexcept
    {
        return slots[slot_index].Resource;
    }

    void* const& FutureTable::Pointer(int32_t slot_index) const noexcept
    {
        return slots[slot_index].Pointer;
    }

    FutureState FutureTable::Wait(int32_t slot_index, uint64_t timeout_ns) const noexcept
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point startTime = Clock::now();
        const auto remainingNs = [&]() -> uint64_t
        {
            if (timeout_ns == k_WaitForever)
            {
                return timeout_ns;
            }

            const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startTime).count();
            return std::cmp_greater_equal(elapsedNs, timeout_ns) ? 0u : timeout_ns - static_cast<uint64_t>(elapsedNs);
        };

        // most calls never reach the transfer system, so the CPU-side state comes first
        FutureState current = State(slot_index);
        while (current == FutureState::Pending)
        {
            if (remainingNs() == 0u)
            {
                return current;
            }

            std::this_thread::yield();
            current = State(slot_index);
        }

        if (current != FutureState::Created)
        {
            return current;
        }

        // sleep on the GPU instead of spinning. a host wait may name a value that no submit has signaled yet
        const uint64_t waitValue = slots[slot_index].TicketOrNextFree.load(std::memory_order_relaxed);
        const VkSemaphoreWaitInfo waitInfo
        {
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            nullptr,
            0u,
            1u,
            &transferTimeline,
            &waitValue
        };

        const VkResult result = vkWaitSemaphores(device, &waitInfo, remainingNs());
        if (result != VK_SUCCESS)
        {
            return State(slot_index);
        }

        // only the transfer thread writes the final state, so a transfer that failed while recording stays Failed. it
        // reads the timeline once per loop, so this wait is short
        current = State(slot_index);
        while (!IsFinal(current))
        {
            if (remainingNs() == 0u)
            {
                return current;
            }

            std::this_thread::yield();
            current = State(slot_index);
        }

        return current;
    }

    void FutureTable::Release(int32_t slot_index) noexcept
    {
        if (slot_index == k_FullTableSlot)
        {
            return;
        }

        const uint64_t previous = slots[slot_index].Word.fetch_or(k_ReleasedBit, std::memory_order_acq_rel);
        if (IsFinal(StateOf(previous)))
        {
            freeSlot(slot_index);
        }
    }

    int64_t FutureTable::LiveCount() const noexcept
    {
        return liveCount.load(std::memory_order_relaxed);
    }

    int64_t FutureTable::FullCount() const noexcept
    {
        return fullCount.load(std::memory_order_relaxed);
    }

    int32_t FutureTable::popFreeSlot() noexcept
    {
        uint64_t head = freeHead.load(std::memory_order_acquire);
        while (FreeHeadSlot(head) != k_NoFreeSlot)
        {
            // a stale read here is harmless: the tag makes the compare-exchange fail, and the loop reads again
            const uint32_t slotIndex = FreeHeadSlot(head);
            const uint64_t next = slots[slotIndex].TicketOrNextFree.load(std::memory_order_relaxed);
            const uint64_t desired = PackFreeHead(FreeHeadTag(head) + 1u, static_cast<uint32_t>(next));
            if (freeHead.compare_exchange_weak(head, desired, std::memory_order_acquire, std::memory_order_acquire))
            {
                return static_cast<int32_t>(slotIndex);
            }
        }

        return k_FullTableSlot;
    }

    void FutureTable::pushFreeSlot(int32_t slot_index) noexcept
    {
        uint64_t head = freeHead.load(std::memory_order_relaxed);
        uint64_t desired = 0u;
        do
        {
            slots[slot_index].TicketOrNextFree.store(FreeHeadSlot(head), std::memory_order_relaxed);
            desired = PackFreeHead(FreeHeadTag(head) + 1u, static_cast<uint32_t>(slot_index));
        } while (!freeHead.compare_exchange_weak(head, desired, std::memory_order_release, std::memory_order_relaxed));
    }

    int32_t FutureTable::allocateSlot() noexcept
    {
        const int32_t slotIndex = popFreeSlot();
        if (slotIndex == k_FullTableSlot)
        {
            fullCount.fetch_add(1, std::memory_order_relaxed);
            return k_FullTableSlot;
        }

        // the message push that follows publishes these to the worker
        FutureSlot& slot = slots[slotIndex];
        slot.TicketOrNextFree.store(0u, std::memory_order_relaxed);
        slot.Resource = GraphicsResource::Null();
        slot.Pointer = nullptr;
        slot.Word.store(PackStateAndError(FutureState::Pending, ResourceError::Success), std::memory_order_relaxed);
        liveCount.fetch_add(1, std::memory_order_relaxed);
        return slotIndex;
    }

    void FutureTable::publish(int32_t slot_index, FutureState state, ResourceError error) noexcept
    {
        // a compare-exchange, not a store, so that a release from the owner in between is not lost
        std::atomic<uint64_t>& word = slots[slot_index].Word;
        uint64_t current = word.load(std::memory_order_relaxed);
        uint64_t desired = 0u;
        do
        {
            desired = (current & ~(k_StateMask | k_ErrorMask)) | PackStateAndError(state, error);
        } while (!word.compare_exchange_weak(current, desired, std::memory_order_acq_rel, std::memory_order_relaxed));

        if (IsFinal(state) && (current & k_ReleasedBit) != 0u)
        {
            freeSlot(slot_index);
        }
    }

    void FutureTable::freeSlot(int32_t slot_index) noexcept
    {
        // only the last of the owner and the producer gets here, so nothing else touches the slot now
        slots[slot_index].Word.store(0u, std::memory_order_relaxed);
        liveCount.fetch_sub(1, std::memory_order_relaxed);
        pushFreeSlot(slot_index);
    }

}

namespace
{

    uint64_t PackStateAndError(rhi::FutureState state, rhi::ResourceError error) noexcept
    {
        return static_cast<uint64_t>(state) | (static_cast<uint64_t>(error) << k_ErrorShift);
    }

    rhi::FutureState StateOf(uint64_t word) noexcept
    {
        return static_cast<rhi::FutureState>(word & k_StateMask);
    }

    rhi::ResourceError ErrorOf(uint64_t word) noexcept
    {
        return static_cast<rhi::ResourceError>((word & k_ErrorMask) >> k_ErrorShift);
    }

    bool IsFinal(rhi::FutureState state) noexcept
    {
        return state == rhi::FutureState::Complete || state == rhi::FutureState::Failed;
    }

    uint64_t PackFreeHead(uint32_t tag, uint32_t slot_index) noexcept
    {
        return (static_cast<uint64_t>(tag) << 32) | slot_index;
    }

    uint32_t FreeHeadTag(uint64_t head) noexcept
    {
        return static_cast<uint32_t>(head >> 32);
    }

    uint32_t FreeHeadSlot(uint64_t head) noexcept
    {
        return static_cast<uint32_t>(head & 0xFFFF'FFFFu);
    }

}
