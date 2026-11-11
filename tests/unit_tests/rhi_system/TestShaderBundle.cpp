#include <gtest/gtest.h>
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

// ShaderBundle with no device: the HashFill bundle that ComputeManifestTest cooks, opened and queried on the CPU.

namespace
{

    constexpr std::string_view k_Module = "HashFill";
    constexpr std::string_view k_EntryPoint = "HashFillCS";

    std::vector<std::byte> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> chars{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

        std::vector<std::byte> bytes(chars.size());
        std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
        return bytes;
    }

}

TEST(ShaderBundle, OpensAModuleWithNoDevice)
{
    rhi::ShaderBundleResult<rhi::ShaderBundle> bundle = rhi::ShaderBundle::Open(HASH_FILL_BUNDLE, "spirv");
    ASSERT_TRUE(bundle.has_value()) << rhi::ToString(bundle.error());

    EXPECT_EQ(bundle->Profile().CodeFormat, lodestone::ShaderCodeFormat::Spirv);

    const std::vector<std::string_view> names = bundle->ModuleNames();
    EXPECT_NE(std::ranges::find(names, k_Module), names.end());

    const rhi::ShaderBundleResult<const lodestone::manifest::EnvironmentView*> environment =
        bundle->ModuleEnvironment(k_Module);
    ASSERT_TRUE(environment.has_value());
    EXPECT_TRUE((*environment)->Module().FindEntryPointIndex(k_EntryPoint).has_value());

    EXPECT_TRUE(bundle->ModuleIndex(k_Module).has_value());
}

// The views live behind the impl pointer, so a move of the bundle leaves every view where it was.
TEST(ShaderBundle, MoveKeepsTheViews)
{
    rhi::ShaderBundleResult<rhi::ShaderBundle> opened = rhi::ShaderBundle::Open(HASH_FILL_BUNDLE, "spirv");
    ASSERT_TRUE(opened.has_value());

    const lodestone::manifest::EnvironmentView* before = *opened->ModuleEnvironment(k_Module);
    const lodestone::ManifestIndex* indexBefore = *opened->ModuleIndex(k_Module);

    rhi::ShaderBundle moved = std::move(*opened);

    EXPECT_EQ(*moved.ModuleEnvironment(k_Module), before);
    EXPECT_EQ(*moved.ModuleIndex(k_Module), indexBefore);
    EXPECT_TRUE(before->Module().FindEntryPointIndex(k_EntryPoint).has_value()) << "the view still reads the bytes";
}

// Bytes in hand (a test, a hot reload) open the same way as a file.
TEST(ShaderBundle, FromBytesMatchesOpen)
{
    std::vector<std::byte> bytes = ReadFile(HASH_FILL_BUNDLE);
    ASSERT_FALSE(bytes.empty());

    rhi::ShaderBundleResult<rhi::ShaderBundle> bundle = rhi::ShaderBundle::FromBytes(std::move(bytes), "spirv");
    ASSERT_TRUE(bundle.has_value()) << rhi::ToString(bundle.error());
    EXPECT_TRUE(bundle->ModuleEnvironment(k_Module).has_value());
}

TEST(ShaderBundle, EachFailureHasItsError)
{
    const auto openError = [](const std::filesystem::path& path, std::string_view profile)
    {
        return rhi::ShaderBundle::Open(path, profile).error();
    };

    EXPECT_EQ(openError("does/not/exist.lsbundle", "spirv"), rhi::ShaderBundleError::FileUnreadable);
    EXPECT_EQ(openError(HASH_FILL_BUNDLE, "wgsl"), rhi::ShaderBundleError::ProfileNotInBundle) << "cooked for spirv only";

    std::vector<std::byte> garbage(256, std::byte{ 0x5a });
    EXPECT_EQ(rhi::ShaderBundle::FromBytes(std::move(garbage), "spirv").error(), rhi::ShaderBundleError::BundleInvalid);

    rhi::ShaderBundleResult<rhi::ShaderBundle> bundle = rhi::ShaderBundle::Open(HASH_FILL_BUNDLE, "spirv");
    ASSERT_TRUE(bundle.has_value());
    EXPECT_EQ(bundle->ModuleEnvironment("NoSuchModule").error(), rhi::ShaderBundleError::ModuleNotInBundle);
    EXPECT_EQ(bundle->ModuleIndex("NoSuchModule").error(), rhi::ShaderBundleError::ModuleNotInBundle);
}
