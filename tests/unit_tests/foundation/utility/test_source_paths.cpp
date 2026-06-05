#include <gtest/gtest.h>
#include "SourcePaths.hpp"
#include <filesystem>

namespace fs = std::filesystem;

class SourcePathsTest : public ::testing::Test
{
protected:
    void SetUp() override 
    {
        // These tests verify that the generated source paths are valid
    }
};

TEST_F(SourcePathsTest, SourceRootExists)
{
    auto sourcePath = DiamondDogs::GetSourceRootPath();
    EXPECT_TRUE(fs::exists(sourcePath)) << "Source root path does not exist: " << sourcePath;
    EXPECT_TRUE(fs::is_directory(sourcePath)) << "Source root is not a directory: " << sourcePath;
    
    // Verify it contains expected subdirectories
    EXPECT_TRUE(fs::exists(sourcePath / "assets")) << "Assets directory not found in source root";
    EXPECT_TRUE(fs::exists(sourcePath / "foundation")) << "Foundation directory not found in source root";
    EXPECT_TRUE(fs::exists(sourcePath / "Modules")) << "Modules directory not found in source root";
    EXPECT_TRUE(fs::exists(sourcePath / "tests")) << "Tests directory not found in source root";
}

TEST_F(SourcePathsTest, AssetsDirectoryExists)
{
    auto assetsPath = DiamondDogs::GetAssetsPath();
    EXPECT_TRUE(fs::exists(assetsPath)) << "Assets path does not exist: " << assetsPath;
    EXPECT_TRUE(fs::is_directory(assetsPath)) << "Assets path is not a directory: " << assetsPath;
    
    // Verify it contains expected subdirectories
    EXPECT_TRUE(fs::exists(assetsPath / "shaders")) << "Shaders directory not found in assets";
    EXPECT_TRUE(fs::exists(assetsPath / "ResourceContextTestAssets")) << "ResourceContextTestAssets directory not found";
}

TEST_F(SourcePathsTest, GetSourceRelativePathWorks)
{
    auto readmePath = DiamondDogs::GetSourceRelativePath("ReadMe.md");
    EXPECT_TRUE(fs::exists(readmePath)) << "README.md not found via GetSourceRelativePath: " << readmePath;
    
    auto cmakePath = DiamondDogs::GetSourceRelativePath("CMakeLists.txt");
    EXPECT_TRUE(fs::exists(cmakePath)) << "CMakeLists.txt not found via GetSourceRelativePath: " << cmakePath;
}

TEST_F(SourcePathsTest, GetAssetPathWorks)
{
    auto loggingConfigPath = DiamondDogs::GetAssetPath("logging.ini");
    EXPECT_TRUE(fs::exists(loggingConfigPath)) << "logging.ini not found via GetAssetPath: " << loggingConfigPath;
    
    // Test with subdirectory
    auto starboxPath = DiamondDogs::GetAssetPath("ResourceContextTestAssets/Starbox.dds");
    EXPECT_TRUE(fs::exists(starboxPath)) << "Starbox.dds not found via GetAssetPath: " << starboxPath;
}

TEST_F(SourcePathsTest, StringViewConstants)
{
    // Test that the string view constants are properly defined
    EXPECT_FALSE(DiamondDogs::SOURCE_ROOT_DIR.empty()) << "SOURCE_ROOT_DIR constant is empty";
    EXPECT_FALSE(DiamondDogs::ASSETS_DIR.empty()) << "ASSETS_DIR constant is empty";
    
    // Verify they end with expected patterns (no trailing slash unless root)
    std::string sourceStr{ DiamondDogs::SOURCE_ROOT_DIR };
    std::string assetsStr{ DiamondDogs::ASSETS_DIR };
    
    EXPECT_TRUE(sourceStr.ends_with("DiamondDogs") || sourceStr.ends_with("DiamondDogs/") || sourceStr.ends_with("DiamondDogs\\"))
        << "SOURCE_ROOT_DIR doesn't end with DiamondDogs: " << sourceStr;
    
    EXPECT_TRUE(assetsStr.ends_with("assets") || assetsStr.ends_with("assets/") || assetsStr.ends_with("assets\\"))
        << "ASSETS_DIR doesn't end with assets: " << assetsStr;
}