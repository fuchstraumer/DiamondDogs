#pragma once
#ifndef RHI_SYSTEM_SHADER_COMPILER_MESSAGES_HPP
#define RHI_SYSTEM_SHADER_COMPILER_MESSAGES_HPP
#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include <variant>
#include <optional>
#include <vector>
#include <string>

namespace rhi
{
    /**
     * @brief Internal message for module compilation request
     */
    struct CompileModuleMessage
    {

        CompileModuleMessage() : Options{}, Reply{ nullptr } {}

        // Copy of options with owned string data (spans converted to vectors)
        struct OwnedModuleCompileOptions
        {
            std::filesystem::path SlangSourcePath;
            std::string ModuleName;
            std::vector<std::filesystem::path> SearchPaths;
            std::vector<std::string> AdditionalModules;
            std::string Target;
            bool EnableDebugInfo;
            bool EnableOptimizations;
            bool EnableValidation;
            bool CompileAllEntryPoints;
            std::vector<std::string> SpecificEntryPoints;
            bool GenerateReflectionData;
            bool GenerateDescriptorReflection;
            bool GenerateMemberReflection;

            OwnedModuleCompileOptions() :
                EnableDebugInfo{ false },
                EnableOptimizations{ false },
                EnableValidation{ false },
                CompileAllEntryPoints{ false },
                GenerateReflectionData{ false },
                GenerateDescriptorReflection{ false },
                GenerateMemberReflection{ false }
            {
            }

            // Construct from user-facing options (takes copies of span data)
            explicit OwnedModuleCompileOptions(const ShaderCompiler::ModuleCompileOptions& opts) :
                SlangSourcePath{ opts.SlangSourcePath },
                ModuleName{ opts.ModuleName },
                SearchPaths{ opts.SearchPaths.begin(), opts.SearchPaths.end() },
                AdditionalModules{ opts.AdditionalModules.begin(), opts.AdditionalModules.end() },
                Target{ opts.Target == TargetShaderIR::SPIRV ? "spirv" : opts.Target == TargetShaderIR::DXIL ? "dxil" : "invalid" },
                EnableDebugInfo{ opts.EnableDebugInfo },
                EnableOptimizations{ opts.EnableOptimizations },
                EnableValidation{ opts.EnableValidation },
                CompileAllEntryPoints{ opts.CompileAllEntryPoints },
                SpecificEntryPoints{ opts.SpecificEntryPoints.begin(), opts.SpecificEntryPoints.end() },
                GenerateReflectionData{ opts.GenerateReflectionData },
                GenerateDescriptorReflection{ opts.GenerateDescriptorReflection },
                GenerateMemberReflection{ opts.GenerateMemberReflection }
            {
            }
        };

        OwnedModuleCompileOptions Options;
        std::shared_ptr<ShaderModuleCompileReply> Reply;
    };

    /**
     * @brief Internal message for reflection query
     */
    struct QueryReflectionMessage
    {
        ShaderCompiler::ShaderIdentifier Identifier;
        std::shared_ptr<ShaderReflectionQueryReply> Reply;
    };

    /**
     * @brief Type-erased message payload variant
     */
    using ShaderCompilerMessagePayload = std::variant<
        CompileModuleMessage,
        QueryReflectionMessage>;

} // namespace rhi

#endif // RHI_SYSTEM_SHADER_COMPILER_MESSAGES_HPP
