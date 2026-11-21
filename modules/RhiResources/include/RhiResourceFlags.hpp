#pragma once
#ifndef RESOURCE_CONTEXT_RESOURCE_FLAGS_HPP
#define RESOURCE_CONTEXT_RESOURCE_FLAGS_HPP
#include "utility/EnumFlagUtils.hpp"
#include <cstdint>

/**
 * @file ResourceFlags.hpp
 * @brief Defines enums and flags used for resource management within the rendering hardware interface (RHI).
 * This includes resource domains, types, and creation flags that dictate how resources are allocated,
 * accessed, and used across different hardware and software layers. This lets us avoid having any Vulkan code
 * or other graphics API specifics leak into the higher-level application logic, promoting portability and
 * easier maintenance.
 * @category RHI
 */


/** @brief Describes where the resource is used or where it is stored within hardware domains */
enum class ResourceDomain : uint16_t
{
    Invalid = 0,
    GPUOnly,
    CPUOnly,
    CPUToGPU,
    GPUToCPU
};

/** @brief Describes the fundamental resource type, i.e. is it a buffer or image or sampler etc */
enum class ResourceType : uint16_t
{
    Invalid = 0,
    Buffer,
    BufferView,
    Image,
    ImageView,
    Sampler,
    CombinedImageSampler,
    /** @brief A block of device memory that placed resources are bound into. Made by `rhi::placement::CreateMemoryHeap`. */
    Heap
};

/** @brief Combinations of flag bits that control resource creation behavior and specific usage hints */
enum class ResourceCreationFlags : uint16_t
{
    None = 0,
    // bit 0 was UserDataAsNameString. Names are now the create infos' Name field
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

/** @brief Flags specifying the intended usage and access patterns for buffer resources.
 *  A shader bit is `<Stage>Shader<Kind>`. A fixed-function bit is `<what uses it><role>`. A group is `All<...>`. */
enum class BufferUsageBits : uint64_t
{
    Invalid = 0,

    /** @brief Buffer used as uniform buffer in the vertex shader stage */
    VertexShaderUBO = 1ull << 0,
    /** @brief Buffer used as uniform buffer in the tessellation control shader stage */
    TessellationControlShaderUBO = 1ull << 1,
    /** @brief Buffer used as uniform buffer in the tessellation evaluation shader stage */
    TessellationEvaluationShaderUBO = 1ull << 2,
    /** @brief Buffer used as uniform buffer in the geometry shader stage */
    GeometryShaderUBO = 1ull << 3,
    /** @brief Buffer used as uniform buffer in the task shader stage */
    TaskShaderUBO = 1ull << 4,
    /** @brief Buffer used as uniform buffer in the mesh shader stage */
    MeshShaderUBO = 1ull << 5,
    /** @brief Buffer used as uniform buffer in the fragment shader stage */
    FragmentShaderUBO = 1ull << 6,
    /** @brief Buffer used as uniform buffer in the compute shader stage */
    ComputeShaderUBO = 1ull << 7,
    /** @brief Buffer used as uniform buffer in the ray tracing shader stages */
    RayTracingShaderUBO = 1ull << 8,

    /** @brief Buffer used as shader resource view (read-only) in the vertex shader stage */
    VertexShaderSRV = 1ull << 9,
    /** @brief Buffer used as shader resource view (read-only) in the tessellation control shader stage */
    TessellationControlShaderSRV = 1ull << 10,
    /** @brief Buffer used as shader resource view (read-only) in the tessellation evaluation shader stage */
    TessellationEvaluationShaderSRV = 1ull << 11,
    /** @brief Buffer used as shader resource view (read-only) in the geometry shader stage */
    GeometryShaderSRV = 1ull << 12,
    /** @brief Buffer used as shader resource view (read-only) in the task shader stage */
    TaskShaderSRV = 1ull << 13,
    /** @brief Buffer used as shader resource view (read-only) in the mesh shader stage */
    MeshShaderSRV = 1ull << 14,
    /** @brief Buffer used as shader resource view (read-only) in the fragment shader stage */
    FragmentShaderSRV = 1ull << 15,
    /** @brief Buffer used as shader resource view (read-only) in the compute shader stage */
    ComputeShaderSRV = 1ull << 16,
    /** @brief Buffer used as shader resource view (read-only) in the ray tracing shader stages */
    RayTracingShaderSRV = 1ull << 17,

    /** @brief Buffer used as unordered access view (read-write) in the vertex shader stage */
    VertexShaderUAV = 1ull << 18,
    /** @brief Buffer used as unordered access view (read-write) in the tessellation control shader stage */
    TessellationControlShaderUAV = 1ull << 19,
    /** @brief Buffer used as unordered access view (read-write) in the tessellation evaluation shader stage */
    TessellationEvaluationShaderUAV = 1ull << 20,
    /** @brief Buffer used as unordered access view (read-write) in the geometry shader stage */
    GeometryShaderUAV = 1ull << 21,
    /** @brief Buffer used as unordered access view (read-write) in the task shader stage */
    TaskShaderUAV = 1ull << 22,
    /** @brief Buffer used as unordered access view (read-write) in the mesh shader stage */
    MeshShaderUAV = 1ull << 23,
    /** @brief Buffer used as unordered access view (read-write) in the fragment shader stage */
    FragmentShaderUAV = 1ull << 24,
    /** @brief Buffer used as unordered access view (read-write) in the compute shader stage */
    ComputeShaderUAV = 1ull << 25,
    /** @brief Buffer used as unordered access view (read-write) in the ray tracing shader stages */
    RayTracingShaderUAV = 1ull << 26,

    /** @brief Buffer read as index data by an indexed draw */
    IndexInput = 1ull << 27,
    /** @brief Buffer read as vertex attribute data */
    VertexInput = 1ull << 28,
    /** @brief Buffer read as indirect draw parameters */
    DrawIndirect = 1ull << 29,
    /** @brief Buffer read as indirect compute dispatch parameters */
    DispatchIndirect = 1ull << 30,
    /** @brief Buffer read as indirect ray tracing dispatch parameters */
    TraceRaysIndirect = 1ull << 31,
    /** @brief Buffer read as the source of a copy or other transfer */
    TransferSource = 1ull << 32,
    /** @brief Buffer written as the destination of a copy, fill or other transfer */
    TransferDestination = 1ull << 33,
    /** @brief Buffer read as a conditional rendering predicate */
    ConditionalRendering = 1ull << 34,
    /** @brief Buffer written by transform feedback */
    TransformFeedback = 1ull << 35,
    /** @brief Buffer that holds transform feedback counters */
    TransformFeedbackCounter = 1ull << 36,
    /** @brief Buffer that holds the commands that device-generated commands execute */
    GeneratedCommandsInput = 1ull << 37,
    /** @brief Buffer that device-generated commands preprocess into */
    GeneratedCommandsPreprocess = 1ull << 38,
    /** @brief Buffer that stores an acceleration structure */
    AccelerationStructureStorage = 1ull << 39,
    /** @brief Buffer read as geometry or instance input by an acceleration structure build */
    AccelerationStructureBuildInput = 1ull << 40,
    /** @brief Buffer used as scratch space during acceleration structure builds */
    AccelerationStructureBuildScratch = 1ull << 41,
    /** @brief Buffer used as shader binding table for ray tracing */
    ShaderBindingTable = 1ull << 42,
    /** @brief Buffer that stores a micromap */
    MicromapStorage = 1ull << 43,
    /** @brief Buffer read as input by a micromap build */
    MicromapBuildInput = 1ull << 44,
    /** @brief Buffer address used for bindless resource access via device addresses */
    DeviceAddress = 1ull << 45,

    // Groups
    AllVertexStage = VertexShaderUBO | VertexShaderSRV | VertexShaderUAV,
    AllTessellationControlStage = TessellationControlShaderUBO | TessellationControlShaderSRV | TessellationControlShaderUAV,
    AllTessellationEvaluationStage = TessellationEvaluationShaderUBO | TessellationEvaluationShaderSRV | TessellationEvaluationShaderUAV,
    AllGeometryStage = GeometryShaderUBO | GeometryShaderSRV | GeometryShaderUAV,
    AllTaskStage = TaskShaderUBO | TaskShaderSRV | TaskShaderUAV,
    AllMeshStage = MeshShaderUBO | MeshShaderSRV | MeshShaderUAV,
    AllFragmentStage = FragmentShaderUBO | FragmentShaderSRV | FragmentShaderUAV,
    AllComputeStage = ComputeShaderUBO | ComputeShaderSRV | ComputeShaderUAV,
    AllRayTracingStage = RayTracingShaderUBO | RayTracingShaderSRV | RayTracingShaderUAV,
    AllShaderUBO = VertexShaderUBO | TessellationControlShaderUBO | TessellationEvaluationShaderUBO | GeometryShaderUBO | TaskShaderUBO | MeshShaderUBO | FragmentShaderUBO | ComputeShaderUBO | RayTracingShaderUBO,
    AllShaderSRV = VertexShaderSRV | TessellationControlShaderSRV | TessellationEvaluationShaderSRV | GeometryShaderSRV | TaskShaderSRV | MeshShaderSRV | FragmentShaderSRV | ComputeShaderSRV | RayTracingShaderSRV,
    AllShaderUAV = VertexShaderUAV | TessellationControlShaderUAV | TessellationEvaluationShaderUAV | GeometryShaderUAV | TaskShaderUAV | MeshShaderUAV | FragmentShaderUAV | ComputeShaderUAV | RayTracingShaderUAV,
    AllShaderStorage = AllShaderSRV | AllShaderUAV,
    AllTransfer = TransferSource | TransferDestination,
    AllIndirect = DrawIndirect | DispatchIndirect | TraceRaysIndirect,
    AllAccelerationStructure = AccelerationStructureStorage | AccelerationStructureBuildInput | AccelerationStructureBuildScratch,
    AllMicromap = MicromapStorage | MicromapBuildInput,
    // this superset includes some usages that fall outside of the ray tracing shader stages, but belong to ray tracing work
    AllRayTracingUsage = AllRayTracingStage | TraceRaysIndirect | AllAccelerationStructure | ShaderBindingTable,
};

/** @brief Flags specifying the intended usage and access patterns for image resources.
 *  A shader bit is `<Stage>Shader<Kind>`. A fixed-function bit is `<what uses it><role>`. A group is `All<...>`. */
enum class ImageUsageBits : uint64_t
{
    Invalid = 0,

    /** @brief Image used as shader resource view (read-only) in the vertex shader stage */
    VertexShaderSRV = 1ull << 0,
    /** @brief Image used as shader resource view (read-only) in the tessellation control shader stage */
    TessellationControlShaderSRV = 1ull << 1,
    /** @brief Image used as shader resource view (read-only) in the tessellation evaluation shader stage */
    TessellationEvaluationShaderSRV = 1ull << 2,
    /** @brief Image used as shader resource view (read-only) in the geometry shader stage */
    GeometryShaderSRV = 1ull << 3,
    /** @brief Image used as shader resource view (read-only) in the task shader stage */
    TaskShaderSRV = 1ull << 4,
    /** @brief Image used as shader resource view (read-only) in the mesh shader stage */
    MeshShaderSRV = 1ull << 5,
    /** @brief Image used as shader resource view (read-only) in the fragment shader stage */
    FragmentShaderSRV = 1ull << 6,
    /** @brief Image used as shader resource view (read-only) in the compute shader stage */
    ComputeShaderSRV = 1ull << 7,
    /** @brief Image used as shader resource view (read-only) in the ray tracing shader stages */
    RayTracingShaderSRV = 1ull << 8,

    /** @brief Image used as unordered access view (read-write) in the vertex shader stage */
    VertexShaderUAV = 1ull << 9,
    /** @brief Image used as unordered access view (read-write) in the tessellation control shader stage */
    TessellationControlShaderUAV = 1ull << 10,
    /** @brief Image used as unordered access view (read-write) in the tessellation evaluation shader stage */
    TessellationEvaluationShaderUAV = 1ull << 11,
    /** @brief Image used as unordered access view (read-write) in the geometry shader stage */
    GeometryShaderUAV = 1ull << 12,
    /** @brief Image used as unordered access view (read-write) in the task shader stage */
    TaskShaderUAV = 1ull << 13,
    /** @brief Image used as unordered access view (read-write) in the mesh shader stage */
    MeshShaderUAV = 1ull << 14,
    /** @brief Image used as unordered access view (read-write) in the fragment shader stage */
    FragmentShaderUAV = 1ull << 15,
    /** @brief Image used as unordered access view (read-write) in the compute shader stage */
    ComputeShaderUAV = 1ull << 16,
    /** @brief Image used as unordered access view (read-write) in the ray tracing shader stages */
    RayTracingShaderUAV = 1ull << 17,

    /** @brief Image used as a regular sampled texture, in any shader stage that a graphics or compute pipeline has */
    SampledImage = 1ull << 18,
    /** @brief Image used as color attachment for reading during rendering */
    RenderTargetRead = 1ull << 19,
    /** @brief Image used as color attachment for writing during rendering */
    RenderTargetWrite = 1ull << 20,
    /** @brief Image used for depth buffer reading operations */
    DepthRead = 1ull << 21,
    /** @brief Image used for depth buffer writing operations */
    DepthWrite = 1ull << 22,
    /** @brief Image used for stencil buffer reading operations */
    StencilRead = 1ull << 23,
    /** @brief Image used for stencil buffer writing operations */
    StencilWrite = 1ull << 24,
    /** @brief Image used for combined depth-stencil reading operations */
    DepthStencilRead = 1ull << 25,
    /** @brief Image used for combined depth-stencil writing operations */
    DepthStencilWrite = 1ull << 26,
    /** @brief Image used as input attachment read by a fragment shader */
    InputAttachmentRead = 1ull << 27,
    /** @brief Image used as variable rate shading attachment */
    ShadingRateRead = 1ull << 28,
    /** @brief Image used as fragment density map */
    FragmentDensityMapRead = 1ull << 29,
    /** @brief Image used as source for copy, blit or other transfer operations */
    TransferSource = 1ull << 30,
    /** @brief Image used as destination for copy, fill, clear, blit or other transfer operations */
    TransferDestination = 1ull << 31,
    /** @brief Image used as source for presentation to display */
    PresentSource = 1ull << 32,

    /** @brief Valid only as `ImageUsageTransition::Before`, and only alone. The old contents of the image do not matter,
     *  and the transition starts from UNDEFINED. */
    Discard = 1ull << 33,

    // Groups
    AllVertexStage = VertexShaderSRV | VertexShaderUAV,
    AllTessellationControlStage = TessellationControlShaderSRV | TessellationControlShaderUAV,
    AllTessellationEvaluationStage = TessellationEvaluationShaderSRV | TessellationEvaluationShaderUAV,
    AllGeometryStage = GeometryShaderSRV | GeometryShaderUAV,
    AllTaskStage = TaskShaderSRV | TaskShaderUAV,
    AllMeshStage = MeshShaderSRV | MeshShaderUAV,
    AllFragmentStage = FragmentShaderSRV | FragmentShaderUAV | RenderTargetRead | RenderTargetWrite | DepthRead | DepthWrite | StencilRead | StencilWrite | DepthStencilRead | DepthStencilWrite | InputAttachmentRead,
    AllComputeStage = ComputeShaderSRV | ComputeShaderUAV,
    AllRayTracingStage = RayTracingShaderSRV | RayTracingShaderUAV,
    AllShaderSRV = VertexShaderSRV | TessellationControlShaderSRV | TessellationEvaluationShaderSRV | GeometryShaderSRV | TaskShaderSRV | MeshShaderSRV | FragmentShaderSRV | ComputeShaderSRV | RayTracingShaderSRV,
    AllShaderUAV = VertexShaderUAV | TessellationControlShaderUAV | TessellationEvaluationShaderUAV | GeometryShaderUAV | TaskShaderUAV | MeshShaderUAV | FragmentShaderUAV | ComputeShaderUAV | RayTracingShaderUAV,
    AllShaderStorage = AllShaderSRV | AllShaderUAV,
    AllTransfer = TransferSource | TransferDestination,
    AllDsRead = DepthRead | StencilRead | DepthStencilRead,
    AllDsWrite = DepthWrite | StencilWrite | DepthStencilWrite,
    AllAttachmentRead = RenderTargetRead | AllDsRead,
    AllAttachmentWrite = RenderTargetWrite | AllDsWrite,
    AllRenderTarget = RenderTargetRead | RenderTargetWrite,
};

enum class ImageType : uint8_t
{
    Invalid = 0,
    Type1D,
    Type2D,
    Type3D
};

enum class ImageViewType : uint8_t
{
    Invalid = 0,
    Type1D,
    Type1DArray,
    Type2D,
    Type2DArray,
    Type3D,
    Cube,
    CubeArray
};

enum class ImageAspectFlags : uint8_t
{
    None = 0,
    Color = 1 << 0,
    Depth = 1 << 1,
    Stencil = 1 << 2,
    DepthStencil = Depth | Stencil
};

enum class ComponentSwizzle : uint8_t
{
    None = 0,
    Red,
    Green,
    Blue,
    Alpha
};

enum class ImageFilteringMode : uint8_t
{
    None = 0,
    Nearest,
    Linear,
    /** @note Not supported for use as a mipmap filtering mode. */
    Cubic
};

/** @brief Comparison operations for sampler depth testing or other comparison-based sampling operations
    @note: To disambiguate between unset values and valid, None and Never no longer overlap, so this does
    **not** mirror Vulkan exactly.
 */
enum class ImageCompareOp : uint8_t
{
    None = 0,
    Never = 1,
    Less = 2,
    Equal = 3,
    LessOrEqual = 4,
    Greater = 5,
    NotEqual = 6,
    GreaterOrEqual = 7,
    Always = 8
};

MAKE_ENUM_CLASS_FLAGS(ResourceCreationFlags)

MAKE_ENUM_CLASS_FLAGS(BufferUsageBits)

MAKE_ENUM_CLASS_FLAGS(ImageUsageBits)

MAKE_ENUM_CLASS_FLAGS(ImageAspectFlags)

#endif // !RESOURCE_CONTEXT_RESOURCE_FLAGS_HPP
