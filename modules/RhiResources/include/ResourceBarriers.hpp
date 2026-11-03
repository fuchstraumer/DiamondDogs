#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#define RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#include "RhiHandle.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include <cstdint>
#include <span>

/**
 * @file ResourceBarriers.hpp
 * @brief Defines resource barrier utilities for synchronizing resource states in a graphics context.
 * Replacement for the deprecated thsvs resource barrier system that uses the older barriers, now the newer
 * and more updated barrier type is provided for improved performance and flexibility.
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

/**
 * @brief Will execute the given buffer barrier on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all buffers, so no ownership transfer is performed.
 * */
void ExecuteBufferBarrier(const rhi::CommandBufferHandle& cmd, const BufferBarrierInfo& barrier);

/**
 * @brief Will execute the given buffer barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all buffers, so no ownership transfer is performed.
 * */
void ExecuteBufferBarriers(const rhi::CommandBufferHandle& cmd, std::span<const BufferBarrierInfo> barriers);

/**
 * @brief Will execute the given image barrier on the provided command buffer. Uses an image barrier when the usages
 * need a layout transition, and a global barrier otherwise.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all images, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteImageBarrier(const rhi::CommandBufferHandle& cmd, const ImageBarrierInfo& barrier);

/**
 * @brief Will execute the given image barriers on the provided command buffer.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all images, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteImageBarriers(const rhi::CommandBufferHandle& cmd, std::span<const ImageBarrierInfo> barriers);

/**
 * @brief Will execute the given buffer and image barriers on the provided command buffer.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all resources, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteBarriers(const rhi::CommandBufferHandle& cmd,
                     std::span<const BufferBarrierInfo> buffer_barriers,
                     std::span<const ImageBarrierInfo> image_barriers);

#endif // !RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
