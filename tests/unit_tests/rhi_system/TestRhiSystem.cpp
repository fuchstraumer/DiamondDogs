#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Instance.hpp"
#include "Device.hpp"
#include <filesystem>
#include <fstream>
#include <span>

class RhiSystemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create a temporary directory for shader cache
        tempShaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "ShaderCache";
        std::filesystem::create_directories(tempShaderCacheDir);
    }
    
    void TearDown() override
    {
        // Cleanup temporary directory
        if (std::filesystem::exists(tempShaderCacheDir))
        {
            std::filesystem::remove_all(tempShaderCacheDir);
        }
    }

    rhi::RhiSystemCreateInfo GetBaseCreateInfo()
    {
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "RhiSystemTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
        createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
        createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
        createInfo.ShaderCacheDir = tempShaderCacheDir.string();
        return createInfo;
    }

    std::filesystem::path tempShaderCacheDir;
};

// Test basic RHI system construction with CreateInfo
TEST_F(RhiSystemTest, BasicCreateInfoConstruction)
{
    auto createInfo = GetBaseCreateInfo();
    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
    });
}

// Test RHI system with minimal configuration
TEST_F(RhiSystemTest, MinimalConfiguration)
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ValidationLevel = rhi::ValidationLayers::None;
    createInfo.ShaderCacheDir = tempShaderCacheDir.string();

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test Vulkan 1.0 API version, to see if we can still create an instance of this version
TEST_F(RhiSystemTest, Vulkan10ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan10;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Vulkan 1.1 API version (another key version for use)
TEST_F(RhiSystemTest, Vulkan11ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan11;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Latest API version
TEST_F(RhiSystemTest, LatestApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Latest;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Verify that we can create an instance with different validation layers
TEST_F(RhiSystemTest, ValidationLayers)
{
    std::vector<rhi::ValidationLayers> validationLevels
    {
        rhi::ValidationLayers::None,
        rhi::ValidationLayers::BaseOnly,
        rhi::ValidationLayers::WithSynchronization,
        rhi::ValidationLayers::Full
    };

    for (const auto& level : validationLevels)
    {
        auto createInfo = GetBaseCreateInfo();
        createInfo.ValidationLevel = level;

        EXPECT_NO_THROW({
            auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
            EXPECT_NE(rhiSystem, nullptr);
            EXPECT_NE(rhiSystem->GetInstance(), nullptr);
            EXPECT_EQ(rhiSystem->GetInstance()->GetValidationLevel(), level);
        }) << "Failed for validation level: " << static_cast<int>(level);
    }
}


// Test RHI system with various device extensions
TEST_F(RhiSystemTest, WithDeviceExtensions)
{
    static const std::vector<std::string> deviceExtensionNames
    {
        "VK_KHR_dedicated_allocation",
        "VK_KHR_get_memory_requirements2",
    };

    auto createInfo = GetBaseCreateInfo();
    createInfo.RequestedDeviceExtensions = deviceExtensionNames;
    createInfo.ShaderCacheDir = tempShaderCacheDir.string();

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension("VK_EXT_debug_utils"));
        for (const auto& ext : deviceExtensionNames)
        {
            EXPECT_TRUE(rhiSystem->GetDevice()->HasExtension(ext)) << "Missing extension: " << ext;
        }
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test object naming functionality
TEST_F(RhiSystemTest, ObjectNaming)
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.ShaderCacheDir = tempShaderCacheDir.string();

    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
    EXPECT_NE(rhiSystem, nullptr);

    // Test object naming (should not throw)
    VkResult result = rhi::RhiSystem::SetObjectName(
        VK_OBJECT_TYPE_DEVICE, 
        reinterpret_cast<uint64_t>(rhiSystem->GetDevice()->Handle().As<VkDevice>()), 
        "TestDevice");
    
    // In debug builds with validation enabled, this should succeed
    // In release builds or without validation, it returns VK_SUCCESS
    EXPECT_TRUE(result == VK_SUCCESS || result == VK_ERROR_FEATURE_NOT_PRESENT);
}