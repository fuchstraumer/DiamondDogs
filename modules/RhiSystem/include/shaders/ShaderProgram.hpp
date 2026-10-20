#pragma once
#ifndef RHI_SYSTEM_SHADER_PROGRAM_HPP
#define RHI_SYSTEM_SHADER_PROGRAM_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "ShaderTypes.hpp"
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
    public:

        ShaderProgram(std::unique_ptr<ShaderProgramImpl>&& impl) noexcept;

        ShaderProgram(const ShaderProgram&) = delete;
        ShaderProgram& operator=(const ShaderProgram&) = delete;
        ShaderProgram(ShaderProgram&& other) noexcept;
        ShaderProgram& operator=(ShaderProgram&& other) noexcept;
        
        ~ShaderProgram();
        void Destroy() noexcept;

        [[nodiscard]] Result BindShaders(const CommandBufferHandle& cmd_buffer) const;
        [[nodiscard]] bool IsSingleStage() const noexcept;
        [[nodiscard]] ShaderStageFlags GetStages() const noexcept;
        [[nodiscard]] std::span<std::string> GetEntryPointNames() const noexcept;
        [[nodiscard]] const std::vector<SpecializationConstantReflection>& GetSpecializationConstants() const noexcept;
        [[nodiscard]] const std::vector<PushConstantReflection>& GetPushConstantRanges() const noexcept;

        static Result CreateFromBinary(
            const DeviceHandle& device,
            std::span<ShaderBinaryOptions> binary_options,
            std::unique_ptr<ShaderProgram>& out_program);
        
    private:
        std::unique_ptr<ShaderProgramImpl> impl;
    };

} // namespace rhi

#endif // !RHI_SYSTEM_SHADER_PROGRAM_HPP
