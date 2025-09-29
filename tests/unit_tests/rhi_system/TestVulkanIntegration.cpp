#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Instance.hpp"
#include "Device.hpp"
#include <filesystem>

class VulkanIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary shader cache directory
        tempShaderCacheDir = std::filesystem::temp_directory_path() / "DiamondDogsTest" / "VulkanIntegration";
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
        createInfo.ApplicationName = "VulkanIntegrationTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.AppVersion = VK_MAKE_VERSION(1, 0, 0);
        createInfo.EngineVersion = VK_MAKE_VERSION(0, 1, 0);
        createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;
        createInfo.RequiredInstanceExtensions = {"VK_EXT_debug_utils"};
        createInfo.ShaderCacheDir = tempShaderCacheDir.string();
        return createInfo;
    }
};

// Test Vulkan 1.0 API version
TEST_F(VulkanIntegrationTest, Vulkan10ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan10;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Vulkan 1.1 API version
TEST_F(VulkanIntegrationTest, Vulkan11ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan11;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Vulkan 1.2 API version
TEST_F(VulkanIntegrationTest, Vulkan12ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan12;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Vulkan 1.3 API version
TEST_F(VulkanIntegrationTest, Vulkan13ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan13;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Vulkan 1.4 API version
TEST_F(VulkanIntegrationTest, Vulkan14ApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Vulkan14;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test Latest API version
TEST_F(VulkanIntegrationTest, LatestApiVersion) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.VkVersion = rhi::ApiVersion::Latest;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    });
}

// Test different validation layer configurations
TEST_F(VulkanIntegrationTest, NoValidationLayers) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.ValidationLevel = rhi::ValidationLayers::None;
    createInfo.RequiredInstanceExtensions.clear(); // Remove debug utils when no validation

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

TEST_F(VulkanIntegrationTest, BaseValidationLayers) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.ValidationLevel = rhi::ValidationLayers::BaseOnly;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

TEST_F(VulkanIntegrationTest, SynchronizationValidationLayers) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.ValidationLevel = rhi::ValidationLayers::WithSynchronization;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

TEST_F(VulkanIntegrationTest, FullValidationLayers) {
    auto createInfo = GetBaseCreateInfo();
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        EXPECT_NE(rhiSystem, nullptr);
    });
}

// Test individual component access
TEST_F(VulkanIntegrationTest, ComponentAccess) {
    auto createInfo = GetBaseCreateInfo();
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    // Test instance access
    auto* instance = rhiSystem->GetInstance();
    EXPECT_NE(instance, nullptr);
    EXPECT_NE(instance->Handle().As<VkInstance>(), VK_NULL_HANDLE);

    // Test logical device access
    auto* device = rhiSystem->GetDevice();
    EXPECT_NE(device, nullptr);
    EXPECT_NE(device->Handle().As<VkDevice>(), VK_NULL_HANDLE);
}
