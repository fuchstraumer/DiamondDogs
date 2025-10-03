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
TEST_F(RhiSystemTest, Vulkan10ApiVersion)
{
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan10;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Vulkan 1.1 API version (another key version for use)
TEST_F(RhiSystemTest, Vulkan11ApiVersion)
{
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

// Test optional instance extensions
TEST_F(RhiSystemTest, OptionalInstanceExtensions)
{
    auto createInfo = GetBaseCreateInfo();
    std::vector<std::string> optionalExtensions
    {
        "VK_EXT_validation_features",  // May or may not be available
        "VK_KHR_get_physical_device_properties2",
        "VK_FAKE_extension_not_real"
    };
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequestedInstanceExtensions = optionalExtensions;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        // required extension should always be present
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension("VK_EXT_debug_utils"));
        EXPECT_FALSE(rhiSystem->GetInstance()->HasExtension("VK_FAKE_extension_not_real"));
    });
}

// Test optional device extensions
TEST_F(RhiSystemTest, OptionalDeviceExtensions)
{
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequiredDeviceExtensions = { "VK_KHR_maintenance1" };
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",
        "VK_KHR_get_memory_requirements2",
        "VK_KHR_pipeline_executable_properties",
        "VK_EXT_memory_budget",
        "VK_FAKE_device_extension_not_real"
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension("VK_EXT_debug_utils"));
        EXPECT_TRUE(rhiSystem->GetDevice()->HasExtension("VK_KHR_maintenance1"));
        EXPECT_TRUE(rhiSystem->GetDevice()->HasExtension("VK_KHR_dedicated_allocation"));
        EXPECT_FALSE(rhiSystem->GetDevice()->HasExtension("VK_FAKE_device_extension_not_real"));
    });
}

// Test mixing required and optional extensions
TEST_F(RhiSystemTest, MixedExtensions)
{
    auto createInfo = GetBaseCreateInfo();
    
    // Instance extensions
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequestedInstanceExtensions = {"VK_FAKE_extension_not_real"};
    
    // Device extensions
    createInfo.RequiredDeviceExtensions = {"VK_KHR_maintenance1"};
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",
        "VK_KHR_pipeline_executable_properties",
        "VK_FAKE_device_extension_not_real"
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension("VK_EXT_debug_utils"));
        EXPECT_FALSE(rhiSystem->GetInstance()->HasExtension("VK_FAKE_extension_not_real"));
        EXPECT_TRUE(rhiSystem->GetDevice()->HasExtension("VK_KHR_maintenance1"));
        EXPECT_FALSE(rhiSystem->GetDevice()->HasExtension("VK_FAKE_device_extension_not_real"));
    });
}

// Test presentation support extensions (platform specific)
TEST_F(RhiSystemTest, PresentationExtensions)
{
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions =
    {
        "VK_EXT_debug_utils",
        "VK_KHR_surface"
    };
    
#ifdef _WIN32
    const char* requiredExtensionToTest = "VK_KHR_win32_surface";
    createInfo.RequiredInstanceExtensions.push_back(requiredExtensionToTest);
#elif defined(__linux__)
    // On Linux, we might be using XCB, Xlib, or Wayland. Here we test XCB as an example.
    const char* requiredExtensionToTest = "VK_KHR_xcb_surface";
    createInfo.RequiredInstanceExtensions.push_back(requiredExtensionToTest);
#endif
    
    createInfo.RequiredDeviceExtensions = {"VK_KHR_swapchain"};

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension("VK_EXT_debug_utils"));
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension("VK_KHR_surface"));
        EXPECT_TRUE(rhiSystem->GetInstance()->HasExtension(requiredExtensionToTest));
    });
}

// Test empty extension lists (should still work)
TEST_F(RhiSystemTest, NoExtensions)
{
    auto createInfo = GetBaseCreateInfo();
    createInfo.ValidationLevel = rhi::ValidationLayers::None;
    // Leave all extension vectors empty

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
    });
}

// Test Vulkan 1.3+ specific extensions
TEST_F(RhiSystemTest, Vulkan13Extensions)
{
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    
    // Extensions that should be core in Vulkan 1.3
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dynamic_rendering",  // Core in 1.3
        "VK_EXT_extended_dynamic_state",
        "VK_EXT_extended_dynamic_state2"
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
        EXPECT_TRUE(rhiSystem->GetDevice()->HasExtension("VK_KHR_dynamic_rendering"));
    });
}

// Test handling of potentially unsupported extensions
TEST_F(RhiSystemTest, UnsupportedOptionalExtensions)
{
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    
    // Add some extensions that might not be supported everywhere
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",  // Likely supported
        "VK_NV_ray_tracing",           // NVIDIA specific, may not be available
        "VK_AMD_buffer_marker",        // AMD specific, may not be available
        "VK_ARM_data_graph"         // May not be supported by all drivers
    };

    // Should not throw even if some optional extensions aren't available
    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
        EXPECT_FALSE(rhiSystem->GetDevice()->HasExtension("VK_ARM_data_graph"));
    });
}
