#pragma once
#ifndef RHI_SYSTEM_SHADER_COMPILER_HPP
#define RHI_SYSTEM_SHADER_COMPILER_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "RhiFlags.hpp"
#include "ShaderTypes.hpp"
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
            ModuleCompileOptions() = default;

            // move ctor since most external callers work with temp blob options objects
            ModuleCompileOptions(ShaderBlobCompileOptions&& blobOptions) noexcept;
            std::filesystem::path SlangSourcePath;
            std::string ModuleName; // If empty, uses filename without extension
            std::span<const std::filesystem::path> SearchPaths{};
            std::span<const std::string_view> AdditionalModules{};
            
            // Compilation settings
            TargetShaderIR Target = TargetShaderIR::Invalid;
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
