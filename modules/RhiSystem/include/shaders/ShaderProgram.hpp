#pragma once
#ifndef RHI_SYSTEM_SHADER_PROGRAM_HPP
#define RHI_SYSTEM_SHADER_PROGRAM_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "ShaderTypes.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi
{
    class Device;
    struct ShaderProgramImpl;

    /**
     * @brief ShaderPrograms represent a either a single compiled shader entry point,
     * or a set of entry points designed to be used together in a pipeline (e.g.,
     * vertex + fragment). They own the underlying API shader resource and expose
     * relevant metadata and reflection data for pipeline creation.
     */
    class ShaderProgram
    {
        // Shouldn't be constructible publicly, only from ShaderBlob or static factory from
        // precompiled bytecode
        ShaderProgram();
    public:

        ShaderProgram(const ShaderProgram&) = delete;
        ShaderProgram& operator=(const ShaderProgram&) = delete;
        ShaderProgram(ShaderProgram&& other) noexcept;
        ShaderProgram& operator=(ShaderProgram&& other) noexcept;
        
        ~ShaderProgram();
        void Destroy() noexcept;

        std::span<ShaderObjectHandle> GetHandles() const noexcept;
        bool IsSingleStage() const noexcept;
        ShaderStageFlags GetStages() const noexcept;
        std::span<std::string_view> GetEntryPointNames() const noexcept;
        const std::vector<SpecializationConstantReflection>& GetSpecializationConstants() const noexcept;
        const std::vector<PushConstantReflection>& GetPushConstantRanges() const noexcept;

    private:
        // again, private ctor so we can control creation exactly
        explicit ShaderProgram(std::unique_ptr<ShaderProgramImpl> impl);
        std::unique_ptr<ShaderProgramImpl> impl;
    };

} // namespace rhi

#endif // !RHI_SYSTEM_SHADER_PROGRAM_HPP
