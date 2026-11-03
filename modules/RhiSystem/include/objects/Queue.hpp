#pragma once
#ifndef DIAMOND_DOGS_RHI_QUEUE_HPP
#define DIAMOND_DOGS_RHI_QUEUE_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include <span>

namespace rhi
{
    /** @brief Submits batches of command buffers (vkQueueSubmit2). Waits and signals can mix timeline and binary semaphores.
     *  The fence, when valid, signals once every batch completes. A queue must not be submitted to from two threads at once. */
    [[nodiscard]] Result QueueSubmit(const QueueHandle& queue, std::span<const SubmitInfo> submits, const FenceHandle& fence = FenceHandle{}) noexcept;
    [[nodiscard]] Result QueueSubmit(const QueueHandle& queue, const SubmitInfo& submit, const FenceHandle& fence = FenceHandle{}) noexcept;
    [[nodiscard]] Result QueueWaitIdle(const QueueHandle& queue) noexcept;
}

#endif // !DIAMOND_DOGS_RHI_QUEUE_HPP
