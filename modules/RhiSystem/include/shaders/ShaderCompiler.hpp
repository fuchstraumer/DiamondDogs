#pragma once
#ifndef RHI_SYSTEM_SHADER_COMPILER_HPP
#define RHI_SYSTEM_SHADER_COMPILER_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "RhiFlags.hpp"
#include <slang.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <span>
#include <memory>
#include <vector>
#include <optional>

namespace rhi
{
    class Device;
    struct ShaderCompilerImpl;
    
    // Forward declarations for reply types
    class ShaderModuleCompileReply;
    class ShaderReflectionQueryReply;

    /**
     * @brief Message-passing shader compilation system for Slang modules
     * 
     * Provides asynchronous compilation of Slang shader modules with support for
     * multiple entry points per module. Currently processes messages synchronously
     * but designed for future multi-threaded operation via MWSR queue pattern.
     */
    class ShaderCompiler
    {
    public:
        /**
         * @brief Configuration for compiling a Slang module with all entry points
         */
        struct ModuleCompileOptions
        {
            std::filesystem::path SlangSourcePath;
            std::string ModuleName; // If empty, uses filename without extension
            std::span<const std::string_view> SearchPaths{};
            std::span<const std::string_view> AdditionalModules{};
            
            // Compilation settings
            std::string Target = "spirv"; // "spirv" for Vulkan, "dxil" for DX12
            bool EnableDebugInfo = false;
            bool EnableOptimizations = true;
            bool EnableValidation = true;
            
            // Control what gets compiled
            bool CompileAllEntryPoints = true;
            std::span<const std::string_view> SpecificEntryPoints{}; // Only if !CompileAllEntryPoints
            
            // Control reflection data generation
            bool GenerateReflectionData = true;
            bool GenerateDescriptorReflection = true; // Requires GenerateReflectionData = true
            bool GenerateMemberReflection = false;    // Include struct member details for descriptors
        };

        /**
         * @brief Identifier for a specific shader within a compiled module
         */
        struct ShaderIdentifier
        {
            std::string ModuleName;
            std::string EntryPointName;
            ShaderStageFlags Stage;
            
            bool operator==(const ShaderIdentifier& other) const noexcept;
            size_t Hash() const noexcept;
        };

        /**
         * @brief Compiled shader binary with minimal metadata
         */
        struct CompiledShader
        {
            ShaderIdentifier Identifier;
            std::span<const uint8_t> Bytecode; // Non-owning view into internal storage
            bool IsValid;
            std::string ErrorMessage; // Only populated if IsValid == false
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
            slang::BindingType BindingType; // From Slang
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
            ShaderIdentifier Identifier;
            
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

        ShaderCompiler();
        ~ShaderCompiler();
        
        // No copy semantics
        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        /**
         * @brief Initialize the shader compiler with a device context
         */
        Result Initialize(DeviceHandle device);
        
        /**
         * @brief Shutdown the compiler and wait for pending operations
         */
        void Shutdown();

        /**
         * @brief Compile a Slang module with all/specified entry points
         * 
         * @param options Compilation configuration
         * @return Shared pointer to reply object for tracking compilation progress
         * 
         * @note Reply contains status and allows querying individual shaders once complete.
         *       Per-entry-point errors are tracked separately - some shaders may succeed while others fail.
         */
        [[nodiscard]] std::shared_ptr<ShaderModuleCompileReply> CompileModule(
            const ModuleCompileOptions& options);

        /**
         * @brief Query reflection data for a previously compiled shader
         * 
         * @param identifier Shader to query reflection for
         * @return Shared pointer to reply object with reflection data
         * 
         * @note Returns cached data if available, otherwise generates on-demand
         */
        [[nodiscard]] std::shared_ptr<ShaderReflectionQueryReply> QueryReflection(
            const ShaderIdentifier& identifier);

        /**
         * @brief Retrieve compiled bytecode for a specific shader
         * 
         * @param identifier Shader to retrieve
         * @return CompiledShader with bytecode span, or invalid shader if not found
         * 
         * @note Synchronous operation - blocks until shader is available or confirmed missing.
         *       Returned span is valid until the module is recompiled or the compiler is destroyed.
         */
        [[nodiscard]] CompiledShader GetCompiledShader(
            const ShaderIdentifier& identifier);

        /**
         * @brief Check if a module has been compiled
         */
        bool IsModuleCompiled(const std::string& moduleName) const;

        /**
         * @brief Get all entry point names discovered in a compiled module
         */
        std::vector<std::string> GetModuleEntryPoints(const std::string& moduleName) const;

    private:
        std::unique_ptr<ShaderCompilerImpl> impl;
    };

} // namespace rhi

// Hash support for ShaderIdentifier (for use in unordered containers)
namespace std
{
    template<>
    struct hash<rhi::ShaderCompiler::ShaderIdentifier>
    {
        size_t operator()(const rhi::ShaderCompiler::ShaderIdentifier& id) const noexcept
        {
            return id.Hash();
        }
    };
}

#endif // RHI_SYSTEM_SHADER_COMPILER_HPP
