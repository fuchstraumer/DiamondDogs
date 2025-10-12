#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Instance.hpp"
#include "Device.hpp"
#include "RhiSystem.hpp"
#include "ShaderCompiler.hpp"
#include "SourcePaths.hpp"
#include <filesystem>

class ShaderCompilerTest : public ::testing::Test
{
public:

    static rhi::RhiSystemCreateInfo GetDefaultCreateInfo()
    {
        // Setup RHI system first
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "ShaderObjectTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
        createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
        createInfo.RequestedDeviceExtensions = { "VK_EXT_shader_object" };
        const auto shaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "ShaderCache";
        createInfo.ShaderCacheDir = shaderCacheDir.string();
        return createInfo;
    }

    void SetUp() override
    {

    }

    void TearDown() override
    {

    }

};

const char* SearchPaths[]
{
    
};

// Single entrypoint compiliation test
TEST_F(ShaderCompilerTest, SimplestPossibleCompile)
{
    
    using namespace rhi;
    auto createInfo = ShaderCompilerTest::GetDefaultCreateInfo();
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
    ASSERT_NE(rhiSystem, nullptr);
    auto device = rhiSystem->GetDevice();
    ASSERT_NE(device, nullptr);

    // Prepare valid Slang shader file path using source tree path
    std::filesystem::path slangShaderPath = DiamondDogs::GetAssetPath("shaders/Triangle.slang");
    ASSERT_TRUE(std::filesystem::exists(slangShaderPath)) << "Slang shader file does not exist: " << slangShaderPath;

    // Setup compile options
    rhi::ShaderCompiler::ModuleCompileOptions VertexCompileOptions{};
    VertexCompileOptions.SlangSourcePath = slangShaderPath;
    VertexCompileOptions.target = "spirv";
    VertexCompileOptions.enableDebugInfo = true;
    VertexCompileOptions.enableOptimizations = false;
    VertexCompileOptions.enableValidation = true;
    rhi::ShaderObject::CompileOptions FragmentCompileOptions = VertexCompileOptions;
    FragmentCompileOptions.EntryPointName = "fragmentMain";

    // Create shader object
    rhi::ShaderObject VertexShader;
    auto result = rhi::ShaderObject::Create(device->Handle(), VertexCompileOptions, VertexShader);
    rhi::ShaderObject FragmentShader;
    result = rhi::ShaderObject::Create(device->Handle(), FragmentCompileOptions, FragmentShader);

    EXPECT_EQ(result.GetCode(), rhi::Result::Code::Success);
    EXPECT_TRUE(VertexShader.IsValid());
    EXPECT_EQ(VertexShader.GetStage(), rhi::ShaderStageFlags::Vertex);
    EXPECT_EQ(VertexShader.GetEntryPointName(), "vertexMain");
    EXPECT_FALSE(VertexShader.GetBytecode().empty());
    EXPECT_GT(VertexShader.GetBytecodeSize(), 0);
    EXPECT_FALSE(VertexShader.GetCompilationLog().empty()); // Should contain info/warnings if any
}