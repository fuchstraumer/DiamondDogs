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
    class ShaderBlob;

    /**
     * @brief ShaderPrograms represent a either a single compiled shader entry point,
     * or a set of entry points designed to be used together in a pipeline (e.g.,
     * vertex + fragment). They own the underlying API shader resource and expose
     * relevant metadata and reflection data for pipeline creation.
     */
    class ShaderProgram
    {
    public:

        ShaderProgram(DeviceHandle device, ShaderBlob& parent_blob, std::span<ShaderStage> stagesInProgram);
        ShaderProgram(std::unique_ptr<ShaderProgramImpl>&& impl) noexcept;

        ShaderProgram(const ShaderProgram&) = delete;
        ShaderProgram& operator=(const ShaderProgram&) = delete;
        ShaderProgram(ShaderProgram&& other) noexcept;
        ShaderProgram& operator=(ShaderProgram&& other) noexcept;
        
        ~ShaderProgram();
        void Destroy() noexcept;

        Result BindShaders(CommandBufferHandle cmdBuffer) const;
        bool IsSingleStage() const noexcept;
        ShaderStageFlags GetStages() const noexcept;
        std::span<std::string> GetEntryPointNames() const noexcept;
        const std::vector<SpecializationConstantReflection>& GetSpecializationConstants() const noexcept;
        const std::vector<PushConstantReflection>& GetPushConstantRanges() const noexcept;

        static Result CreateFromBinary(
            DeviceHandle device,
            std::span<ShaderBinaryOptions> binaryOptions,
            std::unique_ptr<ShaderProgram>& outProgram);
        
    private:
        friend class ShaderBlob;
        std::unique_ptr<ShaderProgramImpl> impl;
    };

} // namespace rhi

#endif // !RHI_SYSTEM_SHADER_PROGRAM_HPP
