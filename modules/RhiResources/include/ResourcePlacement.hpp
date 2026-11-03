#pragma once
#ifndef DIAMOND_DOGS_RESOURCE_PLACEMENT_HPP
#define DIAMOND_DOGS_RESOURCE_PLACEMENT_HPP
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include <cstdint>
#include <string_view>

class ResourceContext;

/**
 * @file ResourcePlacement.hpp
 * @brief Memory heaps and placed resources, for memory aliasing. These calls are for the Vulkan backend only. WebGPU
 * has no heaps and no memory placement, and its backend aliases memory by reusing whole textures. The calls that both
 * backends share stay in ResourceContext.hpp.
 *
 * A heap is one block of device-local memory. A placed buffer or image is bound into a heap at an offset and owns no
 * memory. Two placed resources that cover the same bytes alias each other. The context does not check which
 * placements overlap, because overlap is the purpose. The caller must make sure that the lifetimes of aliasing
 * resources do not overlap on the GPU, and that the first write of a placed image after another resource used the
 * memory has `ImageUsageBits::Discard` as its `Before` usage.
 */

namespace rhi::placement
{
    /** @brief The memory that a buffer or an image needs, as the device reports it. */
    struct MemoryRequirements
    {
        uint64_t Size{ 0u };
        uint64_t Alignment{ 0u };
        /** @brief Bit N is set when memory type N can hold the resource. */
        uint32_t MemoryTypeBits{ 0u };
    };

    struct MemoryHeapCreateInfo
    {
        /** @brief The size of the heap in bytes. Must not be 0. */
        uint64_t Size{ 0u };
        /** @brief The memory types that the heap may use, as in MemoryRequirements. The heap is also device-local. Must not
         *  be 0. For a heap that holds a given set of resources, AND their MemoryTypeBits together. */
        uint32_t MemoryTypeBits{ 0u };
        /** @brief Optional debug name, copied when the call is made. */
        std::string_view Name;
    };

    struct Placement
    {
        /** @brief A heap that CreateMemoryHeap made. */
        GraphicsResource Heap;
        /** @brief The byte offset into the heap. Must be a multiple of the alignment of the resource. */
        uint64_t Offset{ 0u };
    };

    /**
     * @brief Returns the memory that an image made from `create_info` needs. Runs on the calling thread and sends no
     * message, so a render graph can pack offsets while it compiles. The answer is for the image that CreateImage or
     * CreatePlacedImage makes from the same info, so pass the usage that you create with. Returns all zeros for an info
     * that CreateImage refuses (an extent of 0 or an invalid format).
     */
    [[nodiscard]] MemoryRequirements GetImageMemoryRequirements(const ResourceContext& context, const ImageCreateInfo& create_info);

    /** @brief As GetImageMemoryRequirements, for a buffer. Returns all zeros for a size of 0. */
    [[nodiscard]] MemoryRequirements GetBufferMemoryRequirements(const ResourceContext& context, const BufferCreateInfo& create_info);

    /**
     * @brief Allocates a heap of device-local memory, as one dedicated allocation. The heap is Complete when the memory
     * exists. Fails with InvalidCreateInfo for a size or a memory type mask of 0, and with AllocationFailed when no
     * memory type in the mask can hold the size.
     */
    [[nodiscard]] ResourceFuture<GraphicsResource> CreateMemoryHeap(ResourceContext& context, const MemoryHeapCreateInfo& create_info);

    /**
     * @brief Creates an image and binds it into a heap. The image has no data and no defined layout, so its first
     * write must use `Before = Discard`. The domain must be GPUOnly. The future fails with:
     * - InvalidCreateInfo for another domain, or for the same reasons as CreateImage.
     * - StaleResource, ResourceRetiring or WrongResourceKind when `placement.Heap` is not a live heap.
     * - InvalidPlacement when the offset is not a multiple of the alignment, when the offset and the size pass the end of
     *   the heap, or when the memory type of the heap is not in the MemoryTypeBits of the image.
     * The heap counts the image until the image is freed. Destroy every placed resource before its heap.
     */
    [[nodiscard]] ResourceFuture<GraphicsResource> CreatePlacedImage(ResourceContext& context, const ImageCreateInfo& create_info, Placement placement);

    /** @brief As CreatePlacedImage, for a buffer. The buffer has no data, and the domain must be GPUOnly. */
    [[nodiscard]] ResourceFuture<GraphicsResource> CreatePlacedBuffer(ResourceContext& context, const BufferCreateInfo& create_info, Placement placement);
}

#endif //!DIAMOND_DOGS_RESOURCE_PLACEMENT_HPP
