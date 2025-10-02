#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Instance.hpp"
#include "Device.hpp"
#include "ShaderObject.hpp"
#include <filesystem>

class ShaderObjectTest : public ::testing::Test
{
    void SetUp() override
    {

    }

    void TearDown() override
    {

    }

};


TEST_F(ShaderObjectTest, CreateFromValidSlangFile)
{
    // Setup RHI system first
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "ShaderObjectTest";
    createInfo.EngineName = "DiamondDogsTestEngine";
    createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
    createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
    createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
    createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    const auto shaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "ShaderCache";
    createInfo.ShaderCacheDir = shaderCacheDir.string();

    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
    ASSERT_NE(rhiSystem, nullptr);
    auto device = rhiSystem->GetDevice();
    ASSERT_NE(device, nullptr);

    // Prepare valid Slang shader file path (adjust path as needed)
    std::filesystem::path slangShaderPath = std::filesystem::current_path() / "shaders" / "Triangle.slang";
    ASSERT_TRUE(std::filesystem::exists(slangShaderPath)) << "Slang shader file does not exist: " << slangShaderPath;

    // Setup compile options
    rhi::ShaderObject::CompileOptions compileOptions{};
    compileOptions.slangSourcePath = slangShaderPath;
    compileOptions.entryPointName = "main";
    compileOptions.stage = rhi::ShaderStageFlags::Vertex;
    compileOptions.target = "spirv";
    compileOptions.enableDebugInfo = true;
    compileOptions.enableOptimizations = false;
    compileOptions.enableValidation = true;

    // Create shader object
    rhi::ShaderObject shaderObject;
    auto result = rhi::ShaderObject::Create(device->Handle(), compileOptions, shaderObject);
    EXPECT_EQ(result.GetCode(), rhi::Result::Code::Success);
    EXPECT_TRUE(shaderObject.IsValid());
    EXPECT_EQ(shaderObject.GetStage(), rhi::ShaderStageFlags::Vertex);
    EXPECT_EQ(shaderObject.GetEntryPointName(), "main");
    EXPECT_FALSE(shaderObject.GetBytecode().empty());
    EXPECT_GT(shaderObject.GetBytecodeSize(), 0);
    EXPECT_FALSE(shaderObject.GetCompilationLog().empty()); // Should contain info/warnings if any
}