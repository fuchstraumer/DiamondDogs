#include "ShaderBlob.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace rhi
{

    // Temporary for now, until I figure out a better way to handle this (or if we even need to, really)
    // Access to compiled modules and bytecode is already guarded
    static std::unique_ptr<ShaderCompiler> g_ShaderCompiler;

    struct ShaderBlobImpl
    {
        std::string ModuleName;
        std::vector<std::string> EntryPointNames;
        std::unordered_map<std::string_view, ShaderStageFlags> EntryPointToStageFlags;
        using DescriptorBindingRange = std::vector<DescriptorBindingReflection>;
        using SpecConstantRange = std::vector<SpecializationConstantReflection>;
        std::unordered_map<std::string_view, DescriptorBindingRange> EntryPointToDescriptorBindings;
        std::unordered_map<std::string_view, SpecConstantRange> EntryPointToSpecConstants;
    };

    ShaderBlob::ShaderBlob(ShaderBlobCompileOptions&& options) : impl(std::make_unique<ShaderBlobImpl>())
    {
        try
        {
            ShaderCompiler compiler;
            ShaderCompiler::ModuleCompileOptions compileOptions(std::move(options));
            // dispatch compile message, we own the reply object and won't block on it until we need to
            compileReply = compiler.CompileModule(compileOptions);
        }
        catch (...)
        {
            // In case of any exceptions during compilation, we can choose to either throw or set an internal error state. For now, we'll just throw.
            throw std::runtime_error("ShaderBlob compilation failed");
        }
    }

    ShaderBlob::~ShaderBlob()
    {
        impl.reset();
    }

    std::string_view ShaderBlob::GetModuleName() const noexcept
    {
        if (compileReply)
        {
            return compileReply->GetModuleName();
        }
        return {};
    }

}