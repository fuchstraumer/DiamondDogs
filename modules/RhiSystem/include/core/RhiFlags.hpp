#pragma once
#ifndef RHI_SYSTEM_RHI_FLAGS_HPP
#define RHI_SYSTEM_RHI_FLAGS_HPP
#include "utility/EnumFlagUtils.hpp"

namespace rhi
{
    enum class ApiVersion : uint8_t 
    {
        None = 0,
        Vulkan10 = 1,
        Vulkan11 = 2,
        Vulkan12 = 3,
        Vulkan13 = 4,
        Vulkan14 = 5,
        Latest = 254
    };

    struct QueueFamilyIndices 
    {
        uint32_t Graphics{ ~0u };
        uint32_t Compute{ ~0u };
        uint32_t Transfer{ ~0u };
        uint32_t SparseBinding{ ~0u };
    };

    enum class ValidationLayers : uint8_t 
    {
        None = 0,
        BaseOnly = 1,               // Basic validation
        WithSynchronization = 2,    // Base + synchronization validation (the validation layer's validate_sync setting)
        Full = 3,                    // All available layers
        CrashDiagnostic = 4,        // VK_LAYER_crash_diagnostic
    };

    enum class ShaderStageFlags : uint32_t
    {
        None = 0,
        Vertex = 1 << 0,
        TesselationControl = 1 << 1,
        TesselationEvaluation = 1 << 2,
        Geometry = 1 << 3,
        Fragment = 1 << 4,
        Compute = 1 << 5,
        RayGeneration = 1 << 6,
        AnyHit = 1 << 7,
        ClosestHit = 1 << 8,
        Miss = 1 << 9,
        Intersection = 1 << 10,
        Callable = 1 << 11,
        Task = 1 << 12,
        Mesh = 1 << 13,
        Count = 14,
    };

    MAKE_ENUM_CLASS_FLAGS(ShaderStageFlags);

    enum class LoadOp : uint8_t
    {
        Load,       // VK_ATTACHMENT_LOAD_OP_LOAD / don't clear
        Clear,      // VK_ATTACHMENT_LOAD_OP_CLEAR / clear before rendering
        DontCare    // VK_ATTACHMENT_LOAD_OP_DONT_CARE / undefined
    };

    enum class StoreOp : uint8_t
    {
        Store,      // VK_ATTACHMENT_STORE_OP_STORE / keep contents
        DontCare    // VK_ATTACHMENT_STORE_OP_DONT_CARE / discard contents
    };

    enum class DynamicState : uint32_t
    {
        // States supported by both Vulkan and DX12
        Viewport = 0,
        Scissor = 1,
        BlendConstants = 2,
        StencilReference = 3,
        
        // Vulkan-only states (will require PSO variants on DX12)
        LineWidth = 4,
        DepthBias = 5,
        CullMode = 6,
        FrontFace = 7,
        PrimitiveTopology = 8,
        DepthTestEnable = 9,
        DepthWriteEnable = 10,
        DepthCompareOp = 11,
        StencilTestEnable = 12,
        StencilOp = 13,
        
        // Count for array sizing
        Count = 14
    };

    enum class CullMode : uint32_t
    {
        None = 0,
        Front = 1,
        Back = 2,
        FrontAndBack = 3
    };

    enum class FrontFace : uint32_t
    {
        CounterClockwise = 0,
        Clockwise = 1
    };

    enum class CompareOp : uint32_t
    {
        Never = 0,
        Less = 1,
        Equal = 2,
        LessOrEqual = 3,
        Greater = 4,
        NotEqual = 5,
        GreaterOrEqual = 6,
        Always = 7
    };

    // 1:1 match to Slang, will tweak later if needed but now this lets us publish clear reflection data w/o needing slang polluting top-level includes
    enum class ShaderBindingType : uint8_t
    {
        Sampler,
        Texture,
        ConstantBuffer,
        ParameterBlock,
        TypedBuffer,
        RawBuffer,
        CombinedTextureSampler,
        InputRenderTarget,
        InlineUniformData,
        RayTracingAccelerationStructure,
        VaryingInput,
        VaryingOutput,
        ExistentialValue,
        PushConstant,
        MutableTexture,
        MutableTypedBuffer,
        MutableRawBuffer
    };

    // The values below match VK_PIPELINE_STAGE_2_*, VK_ACCESS_2_* and VkImageLayout (VulkanConversions.hpp checks them),
    // like LoadOp and StoreOp, so the Vulkan backend casts. A DX12 backend would map them to D3D12_BARRIER_SYNC/ACCESS.
    enum class PipelineStage : uint64_t
    {
        None = 0,
        TopOfPipe = 0x00000001ull,
        DrawIndirect = 0x00000002ull,
        VertexInput = 0x00000004ull,
        VertexShader = 0x00000008ull,
        FragmentShader = 0x00000080ull,
        EarlyFragmentTests = 0x00000100ull,
        LateFragmentTests = 0x00000200ull,
        ColorAttachmentOutput = 0x00000400ull,
        ComputeShader = 0x00000800ull,
        AllTransfer = 0x00001000ull,
        BottomOfPipe = 0x00002000ull,
        Host = 0x00004000ull,
        AllGraphics = 0x00008000ull,
        AllCommands = 0x00010000ull,
        TaskShader = 0x00080000ull,
        MeshShader = 0x00100000ull,
        Copy = 0x100000000ull,
        Resolve = 0x200000000ull,
        Blit = 0x400000000ull,
        Clear = 0x800000000ull,
    };

    MAKE_ENUM_CLASS_FLAGS(PipelineStage);

    enum class Access : uint64_t
    {
        None = 0,
        IndirectCommandRead = 0x00000001ull,
        IndexRead = 0x00000002ull,
        VertexAttributeRead = 0x00000004ull,
        UniformRead = 0x00000008ull,
        InputAttachmentRead = 0x00000010ull,
        ShaderRead = 0x00000020ull,
        ShaderWrite = 0x00000040ull,
        ColorAttachmentRead = 0x00000080ull,
        ColorAttachmentWrite = 0x00000100ull,
        DepthStencilAttachmentRead = 0x00000200ull,
        DepthStencilAttachmentWrite = 0x00000400ull,
        TransferRead = 0x00000800ull,
        TransferWrite = 0x00001000ull,
        HostRead = 0x00002000ull,
        HostWrite = 0x00004000ull,
        MemoryRead = 0x00008000ull,
        MemoryWrite = 0x00010000ull,
        ShaderSampledRead = 0x100000000ull,
        ShaderStorageRead = 0x200000000ull,
        ShaderStorageWrite = 0x400000000ull,
    };

    MAKE_ENUM_CLASS_FLAGS(Access);

    enum class ImageLayout : uint32_t
    {
        Undefined = 0,
        General = 1,
        ColorAttachment = 2,
        DepthStencilAttachment = 3,
        DepthStencilReadOnly = 4,
        ShaderReadOnly = 5,
        TransferSource = 6,
        TransferDestination = 7,
        Preinitialized = 8,
        ReadOnly = 1000314000,
        Attachment = 1000314001,
        PresentSource = 1000001002,
    };

    enum class ImageAspect : uint32_t
    {
        None = 0,
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2,
    };

    MAKE_ENUM_CLASS_FLAGS(ImageAspect);

    enum class PipelineBindPoint : uint8_t
    {
        Graphics,
        Compute,
    };

    enum class IndexType : uint8_t
    {
        Uint16,
        Uint32,
    };

}

#endif //!RHI_SYSTEM_RHI_FLAGS_HPP
