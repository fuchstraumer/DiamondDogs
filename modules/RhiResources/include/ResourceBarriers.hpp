#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#define RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiTypes.hpp"
#include "ResourceTypes.hpp"
#include <cstdint>
#include <span>

/**
 * @file ResourceBarriers.hpp
 * @brief Translates changes of buffer and image usage into the barrier vocabulary of the RHI (rhi::GlobalBarrier and
 * rhi::ImageBarrier). The caller records the result with rhi::CommandBuffer::Barrier. Nothing here touches a device,
 * a command buffer or any graphics API, and nothing stores a usage or a layout: every result is a function of the
 * before and after usage that the caller gives.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all resources, so no ownership transfer is needed.
 */


struct BufferBarrierInfo
{
    rhi::BufferHandle Handle;
    /** @brief Buffer usage flags applicable to given buffer before this barrier */
    BufferUsageBits BeforeUsage;
    /** @brief Buffer usage flags applicable to given buffer after this barrier */
    BufferUsageBits AfterUsage;
};

struct ImageBarrierInfo
{
    rhi::ImageHandle Handle;
    /** @brief Image usage flags applicable to given image before this barrier */
    ImageUsageBits BeforeUsage;
    /** @brief Image usage flags applicable to given image after this barrier */
    ImageUsageBits AfterUsage;
    /** @brief The subresources that a layout transition applies to. A layout transition needs a non-empty aspect mask. */
    ImageRange Range;
};

/** @brief The barriers that one batch of usage changes needs. Buffer changes, and image changes that keep their
 *  layout, merge into Global. Each image change that changes the layout adds one image barrier. */
struct UsageBarriers
{
    rhi::GlobalBarrier Global;
    int32_t ImageCount{ 0 };
};

/**
 * @brief Returns the barriers for one batch of usage changes.
 *
 * The source access of every barrier keeps only write accesses, because a read makes nothing available. The source
 * stages stay, because a write after a read needs the execution dependency. A change whose before and after usages
 * both only read, with the same layout, contributes nothing. A `Discard` before usage starts from the undefined
 * layout and has no source stages or accesses.
 *
 * `images_out` must have room for `image_changes.size()` barriers; the first `ImageCount` entries are the result.
 * Too little room is a programming error (assert), and only the barriers that fit are written. Allocates nothing.
 */
[[nodiscard]] UsageBarriers TranslateUsageChanges(
    std::span<const BufferBarrierInfo> buffer_changes,
    std::span<const ImageBarrierInfo> image_changes,
    std::span<rhi::ImageBarrier> images_out) noexcept;

/** @brief Returns the pipeline stages that a buffer usage names. A usage with no stage (`DeviceAddress`) gives None. */
[[nodiscard]] rhi::PipelineStage StagesOfUsage(BufferUsageBits usage) noexcept;
/** @brief Returns the accesses that a buffer usage names, reads and writes. */
[[nodiscard]] rhi::Access AccessesOfUsage(BufferUsageBits usage) noexcept;
/** @brief Returns the pipeline stages that an image usage names. A usage with no stage (`Discard`) gives None. */
[[nodiscard]] rhi::PipelineStage StagesOfUsage(ImageUsageBits usage) noexcept;
/** @brief Returns the accesses that an image usage names, reads and writes. */
[[nodiscard]] rhi::Access AccessesOfUsage(ImageUsageBits usage) noexcept;
/** @brief Returns the layout that an image usage needs, or Undefined when the usage names none (`Discard`, or no bits).
 *  Used for the barriers, and by the context, which derives the layouts of a transfer from the caller's usages. */
[[nodiscard]] rhi::ImageLayout ImageLayoutOfUsage(ImageUsageBits usage) noexcept;

#endif // !RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
