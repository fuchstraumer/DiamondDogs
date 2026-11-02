#pragma once
#ifndef DIAMOND_DOGS_RHI_RESOURCE_FUTURE_HPP
#define DIAMOND_DOGS_RHI_RESOURCE_FUTURE_HPP
#include "ResourceTypes.hpp"
#include "RhiHandle.hpp"
#include <cstdint>
#include <limits>
#include <string_view>

namespace rhi
{
    class FutureTable;

    enum class FutureState : uint8_t
    {
        Invalid = 0,
        /** @brief The context has not processed the call yet. */
        Pending = 1,
        /** @brief The object exists, so commands that use it can be recorded. A transfer is still on its way. */
        Created = 2,
        /** @brief The call is done, and any transfer that it started finished on the GPU. */
        Complete = 3,
        /** @brief Permanent. Error() returns the reason. */
        Failed = 4,
    };

    enum class ResourceError : uint8_t
    {
        Invalid = 0,
        Success = 1,
        /** @brief Every slot of the future table was in use. The context did nothing. */
        FutureTableFull = 2,
        /** @brief The GraphicsResource does not match a live resource: it was destroyed, or its handle is stale. */
        StaleResource = 3,
        /** @brief The resource was destroyed, and waits for the GPU before it is freed. */
        ResourceRetiring = 4,
        /** @brief A buffer call got an image, or the reverse. */
        WrongResourceKind = 5,
        /** @brief An invalid domain, a size of zero, an extent of zero, or an invalid format. */
        InvalidCreateInfo = 6,
        /** @brief DeviceAddressBuffer usage on a context without device addresses, or a raytracing usage. */
        UnsupportedUsage = 7,
        /** @brief The resource lacks the CopySource, CopyDestination, TransferSource or TransferDestination usage. */
        MissingTransferUsage = 8,
        /** @brief A fill offset or size that is not a multiple of 4. */
        InvalidFillRange = 9,
        /** @brief A buffer copy whose source is larger than its destination. */
        CopySizeMismatch = 10,
        /** @brief An image copy between images with a different aspect, extent, layer count or mip count. */
        CopyShapeMismatch = 11,
        /** @brief A copy between resource kinds that have no copy path, such as a sampler. */
        UnsupportedCopy = 12,
        /** @brief A map of memory that the host cannot see. */
        NotHostVisible = 13,
        MapFailed = 14,
        /** @brief VMA could not allocate the buffer or the image. */
        AllocationFailed = 15,
        ViewCreationFailed = 16,
        SamplerCreationFailed = 17,
        /** @brief The transfer system could not create or fill its staging buffer. */
        StagingFailed = 18,
        /** @brief The transfer system could not begin or end its command buffer. */
        RecordingFailed = 19,
        /** @brief The transfer queue submit failed. */
        SubmitFailed = 20,
        /** @brief A null timeline semaphore. */
        InvalidTimeline = 21,
        TimelineAlreadyRegistered = 22,
        /** @brief k_MaxConsumerTimelines timelines are registered already. */
        NoFreeTimelineSlot = 23,
        TimelineNotRegistered = 24,
    };

    [[nodiscard]] std::string_view ToString(ResourceError error) noexcept;

    /** @brief A value on a timeline semaphore for a submit to wait on. A Value of 0 means that there is nothing to
     *  wait for. Wait only when Submitted is true: a submit must not wait on a value that no submit will signal. */
    struct GpuWait
    {
        TimelineSemaphoreHandle Semaphore;
        uint64_t Value{ 0u };
        bool Submitted{ false };
    };

    /** @brief Use with ResourceContext::Wait to wait with no time limit. */
    constexpr inline uint64_t k_WaitForever = std::numeric_limits<uint64_t>::max();

    /**
     * @brief The result of a ResourceContext call that returns no value. Every call returns at once, and the future is
     * polled: State() and Error() never block. ResourceContext::Wait is the blocking call.
     *
     * A future is move-only, and owns one slot of its context's future table until it is destroyed. Destroy every
     * future before the ResourceContext that made it.
     */
    class OperationFuture
    {
    public:
        OperationFuture() noexcept;
        ~OperationFuture();
        OperationFuture(const OperationFuture&) = delete;
        OperationFuture& operator=(const OperationFuture&) = delete;
        OperationFuture(OperationFuture&& other) noexcept;
        OperationFuture& operator=(OperationFuture&& other) noexcept;

        /** @brief Returns Invalid for a future that was default-constructed or moved from. */
        [[nodiscard]] FutureState State() const noexcept;
        /** @brief Returns the reason for Failed, and Success for every other state. */
        [[nodiscard]] ResourceError Error() const noexcept;
        /** @brief Returns the transfer timeline and this call's ticket, for a submit on another queue to wait on. */
        [[nodiscard]] GpuWait Completion() const noexcept;
        /** @brief Returns true for Complete and Failed: the context does nothing more for this call. */
        [[nodiscard]] bool IsDone() const noexcept;

    protected:
        friend class FutureTable;
        OperationFuture(FutureTable* _table, int32_t slot_index) noexcept;

        FutureTable* table{ nullptr };
        int32_t slotIndex{ -1 };
    };

    /** @brief The result of a ResourceContext call that returns a value: a GraphicsResource, or a mapped pointer. */
    template<typename T>
    class ResourceFuture final : public OperationFuture
    {
    public:
        ResourceFuture() noexcept;

        /** @brief Returns the value. Valid in the Created and Complete states only, so check State() first. The
         *  reference stays valid while this future lives. */
        [[nodiscard]] const T& Get() const noexcept;

    private:
        friend class FutureTable;
        ResourceFuture(FutureTable* _table, int32_t slot_index) noexcept;
    };

    template<>
    const GraphicsResource& ResourceFuture<GraphicsResource>::Get() const noexcept;
    template<>
    void* const& ResourceFuture<void*>::Get() const noexcept;

    extern template class ResourceFuture<GraphicsResource>;
    extern template class ResourceFuture<void*>;

}

#endif //!DIAMOND_DOGS_RHI_RESOURCE_FUTURE_HPP
