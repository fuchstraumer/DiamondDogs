#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#define RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#include "RhiHandle.hpp"
#include "ResourceFlags.hpp"
#include <cstdint>

/**
 * @file ResourceBarriers.hpp
 * @brief Defines resource barrier utilities for synchronizing resource states in a graphics context.
 * Replacement for the deprecated thsvs resource barrier system that uses the older barriers, now the newer
 * and more updated barrier type is provided for improved performance and flexibility.
 */


struct BufferBarrierInfo
{
    /** @brief VkBuffer handle, cast to uint64_t to keep this header clean of Vulkan includes */
    rhi::BufferHandle Handle;
    /** @brief Buffer usage flags applicable to given buffer before this barrier */
    BufferUsageBits BeforeUsage;
    /** @brief Buffer usage flags applicable to given buffer after this barrier */
    BufferUsageBits AfterUsage;
};

struct ImageBarrierInfo
{
    /** @brief VkImage handle, cast to uint64_t to keep this header clean of Vulkan includes */
    rhi::ImageHandle Handle;
    /** @brief Image usage flags applicable to given image before this barrier */
    ImageUsageBits BeforeUsage;
    /** @brief Image usage flags applicable to given image after this barrier */
    ImageUsageBits AfterUsage;
};

/** 
 * @brief Will execute the given buffer barrier on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all buffers, so no ownership transfer is performed.
 * */
void ExecuteBufferBarrier(uint64_t cmdBufferHandle, const BufferBarrierInfo& Barrier);

/** 
 * @brief Will execute the given buffer barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all buffers, so no ownership transfer is performed.
 * */
void ExecuteBufferBarriers(uint64_t cmdBufferHandle, const size_t BarrierCount, const BufferBarrierInfo* Barriers);

/**
 * @brief Will execute the given image barrier on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all images, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteImageBarrier(uint64_t cmdBufferHandle, const ImageBarrierInfo& Barrier);

/**
 * @brief Will execute the given image barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all images, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteImageBarriers(uint64_t cmdBufferHandle, const size_t BarrierCount, const ImageBarrierInfo* Barriers);

/**
 * @brief Will execute the given buffer and image barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all resources, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteBarriers(uint64_t cmdBufferHandle,
                     const size_t BufferBarrierCount, const BufferBarrierInfo* BufferBarriers,
                     const size_t ImageBarrierCount, const ImageBarrierInfo* ImageBarriers);

#endif // !RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
