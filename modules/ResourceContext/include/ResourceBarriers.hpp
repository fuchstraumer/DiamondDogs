#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#define RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
#include <vulkan/vulkan_core.h>
#include <type_traits>
#include <cstdint>

/**
 * @file ResourceBarriers.hpp
 * @brief Defines resource barrier utilities for synchronizing resource states in a graphics context.
 * Replacement for the deprecated thsvs resource barrier system that uses the older barriers, now the newer
 * and more updated barrier type is provided for improved performance and flexibility.
 */

 /**
  * Thinking about logical groupings of buffer usage flags and access bits:
  * - Transfer usage + transfer accesses for staging and transfer buffers. Probably a special case and not often visible
  *   to users writing typical rendering or compute code. Can probably exclude
  * - Vertex and index buffers are both usually just storage types that are read from, rarely written to. Nowadays in
  *   most hardware they're not even given special caches or hardware accesses, just regular storage buffers.
  * - Storage buffers are more general-purpose and can be read from and written to by shaders. They often require more
  *   careful synchronization due to their mutable nature, and users should expect to specify read and write accesses
  *   explicitly. This includes defining the exact stages and types of access (e.g., uniform, storage, or atomic) to ensure
  *   correct ordering and visibility of memory operations.
  * - Uniform resources are always going to be read only and also typically have some kind of caching going on bc of this,
  *   so worth including
  * - Indirect rendering buffers are similar, in that they can be written to but during reading are probably placed in
  *   special caches or made part of the GPU command stream directly (reports of NVIDIA hangs when not properly synchronized)
  * - Raytracing structures also have some special cases around them. Skip for now until we do raytracing work
  * - AMD execution graph is another special case. Not sure how user facing it is. Skip for now.
  * - Device address resources are definitely worth handling specifically, since we're effectively just replacing the 
  *   binding table and descriptors ourself. Definitely expect specialized caching around this
  * 
  * So our flags shuold be a combination of the usage, and additionally the stages or queues it'll be used on to help
  * us create more succinct specifications from the very wide Vulkan ones. It seems that someone else reached this same
  * conclusion:
  * https://anki3d.org/simplified-pipeline-barriers/
  */

enum class BufferUsageBits : uint64_t
{
    Invalid = 0,

    VertexShaderUBO = 1 << 0,
    FragmentShaderUBO = 1 << 1,
    ComputeShaderUBO = 1 << 2,
    RaytracingShaderUBO = 1 << 3,

    // Using SRV (Shader Resource View) because it's a more general and descriptive term for resources 
    // that shaders can read from 
    VertexShaderSRV = 1 << 4,
    FragmentShaderSRV = 1 << 5,
    ComputeShaderSRV = 1 << 6,
    RaytracingShaderSRV = 1 << 7,

    // In turn, using UAV because it's also a more succinct term than anything Vulkan gives us for this same 
    // concept (I'm guessing it must map to some hardware concept? AMD folks seemed to suggest as such once...)
    VertexShaderUAV = 1 << 8,
    FragmentShaderUAV = 1 << 9,
    ComputeShaderUAV = 1 << 10,
    RaytracingShaderUAV = 1 << 11,

    // Now for some of our more specific use cases
    VertexOrIndex = 1 << 12,
    IndirectCompute = 1 << 13,
    IndirectDraw = 1 << 14,
    IndirectRaytracing = 1 << 15,
    CopySource = 1 << 16,
    CopyDestination = 1 << 17,

    DeviceAddressBuffer = 1 << 18,

    AccelerationStructureBuild = 1 << 19,
    ShaderBindingTable = 1 << 20,
    AccelerationStructureBuildScratch = 1 << 21,

    ConditionalRendering = 1 << 22,



    // Can add more specific flags as needed for various extensions after this, we still have many many bits left

    // Collected flags for common access patterns
    AllShaderUBO = VertexShaderUBO | FragmentShaderUBO | ComputeShaderUBO | RaytracingShaderUBO,
    AllShaderSRV = VertexShaderSRV | FragmentShaderSRV | ComputeShaderSRV | RaytracingShaderSRV,
    AllShaderUAV = VertexShaderUAV | FragmentShaderUAV | ComputeShaderUAV | RaytracingShaderUAV,
    AllIndirect = IndirectCompute | IndirectDraw | IndirectRaytracing,
    AllTransfer = CopySource | CopyDestination,

    // Collected flags for stages
    AllVertexStage = VertexShaderUBO | VertexShaderSRV | VertexShaderUAV,
    AllFragmentStage = FragmentShaderUBO | FragmentShaderSRV | FragmentShaderUAV,
    AllComputeStage = ComputeShaderUBO | ComputeShaderSRV | ComputeShaderUAV,
    AllRaytracingStage = RaytracingShaderUBO | RaytracingShaderSRV | RaytracingShaderUAV,
    AllRaytracingUsage = AllRaytracingStage | IndirectRaytracing | AccelerationStructureBuild | ShaderBindingTable | AccelerationStructureBuildScratch,

    AllRead = AllShaderUBO | AllShaderSRV | VertexOrIndex | AllIndirect | CopySource | DeviceAddressBuffer | ShaderBindingTable | AccelerationStructureBuild | AccelerationStructureBuildScratch,
    // hmm, thinking now maybe we want to disambiguate between indirect reads and writes: it is a stage flag and could have hardware relevancy. maybe check Mesa?
    AllWrite = AllShaderUAV | CopyDestination | AccelerationStructureBuildScratch,

};

enum class ImageUsageBits : uint64_t
{
    Invalid = 0,

    SrvVertexShader = 1 << 0,
    SrvFragmentShader = 1 << 1,
    SrvComputeShader = 1 << 2,
    SrvRaytracingShader = 1 << 3,

    UavVertexShader = 1 << 4,
    UavFragmentShader = 1 << 5,
    UavComputeShader = 1 << 6,
    UavRaytracingShader = 1 << 7,

    RenderTargetRead = 1 << 8,
    RenderTargetWrite = 1 << 9,
    ShadingRateImage = 1 << 10,

    PresentSource = 1 << 11,
    TransferDestination = 1 << 12,

    // Not sure yet if we should be breaking DS usage down this deeply, especially with the direction the API is heading wrt barriers
    // and layout transitions, but right now each of these is treated as a distinct usage for more granular introspection on src/dest layouts especially
    DepthRead = 1 << 13,
    DepthWrite = 1 << 14,
    StencilRead = 1 << 15,
    StencilWrite = 1 << 16,
    DepthStencilRead = 1 << 17,
    DepthStencilWrite = 1 << 18,

    // generic sampled image, i.e. just a regular ol' texture. funny how this is now the last case we list, such has become the range of ways we use images in shaders
    SampledImage = 1 << 19,

    // Can add more specific flags as needed for various extensions after this, we still have many many bits left

    // Collected flags for common *access* patterns (also useful with layouts, since that understandably tends to mirror access patterns)
    AllShaderSRV = SrvVertexShader | SrvFragmentShader | SrvComputeShader | SrvRaytracingShader,
    AllTransfer = TransferDestination,
    AllShaderUAV = UavVertexShader | UavFragmentShader | UavComputeShader | UavRaytracingShader,
    AllRenderTarget = RenderTargetRead | RenderTargetWrite,
    AllDepth = DepthRead | DepthWrite,
    AllDepthStencil = DepthStencilRead | DepthStencilWrite,
    AllStencil = StencilRead | StencilWrite,
    AllDsWrite = DepthWrite | StencilWrite | DepthStencilWrite,
    AllDsRead = DepthRead | StencilRead | DepthStencilRead,
    AllUsage = AllShaderSRV | AllShaderUAV | AllRenderTarget | ShadingRateImage | PresentSource | AllDepth,
    AllAttachmentWrite = RenderTargetWrite | DepthWrite | DepthStencilWrite | StencilWrite,
    AllAttachmentRead = RenderTargetRead | DepthRead | DepthStencilRead | StencilRead,

    // per-stage groups
    AllVertexStage = SrvVertexShader | UavVertexShader,
    AllFragmentStage = SrvFragmentShader | UavFragmentShader | RenderTargetRead | RenderTargetWrite,
    AllComputeStage = SrvComputeShader | UavComputeShader,
    AllRaytracingStage = SrvRaytracingShader | UavRaytracingShader,
    AllRaytracingUsage = AllRaytracingStage | PresentSource,

};

// Set of shims and casts that let us do boolean test on bit flags in a typesafe manner
template<typename T>
using UnderlyingType = typename std::underlying_type<T>::type;
template<typename T>
constexpr UnderlyingType<T> ToUnderlying(T value) noexcept
{
    return static_cast<UnderlyingType<T>>(value);
}

template<typename T>
struct BitmaskTrueType
{
    T value;
    constexpr BitmaskTrueType(T _value) : value(_value) {}
    constexpr operator T() const { return value; }
    constexpr explicit operator bool() const noexcept { return ToUnderlying(value); }
};

constexpr inline BitmaskTrueType<BufferUsageBits> operator|(BufferUsageBits a, BufferUsageBits b)
{
    return static_cast<BufferUsageBits>(ToUnderlying(a) | ToUnderlying(b));
}

constexpr inline BitmaskTrueType<BufferUsageBits> operator&(BufferUsageBits a, BufferUsageBits b)
{
    return static_cast<BufferUsageBits>(ToUnderlying<BufferUsageBits>(a) & ToUnderlying<BufferUsageBits>(b));
}

constexpr inline BufferUsageBits& operator|=(BufferUsageBits& a, BufferUsageBits b)
{
    a = a | b;
    return a;
}

constexpr inline BufferUsageBits& operator&=(BufferUsageBits& a, BufferUsageBits b)
{
    a = a & b;
    return a;
}

constexpr inline BitmaskTrueType<ImageUsageBits> operator|(ImageUsageBits a, ImageUsageBits b)
{
    return static_cast<ImageUsageBits>(ToUnderlying(a) | ToUnderlying(b));
}

constexpr inline BitmaskTrueType<ImageUsageBits> operator&(ImageUsageBits a, ImageUsageBits b)
{
    return static_cast<ImageUsageBits>(ToUnderlying<ImageUsageBits>(a) & ToUnderlying<ImageUsageBits>(b));
}

constexpr inline ImageUsageBits& operator|=(ImageUsageBits& a, ImageUsageBits b)
{
    a = a | b;
    return a;
}

constexpr inline ImageUsageBits& operator&=(ImageUsageBits& a, ImageUsageBits b)
{
    a = a & b;
    return a;
}

struct BufferBarrierInfo
{
    /** @brief VkBuffer handle, cast to uint64_t to keep this header clean of Vulkan includes */
    uint64_t Handle;
    /** @brief Buffer usage flags applicable to given buffer before this barrier */
    BufferUsageBits BeforeUsage;
    /** @brief Buffer usage flags applicable to given buffer after this barrier */
    BufferUsageBits AfterUsage;
};


struct ImageBarrierInfo
{
    /** @brief VkImage handle, cast to uint64_t to keep this header clean of Vulkan includes */
    uint64_t Handle;
    /** @brief Image usage flags applicable to given image before this barrier */
    ImageUsageBits BeforeUsage;
    /** @brief Image usage flags applicable to given image after this barrier */
    ImageUsageBits AfterUsage;
};

/** 
 * @brief Will execute the given buffer barrier on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all buffers, so no ownership transfer is performed.
 * */
void ExecuteBufferBarrier(VkCommandBuffer cmd, const BufferBarrierInfo& Barrier);

/** 
 * @brief Will execute the given buffer barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all buffers, so no ownership transfer is performed.
 * */
void ExecuteBufferBarriers(VkCommandBuffer cmd, const size_t BarrierCount, const BufferBarrierInfo* Barriers);

/**
 * @brief Will execute the given image barrier on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all images, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteImageBarrier(VkCommandBuffer cmd, const ImageBarrierInfo& Barrier);

/**
 * @brief Will execute the given image barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all images, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteImageBarriers(VkCommandBuffer cmd, const size_t BarrierCount, const ImageBarrierInfo* Barriers);

/**
 * @brief Will execute the given buffer and image barriers on the provided command buffer, using a global barrier in all cases currently.
 * @note ResourceContext assumes VK_SHARING_MODE_CONCURRENT for all resources, so no ownership transfer is performed.
 * @note With VK_SYNCHRONIZATION_2, image layouts are either read optimal or just optimal. This accounts for that.
 */
void ExecuteBarriers(VkCommandBuffer cmd,
                     const size_t BufferBarrierCount, const BufferBarrierInfo* BufferBarriers,
                     const size_t ImageBarrierCount, const ImageBarrierInfo* ImageBarriers);

#endif // !RESOURCE_CONTEXT_RESOURCE_BARRIERS_HPP
