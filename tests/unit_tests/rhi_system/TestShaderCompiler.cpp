#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Instance.hpp"
#include "Device.hpp"
#include "RhiSystem.hpp"
#include "ShaderTypes.hpp"
#include "ShaderCompiler.hpp"
#include "ShaderCompilerReply.hpp"
#include "SourcePaths.hpp"

class ShaderCompilerTest : public ::testing::Test
{
public:

    rhi::ShaderCompiler compiler;
    std::unique_ptr<rhi::RhiSystem> rhiSystem;
    

    static rhi::RhiSystemCreateInfo GetDefaultCreateInfo()
    {
        // Setup RHI system first
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "ShaderCompilerTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
        createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils", "VK_KHR_surface" };
        createInfo.RequestedDeviceExtensions = { "VK_EXT_shader_object" };
        const auto shaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "ShaderCache";
        createInfo.ShaderCacheDir = shaderCacheDir.string();
        return createInfo;
    }

    void SetUp() override
    {
        auto createInfo = GetDefaultCreateInfo();
        rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        ASSERT_NE(rhiSystem, nullptr);
        auto device = rhiSystem->GetDevice();
        ASSERT_NE(device, nullptr);
        auto result = compiler.Initialize(device->Handle());
        ASSERT_EQ(result, rhi::Result::Success());
    }

    void TearDown() override
    {
        compiler.Shutdown();
        rhiSystem.reset();
    }

};

namespace fs = std::filesystem;
static const fs::path ShadersPath = fs::path(DiamondDogs::ASSETS_DIR) / "shaders";
static const fs::path VtfPath = ShadersPath / "VolumeTiledForwardShading";
static const fs::path CommonShadersPath = ShadersPath / "common";

const fs::path SearchPaths[]
{
    ShadersPath,
    VtfPath,
    CommonShadersPath
};

// Single entrypoint compiliation test
TEST_F(ShaderCompilerTest, SimplestPossibleCompile)
{
    using namespace rhi;

    // Prepare valid Slang shader file path using source tree path
    std::filesystem::path slangShaderPath = DiamondDogs::GetAssetPath("shaders/Triangle.slang");
    ASSERT_TRUE(std::filesystem::exists(slangShaderPath)) << "Slang shader file does not exist: " << slangShaderPath;

    // Setup compile options
    rhi::ShaderCompiler::ModuleCompileOptions VertexCompileOptions{};
    VertexCompileOptions.SlangSourcePath = slangShaderPath;
    VertexCompileOptions.SearchPaths = std::span<const fs::path>(SearchPaths, std::size(SearchPaths));
    VertexCompileOptions.Target = TargetShaderIR::SPIRV;
    auto reply = compiler.CompileModule(VertexCompileOptions);
    ASSERT_NE(reply, nullptr);
    ShaderModuleCompileReply::Status compileStatus = reply->GetStatus();
    ASSERT_EQ(compileStatus, ShaderModuleCompileReply::Status::Complete) << "Shader compilation did not complete successfully";
    std::string compiliationLog = reply->GetCompilationLog();
    ASSERT_TRUE(compiliationLog.empty()) << "Shader compilation log is not empty: " << compiliationLog;
}

// Test compiling the common shader code modules, which doesn't contain entrypoints but verifies include/implementing directives work
TEST_F(ShaderCompilerTest, CompileCommonShaders)
{
    using namespace rhi;
    // Prepare valid Slang shader file path using source tree path
    std::filesystem::path slangShaderPath = DiamondDogs::GetAssetPath("shaders/VolumeTiledForwardShading/VolumeTiledForwardShading.slang");
    ASSERT_TRUE(std::filesystem::exists(slangShaderPath)) << "Slang shader file does not exist: " << slangShaderPath;
    // Setup compile options
    rhi::ShaderCompiler::ModuleCompileOptions VtfCompileOptions{};
    VtfCompileOptions.SlangSourcePath = slangShaderPath;
    VtfCompileOptions.ModuleName = "ddCommon";
    VtfCompileOptions.SearchPaths = std::span<const fs::path>(SearchPaths, std::size(SearchPaths));
    VtfCompileOptions.EnableDebugInfo = true;
    VtfCompileOptions.EnableOptimizations = false;
    VtfCompileOptions.EnableValidation = true;
    VtfCompileOptions.CompileAllEntryPoints = false; // No entrypoints in common module
    VtfCompileOptions.Target = TargetShaderIR::SPIRV;
    auto reply = compiler.CompileModule(VtfCompileOptions);
    ASSERT_NE(reply, nullptr);
    ShaderModuleCompileReply::Status compileStatus = reply->GetStatus();
    std::string compiliationLog = reply->GetCompilationLog();
    const std::string expectedLog = "No entry points found in Slang module";
    ASSERT_TRUE(compiliationLog.find(expectedLog) != std::string::npos) << "Shader compilation log does not contain expected message: " << compiliationLog;
}

// Test compiling VTF, which is a complex multiple-entrypoint shader codebase with dependencies and lots of resources
TEST_F(ShaderCompilerTest, CompileVtf)
{
    using namespace rhi;
    // Prepare valid Slang shader file path using source tree path
    std::filesystem::path slangShaderPath = DiamondDogs::GetAssetPath("shaders/VolumeTiledForwardShading/VolumeTiledForwardShading.slang");
    ASSERT_TRUE(std::filesystem::exists(slangShaderPath)) << "Slang shader file does not exist: " << slangShaderPath;
    // Setup compile options
    rhi::ShaderCompiler::ModuleCompileOptions VtfCompileOptions{};
    VtfCompileOptions.SlangSourcePath = slangShaderPath;
    VtfCompileOptions.ModuleName = "VolumeTiledForwardShading";
    VtfCompileOptions.SearchPaths = std::span<const fs::path>(SearchPaths, std::size(SearchPaths));
    VtfCompileOptions.EnableDebugInfo = true;
    VtfCompileOptions.EnableOptimizations = false;
    VtfCompileOptions.EnableValidation = true;
    VtfCompileOptions.CompileAllEntryPoints = true;
    VtfCompileOptions.Target = TargetShaderIR::SPIRV;
    auto reply = compiler.CompileModule(VtfCompileOptions);
    ASSERT_NE(reply, nullptr);
    ShaderModuleCompileReply::Status compileStatus = reply->GetStatus();
    ASSERT_EQ(compileStatus, ShaderModuleCompileReply::Status::Complete) << "Shader compilation did not complete successfully";  
    auto entryPoints = reply->GetEntryPointNames();
    for (const auto& entryPoint : entryPoints)
    {
        auto shader = reply->GetShader(entryPoint);
        std::cerr << "Entry point: " << entryPoint << " IsValid: " << shader.IsValid << " Error: " << shader.ErrorMessage << std::endl;
        ASSERT_TRUE(shader.IsValid) << "Entry point compilation failed: " << entryPoint << " Error: " << shader.ErrorMessage;
    }
    ASSERT_TRUE(entryPoints.size() > 0) << "No entry points found in compiled module";
}
