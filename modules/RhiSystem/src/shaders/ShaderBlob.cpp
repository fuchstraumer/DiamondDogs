#include "ShaderBlob.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include "utility/MurmurHash.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace rhi
{

    // Temporary for now, until I figure out a better way to handle this (or if we even need to, really)
    // Access to compiled modules and bytecode is already guarded
    static std::unique_ptr<ShaderCompiler> g_ShaderCompiler;
    constexpr static uint64_t k_MurmurHashSeed = 0x9E3779B97F4A7C15; // Large prime number for hashing

    uint64_t ComputeProgramHash(std::span<ShaderStage> stages)
    {
        // Simple hash combining stage flags and entry point names
        uint64_t hash = k_MurmurHashSeed;
        for (const auto& stage : stages)
        {
            hash = MurmurHash2(&stage.Stage, sizeof(stage.Stage), hash);
            hash = MurmurHash2(stage.EntryPointName.data(), stage.EntryPointName.size(), hash);
        }
        return hash;
    }

    struct ShaderBlobImpl
    {
        ShaderBlobImpl(DeviceHandle device);
        ~ShaderBlobImpl();
        DeviceHandle ParentDevice;
        std::string ModuleName;
        std::vector<std::string> EntryPointNames;
        std::unordered_map<std::string_view, ShaderStageFlags> EntryPointToStageFlags;
        using DescriptorBindingRange = std::vector<DescriptorBindingReflection>;
        using SpecConstantRange = std::vector<SpecializationConstantReflection>;
        std::unordered_map<std::string_view, DescriptorBindingRange> EntryPointToDescriptorBindings;
        std::unordered_map<std::string_view, SpecConstantRange> EntryPointToSpecConstants;
        std::unordered_map<uint64_t, ShaderProgram> CachedShaderPrograms; // Hash of combined entry points and stages -> ShaderProgram
    };

    ShaderBlob::ShaderBlob(DeviceHandle device, ShaderBlobCompileOptions&& options) : impl(std::make_unique<ShaderBlobImpl>(device))
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

    ShaderProgram* ShaderBlob::GetShaderProgram(std::span<ShaderStage> stagesInProgram)
    {
        if (!compileReply || !compileReply->IsComplete())
        {
            throw std::runtime_error("ShaderBlob compilation not complete");
        }

        uint64_t programHash = ComputeProgramHash(stagesInProgram);
        auto cachedIt = impl->CachedShaderPrograms.find(programHash);
        if (cachedIt != impl->CachedShaderPrograms.end())
        {
            return &cachedIt->second;
        }
        else
        {
            // time to create a new program! We'll do this synchronously for now, until we create our meta-description file that 
            // tells us which combinations of entry points and stages we actually need to create programs for, at which point we can
            // start doing this asynchronously as well
            auto [it, inserted] =
                impl->CachedShaderPrograms.emplace(programHash, ShaderProgram(impl->ParentDevice, *this, stagesInProgram));
            return &it->second;
        }

    }

}