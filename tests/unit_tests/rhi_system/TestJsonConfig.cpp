#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Device.hpp"
#include <filesystem>
#include <fstream>

class JsonConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Get the test directory path
        // Get current path, and see if it's the build subdir
        testDir = "D:\\DiamondDogs\\tests\\unit_tests\\rhi_system";

        // Create shader cache cleanup directories
        shaderCacheCleanup.push_back(std::filesystem::temp_directory_path() / "temp_test_cache");
        shaderCacheCleanup.push_back(std::filesystem::temp_directory_path() / "temp_presentation_cache");
    }

    void TearDown() override
    {
        // Clean up shader cache directories
        for (const auto& dir : shaderCacheCleanup)
        {
            if (std::filesystem::exists(dir)) {
                std::filesystem::remove_all(dir);
            }
        }
    }

    std::filesystem::path testDir;
    std::vector<std::filesystem::path> shaderCacheCleanup;
};

// Test basic JSON configuration loading
TEST_F(JsonConfigTest, BasicJsonConfig)
{
    auto configPath = testDir / "TestBasicConfig.json";
    
    // Skip test if config file doesn't exist
    if (!std::filesystem::exists(configPath))
    {
        GTEST_SKIP() << "Test config file not found: " << configPath;
        return;
    }

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(configPath.string().c_str());
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetPhysicalDevice(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
    });
}

// Test presentation support JSON configuration
TEST_F(JsonConfigTest, PresentationJsonConfig)
{
    auto configPath = testDir / "TestPresentationConfig.json";
    
    // Skip test if config file doesn't exist
    if (!std::filesystem::exists(configPath))
    {
        GTEST_SKIP() << "Test config file not found: " << configPath;
        return;
    }

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(configPath.string().c_str());
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetPhysicalDevice(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
    });
}

// Test minimal JSON configuration
TEST_F(JsonConfigTest, MinimalJsonConfig)
{
    auto configPath = testDir / "TestHeadlessConfig.json";
    
    // Skip test if config file doesn't exist
    if (!std::filesystem::exists(configPath))
    {
        GTEST_SKIP() << "Test config file not found: " << configPath;
        return;
    }

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(configPath.string().c_str());
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetPhysicalDevice(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
    });
}

// Test using the provided RendererContextCfg.json as a template
TEST_F(JsonConfigTest, RendererContextConfig)
{
    // Create a test configuration based on the provided example
    std::filesystem::path tempConfigPath = std::filesystem::temp_directory_path() / "test_renderer_context.json";
    
    // Create the JSON content programmatically
    std::ofstream configFile(tempConfigPath);
    configFile << R"({
    "EngineConfig": {
        "EngineName": "DiamondDogsTestEngine",
        "EngineVersion": "0.1.0",
        "ApplicationName": "RendererContextTest",
        "ApplicationVersion": "1.0.0"
    },
    "RHISystemConfig": {
        "ApiVersion": "Latest",
        "ValidationLayers": "Base",
        "NeedsPresentationSupport": true,
        "RequiredInstanceExtensions": [
            "VK_EXT_debug_utils"
        ],
        "RequestedInstanceExtensions": [],
        "RequiredDeviceExtensions": [],
        "RequestedDeviceExtensions": [
            "VK_KHR_dedicated_allocation",
            "VK_KHR_get_memory_requirements2",
            "VK_KHR_pipeline_executable_properties"
        ],
        "ShaderCacheDir": "temp_renderer_cache"
    }
})";
    configFile.close();

    // Add to cleanup list
    shaderCacheCleanup.push_back(std::filesystem::temp_directory_path() / "temp_renderer_cache");

    EXPECT_NO_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(tempConfigPath.string().c_str());
        EXPECT_NE(rhiSystem, nullptr);
        EXPECT_NE(rhiSystem->GetInstance(), nullptr);
        EXPECT_NE(rhiSystem->GetPhysicalDevice(), nullptr);
        EXPECT_NE(rhiSystem->GetDevice(), nullptr);
    });

    // Clean up temporary config file
    std::filesystem::remove(tempConfigPath);
}

TEST_F(JsonConfigTest, EnableHdrConfig)
{
    // Create a test configuration based on the provided example
    std::filesystem::path tempConfigPath = std::filesystem::temp_directory_path() / "test_renderer_context.json";

    // Create the JSON content programmatically
    std::ofstream configFile(tempConfigPath);
    configFile << R"({
    "EngineConfig": {
        "EngineName": "DiamondDogsTestEngine",
        "EngineVersion": "0.1.0",
        "ApplicationName": "RendererContextTest",
        "ApplicationVersion": "1.0.0"
    },
    "RHISystemConfig": {
        "ApiVersion": "Latest",
        "ValidationLayers": "Base",
        "NeedsPresentationSupport": true,
        "RequiredInstanceExtensions": [
            "VK_EXT_debug_utils"
        ],
        "RequestedInstanceExtensions": [],
        "RequiredDeviceExtensions": [],
        "RequestedDeviceExtensions": [
            "VK_KHR_dedicated_allocation",
            "VK_KHR_get_memory_requirements2",
            "VK_KHR_pipeline_executable_properties"
        ],
        "ShaderCacheDir": "temp_renderer_cache"
    },
    "PlatformSystemConfig": {
        "EnableHDR": true
    }
    })";
    configFile.close();

    // Add to cleanup list
    shaderCacheCleanup.push_back(std::filesystem::temp_directory_path() / "temp_renderer_cache");
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(tempConfigPath.string().c_str());
    EXPECT_NE(rhiSystem, nullptr);
    EXPECT_NE(rhiSystem->GetInstance(), nullptr);
    EXPECT_NE(rhiSystem->GetPhysicalDevice(), nullptr);
    rhi::Device* device = rhiSystem->GetDevice();
    EXPECT_NE(device, nullptr);
    EXPECT_TRUE(device->HasExtension(VK_EXT_HDR_METADATA_EXTENSION_NAME));

    // Clean up temporary config file
    std::filesystem::remove(tempConfigPath);
}

// Test different API versions through JSON
TEST_F(JsonConfigTest, ApiVersionsViaJson)
{
    std::vector<std::string> apiVersions = {"1.0", "1.1", "1.2", "1.3", "1.4", "Latest"};
    std::vector<uint32_t> apiVersionValues = {
        VK_API_VERSION_1_0,
        VK_API_VERSION_1_1,
        VK_API_VERSION_1_2,
        VK_API_VERSION_1_3,
        VK_API_VERSION_1_4,
        VK_API_VERSION_1_4
    };
    
    for (size_t i = 0; i < apiVersions.size(); ++i)
    {
        // Create temporary config for each version
        std::filesystem::path tempConfigPath = std::filesystem::temp_directory_path() / ("test_api_" + apiVersions[i] + ".json");
        
        std::ofstream configFile(tempConfigPath);
        configFile << R"({
    "EngineConfig": {
        "EngineName": "DiamondDogsTestEngine",
        "EngineVersion": "0.1.0",
        "ApplicationName": "ApiVersionTest",
        "ApplicationVersion": "1.0.0"
    },
    "RHISystemConfig": {
        "ApiVersion": ")" << apiVersions[i] << R"(",
        "ValidationLayers": "Base",
        "NeedsPresentationSupport": false,
        "RequiredInstanceExtensions": [
            "VK_EXT_debug_utils"
        ],
        "RequestedInstanceExtensions": [],
        "RequiredDeviceExtensions": [],
        "RequestedDeviceExtensions": [
            "VK_KHR_dedicated_allocation"
        ],
        "ShaderCacheDir": "temp_api_cache_)" << apiVersions[i] << R"("
    }
})";
        configFile.close();

        // Add to cleanup
        shaderCacheCleanup.push_back(std::filesystem::temp_directory_path() / ("temp_api_cache_" + apiVersions[i]));

        EXPECT_NO_THROW({
            auto rhiSystem = std::make_unique<rhi::RhiSystem>(tempConfigPath.string().c_str());
            EXPECT_NE(rhiSystem, nullptr);
            EXPECT_NE(rhiSystem->GetInstance(), nullptr);
            EXPECT_EQ(rhiSystem->GetVulkanApiVersion(), apiVersionValues[i]);
        }) << "Failed for API version: " << apiVersions[i];

        // Clean up temporary config file
        std::filesystem::remove(tempConfigPath);
    }
}

// Test validation layer configurations through JSON
TEST_F(JsonConfigTest, ValidationLayersViaJson)
{
    std::vector<std::pair<std::string, bool>> validationConfigs =
    {
        {"None", false},     // No debug utils extension needed
        {"Base", true},      // Need debug utils extension
        {"Synchronization", true}, // Need debug utils extension
        {"All", true}        // Need debug utils extension
    };
    
    for (const auto& [validation, needsDebugUtils] : validationConfigs)
    {
        std::filesystem::path tempConfigPath = std::filesystem::temp_directory_path() / ("test_validation_" + validation + ".json");
        
        std::ofstream configFile(tempConfigPath);
        configFile << R"({
    "EngineConfig": {
        "EngineName": "DiamondDogsTestEngine",
        "EngineVersion": "0.1.0",
        "ApplicationName": "ValidationTest",
        "ApplicationVersion": "1.0.0"
    },
    "RHISystemConfig": {
        "ApiVersion": "1.3",
        "ValidationLayers": ")" << validation << R"(",
        "NeedsPresentationSupport": false,
        "RequiredInstanceExtensions": [)";
        
        if (needsDebugUtils) {
            configFile << R"("VK_EXT_debug_utils")";
        }
        
        configFile << R"(],
        "RequestedInstanceExtensions": [],
        "RequiredDeviceExtensions": [],
        "RequestedDeviceExtensions": [],
        "ShaderCacheDir": "temp_validation_cache_)" << validation << R"("
    }
})";
        configFile.close();

        // Add to cleanup
        shaderCacheCleanup.push_back(std::filesystem::temp_directory_path() / ("temp_validation_cache_" + validation));

        EXPECT_NO_THROW({
            auto rhiSystem = std::make_unique<rhi::RhiSystem>(tempConfigPath.string().c_str());
            EXPECT_NE(rhiSystem, nullptr);
        }) << "Failed for validation layer: " << validation;

        // Clean up temporary config file
        std::filesystem::remove(tempConfigPath);
    }
}

// Test invalid JSON file handling
TEST_F(JsonConfigTest, InvalidJsonFile)
{
    std::filesystem::path invalidConfigPath = std::filesystem::temp_directory_path() / "invalid_config.json";
    
    // Create invalid JSON
    std::ofstream configFile(invalidConfigPath);
    configFile << "{ invalid json content without proper structure";
    configFile.close();

    EXPECT_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(invalidConfigPath.string().c_str());
    }, std::exception);

    // Clean up
    std::filesystem::remove(invalidConfigPath);
}

// Test missing JSON file handling
TEST_F(JsonConfigTest, MissingJsonFile)
{
    std::filesystem::path missingPath = "nonexistent_config.json";

    EXPECT_THROW({
        auto rhiSystem = std::make_unique<rhi::RhiSystem>(missingPath.string().c_str());
    }, std::runtime_error);
}
