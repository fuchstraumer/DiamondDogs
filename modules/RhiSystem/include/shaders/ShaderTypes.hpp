#pragma once
#ifndef RHI_SYSTEM_SHADER_TYPES_HPP
#define RHI_SYSTEM_SHADER_TYPES_HPP
#include "RhiFlags.hpp"
#include "RhiTypes.hpp"
#include "RhiHandle.hpp"
#include <optional>
#include <string>
#include <filesystem>
#include <span>
#include <string_view>


/** Contains shared structures used across the shader system, mostly those that are publicly accessible and used for compile configuration + reflection */
namespace rhi
{

    /** @brief Used primarily to create ShaderPrograms - a span of these is used to
     * retrieve the entry points to bundle together from the parent blob, to create
     * the needed API objects. */
    struct ShaderStage
    {
        ShaderStageFlags Stage;
        std::string_view EntryPointName;
    };

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
     * @brief Reflection data for descriptor bindings
     */
    struct DescriptorBindingReflection
    {
        uint32_t Set;
        uint32_t Binding;
        std::string Name;
        ShaderBindingType BindingType; // From Slang
        uint32_t DescriptorCount;
        ShaderStageFlags StageFlags;
        std::string ResourceTypeName; // e.g., "Texture2D", "StructuredBuffer<Foo>"
        
        // Optional member-level reflection for structured types
        struct MemberReflection
        {
            std::string Name;
            uint32_t Offset;
            uint32_t Size;
            std::string TypeName;
        };
        std::optional<std::vector<MemberReflection>> Members;
    };

    /**
     * @brief Complete reflection data for a single shader entry point
     */
    struct ShaderReflection
    {
        std::string EntryPointName;
        ShaderStageFlags Stage;

        // Resource bindings (optional)
        std::optional<std::vector<DescriptorBindingReflection>> DescriptorBindings;
        
        // Specialization constants (always generated if reflection enabled)
        std::vector<SpecializationConstantReflection> SpecializationConstants;
        
        // Push constants (always generated if reflection enabled)
        std::vector<PushConstantReflection> PushConstantRanges;
        
        // Compute shader specific
        struct ComputeInfo
        {
            uint32_t ThreadGroupSizeX;
            uint32_t ThreadGroupSizeY;
            uint32_t ThreadGroupSizeZ;
        };
        std::optional<ComputeInfo> ComputeWorkgroupSize;
        
        // Fragment shader specific
        struct FragmentInfo
        {
            bool WritesDepth;
            uint32_t NumColorAttachments;
        };
        std::optional<FragmentInfo> FragmentOutputs;
        
        // Full YAML dump (only in debug builds when SHADER_COMPILER_ENABLE_YAML_REFLECTION is true)
        #if defined(_DEBUG) || !defined(NDEBUG)
        std::string FullReflectionYaml;
        #endif
    };

    enum class TargetShaderIR : uint8_t
    {
        Invalid = 0,
        SPIRV,
        DXIL
    };

    /**
     * @brief Shader compilation options for Slang source files
    */
    struct ShaderBlobCompileOptions
    {
        std::filesystem::path SlangSourcePath{};
        // Name to assign to the module in Slang. If empty, will use filename without extension
        std::string ModuleName;
        std::span<std::filesystem::path> SearchPaths{}; // Additional include search paths
        std::span<std::string_view> ModuleNames{}; // Additional module names to load

        TargetShaderIR TargetIR = TargetShaderIR::SPIRV;
        bool EnableDebugInfo = false;
        bool EnableOptimizations = true;
        bool EnableValidation = true;

        bool CompileAllEntryPoints = true;
        std::span<std::string_view> SpecificEntryPoints{}; // Only used if CompileAllEntryPoints is false

        bool GenerateReflectionData = true;
        bool GenerateDescriptorReflection = true; // Requires GenerateReflectionData = true
        bool GenerateMemberReflection = true;    // Include struct member details for descriptors
    };

    // Minimal struct for push constant ranges, for use with ShaderBinaryOptions when creating ShaderPrograms without reflection data
    struct PushConstantRange
    {
        uint32_t Offset;
        uint32_t Size;
        ShaderStageFlags StageFlags;
    };
    
    /**
     * @brief Raw bytecode options for pre-compiled shaders
     */
    struct ShaderBinaryOptions
    {
        std::span<const uint32_t> Bytecode{}; // SPIR-V or DXIL bytecode
        ShaderStageFlags Stage = ShaderStageFlags::None;
        std::string EntryPointName = "main";
        // Push constants need to be provided separately since we don't reflect on them
        std::span<const PushConstantRange> PushConstants{};
        std::span<rhi::DescriptorSetLayoutHandle> DescriptorLayouts{};
    };

}

#endif //!RHI_SYSTEM_SHADER_TYPES_HPP
