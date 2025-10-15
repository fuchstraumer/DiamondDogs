#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include "ShaderCompilerMessages.hpp"
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
#include <vector>
#include <unordered_map>
#include <mutex>
#include <algorithm>

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
    // Compile-time toggle for YAML reflection output (debug only)
    #if defined(_DEBUG) || !defined(NDEBUG)
    constexpr static bool SHADER_COMPILER_ENABLE_YAML_REFLECTION = true;
    #else
    constexpr static bool SHADER_COMPILER_ENABLE_YAML_REFLECTION = false;
    #endif

    using SlangModulePtr = Slang::ComPtr<slang::IModule>;
    using SlangBlobPtr = Slang::ComPtr<slang::IBlob>;
    using SlangProgramPtr = Slang::ComPtr<slang::IComponentType>;
    using SlangLayoutPtr = slang::ProgramLayout*;
    using SlangMetadataPtr = Slang::ComPtr<slang::IMetadata>;

    // Helper to convert Slang stage to our stage flags
    static ShaderStageFlags FromSlangStage(SlangStage stage)
    {
        switch (stage)
        {
            case SLANG_STAGE_VERTEX: return ShaderStageFlags::Vertex;
            case SLANG_STAGE_HULL: return ShaderStageFlags::TesselationControl;
            case SLANG_STAGE_DOMAIN: return ShaderStageFlags::TesselationEvaluation;
            case SLANG_STAGE_GEOMETRY: return ShaderStageFlags::Geometry;
            case SLANG_STAGE_FRAGMENT: return ShaderStageFlags::Fragment;
            case SLANG_STAGE_COMPUTE: return ShaderStageFlags::Compute;
            case SLANG_STAGE_RAY_GENERATION: return ShaderStageFlags::RayGeneration;
            case SLANG_STAGE_ANY_HIT: return ShaderStageFlags::AnyHit;
            case SLANG_STAGE_CLOSEST_HIT: return ShaderStageFlags::ClosestHit;
            case SLANG_STAGE_MISS: return ShaderStageFlags::Miss;
            case SLANG_STAGE_INTERSECTION: return ShaderStageFlags::Intersection;
            case SLANG_STAGE_CALLABLE: return ShaderStageFlags::Callable;
            case SLANG_STAGE_MESH: return ShaderStageFlags::Mesh;
            case SLANG_STAGE_AMPLIFICATION: return ShaderStageFlags::Task;
            default: return ShaderStageFlags::None;
        }
    }

    // ShaderIdentifier implementation
    bool ShaderCompiler::ShaderIdentifier::operator==(const ShaderIdentifier& other) const noexcept
    {
        return ModuleName == other.ModuleName &&
               EntryPointName == other.EntryPointName &&
               Stage == other.Stage;
    }

    size_t ShaderCompiler::ShaderIdentifier::Hash() const noexcept
    {
        size_t h1 = std::hash<std::string>{}(ModuleName);
        size_t h2 = std::hash<std::string>{}(EntryPointName);
        size_t h3 = std::hash<uint32_t>{}(static_cast<uint32_t>(Stage));
        
        // Combine hashes
        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        h1 ^= h3 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        return h1;
    }

    // Implementation details
    struct ShaderCompilerImpl
    {
        DeviceHandle ParentDevice;
        
        // Slang global session (reused across compilations)
        Slang::ComPtr<slang::IGlobalSession> SlangGlobalSession;
        
        // Module storage: moduleName -> module data
        struct ModuleStorage
        {
            std::string ModuleName;
            std::filesystem::path SourcePath;
            SlangProgramPtr LinkedProgram;
            SlangLayoutPtr ProgramLayout;
            std::vector<SlangMetadataPtr> Metadata;
            std::string CompilationLog;
            
            // Per-entry-point storage
            struct EntryPointData
            {
                std::string Name;
                ShaderStageFlags Stage;
                std::vector<uint8_t> Bytecode;
                
                // Optional cached reflection
                std::optional<ShaderCompiler::ShaderReflection> CachedReflection;
            };
            std::unordered_map<std::string, EntryPointData> EntryPoints;
        };
        
        // Thread-safe module storage
        mutable std::mutex moduleStorageMutex;
        std::unordered_map<std::string, ModuleStorage> compiledModules;
        
        // For synchronous processing (future: replace with mwsrQueue + worker thread)
        void ProcessMessage(ShaderCompilerMessagePayload message);
        
        // Message handlers
        void ProcessCompileModuleMessage(CompileModuleMessage&& message);
        void ProcessQueryReflectionMessage(QueryReflectionMessage&& message);
        
        // Core compilation logic
        Result CompileSlangModule(
            const CompileModuleMessage::OwnedModuleCompileOptions& options,
            ModuleStorage& outStorage);
        
        // Helper: Extract entry point bytecode from linked program
        bool ExtractEntryPointBytecode(
            SlangProgramPtr linkedProgram,
            size_t entryPointIndex,
            std::vector<uint8_t>& outBytecode,
            std::string& outError);
        
        // Reflection generation (placeholder - will be implemented)
        ShaderCompiler::ShaderReflection GenerateReflection(
            const ModuleStorage& module,
            const std::string& entryPointName,
            bool includeDescriptors,
            bool includeMemberReflection);
        
        // Helper: Create composite component type from module and entry points
        Result CreateComposite(
            slang::ISession* session,
            const std::vector<slang::IComponentType*>& componentTypes,
            Slang::ComPtr<slang::IComponentType>& outComposite,
            std::string& outDiagnostics);
        
        // Helper: Link a composite component type
        Result LinkComposite(
            Slang::ComPtr<slang::IComponentType> composite,
            SlangProgramPtr& outLinkedProgram,
            std::string& outDiagnostics);
    };

    void ShaderCompilerImpl::ProcessMessage(ShaderCompilerMessagePayload message)
    {
        std::visit([this](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, CompileModuleMessage>)
            {
                ProcessCompileModuleMessage(std::move(arg));
            }
            else if constexpr (std::is_same_v<T, QueryReflectionMessage>)
            {
                ProcessQueryReflectionMessage(std::move(arg));
            }
        }, std::move(message));
    }

    void ShaderCompilerImpl::ProcessCompileModuleMessage(CompileModuleMessage&& message)
    {
        message.Reply->SetStatus(ShaderModuleCompileReply::Status::Compiling);
        
        ModuleStorage storage;
        Result compileResult = CompileSlangModule(message.Options, storage);
        
        if (compileResult != Result::Success())
        {
            message.Reply->SetStatus(ShaderModuleCompileReply::Status::Failed);
            message.Reply->SetCompilationLog(storage.CompilationLog);
            return;
        }
        
        // Store module
        {
            std::lock_guard<std::mutex> lock{ moduleStorageMutex };
            message.Reply->SetModuleName(storage.ModuleName);
            
            // Add all compiled shaders to reply
            for (const auto& [entryPointName, entryData] : storage.EntryPoints)
            {
                ShaderCompiler::CompiledShader shader;
                shader.Identifier.ModuleName = storage.ModuleName;
                shader.Identifier.EntryPointName = entryPointName;
                shader.Identifier.Stage = entryData.Stage;
                shader.Bytecode = std::span<const uint8_t>{ entryData.Bytecode };
                shader.IsValid = !entryData.Bytecode.empty();
                
                message.Reply->AddCompiledShader(std::move(shader));
            }
            
            message.Reply->SetCompilationLog(storage.CompilationLog);
            compiledModules[storage.ModuleName] = std::move(storage);
        }
        
        message.Reply->SetStatus(ShaderModuleCompileReply::Status::Complete);
    }

    void ShaderCompilerImpl::ProcessQueryReflectionMessage(QueryReflectionMessage&& message)
    {
        message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Processing);
        
        std::lock_guard<std::mutex> lock{ moduleStorageMutex };
        
        // Find the module
        auto moduleIt = compiledModules.find(message.Identifier.ModuleName);
        if (moduleIt == compiledModules.end())
        {
            message.Reply->SetError("Module not found");
            message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Failed);
            return;
        }
        
        // Find the entry point
        auto& module = moduleIt->second;
        auto entryIt = module.EntryPoints.find(message.Identifier.EntryPointName);
        if (entryIt == module.EntryPoints.end())
        {
            message.Reply->SetError("Entry point not found in module");
            message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Failed);
            return;
        }
        
        // Check if we have cached reflection
        auto& entryData = entryIt->second;
        if (entryData.CachedReflection.has_value())
        {
            message.Reply->SetReflection(entryData.CachedReflection.value());
            message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Complete);
            return;
        }
        
        // Generate reflection on-demand (placeholder - needs implementation)
        ShaderCompiler::ShaderReflection reflection = GenerateReflection(
            module,
            message.Identifier.EntryPointName,
            true,  // includeDescriptors
            false  // includeMemberReflection
        );
        
        // Cache it
        entryData.CachedReflection = reflection;
        
        message.Reply->SetReflection(std::move(reflection));
        message.Reply->SetStatus(ShaderReflectionQueryReply::Status::Complete);
    }

    Result ShaderCompilerImpl::CompileSlangModule(
        const CompileModuleMessage::OwnedModuleCompileOptions& options,
        ModuleStorage& outStorage)
    {
        try
        {
            // Initialize Slang global session if needed
            if (!SlangGlobalSession)
            {
                SlangResult result = slang::createGlobalSession(SlangGlobalSession.writeRef());
                if (result != SLANG_OK)
                {
                    outStorage.CompilationLog = "Failed to create Slang global session";
                    return Result::Failure();
                }
            }

            // Convert search paths to C-string pointers
            std::vector<const char*> searchPathPtrs;
            searchPathPtrs.reserve(options.SearchPaths.size());
            for (const auto& path : options.SearchPaths)
            {
                searchPathPtrs.push_back(path.c_str());
            }

            // Create session descriptor
            slang::SessionDesc sessionDesc = {};
            sessionDesc.searchPaths = searchPathPtrs.data();
            sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPtrs.size());

            // Create compilation target
            slang::TargetDesc targetDesc = {};
            if (options.Target == "spirv")
            {
                targetDesc.format = SLANG_SPIRV;
                targetDesc.profile = SlangGlobalSession->findProfile("spirv_1_6");
            }
            else if (options.Target == "dxil")
            {
                targetDesc.format = SLANG_DXIL;
                targetDesc.profile = SlangGlobalSession->findProfile("sm_6_6");
            }
            else
            {
                outStorage.CompilationLog = std::format("Unsupported compilation target: {}", options.Target);
                return Result::Failure();
            }

            sessionDesc.targets = &targetDesc;
            sessionDesc.targetCount = 1;

            // Add compiler options
            std::vector<slang::CompilerOptionEntry> compilerOptions;
            
            if (options.EnableOptimizations)
            {
                slang::CompilerOptionEntry opt;
                opt.name = slang::CompilerOptionName::Optimization;
                opt.value.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
                compilerOptions.push_back(opt);
            }

            if (options.EnableDebugInfo)
            {
                slang::CompilerOptionEntry debug;
                debug.name = slang::CompilerOptionName::DebugInformation;
                debug.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
                compilerOptions.push_back(debug);
            }

            #ifdef RHI_SYSTEM_USE_VULKAN
            slang::CompilerOptionEntry vulkanOption;
            vulkanOption.name = slang::CompilerOptionName::VulkanEmitReflection;
            vulkanOption.value.intValue0 = 1;
            compilerOptions.push_back(vulkanOption);
            
            slang::CompilerOptionEntry spirvOption;
            spirvOption.name = slang::CompilerOptionName::EmitSpirvDirectly;
            spirvOption.value.intValue0 = 1;
            compilerOptions.push_back(spirvOption);
            #endif

            sessionDesc.compilerOptionEntries = compilerOptions.data();
            sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(compilerOptions.size());

            // Create session
            Slang::ComPtr<slang::ISession> resultSession;
            SlangResult result = SlangGlobalSession->createSession(sessionDesc, resultSession.writeRef());
            if (SLANG_FAILED(result) || !resultSession)
            {
                outStorage.CompilationLog = "Failed to create Slang session";
                return Result::Failure();
            }

            // Load additional modules
            std::vector<SlangModulePtr> loadedModules;
            for (const auto& moduleName : options.AdditionalModules)
            {
                SlangBlobPtr moduleLoadDiagnostics;
                SlangModulePtr module;
                module = resultSession->loadModule(moduleName.c_str(), moduleLoadDiagnostics.writeRef());
                if (!module)
                {
                    outStorage.CompilationLog = std::format("Failed to load additional Slang module: {}", moduleName);
                    if (moduleLoadDiagnostics)
                    {
                        const char* diagStr = static_cast<const char*>(moduleLoadDiagnostics->getBufferPointer());
                        #ifdef WIN32
                        win32_OutputDebugString(diagStr);
                        #endif
                        outStorage.CompilationLog += std::format("\nDiagnostics: {}", diagStr);
                    }
                    return Result::Failure();
                }
                loadedModules.push_back(module);
            }

            // Load the main module from source
            SlangModulePtr sourceModule;
            std::filesystem::path absolutePath = std::filesystem::absolute(options.SlangSourcePath);
            std::string filePath = absolutePath.string();
            SlangBlobPtr diagnosticsBlob;
            
            std::string moduleNameToUse = options.ModuleName.empty() ? 
                options.SlangSourcePath.stem().string() : 
                options.ModuleName;

            // Important: use loadModule, not loadModuleFromSourceString, to respect search paths and import declarations
            // Otherwise, this crashes since things can't be resolved correctly (especially with multiple entry points and dependencies)
            sourceModule = resultSession->loadModule(
                moduleNameToUse.c_str(),
                diagnosticsBlob.writeRef());

            if (!sourceModule)
            {
                outStorage.CompilationLog = "Failed to load Slang module from source";
                if (diagnosticsBlob)
                {
                    const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
                    #ifdef WIN32
                    win32_OutputDebugString(diagStr);
                    #endif
                    outStorage.CompilationLog += std::format("\nSlang Diagnostics: {}", diagStr);
                }
                return Result::Failure();
            }
            else if (diagnosticsBlob)
            {
                if (diagnosticsBlob)
                {
                    const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
#ifdef WIN32
                    win32_OutputDebugString(diagStr);
#endif
                    outStorage.CompilationLog += std::format("\nSlang Diagnostics: {}", diagStr);
                }
            }

            // Discover all entry points in the module
            SlangUInt entryPointCount = sourceModule->getDefinedEntryPointCount();
            if (entryPointCount == 0)
            {
                outStorage.CompilationLog = "No entry points found in Slang module";
                return Result::Failure();
            }

            std::vector<std::string> entryPointsToCompile;
            
            if (options.CompileAllEntryPoints)
            {
                // Get all entry point names
                for (SlangUInt i = 0; i < entryPointCount; ++i)
                {
                    Slang::ComPtr<slang::IEntryPoint> entryPoint;
                    SlangResult epResult = sourceModule->getDefinedEntryPoint(i, entryPoint.writeRef());
                    if (SLANG_SUCCEEDED(epResult) && entryPoint)
                    {
                        const char* epName = entryPoint->getFunctionReflection()->getName();
                        if (epName)
                        {
                            entryPointsToCompile.push_back(epName);
                        }
                    }
                }
            }
            else
            {
                // Use specific entry points
                entryPointsToCompile = options.SpecificEntryPoints;
            }

            // Set output storage fields
            outStorage.ModuleName = moduleNameToUse;
            outStorage.SourcePath = options.SlangSourcePath;

            // start with root module, then add each entry point and do a collective link and compile
            std::vector<slang::IComponentType*> componentTypes;
            std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointStorage;
            
            componentTypes.push_back(sourceModule);

            // add entry points - store ComPtrs to maintain lifetime
            for (const auto& entryPointName : entryPointsToCompile)
            {
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                SlangResult epResult = sourceModule->findEntryPointByName(
                    entryPointName.c_str(), entryPoint.writeRef());
                if (SLANG_SUCCEEDED(epResult) && entryPoint)
                {
                    entryPointStorage.push_back(entryPoint);
                    componentTypes.push_back(entryPoint.get());
                }
                else
                {
                    // Track failed entry point but continue with others (per-entry-point error handling)
                    std::string errorMsg = std::format("Entry point '{}' not found in module", entryPointName);
                    outStorage.CompilationLog += errorMsg + "\n";
                }
            }

            // Create composite component type from module + all entry points
            Slang::ComPtr<slang::IComponentType> composedProgram;
            std::string compositeDiagnostics;
            Result composeResult = CreateComposite(
                resultSession,
                componentTypes,
                composedProgram,
                compositeDiagnostics);
            
            if (composeResult != Result::Success())
            {
                outStorage.CompilationLog += std::format("Failed to create composite: {}\n", compositeDiagnostics);
                return Result::Failure();
            }

            // Link the composite to resolve all dependencies
            SlangProgramPtr linkedProgram;
            std::string linkDiagnostics;
            Result linkResult = LinkComposite(composedProgram, linkedProgram, linkDiagnostics);
            
            if (linkResult != Result::Success())
            {
                outStorage.CompilationLog += std::format("Failed to link composite: {}\n", linkDiagnostics);
                return Result::Failure();
            }

            // Store the linked program and layout for later reflection queries
            outStorage.LinkedProgram = linkedProgram;
            outStorage.ProgramLayout = linkedProgram->getLayout();

            // Now extract bytecode for each entry point from the linked program
            // Entry points start at index 0 (module is not an entry point in the composite)
            for (size_t i = 0; i < entryPointsToCompile.size(); ++i)
            {
                const std::string& entryPointName = entryPointsToCompile[i];
                
                // Extract bytecode
                std::vector<uint8_t> bytecode;
                std::string bytecodeError;
                if (!ExtractEntryPointBytecode(linkedProgram, i, bytecode, bytecodeError))
                {
                    std::string errorMsg = std::format("Failed to extract bytecode for entry point '{}': {}", 
                        entryPointName, bytecodeError);
                    outStorage.CompilationLog += errorMsg + "\n";
                    continue; // Per-entry-point error handling
                }

                // Get stage from entry point reflection
                slang::EntryPointReflection* epReflection = outStorage.ProgramLayout->getEntryPointByIndex(i);
                SlangStage slangStage = epReflection->getStage();
                ShaderStageFlags stage = FromSlangStage(slangStage);

                // Store entry point data
                ModuleStorage::EntryPointData epData;
                epData.Name = entryPointName;
                epData.Stage = stage;
                epData.Bytecode = std::move(bytecode);

                outStorage.EntryPoints[entryPointName] = std::move(epData);
            }

            // Check if we compiled at least one entry point
            if (outStorage.EntryPoints.empty())
            {
                outStorage.CompilationLog = "Failed to compile any entry points from module";
                return Result::Failure();
            }

            return Result::Success();
        }
        catch (const std::exception& e)
        {
            outStorage.CompilationLog = std::format("Exception during Slang compilation: {}", e.what());
            return Result::Failure();
        }
    }

    bool ShaderCompilerImpl::ExtractEntryPointBytecode(
        SlangProgramPtr linkedProgram,
        size_t entryPointIndex,
        std::vector<uint8_t>& outBytecode,
        std::string& outError)
    {
        Slang::ComPtr<slang::IBlob> codeBlob;
        SlangBlobPtr diagnosticsBlob;
        
        SlangResult getCodeResult = linkedProgram->getEntryPointCode(
            entryPointIndex, 
            0, // target index
            codeBlob.writeRef(), 
            diagnosticsBlob.writeRef());
        
        if (SLANG_FAILED(getCodeResult) || !codeBlob)
        {
            outError = "Failed to get entry point bytecode";
            if (diagnosticsBlob)
            {
                const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
                #ifdef WIN32
                win32_OutputDebugString(diagStr);
                #endif
                outError += std::format("\nDiagnostics: {}", diagStr);
            }
            return false;
        }

        size_t codeSize = codeBlob->getBufferSize();
        if (codeSize % 4 != 0)
        {
            outError = "Bytecode size is not a multiple of 4";
            return false;
        }

        outBytecode.resize(codeSize);
        std::memcpy(outBytecode.data(), codeBlob->getBufferPointer(), codeSize);
        return true;
    }

    ShaderCompiler::ShaderReflection ShaderCompilerImpl::GenerateReflection(
        const ModuleStorage& module,
        const std::string& entryPointName,
        bool includeDescriptors,
        bool includeMemberReflection)
    {
        // Placeholder implementation - will be expanded with actual reflection extraction
        ShaderCompiler::ShaderReflection reflection;
        
        auto entryIt = module.EntryPoints.find(entryPointName);
        if (entryIt != module.EntryPoints.end())
        {
            reflection.Identifier.ModuleName = module.ModuleName;
            reflection.Identifier.EntryPointName = entryPointName;
            reflection.Identifier.Stage = entryIt->second.Stage;
        }
        
        // TODO: Extract actual reflection data from module.ProgramLayout
        // This will be implemented in a follow-up
        
        return reflection;
    }

    Result ShaderCompilerImpl::CreateComposite(
        slang::ISession* session,
        const std::vector<slang::IComponentType*>& componentTypes,
        Slang::ComPtr<slang::IComponentType>& outComposite,
        std::string& outDiagnostics)
    {
        SlangBlobPtr diagnosticsBlob;
        
        SlangResult result = session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            outComposite.writeRef(),
            diagnosticsBlob.writeRef());
        
        if (diagnosticsBlob)
        {
            const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
            #ifdef WIN32
            win32_OutputDebugString(diagStr);
            #endif
            outDiagnostics += diagStr;
        }
        
        if (SLANG_FAILED(result) || !outComposite)
        {
            if (outDiagnostics.empty())
            {
                outDiagnostics = "Failed to create composite component type";
            }
            return Result::Failure();
        }
        
        return Result::Success();
    }

    Result ShaderCompilerImpl::LinkComposite(
        Slang::ComPtr<slang::IComponentType> composite,
        SlangProgramPtr& outLinkedProgram,
        std::string& outDiagnostics)
    {
        SlangBlobPtr diagnosticsBlob;
        
        SlangResult result = composite->link(
            outLinkedProgram.writeRef(),
            diagnosticsBlob.writeRef());
        
        if (diagnosticsBlob)
        {
            const char* diagStr = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
            #ifdef WIN32
            win32_OutputDebugString(diagStr);
            #endif
            outDiagnostics += diagStr;
        }
        
        if (SLANG_FAILED(result) || !outLinkedProgram)
        {
            if (outDiagnostics.empty())
            {
                outDiagnostics = "Failed to link composite component type";
            }
            return Result::Failure();
        }
        
        return Result::Success();
    }

    // ShaderCompiler public API implementation
    ShaderCompiler::ShaderCompiler() :
        impl{ std::make_unique<ShaderCompilerImpl>() }
    {
    }

    ShaderCompiler::~ShaderCompiler() = default;

    Result ShaderCompiler::Initialize(DeviceHandle device)
    {
        impl->ParentDevice = device;
        return Result::Success();
    }

    void ShaderCompiler::Shutdown()
    {
        // For now, just cleanup. Later this will signal worker thread to exit
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        impl->compiledModules.clear();
        
        if (impl->SlangGlobalSession)
        {
            impl->SlangGlobalSession.setNull();
        }
    }

    std::shared_ptr<ShaderModuleCompileReply> ShaderCompiler::CompileModule(const ModuleCompileOptions& options)
    {
        auto reply = std::make_shared<ShaderModuleCompileReply>();
        reply->SetStatus(ShaderModuleCompileReply::Status::Pending);
        
        CompileModuleMessage message;
        message.Options = CompileModuleMessage::OwnedModuleCompileOptions{ options };
        message.Reply = reply;
        
        // Process synchronously for now
        impl->ProcessMessage(std::move(message));
        
        return reply;
    }

    std::shared_ptr<ShaderReflectionQueryReply> ShaderCompiler::QueryReflection(const ShaderIdentifier& identifier)
    {
        auto reply = std::make_shared<ShaderReflectionQueryReply>();
        reply->SetStatus(ShaderReflectionQueryReply::Status::Pending);
        
        QueryReflectionMessage message;
        message.Identifier = identifier;
        message.Reply = reply;
        
        // Process synchronously for now
        impl->ProcessMessage(std::move(message));
        
        return reply;
    }

    ShaderCompiler::CompiledShader ShaderCompiler::GetCompiledShader(const ShaderIdentifier& identifier)
    {
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        
        auto moduleIt = impl->compiledModules.find(identifier.ModuleName);
        if (moduleIt == impl->compiledModules.end())
        {
            CompiledShader notFound;
            notFound.IsValid = false;
            notFound.ErrorMessage = "Module not found";
            return notFound;
        }
        
        auto& module = moduleIt->second;
        auto entryIt = module.EntryPoints.find(identifier.EntryPointName);
        if (entryIt == module.EntryPoints.end())
        {
            CompiledShader notFound;
            notFound.IsValid = false;
            notFound.ErrorMessage = "Entry point not found";
            return notFound;
        }
        
        auto& entryData = entryIt->second;
        CompiledShader shader;
        shader.Identifier = identifier;
        shader.Bytecode = std::span<const uint8_t>{ entryData.Bytecode };
        shader.IsValid = !entryData.Bytecode.empty();
        
        return shader;
    }

    bool ShaderCompiler::IsModuleCompiled(const std::string& moduleName) const
    {
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        return impl->compiledModules.find(moduleName) != impl->compiledModules.end();
    }

    std::vector<std::string> ShaderCompiler::GetModuleEntryPoints(const std::string& moduleName) const
    {
        std::lock_guard<std::mutex> lock{ impl->moduleStorageMutex };
        
        auto it = impl->compiledModules.find(moduleName);
        if (it == impl->compiledModules.end())
        {
            return {};
        }
        
        std::vector<std::string> entryPoints;
        entryPoints.reserve(it->second.EntryPoints.size());
        for (const auto& [name, data] : it->second.EntryPoints)
        {
            entryPoints.push_back(name);
        }
        
        return entryPoints;
    }

} // namespace rhi
