#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Device.hpp"
#include <filesystem>
#include <fstream>

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

    std::filesystem::path tempShaderCacheDir;
};

// Test basic RHI system construction with CreateInfo
TEST_F(RhiSystemTest, BasicCreateInfoConstruction)
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

// Test RHI system with presentation support
TEST_F(RhiSystemTest, WithPresentationSupport)
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "PresentationTest";
    createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    
#ifdef _WIN32
    createInfo.RequiredInstanceExtensions.push_back("VK_KHR_surface");
    createInfo.RequiredInstanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(__linux__)
    createInfo.RequiredInstanceExtensions.push_back("VK_KHR_surface");
    createInfo.RequiredInstanceExtensions.push_back("VK_KHR_xcb_surface");
#endif
    
    createInfo.RequiredDeviceExtensions = {"VK_KHR_swapchain"};
    createInfo.ShaderCacheDir = tempShaderCacheDir.string();

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test RHI system with various device extensions
TEST_F(RhiSystemTest, WithDeviceExtensions)
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "ExtensionTest";
    createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",
        "VK_KHR_get_memory_requirements2",
        "VK_KHR_pipeline_executable_properties"
    };
    createInfo.ShaderCacheDir = tempShaderCacheDir.string();

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
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