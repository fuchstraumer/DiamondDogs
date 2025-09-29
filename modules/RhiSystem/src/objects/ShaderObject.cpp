#include "ShaderObject.hpp"
#include "Device.hpp"
#include "RhiDefines.hpp"
#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>
#include <format>

#ifdef RHI_SYSTEM_USE_VULKAN
    #include <vulkan/vulkan.h>
#elif defined(RHI_SYSTEM_USE_DX12)
    #include <d3d12.h>
    #include <dxgi1_6.h>
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <debugapi.h>

void win32_OutputDebugString(const char* str)
{
    OutputDebugStringA(str);
}

#endif

namespace rhi
{

    using SlangModulePtr = Slang::ComPtr<slang::IModule>;
    using SlangBlobPtr = Slang::ComPtr<slang::IBlob>;

#ifdef RHI_SYSTEM_USE_VULKAN
    struct ShaderObjectImpl
    {
        ShaderObjectImpl(const Device* device) :
            parentDevice{ device },
            vkShaderObject{ VK_NULL_HANDLE },
            stage{ ShaderStageFlags::None },
            bytecode{},
            specializationConstants{},
            pushConstantRanges{},
            compilationLog{},
            entryPointName{},
            sourcePath{},
            isValid{ false }
        {
        }

        ~ShaderObjectImpl()
        {
            if (vkShaderObject != VK_NULL_HANDLE)
            {
                vkDestroyShaderEXT(parentDevice->Handle().As<VkDevice>(), vkShaderObject, nullptr);
                vkShaderObject = VK_NULL_HANDLE;
            }
        }

        const Device* parentDevice;
        VkShaderEXT vkShaderObject;
        ShaderStageFlags stage;
        std::vector<uint8_t> bytecode;
        std::vector<ShaderObject::ReflectedSpecializationConstant> specializationConstants;
        std::vector<PushConstantRange> pushConstantRanges;
        std::string compilationLog;
        std::string entryPointName;
        std::filesystem::path sourcePath;
        bool isValid;
        Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;

        static VkShaderStageFlagBits ConvertStageToVulkan(ShaderStageFlags stage)
        {
            switch (stage)
            {
                case ShaderStageFlags::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
                case ShaderStageFlags::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
                case ShaderStageFlags::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
                case ShaderStageFlags::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
                case ShaderStageFlags::TesselationControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                case ShaderStageFlags::TesselationEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                default:
                    throw std::invalid_argument("Unsupported shader stage for Vulkan conversion");
            }
        }
#endif // RHI_SYSTEM_USE_VULKAN

        bool CompileShaderFromSlang(const ShaderObject::CompileOptions& options)
        {
            try
            {
                // Read source file
                if (!std::filesystem::exists(options.slangSourcePath))
                {
                    compilationLog = std::format("Slang source file not found: {}", options.slangSourcePath.string());
                    return false;
                }

                std::ifstream file(options.slangSourcePath, std::ios::binary | std::ios::ate);
                if (!file.is_open())
                {
                    compilationLog = std::format("Failed to open Slang source file: {}", options.slangSourcePath.string());
                    return false;
                }

                // use istreambuf iterator to read file content all at once into source code string
                std::string sourceCode{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
                if (sourceCode.empty())
                {
                    compilationLog = std::format("Failed to read Slang source file: {}", options.slangSourcePath.string());
                    return false;
                }

                // Initialize Slang
                if (!slangGlobalSession)
                {
                    // attempt to create global session
                    SlangResult result = slang::createGlobalSession(slangGlobalSession.writeRef());
                    if (result != SLANG_OK)
                    {
                        compilationLog = "Failed to create Slang global session";
                        return false;
                    }
                }

                // Will eventually cache this and make it more reusable, but for now this gets us running
                slang::SessionDesc sessionDesc = {};
                sessionDesc.searchPaths = options.searchPaths.data();
                sessionDesc.searchPathCount = static_cast<SlangInt>(options.searchPaths.size());
                sessionDesc.allowGLSLSyntax = true; // We write with GLSL-like syntax in many cases

                // Create compilation target
                slang::TargetDesc targetDesc = {};
                if (options.target == "spirv")
                {
                    targetDesc.format = SLANG_SPIRV;
                    targetDesc.profile = slangGlobalSession->findProfile("spirv_1_5");
                }
                else if (options.target == "dxil")
                {
                    targetDesc.format = SLANG_DXIL;
                    targetDesc.profile = slangGlobalSession->findProfile("sm_6_6");
                }
                else
                {
                    compilationLog = std::format("Unsupported compilation target: {}", options.target);
                    slangGlobalSession->release();
                    return false;
                }

                sessionDesc.targets = &targetDesc;
                sessionDesc.targetCount = 1;

                // Add compiler options
                std::vector<slang::CompilerOptionEntry> compilerOptions;
                
                if (options.enableOptimizations)
                {
                    slang::CompilerOptionEntry opt;
                    opt.name = slang::CompilerOptionName::Optimization;
                    opt.value.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
                    compilerOptions.push_back(opt);
                }

                if (options.enableDebugInfo)
                {
                    slang::CompilerOptionEntry debug;
                    debug.name = slang::CompilerOptionName::DebugInformation;
                    debug.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
                    compilerOptions.push_back(debug);
                }

#ifdef RHI_SYSTEM_USE_VULKAN
                slang::CompilerOptionEntry vulkanOption;
                vulkanOption.name = slang::CompilerOptionName::VulkanEmitReflection;
                vulkanOption.value.intValue0 = 1; // Enable reflection
                compilerOptions.push_back(vulkanOption);
                vulkanOption.name = slang::CompilerOptionName::AllowGLSL;
                vulkanOption.value.intValue0 = 1; // Allow GLSL-like syntax
                compilerOptions.push_back(vulkanOption);
                vulkanOption.name = slang::CompilerOptionName::EmitSpirvDirectly;
                vulkanOption.value.intValue0 = 1; // Emit SPIR-V directly, so we can use it as-is
                compilerOptions.push_back(vulkanOption);
#endif

                sessionDesc.compilerOptionEntries = compilerOptions.data();
                sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptions.size());

                // Create session
                Slang::ComPtr<slang::ISession> resultSession;
                SlangResult result = slangGlobalSession->createSession(sessionDesc, resultSession.writeRef());
                if (SLANG_FAILED(result) || !resultSession)
                {
                    compilationLog = "Failed to create Slang session";
                    slangGlobalSession->release();
                    return false;
                }

                std::vector<SlangModulePtr> loadedModules;
                for (const char* moduleName : options.moduleNames)
                {
                    SlangModulePtr module;
                    module = resultSession->loadModule(moduleName);
                    if (SLANG_FAILED(result) || !module)
                    {
                        compilationLog = std::format("Failed to load Slang module: {}", moduleName);
                        for (auto& mod : loadedModules) mod->release();
                        resultSession->release();
                        slangGlobalSession->release();
                        return false;
                    }
                    loadedModules.push_back(module);
                }

                // Now load the main module from given source path
                SlangModulePtr sourceModule;
                std::filesystem::path absolutePath = std::filesystem::absolute(options.slangSourcePath);
                std::string filePath = absolutePath.string();
                SlangBlobPtr diagnosticsBlob;
                sourceModule = resultSession->loadModuleFromSourceString(
                    options.slangSourcePath.filename().string().c_str(),
                    filePath.c_str(),
                    sourceCode.c_str(),
                    diagnosticsBlob.writeRef());

                if (!sourceModule)
                {
                    compilationLog = "Failed to load Slang module from source";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        compilationLog += "\n" + diagString;
                    }
                    resultSession->release();
                    slangGlobalSession->release();
                    return false;
                }

                // Create entry point
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                SlangResult entryPointResult = sourceModule->findEntryPointByName(options.entryPointName.c_str(), entryPoint.writeRef());
                if (SLANG_FAILED(entryPointResult) || !entryPoint)
                {
                    compilationLog = std::format("Entry point '{}' not found in Slang module", options.entryPointName);
                    resultSession->release();
                    slangGlobalSession->release();
                    return false;
                }

                // Create component types and compile
                std::vector<slang::IComponentType*> componentTypes = { sourceModule, entryPoint };
                // add any additional loaded modules
                for (const auto& mod : loadedModules)
                {
                    componentTypes.push_back(mod);
                }

                Slang::ComPtr<slang::IComponentType> composedProgram;
                SlangResult composeResult = resultSession->createCompositeComponentType(
                    componentTypes.data(),
                    componentTypes.size(),
                    composedProgram.writeRef(),
                    diagnosticsBlob.writeRef());

                if (SLANG_FAILED(composeResult) || !composedProgram)
                {
                    compilationLog = "Failed to create composed Slang program";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        compilationLog += "\n" + diagString;
                    }
                    resultSession->release();
                    slangGlobalSession->release();
                    return false;
                }

                Slang::ComPtr<slang::IComponentType> linkedProgram;
                SlangResult linkResult = composedProgram->link(
                    linkedProgram.writeRef(),
                    diagnosticsBlob.writeRef());
                
                if (SLANG_FAILED(linkResult) || !linkedProgram)
                {
                    compilationLog = "Failed to link Slang program";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        compilationLog += "\n" + diagString;
                    }
                    resultSession->release();
                    slangGlobalSession->release();
                    return false;
                }

                // Get bytecode now
                Slang::ComPtr<slang::IBlob> codeBlob;
                SlangResult getCodeResult = linkedProgram->getEntryPointCode(0, 0, codeBlob.writeRef(), diagnosticsBlob.writeRef());
                if (SLANG_FAILED(getCodeResult) || !codeBlob)
                {
                    compilationLog = "Failed to get Slang entry point bytecode";
                    if (diagnosticsBlob != nullptr)
                    {
                        const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                        win32_OutputDebugString(diagStr);
#endif
                        std::string diagString = std::format("Slang Diagnostics: {}\n", diagStr);
                        compilationLog += "\n" + diagString;
                    }
                    linkedProgram->release();
                    resultSession->release();
                    slangGlobalSession->release();
                    return false;
                }

                size_t codeSize = codeBlob->getBufferSize();
                if (codeSize % 4 != 0)
                {
                    compilationLog = "Slang bytecode size is not a multiple of 4";
                    linkedProgram->release();
                    resultSession->release();
                    slangGlobalSession->release();
                    return false;
                }
                else
                {
                    bytecode.resize(codeSize);
                    std::memcpy(bytecode.data(), codeBlob->getBufferPointer(), codeSize);
                }

                // Extract reflection information
                ExtractReflectionData(linkedProgram);

                // Store compilation info
                entryPointName = options.entryPointName;
                sourcePath = options.slangSourcePath;
                stage = options.stage;

                // Store diagnostics even on success (warnings, etc.)
                if (diagnosticsBlob)
                {
                    compilationLog = std::string(static_cast<const char*>(diagnosticsBlob->getBufferPointer()), 
                                               diagnosticsBlob->getBufferSize());
                    diagnosticsBlob->release();
                }

                // Cleanup
                linkedProgram->release();
                resultSession->release();
                slangGlobalSession->release();

                return true;
            }
            catch (const std::exception& e)
            {
                compilationLog = std::format("Exception during Slang compilation: {}", e.what());
                return false;
            }
        }

        void ExtractReflectionData(slang::IComponentType* program)
        {
            // Get reflection layout
            slang::ProgramLayout* layout = program->getLayout();
            if (!layout)
            {
                return;
            }

            // Extract specialization constants
            // Note: This is a simplified extraction - real implementation would need
            // to traverse the reflection layout more thoroughly
            specializationConstants.clear();
            
            // Extract push constant ranges
            pushConstantRanges.clear();
            
            // Get parameter count
            auto parameterCount = layout->getParameterCount();
            for (unsigned int i = 0; i < parameterCount; ++i)
            {
                auto param = layout->getParameterByIndex(i);
                if (!param) continue;

                auto category = param->getCategory();
                if (category == slang::ParameterCategory::PushConstantBuffer)
                {
                    PushConstantRange range;
                    range.stageFlags = stage;
                    range.Offset = 0; // Would need to get actual offset from reflection
                    pushConstantRanges.push_back(range);
                }
            }
        }

        bool CreateVulkanShaderObject()
        {
            if (bytecode.empty())
            {
                compilationLog = "No bytecode available for Vulkan shader object creation";
                return false;
            }

            VkShaderCreateInfoEXT createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.stage = ConvertStageToVulkan(stage);
            createInfo.nextStage = 0; // Will be set dynamically
            createInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
            createInfo.codeSize = bytecode.size();
            createInfo.pCode = bytecode.data();
            createInfo.pName = entryPointName.c_str();
            createInfo.setLayoutCount = 0; // Descriptor set layouts - not needed for minimal implementation
            createInfo.pSetLayouts = nullptr;
            createInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
            createInfo.pPushConstantRanges = reinterpret_cast<const VkPushConstantRange*>(pushConstantRanges.data());

            VkResult result = vkCreateShadersEXT(
                parentDevice->Handle().As<VkDevice>(),
                1,
                &createInfo,
                nullptr,
                &vkShaderObject
            );

            if (result != VK_SUCCESS)
            {
                compilationLog = std::format("Failed to create Vulkan shader object: VkResult = {}", static_cast<int>(result));
                return false;
            }

            isValid = true;
            return true;
        }
    };

#if defined(RHI_SYSTEM_USE_DX12)
    // DX12 implementation would go here
    struct ShaderObjectImpl
    {
        ShaderObjectImpl(const Device* device) :
            parentDevice{ device }
        {
            // DX12 shader object implementation
            throw std::runtime_error("DX12 ShaderObject implementation not yet available");
        }

        const Device* parentDevice;
        // DX12 shader object members would go here
    };
#endif

    // ShaderObject implementation
    ShaderObject::ShaderObject() :
        impl{},
        handle{}
    {
    }

    ShaderObject::ShaderObject(std::unique_ptr<ShaderObjectImpl> impl) :
        impl{ std::move(impl) },
        handle{}
    {
        if (this->impl)
        {
            const uint64_t implHandle = reinterpret_cast<uint64_t>(this->impl.get());
            handle.Set(implHandle);
        }
    }

    ShaderObject::~ShaderObject() = default;

    ShaderObject::ShaderObject(ShaderObject&& other) noexcept :
        impl{ std::move(other.impl) },
        handle{ std::move(other.handle) }
    {
        other.handle = {};
    }

    ShaderObject& ShaderObject::operator=(ShaderObject&& other) noexcept
    {
        if (this != &other)
        {
            impl = std::move(other.impl);
            handle = std::move(other.handle);
            other.handle = {};
        }
        return *this;
    }

    Result ShaderObject::Create(const Device& device, const CompileOptions& options, ShaderObject& outShaderObject)
    {
        try
        {
            auto impl = std::make_unique<ShaderObjectImpl>(&device);
            
            // Compile shader from Slang source
            if (!impl->CompileShaderFromSlang(options))
            {
                return Result(Result::Code::InitializationFailed);
            }

            // Create platform-specific shader object
#ifdef RHI_SYSTEM_USE_VULKAN
            if (!impl->CreateVulkanShaderObject())
            {
                return Result(Result::Code::InitializationFailed);
            }
#elif defined(RHI_SYSTEM_USE_DX12)
            // DX12 shader object creation would go here
            return Result(Result::Code::FeatureNotPresent);
#endif

            outShaderObject = ShaderObject(std::move(impl));
            return Result(Result::Code::Success);
        }
        catch (...)
        {
            return Result(Result::Code::InitializationFailed);
        }
    }

    ShaderObjectHandle ShaderObject::Handle() const noexcept
    {
        return handle;
    }

    ShaderStageFlags ShaderObject::GetStage() const noexcept
    {
        return impl ? impl->stage : ShaderStageFlags::None;
    }

    std::span<const uint8_t> ShaderObject::GetBytecode() const noexcept
    {
        return impl ? std::span<const uint8_t>(impl->bytecode) : std::span<const uint8_t>{};
    }

    size_t ShaderObject::GetBytecodeSize() const noexcept
    {
        return impl ? impl->bytecode.size() : 0;
    }

    const std::vector<ShaderObject::ReflectedSpecializationConstant>& ShaderObject::GetSpecializationConstants() const noexcept
    {
        static const std::vector<ReflectedSpecializationConstant> empty;
        return impl ? impl->specializationConstants : empty;
    }

    const std::vector<PushConstantRange>& ShaderObject::GetPushConstantRanges() const noexcept
    {
        static const std::vector<PushConstantRange> empty;
        return impl ? impl->pushConstantRanges : empty;
    }

    bool ShaderObject::IsValid() const noexcept
    {
        return impl && impl->isValid;
    }

    std::string_view ShaderObject::GetCompilationLog() const noexcept
    {
        return impl ? std::string_view(impl->compilationLog) : std::string_view{};
    }

    std::string_view ShaderObject::GetEntryPointName() const noexcept
    {
        return impl ? std::string_view(impl->entryPointName) : std::string_view{};
    }

    const std::filesystem::path& ShaderObject::GetSourcePath() const noexcept
    {
        static const std::filesystem::path empty;
        return impl ? impl->sourcePath : empty;
    }

} // namespace rhi