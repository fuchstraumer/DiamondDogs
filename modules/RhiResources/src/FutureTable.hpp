#pragma once
#ifndef DIAMOND_DOGS_RHI_FUTURE_TABLE_HPP
#define DIAMOND_DOGS_RHI_FUTURE_TABLE_HPP
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include <atomic>
#include <cstdint>
#include <memory>

#include <vulkan/vulkan_core.h>

namespace rhi
{

    /** @brief The fixed table of slots behind every future of one ResourceContext. ResourceContext allocates a slot on
     *  the calling thread, the worker and the transfer thread write it, and the future reads it and releases it. */
    class FutureTable
    {
    public:
        FutureTable() noexcept;
        ~FutureTable();
        FutureTable(const FutureTable&) = delete;
        FutureTable& operator=(const FutureTable&) = delete;

        /** @brief Allocates the slots. `capacity` counts the slot that full-table futures share, so the table holds
         *  `_capacity - 1` live futures. */
        void Initialize(int32_t _capacity, VkDevice _device, VkSemaphore transfer_timeline);

        // any thread. a full table returns a future that is already Failed with FutureTableFull
        [[nodiscard]] OperationFuture AllocateOperation() noexcept;
        [[nodiscard]] ResourceFuture<GraphicsResource> AllocateResource() noexcept;
        [[nodiscard]] ResourceFuture<void*> AllocatePointer() noexcept;
        /** @brief Returns the slot that the future owns. Messages carry this index, not the future. */
        [[nodiscard]] static int32_t SlotOf(const OperationFuture& future) noexcept;
        /** @brief Returns the table that the future belongs to, or null for a default-constructed or moved-from one. */
        [[nodiscard]] static const FutureTable* TableOf(const OperationFuture& future) noexcept;
        /** @brief Returns true when `slot_index` is the shared full-table slot, so the call must send no message. */
        [[nodiscard]] static bool IsFullTableSlot(int32_t slot_index) noexcept;

        // the producers: the context worker, and the transfer thread. after a final state, the producer must not
        // touch the slot again, because the owner may have freed it
        /** @brief Stores the resource without a state change. The next SetTransferring publishes it. */
        void StoreResource(int32_t slot_index, const GraphicsResource& resource) noexcept;
        /** @brief Stores the ticket, then publishes Created. */
        void SetTransferring(int32_t slot_index, uint64_t ticket) noexcept;
        void SetSubmitted(int32_t slot_index) noexcept;
        void Complete(int32_t slot_index) noexcept;
        void CompleteWithResource(int32_t slot_index, const GraphicsResource& resource) noexcept;
        void CompleteWithPointer(int32_t slot_index, void* pointer) noexcept;
        void Fail(int32_t slot_index, ResourceError error) noexcept;

        // the owner's reads
        [[nodiscard]] FutureState State(int32_t slot_index) const noexcept;
        [[nodiscard]] ResourceError Error(int32_t slot_index) const noexcept;
        [[nodiscard]] GpuWait Completion(int32_t slot_index) const noexcept;
        [[nodiscard]] const GraphicsResource& Resource(int32_t slot_index) const noexcept;
        [[nodiscard]] void* const& Pointer(int32_t slot_index) const noexcept;
        /** @brief Blocks until the future is done or the timeout passes. Returns the state at that moment. */
        [[nodiscard]] FutureState Wait(int32_t slot_index, uint64_t timeout_ns) const noexcept;
        /** @brief The owner gives up the slot. Whichever of the owner and the producer comes last frees it. */
        void Release(int32_t slot_index) noexcept;

        [[nodiscard]] int64_t LiveCount() const noexcept;
        [[nodiscard]] int64_t FullCount() const noexcept;

    private:

        // one cache line. Word packs the state, the error, and the submitted and released bits, so that the owner and
        // the producers change them with one atomic operation each
        struct alignas(64) FutureSlot
        {
            std::atomic<uint64_t> Word{ 0u };
            // the ticket while the slot is in use, the next free slot while it is on the free list
            std::atomic<uint64_t> TicketOrNextFree{ 0u };
            GraphicsResource Resource{};
            void* Pointer{ nullptr };
        };

        static_assert(sizeof(FutureSlot) == 64, "FutureSlot is sized to one cache line");

        [[nodiscard]] int32_t popFreeSlot() noexcept;
        void pushFreeSlot(int32_t slot_index) noexcept;
        /** @brief Allocates a slot and sets it Pending. Returns the full-table slot when none is free. */
        [[nodiscard]] int32_t allocateSlot() noexcept;
        /** @brief Sets the state and the error, and keeps the other bits. Frees the slot when the state is final and
         *  the owner already released it. */
        void publish(int32_t slot_index, FutureState state, ResourceError error) noexcept;
        void freeSlot(int32_t slot_index) noexcept;

        std::unique_ptr<FutureSlot[]> slots;
        int32_t capacity{ 0 };
        // the free list is a Treiber stack. the high 32 bits are a tag that changes on every pop and push, so that a
        // pop that read a stale head fails its compare-exchange
        std::atomic<uint64_t> freeHead{ 0u };
        std::atomic<int64_t> liveCount{ 0 };
        std::atomic<int64_t> fullCount{ 0 };

        VkDevice device{ VK_NULL_HANDLE };
        VkSemaphore transferTimeline{ VK_NULL_HANDLE };
    };

}

#endif //!DIAMOND_DOGS_RHI_FUTURE_TABLE_HPP
