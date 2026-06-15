#pragma once
#ifndef RHI_SYSTEM_SHADER_COMPILER_REPLY_HPP
#define RHI_SYSTEM_SHADER_COMPILER_REPLY_HPP
#include "ShaderCompiler.hpp"
#include "ShaderTypes.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace rhi
{
    /**
     * @brief Reply object for module compilation operations
     * 
     * Thread-safe status checking and shader retrieval with per-entry-point error tracking
     */
    class ShaderModuleCompileReply
    {
    public:
        enum class Status
        {
            Pending,        // Queued but not started
            Compiling,      // Currently being processed
            Complete,       // All entry points processed (some may have failed)
            Failed,         // Module-level failure (couldn't load/parse module)
            Invalid         // Uninitialized
        };

        ShaderModuleCompileReply();
        ~ShaderModuleCompileReply() = default;

        // Thread-safe status query
        Status GetStatus() const noexcept;
        bool IsComplete() const noexcept;
        bool HasFailed() const noexcept;

        // Access compilation results (blocks until complete)
        std::string GetModuleName() const;
        std::vector<std::string> GetEntryPointNames() const;
        std::string GetCompilationLog() const;
        
        /**
         * @brief Get specific compiled shader (returns invalid shader if not found or failed)
         * @note Check CompiledShader::IsValid and CompiledShader::ErrorMessage for per-shader errors
         */
        ShaderCompiler::CompiledShader GetShader(const std::string& entryPointName) const;

        /**
         * @brief Get all successfully compiled shaders
         */
        std::vector<ShaderCompiler::CompiledShader> GetAllShaders() const;

        /**
         * @brief Check if a specific entry point compiled successfully
         */
        bool IsEntryPointValid(const std::string& entryPointName) const;

        // Internal use only - called by compiler implementation
        void SetStatus(Status status);
        void SetModuleName(std::string name);
        void AddCompiledShader(ShaderCompiler::CompiledShader shader);
        void AddFailedEntryPoint(std::string entryPointName, std::string errorMessage);
        void SetCompilationLog(std::string log);

    private:
        mutable std::mutex dataMutex;
        std::atomic<Status> status;
        
        std::string moduleName;
        std::vector<std::string> entryPointNames;
        std::unordered_map<std::string, ShaderCompiler::CompiledShader> compiledShaders;
        std::unordered_map<std::string, std::string> failedEntryPoints; // entryPoint -> error message
        std::string compilationLog;
    };

    /**
     * @brief Reply object for reflection data queries
     */
    class ShaderReflectionQueryReply
    {
    public:
        enum class Status
        {
            Pending,        // Queued but not started
            Processing,     // Currently generating reflection data
            Complete,       // Reflection data ready
            Failed,         // Reflection generation failed
            Invalid         // Uninitialized
        };

        ShaderReflectionQueryReply();
        ~ShaderReflectionQueryReply() = default;

        Status GetStatus() const noexcept;
        bool IsComplete() const noexcept;
        bool HasFailed() const noexcept;

        // Access reflection data (blocks until complete)
        ShaderReflection GetReflection() const;
        std::string GetErrorMessage() const;

        // Internal use only
        void SetStatus(Status status);
        void SetReflection(ShaderReflection reflection);
        void SetError(std::string error);

    private:
        mutable std::mutex dataMutex;
        std::atomic<Status> status;
        
        ShaderReflection reflection;
        std::string errorMessage;
    };

} // namespace rhi

#endif // RHI_SYSTEM_SHADER_COMPILER_REPLY_HPP
