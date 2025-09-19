#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include <filesystem>
#include <vector>
#include <string>

class ExtensionManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary shader cache directory
        tempShaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "ExtensionMgmt";
        std::filesystem::create_directories(tempShaderCacheDir);
    }

    void TearDown() override {
        if (std::filesystem::exists(tempShaderCacheDir))
        {
            std::filesystem::remove_all(tempShaderCacheDir);
        }
    }

    std::filesystem::path tempShaderCacheDir;

    rhi::RhiSystemCreateInfo GetBaseCreateInfo()
    {
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "ExtensionManagementTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
        createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
        createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ShaderCacheDir = tempShaderCacheDir.string();
        return createInfo;
    }
};

// Test required instance extensions
TEST_F(ExtensionManagementTest, RequiredInstanceExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {
        "VK_EXT_debug_utils",
        "VK_KHR_surface"
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test optional instance extensions
TEST_F(ExtensionManagementTest, OptionalInstanceExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequestedInstanceExtensions = {
        "VK_EXT_validation_features",  // May or may not be available
        "VK_KHR_get_physical_device_properties2"
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test required device extensions
TEST_F(ExtensionManagementTest, RequiredDeviceExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequiredDeviceExtensions = {
        "VK_KHR_maintenance1"  // Should be widely supported
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test optional device extensions
TEST_F(ExtensionManagementTest, OptionalDeviceExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",
        "VK_KHR_get_memory_requirements2",
        "VK_KHR_pipeline_executable_properties",
        "VK_EXT_memory_budget"  // May not be available on all drivers
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test mixing required and optional extensions
TEST_F(ExtensionManagementTest, MixedExtensions) {
    auto createInfo = GetBaseCreateInfo();
    
    // Instance extensions
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    createInfo.RequestedInstanceExtensions = {"VK_EXT_validation_features"};
    
    // Device extensions
    createInfo.RequiredDeviceExtensions = {"VK_KHR_maintenance1"};
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",
        "VK_KHR_pipeline_executable_properties"
    };

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test presentation support extensions (platform specific)
TEST_F(ExtensionManagementTest, PresentationExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {
        "VK_EXT_debug_utils",
        "VK_KHR_surface"
    };
    
#ifdef _WIN32
    createInfo.RequiredInstanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(__linux__)
    createInfo.RequiredInstanceExtensions.push_back("VK_KHR_xcb_surface");
#endif
    
    createInfo.RequiredDeviceExtensions = {"VK_KHR_swapchain"};

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test empty extension lists (should still work)
TEST_F(ExtensionManagementTest, NoExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.ValidationLevel = rhi::ValidationLayers::None;
    // Leave all extension vectors empty

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test Vulkan 1.3+ specific extensions
TEST_F(ExtensionManagementTest, Vulkan13Extensions) {
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
    });
}

// Test handling of potentially unsupported extensions
TEST_F(ExtensionManagementTest, UnsupportedOptionalExtensions) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
    
    // Add some extensions that might not be supported everywhere
    createInfo.RequestedDeviceExtensions = {
        "VK_KHR_dedicated_allocation",  // Likely supported
        "VK_NV_ray_tracing",           // NVIDIA specific, may not be available
        "VK_AMD_buffer_marker",        // AMD specific, may not be available
        "VK_EXT_memory_budget"         // May not be supported by all drivers
    };

    // Should not throw even if some optional extensions aren't available
    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}
