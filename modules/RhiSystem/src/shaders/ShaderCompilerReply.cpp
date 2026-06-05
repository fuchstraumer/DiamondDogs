#include "ShaderCompilerReply.hpp"
#include <stdexcept>

namespace rhi
{
    // ShaderModuleCompileReply implementation
    ShaderModuleCompileReply::ShaderModuleCompileReply() :
        status{ Status::Invalid }
    {
    }

    ShaderModuleCompileReply::Status ShaderModuleCompileReply::GetStatus() const noexcept
    {
        return status.load(std::memory_order_acquire);
    }

    bool ShaderModuleCompileReply::IsComplete() const noexcept
    {
        return status.load(std::memory_order_acquire) == Status::Complete;
    }

    bool ShaderModuleCompileReply::HasFailed() const noexcept
    {
        return status.load(std::memory_order_acquire) == Status::Failed;
    }

    std::string ShaderModuleCompileReply::GetModuleName() const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        return moduleName;
    }

    std::vector<std::string> ShaderModuleCompileReply::GetEntryPointNames() const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        return entryPointNames;
    }

    std::string ShaderModuleCompileReply::GetCompilationLog() const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        return compilationLog;
    }

    ShaderCompiler::CompiledShader ShaderModuleCompileReply::GetShader(const std::string& entryPointName) const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        
        // Check if this entry point failed
        auto failedIt = failedEntryPoints.find(entryPointName);
        if (failedIt != failedEntryPoints.end())
        {
            ShaderCompiler::CompiledShader failedShader{};
            failedShader.IsValid = false;
            failedShader.ErrorMessage = failedIt->second;
            return failedShader;
        }
        
        // Try to find compiled shader
        auto it = compiledShaders.find(entryPointName);
        if (it != compiledShaders.end())
        {
            return it->second;
        }
        
        // Not found
        ShaderCompiler::CompiledShader notFound{};
        notFound.IsValid = false;
        notFound.ErrorMessage = "Entry point not found in compiled module";
        return notFound;
    }

    std::vector<ShaderCompiler::CompiledShader> ShaderModuleCompileReply::GetAllShaders() const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        std::vector<ShaderCompiler::CompiledShader> result;
        result.reserve(compiledShaders.size());
        
        for (const auto& [name, shader] : compiledShaders)
        {
            if (shader.IsValid)
            {
                result.push_back(shader);
            }
        }
        
        return result;
    }

    bool ShaderModuleCompileReply::IsEntryPointValid(const std::string& entryPointName) const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        
        // Check if it failed
        if (failedEntryPoints.find(entryPointName) != failedEntryPoints.end())
        {
            return false;
        }
        
        // Check if it compiled successfully
        auto it = compiledShaders.find(entryPointName);
        if (it != compiledShaders.end())
        {
            return it->second.IsValid;
        }
        
        return false;
    }

    void ShaderModuleCompileReply::SetStatus(Status newStatus)
    {
        status.store(newStatus, std::memory_order_release);
    }

    void ShaderModuleCompileReply::SetModuleName(std::string name)
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        moduleName = std::move(name);
    }

    void ShaderModuleCompileReply::AddCompiledShader(ShaderCompiler::CompiledShader shader)
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        
        const std::string& entryPointName = shader.Identifier.EntryPointName;
        
        // Add to entry point names list if not already present
        if (std::find(entryPointNames.begin(), entryPointNames.end(), entryPointName) == entryPointNames.end())
        {
            entryPointNames.push_back(entryPointName);
        }
        
        compiledShaders[entryPointName] = std::move(shader);
    }

    void ShaderModuleCompileReply::AddFailedEntryPoint(std::string entryPointName, std::string errorMessage)
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        
        // Add to entry point names list if not already present
        if (std::find(entryPointNames.begin(), entryPointNames.end(), entryPointName) == entryPointNames.end())
        {
            entryPointNames.push_back(entryPointName);
        }
        
        failedEntryPoints[std::move(entryPointName)] = std::move(errorMessage);
    }

    void ShaderModuleCompileReply::SetCompilationLog(std::string log)
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        compilationLog = std::move(log);
    }

    // ShaderReflectionQueryReply implementation
    ShaderReflectionQueryReply::ShaderReflectionQueryReply() :
        status{ Status::Invalid }
    {
    }

    ShaderReflectionQueryReply::Status ShaderReflectionQueryReply::GetStatus() const noexcept
    {
        return status.load(std::memory_order_acquire);
    }

    bool ShaderReflectionQueryReply::IsComplete() const noexcept
    {
        return status.load(std::memory_order_acquire) == Status::Complete;
    }

    bool ShaderReflectionQueryReply::HasFailed() const noexcept
    {
        return status.load(std::memory_order_acquire) == Status::Failed;
    }

    ShaderCompiler::ShaderReflection ShaderReflectionQueryReply::GetReflection() const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        return reflection;
    }

    std::string ShaderReflectionQueryReply::GetErrorMessage() const
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        return errorMessage;
    }

    void ShaderReflectionQueryReply::SetStatus(Status newStatus)
    {
        status.store(newStatus, std::memory_order_release);
    }

    void ShaderReflectionQueryReply::SetReflection(ShaderCompiler::ShaderReflection newReflection)
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        reflection = std::move(newReflection);
    }

    void ShaderReflectionQueryReply::SetError(std::string error)
    {
        std::lock_guard<std::mutex> lock{ dataMutex };
        errorMessage = std::move(error);
    }

} // namespace rhi
