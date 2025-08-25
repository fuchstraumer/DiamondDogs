#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_FLAGS_HPP
#define RESOURCE_CONTEXT_RESOURCE_FLAGS_HPP
#include <type_traits>


/** @brief Describes where the resource is used or where it is stored within hardware domains */
enum class ResourceDomain : uint8_t
{
    Invalid = 0,
    GPUOnly = 1 << 0,
    CPUOnly = 1 << 1,
    CPUToGPU = 1 << 2,
    GPUToCPU = 1 << 3,
};

/** @brief Describes the fundamental resource type, i.e. is it a buffer or image or sampler etc */
enum class ResourceType : uint8_t
{
    Invalid = 0,
    Buffer = 1 << 0,
    BufferView = 1 << 1,
    Image = 1 << 2,
    ImageView = 1 << 3,
    Sampler = 1 << 4,
    CombinedImageSampler = 1 << 5
};

/** @brief Combinations of flag bits that control resource creation behavior and specific usage hints */
enum class ResourceCreationFlags : uint16_t
{
    None = 0,
    /** @brief User data pointer is a null-terminated string naming the resource */
    UserDataAsNameString = 1 << 0,
    /** @brief Place the resource in its own dedicated memory allocation */
    DedicatedMemory = 1 << 1,
    /** @brief Map the resource as part of creation process */
    CreateMapped = 1 << 2,
    /** @brief Keep the resource persistently mapped for its entire lifetime */
    PersistentlyMapped = 1 << 3,
    /** @brief Host will write to the resource in a consistent, linear fashion */
    HostWritesLinear = 1 << 4,
    /** @brief Host will write to the resource in a random access pattern */
    HostWritesRandom = 1 << 5,
};

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

/** @brief Flags specifying the intended usage and access patterns for buffer resources */
enum class BufferUsageBits : uint64_t
{
    Invalid = 0,

    /** @brief Buffer used as uniform buffer in vertex shader stage */
    VertexShaderUBO = 1 << 0,
    /** @brief Buffer used as uniform buffer in fragment shader stage */
    FragmentShaderUBO = 1 << 1,
    /** @brief Buffer used as uniform buffer in compute shader stage */
    ComputeShaderUBO = 1 << 2,
    /** @brief Buffer used as uniform buffer in raytracing shader stages */
    RaytracingShaderUBO = 1 << 3,

    /** @brief Buffer used as shader resource view (read-only) in vertex shader stage */
    VertexShaderSRV = 1 << 4,
    /** @brief Buffer used as shader resource view (read-only) in fragment shader stage */
    FragmentShaderSRV = 1 << 5,
    /** @brief Buffer used as shader resource view (read-only) in compute shader stage */
    ComputeShaderSRV = 1 << 6,
    /** @brief Buffer used as shader resource view (read-only) in raytracing shader stages */
    RaytracingShaderSRV = 1 << 7,

    /** @brief Buffer used as unordered access view (read-write) in vertex shader stage */
    VertexShaderUAV = 1 << 8,
    /** @brief Buffer used as unordered access view (read-write) in fragment shader stage */
    FragmentShaderUAV = 1 << 9,
    /** @brief Buffer used as unordered access view (read-write) in compute shader stage */
    ComputeShaderUAV = 1 << 10,
    /** @brief Buffer used as unordered access view (read-write) in raytracing shader stages */
    RaytracingShaderUAV = 1 << 11,

    // Now for some of our more specific use cases
    /** @brief Buffer used as vertex or index data for rasterization */
    VertexOrIndex = 1 << 12,
    /** @brief Buffer used as indirect dispatch parameters for compute operations */
    IndirectCompute = 1 << 13,
    /** @brief Buffer used as indirect draw parameters for rasterization */
    IndirectDraw = 1 << 14,
    /** @brief Buffer used as indirect raytracing dispatch parameters */
    IndirectRaytracing = 1 << 15,
    /** @brief Buffer used as source for copy/transfer operations */
    CopySource = 1 << 16,
    /** @brief Buffer used as destination for copy/transfer operations */
    CopyDestination = 1 << 17,

    /** @brief Buffer address used for bindless resource access via device addresses */
    DeviceAddressBuffer = 1 << 18,

    /** @brief Buffer used for acceleration structure build operations */
    AccelerationStructureBuild = 1 << 19,
    /** @brief Buffer used as shader binding table for raytracing */
    ShaderBindingTable = 1 << 20,
    /** @brief Buffer used as scratch space during acceleration structure builds */
    AccelerationStructureBuildScratch = 1 << 21,

    /** @brief Buffer used for conditional rendering predicates */
    ConditionalRendering = 1 << 22,

    // All flags after this point are reserved for internal usage in resource barrier generation, and should not be used by users
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

/** @brief Flags specifying the intended usage and access patterns for image resources */
enum class ImageUsageBits : uint64_t
{
    Invalid = 0,

    /** @brief Image used as shader resource view (read-only) in vertex shader stage */
    SrvVertexShader = 1 << 0,
    /** @brief Image used as shader resource view (read-only) in fragment shader stage */
    SrvFragmentShader = 1 << 1,
    /** @brief Image used as shader resource view (read-only) in compute shader stage */
    SrvComputeShader = 1 << 2,
    /** @brief Image used as shader resource view (read-only) in raytracing shader stages */
    SrvRaytracingShader = 1 << 3,

    /** @brief Image used as unordered access view (read-write) in vertex shader stage */
    UavVertexShader = 1 << 4,
    /** @brief Image used as unordered access view (read-write) in fragment shader stage */
    UavFragmentShader = 1 << 5,
    /** @brief Image used as unordered access view (read-write) in compute shader stage */
    UavComputeShader = 1 << 6,
    /** @brief Image used as unordered access view (read-write) in raytracing shader stages */
    UavRaytracingShader = 1 << 7,

    /** @brief Image used as color attachment for reading during rendering */
    RenderTargetRead = 1 << 8,
    /** @brief Image used as color attachment for writing during rendering */
    RenderTargetWrite = 1 << 9,
    /** @brief Image used as variable rate shading attachment */
    ShadingRateImage = 1 << 10,

    /** @brief Image used as source for presentation to display */
    PresentSource = 1 << 11,
    /** @brief Image used as destination for copy/transfer operations */
    TransferDestination = 1 << 12,

    // Not sure yet if we should be breaking DS usage down this deeply, especially with the direction the API is heading wrt barriers
    // and layout transitions, but right now each of these is treated as a distinct usage for more granular introspection on src/dest layouts especially

    /** @brief Image used for depth buffer reading operations */
    DepthRead = 1 << 13,
    /** @brief Image used for depth buffer writing operations */
    DepthWrite = 1 << 14,
    /** @brief Image used for stencil buffer reading operations */
    StencilRead = 1 << 15,
    /** @brief Image used for stencil buffer writing operations */
    StencilWrite = 1 << 16,
    /** @brief Image used for combined depth-stencil reading operations */
    DepthStencilRead = 1 << 17,
    /** @brief Image used for combined depth-stencil writing operations */
    DepthStencilWrite = 1 << 18,

    // generic sampled image, i.e. just a regular ol' texture. funny how this is now the last case we list, such has become the range of ways we use images in shaders
    /** @brief Image used as regular sampled texture in shaders */
    SampledImage = 1 << 19,

    // All flags after this point are reserved for internal usage in resource barrier generation, and should not be used by users
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

/** @brief Stores the bitmask value and preserves that intact, but also lets us implicitly convert to bool for quick checks after mask operations */
template<typename T>
struct BitmaskTrueType
{
    T value;
    constexpr BitmaskTrueType(T _value) : value(_value) {}
    constexpr operator T() const { return value; }
    constexpr explicit operator bool() const noexcept { return ToUnderlying(value); }
};

constexpr inline BitmaskTrueType<ResourceCreationFlags> operator|(const ResourceCreationFlags a, const ResourceCreationFlags b) noexcept
{
    return static_cast<ResourceCreationFlags>(ToUnderlying(a) | ToUnderlying(b));
}

constexpr inline BitmaskTrueType<ResourceCreationFlags> operator&(const ResourceCreationFlags a, const ResourceCreationFlags b) noexcept
{
    return static_cast<ResourceCreationFlags>(ToUnderlying(a) & ToUnderlying(b));
}

constexpr inline ResourceCreationFlags& operator|=(ResourceCreationFlags& a, const ResourceCreationFlags b) noexcept
{
    a = a | b;
    return a;
}

constexpr inline ResourceCreationFlags& operator&=(ResourceCreationFlags& a, const ResourceCreationFlags b) noexcept
{
    a = a & b;
    return a;
}

constexpr inline BitmaskTrueType<BufferUsageBits> operator|(const BufferUsageBits a, const BufferUsageBits b) noexcept
{
    return static_cast<BufferUsageBits>(ToUnderlying(a) | ToUnderlying(b));
}

constexpr inline BitmaskTrueType<BufferUsageBits> operator&(const BufferUsageBits a, const BufferUsageBits b) noexcept
{
    return static_cast<BufferUsageBits>(ToUnderlying(a) & ToUnderlying(b));
}

constexpr inline BufferUsageBits& operator|=(BufferUsageBits& a, const BufferUsageBits b) noexcept
{
    a = a | b;
    return a;
}

constexpr inline BufferUsageBits& operator&=(BufferUsageBits& a, const BufferUsageBits b) noexcept
{
    a = a & b;
    return a;
}

constexpr inline BitmaskTrueType<ImageUsageBits> operator|(const ImageUsageBits a, const ImageUsageBits b) noexcept
{
    return static_cast<ImageUsageBits>(ToUnderlying(a) | ToUnderlying(b));
}

constexpr inline BitmaskTrueType<ImageUsageBits> operator&(const ImageUsageBits a, const ImageUsageBits b) noexcept
{
    return static_cast<ImageUsageBits>(ToUnderlying(a) & ToUnderlying(b));
}

constexpr inline ImageUsageBits& operator|=(ImageUsageBits& a, const ImageUsageBits b) noexcept
{
    a = a | b;
    return a;
}

constexpr inline ImageUsageBits& operator&=(ImageUsageBits& a, const ImageUsageBits b) noexcept
{
    a = a & b;
    return a;
}

#endif // !RESOURCE_CONTEXT_RESOURCE_FLAGS_HPP
