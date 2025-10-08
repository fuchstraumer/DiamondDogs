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
        WithSynchronization = 2,    // Base + synchronization validation
        Full = 3                    // All available layers
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

}

#endif //!RHI_SYSTEM_RHI_FLAGS_HPP
