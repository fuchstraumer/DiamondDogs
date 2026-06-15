#pragma once
#ifndef RHI_SYSTEM_SHADER_BLOB_HPP
#define RHI_SYSTEM_SHADER_BLOB_HPP
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include <span>
#include <string_view>

namespace rhi
{
    struct ShaderBlobImpl;
    class ShaderModuleCompileReply;

    /** @brief Container for multiple entry points, effectively the output of a Slang module compile. It owns the API resource,
     * i.e. ShaderModule for Vulkan and array of ID3D12Blobs per entry point for DirectX. Users then retrieve individual entry
     * points for pipeline creation and such by using ShaderStage objects, with reflection data queried from this as well (since
     * that is shared between stages) */
    class ShaderBlob
    {
    public:

        /** TODO: We probably want a way to either store enabled device/IR options in the compile options, or query them from the RHI device per API? */
        ShaderBlob(DeviceHandle device, ShaderBlobCompileOptions&& options);
        ~ShaderBlob();

        ShaderBlob(const ShaderBlob&) = delete;
        ShaderBlob& operator=(const ShaderBlob&) = delete;

        ShaderBlob(ShaderBlob&& other) noexcept;
        ShaderBlob& operator=(ShaderBlob&& other) noexcept;

        std::span<const std::string_view> GetAllEntrypoints() const noexcept;
        std::string_view GetModuleName() const noexcept;

        /** @brief Retrieve a program object constructed from the given set of shader stages and entrypoint 
         *  @param stagesInProgram Set of stages and entry points to include in the program. Must be a subset of the entry points compiled in this blob.
         *  @return ShaderProgram reference with the requested stages, or throws if any entry point is invalid or compilation is not yet complete. 
         * @note The returned reference is valid as long as the ShaderBlob object is alive.
        */
        ShaderProgram* GetShaderProgram(std::span<ShaderStage> stagesInProgram);

    private:
        friend class ShaderProgram;
        std::unique_ptr<ShaderBlobImpl> impl;
        std::shared_ptr<ShaderModuleCompileReply> compileReply;
    };

}

#endif // !RHI_SYSTEM_SHADER_BLOB_HPP
