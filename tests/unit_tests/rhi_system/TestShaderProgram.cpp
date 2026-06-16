#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Instance.hpp"
#include "Device.hpp"
#include "ShaderBlob.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include "SourcePaths.hpp"
#include <filesystem>

constexpr static const uint32_t triangle_vert_shader_spv[349] =
{
	0x07230203,0x00010000,0x00080007,0x0000002c,0x00000000,0x00020011,0x00000001,0x0006000b,
	0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
	0x0009000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000a,0x0000001e,0x00000029,
	0x0000002a,0x00030003,0x00000002,0x000001c2,0x00090004,0x415f4c47,0x735f4252,0x72617065,
	0x5f657461,0x64616873,0x6f5f7265,0x63656a62,0x00007374,0x00040005,0x00000004,0x6e69616d,
	0x00000000,0x00060005,0x00000008,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,
	0x00000008,0x00000000,0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000000a,0x00000000,
	0x00040005,0x0000000e,0x62755f5f,0x005f5f6f,0x00050006,0x0000000e,0x00000000,0x65646f6d,
	0x0000006c,0x00050006,0x0000000e,0x00000001,0x77656976,0x00000000,0x00060006,0x0000000e,
	0x00000002,0x6a6f7270,0x69746365,0x00006e6f,0x00030005,0x00000010,0x006f6275,0x00050005,
	0x0000001e,0x69736f70,0x6e6f6974,0x00000000,0x00040005,0x00000029,0x6c6f4376,0x0000726f,
	0x00040005,0x0000002a,0x6f6c6f63,0x00000072,0x00050048,0x00000008,0x00000000,0x0000000b,
	0x00000000,0x00030047,0x00000008,0x00000002,0x00040048,0x0000000e,0x00000000,0x00000005,
	0x00050048,0x0000000e,0x00000000,0x00000023,0x00000000,0x00050048,0x0000000e,0x00000000,
	0x00000007,0x00000010,0x00040048,0x0000000e,0x00000001,0x00000005,0x00050048,0x0000000e,
	0x00000001,0x00000023,0x00000040,0x00050048,0x0000000e,0x00000001,0x00000007,0x00000010,
	0x00040048,0x0000000e,0x00000002,0x00000005,0x00050048,0x0000000e,0x00000002,0x00000023,
	0x00000080,0x00050048,0x0000000e,0x00000002,0x00000007,0x00000010,0x00030047,0x0000000e,
	0x00000002,0x00040047,0x00000010,0x00000022,0x00000000,0x00040047,0x00000010,0x00000021,
	0x00000000,0x00040047,0x0000001e,0x0000001e,0x00000000,0x00040047,0x00000029,0x0000001e,
	0x00000000,0x00040047,0x0000002a,0x0000001e,0x00000001,0x00020013,0x00000002,0x00030021,
	0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,
	0x00000004,0x0003001e,0x00000008,0x00000007,0x00040020,0x00000009,0x00000003,0x00000008,
	0x0004003b,0x00000009,0x0000000a,0x00000003,0x00040015,0x0000000b,0x00000020,0x00000001,
	0x0004002b,0x0000000b,0x0000000c,0x00000000,0x00040018,0x0000000d,0x00000007,0x00000004,
	0x0005001e,0x0000000e,0x0000000d,0x0000000d,0x0000000d,0x00040020,0x0000000f,0x00000002,
	0x0000000e,0x0004003b,0x0000000f,0x00000010,0x00000002,0x0004002b,0x0000000b,0x00000011,
	0x00000002,0x00040020,0x00000012,0x00000002,0x0000000d,0x0004002b,0x0000000b,0x00000015,
	0x00000001,0x00040017,0x0000001c,0x00000006,0x00000003,0x00040020,0x0000001d,0x00000001,
	0x0000001c,0x0004003b,0x0000001d,0x0000001e,0x00000001,0x0004002b,0x00000006,0x00000020,
	0x3f800000,0x00040020,0x00000026,0x00000003,0x00000007,0x00040020,0x00000028,0x00000003,
	0x0000001c,0x0004003b,0x00000028,0x00000029,0x00000003,0x0004003b,0x0000001d,0x0000002a,
	0x00000001,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,
	0x00050041,0x00000012,0x00000013,0x00000010,0x00000011,0x0004003d,0x0000000d,0x00000014,
	0x00000013,0x00050041,0x00000012,0x00000016,0x00000010,0x00000015,0x0004003d,0x0000000d,
	0x00000017,0x00000016,0x00050092,0x0000000d,0x00000018,0x00000014,0x00000017,0x00050041,
	0x00000012,0x00000019,0x00000010,0x0000000c,0x0004003d,0x0000000d,0x0000001a,0x00000019,
	0x00050092,0x0000000d,0x0000001b,0x00000018,0x0000001a,0x0004003d,0x0000001c,0x0000001f,
	0x0000001e,0x00050051,0x00000006,0x00000021,0x0000001f,0x00000000,0x00050051,0x00000006,
	0x00000022,0x0000001f,0x00000001,0x00050051,0x00000006,0x00000023,0x0000001f,0x00000002,
	0x00070050,0x00000007,0x00000024,0x00000021,0x00000022,0x00000023,0x00000020,0x00050091,
	0x00000007,0x00000025,0x0000001b,0x00000024,0x00050041,0x00000026,0x00000027,0x0000000a,
	0x0000000c,0x0003003e,0x00000027,0x00000025,0x0004003d,0x0000001c,0x0000002b,0x0000002a,
	0x0003003e,0x00000029,0x0000002b,0x000100fd,0x00010038
};


constexpr static VkDescriptorSetLayoutBinding TestSetLayoutBinding
{
    0,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    1,
    VK_SHADER_STAGE_VERTEX_BIT,
    nullptr
};

constexpr static VkDescriptorSetLayoutCreateInfo TestSetLayoutCreateInfo
{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr,
    0,
    1,
    &TestSetLayoutBinding
};

class ShaderProgramTest : public ::testing::Test
{
public:
    // need test set layout so we can create the dummy shader we use
    std::unique_ptr<rhi::RhiSystem> rhiSystem;
    rhi::DeviceHandle testDevice;
    VkDescriptorSetLayout testSetLayout;


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
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils", "VK_KHR_surface" };
        createInfo.RequestedDeviceExtensions = { "VK_EXT_shader_object" };
        const auto shaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsUnitTesting" / "ShaderCache";
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
        testDevice = device->Handle();
        ASSERT_TRUE(testDevice.IsValid());
        rhi::Result result = vkCreateDescriptorSetLayout(testDevice.As<VkDevice>(), &TestSetLayoutCreateInfo, nullptr, &testSetLayout);
        ASSERT_EQ(result.GetCode(), rhi::Result::Code::Success);
        ASSERT_NE(testSetLayout, VK_NULL_HANDLE);
    }

    void TearDown() override
    {
        if (testSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(testDevice.As<VkDevice>(), testSetLayout, nullptr);
            testSetLayout = VK_NULL_HANDLE;
        }

        rhiSystem.reset();
    }

};

TEST_F(ShaderProgramTest, CreateFromBakedBinary)
{
    // Prepare shader binary options
    rhi::DescriptorSetLayoutHandle testSetLayoutHandle(reinterpret_cast<uint64_t>(testSetLayout));
    rhi::ShaderBinaryOptions testBinaryOptions
    {
        std::span<const uint32_t>(triangle_vert_shader_spv, 349),
        rhi::ShaderStageFlags::Vertex,
        "main",
        std::span<rhi::PushConstantRange>{},
        std::span<rhi::DescriptorSetLayoutHandle>{ &testSetLayoutHandle, 1 }
    };

    std::span<rhi::ShaderBinaryOptions> binaryOptionsSpan(&testBinaryOptions, 1);
    std::unique_ptr<rhi::ShaderProgram> shaderProgram = nullptr;
    rhi::Result result = rhi::ShaderProgram::CreateFromBinary(testDevice, binaryOptionsSpan, shaderProgram);
    ASSERT_TRUE(result.IsSuccess());
    ASSERT_NE(shaderProgram, nullptr);

    ASSERT_TRUE(shaderProgram->IsSingleStage());
    rhi::ShaderStageFlags expectedStages = rhi::ShaderStageFlags::Vertex;
    ASSERT_EQ(shaderProgram->GetStages(), expectedStages);
    std::span<std::string> entryPoints = shaderProgram->GetEntryPointNames();
    ASSERT_EQ(entryPoints.size(), 1);
    ASSERT_EQ(entryPoints[0], "main");

}
