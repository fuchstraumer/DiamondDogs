#include "ShaderBlob.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"

namespace rhi
{
    // Temporary for now, until I figure out a better way to handle this (or if we even need to, really)
    // Access to compiled modules and bytecode is already guarded
    static std::unique_ptr<ShaderCompiler> g_ShaderCompiler;

    ShaderBlob::ShaderBlob(ShaderBlobCompileOptions&& options)
    {
        try
        {
            ShaderCompiler compiler;
            ShaderCompiler::ModuleCompileOptions compileOptions(std::move(options));
            // dispatch compile message, we own the reply object and won't block on it until we need to
            auto compileReply = compiler.CompileModule(compileOptions);
        }
        catch (...)
        {
            // In case of any exceptions during compilation, we can choose to either throw or set an internal error state. For now, we'll just throw.
            throw std::runtime_error("ShaderBlob compilation failed");
        }
    }

}