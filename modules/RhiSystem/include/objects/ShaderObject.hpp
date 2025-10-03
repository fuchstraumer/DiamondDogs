#pragma once
#ifndef RHI_SYSTEM_SHADER_OBJECT_HPP
#define RHI_SYSTEM_SHADER_OBJECT_HPP
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiTypes.hpp"
#include "RhiFlags.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <memory>

namespace rhi
{
    class Device;
    struct ShaderObjectImpl;

    /**
     * @brief Modern shader object abstraction using VK_EXT_shader_object or DirectX 12 equivalents
     * Provides Slang-based shader compilation with reflection data extraction for specialization
     * constants and push constants. Designed as minimal foundation for higher-level abstractions.
     */
    class ShaderObject
    {
    public:
        /**
         * @brief Shader compilation options for Slang source files
         */
        struct CompileOptions
        {
            std::span<const char*> SearchPaths{}; // Additional include search paths
            std::span<const char*> ModuleNames{}; // Additional module names to load
            std::filesystem::path SlangSourcePath{};
            // Name to assign to the module in Slang. If empty, will use filename without extension
            std::string ModuleName;
            // Entry point name
            std::string EntryPointName = "main";
            ShaderStageFlags Stage = ShaderStageFlags::None;
            
            // Slang-specific compilation options
            std::string target = "spirv";           // "spirv" for Vulkan, "dxil" for DX12
            bool enableDebugInfo = false;
            bool enableOptimizations = true;
            bool enableValidation = true;
        };

        /**
         * @brief Extended specialization constant with name and size information from reflection
         */
        struct ReflectedSpecializationConstant
        {
            uint32_t constantId = 0;
            std::string name{};
            uint32_t size = 0;                      // Size in bytes
            SpecializationConstant::ValueType defaultValue{};
        };


        ShaderObject();

        static Result Create(DeviceHandle device, const CompileOptions& options, ShaderObject& outShaderObject);
        
        // No copy semantics
        ShaderObject(const ShaderObject&) = delete;
        ShaderObject& operator=(const ShaderObject&) = delete;
        
        // Move semantics
        ShaderObject(ShaderObject&& other) noexcept;
        ShaderObject& operator=(ShaderObject&& other) noexcept;
        
        ~ShaderObject();

        // Core access
        ShaderObjectHandle Handle() const noexcept;
        ShaderStageFlags GetStage() const noexcept;
        
        // Compiled bytecode access
        std::span<const uint8_t> GetBytecode() const noexcept;
        size_t GetBytecodeSize() const noexcept;
        
        // Reflection data
        const std::vector<ReflectedSpecializationConstant>& GetSpecializationConstants() const noexcept;
        const std::vector<PushConstantRange>& GetPushConstantRanges() const noexcept;
        
        // Validation and diagnostics
        bool IsValid() const noexcept;
        std::string_view GetCompilationLog() const noexcept;
        std::string_view GetEntryPointName() const noexcept;
        const std::filesystem::path& GetSourcePath() const noexcept;

    private:
        // Private constructor - use Create() factory method
        explicit ShaderObject(std::unique_ptr<ShaderObjectImpl> impl);
 
        
        std::unique_ptr<ShaderObjectImpl> impl;
        ShaderObjectHandle handle;
    };

} // namespace rhi

#endif // !RHI_SYSTEM_SHADER_OBJECT_HPP