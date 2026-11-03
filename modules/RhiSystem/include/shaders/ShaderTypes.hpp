#pragma once
#ifndef RHI_SYSTEM_SHADER_TYPES_HPP
#define RHI_SYSTEM_SHADER_TYPES_HPP
#include "RhiFlags.hpp"
#include "RhiTypes.hpp"
#include "RhiHandle.hpp"
#include <vector>
#include <string>
#include <span>
#include <string_view>


/** Contains shared structures used across the shader system: program creation input and reflection */
namespace rhi
{

    /**
     * @brief Reflection data for specialization constants
     */
    struct SpecializationConstantReflection
    {
        uint32_t ConstantId;
        std::string Name;
        uint32_t SizeBytes;
        std::string TypeName; // e.g., "uint", "float", "bool"
        SpecializationConstant::ValueType DefaultValue;
    };

    /**
     * @brief Reflection data for push constants
     */
    struct PushConstantReflection
    {
        std::string Name;
        uint32_t Offset;
        uint32_t Size;
        ShaderStageFlags StageFlags;
        std::string TypeName; // Struct type name if applicable
        
        // Nested member information for structured push constants (optional)
        struct Member
        {
            std::string Name;
            uint32_t Offset;
            uint32_t Size;
            std::string TypeName;
        };
        std::vector<Member> Members;
    };

    /**
     * @brief Raw bytecode options for pre-compiled shaders
     */
    struct ShaderBinaryOptions
    {
        ShaderBinaryOptions(
            std::span<const uint32_t> bytecode,
            ShaderStageFlags stage,
            std::string_view entryPointName,
            std::span<PushConstantRange> pushConstants = {},
            std::span<rhi::DescriptorSetLayoutHandle> descriptorLayouts = {}) noexcept :
            Bytecode{ bytecode },
            Stage{ stage },
            EntryPointName{ entryPointName },
            PushConstants{ pushConstants },
            DescriptorLayouts{ descriptorLayouts }
        {
        }
        std::span<const uint32_t> Bytecode{}; // SPIR-V or DXIL bytecode
        ShaderStageFlags Stage = ShaderStageFlags::None;
        std::string EntryPointName = "main";
        // Push constants need to be provided separately since we don't reflect on them
        std::span<PushConstantRange> PushConstants{};
        std::span<rhi::DescriptorSetLayoutHandle> DescriptorLayouts{};
        // Compute only. Code that exchanges data across lanes (wave ops) and assumes lane == local thread index needs
        // both: a fixed subgroup size, and every subgroup full. The workgroup X size must be a multiple of the size.
        // 0 leaves the size to the driver. Needs the subgroupSizeControl and computeFullSubgroups features (1.3)
        uint32_t RequiredSubgroupSize{ 0u };
        bool RequireFullSubgroups{ false };
    };

}

#endif //!RHI_SYSTEM_SHADER_TYPES_HPP
