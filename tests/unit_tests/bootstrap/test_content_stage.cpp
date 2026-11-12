#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "ShaderBundle.hpp"
#include "test_modules.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using bootstrap::BootResult;
using bootstrap::BootResultCode;
using bootstrap::BundleRequest;
using bootstrap::ContentStage;
using bootstrap::CoreStage;
using bootstrap::LaunchStage;
using bootstrap::ModuleDescriptor;
using bootstrap::Stage;
using bootstrap_tests::TestArguments;

namespace
{
    const std::array<BundleRequest, 1> k_AppBundles{ bootstrap_tests::k_BootNeedsRequest };
}

TEST(ContentStage, OpensTheAppBundles)
{
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), std::span<const ModuleDescriptor>{}, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;
    const BootResult result = content.Start(core, k_AppBundles);

    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    ASSERT_TRUE(content.Started());
    EXPECT_EQ(&content.Core(), &core);
    ASSERT_EQ(content.Bundles().size(), 1u);

    const bootstrap::LoadedBundle& loaded = content.Bundles()[0];

    EXPECT_EQ(loaded.FileName, bootstrap_tests::k_BootNeedsRequest.FileName);
    EXPECT_TRUE(loaded.RequestedBy.empty());
    EXPECT_EQ(loaded.Path, std::filesystem::path{ bootstrap_tests::k_BootNeedsRequest.BuildTreePath });
    EXPECT_EQ(content.FindBundle(loaded.FileName), &loaded.Bundle);
    EXPECT_EQ(content.FindBundle("other.ldbundle"), nullptr);

    const std::vector<std::string_view> moduleNames = loaded.Bundle.ModuleNames();

    EXPECT_TRUE(std::ranges::contains(moduleNames, "BootNeedsWave"));
    EXPECT_TRUE(std::ranges::contains(moduleNames, "BootNeedsPlain"));
}

TEST(ContentStage, OpensEachModuleBundleOnce)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_HashFillUserModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;
    const BootResult result = content.Start(core, k_AppBundles);

    // the module asks for HashFill and for the app's bundle again: the second request opens nothing
    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    ASSERT_EQ(content.Bundles().size(), 2u);
    EXPECT_EQ(content.Bundles()[0].FileName, bootstrap_tests::k_BootNeedsRequest.FileName);
    EXPECT_TRUE(content.Bundles()[0].RequestedBy.empty());
    EXPECT_EQ(content.Bundles()[1].FileName, bootstrap_tests::k_HashFillRequest.FileName);
    EXPECT_EQ(content.Bundles()[1].RequestedBy, "hash_fill_user");
    EXPECT_NE(content.FindBundle(bootstrap_tests::k_HashFillRequest.FileName), nullptr);
}

TEST(ContentStage, AModuleThatIsNotSelectedOpensNothing)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_HashFillUserModule, bootstrap_tests::k_GammaModule };
    const std::filesystem::path config = bootstrap_tests::WriteConfig("bootstrap_content_profile.json",
                                                                      R"({ "Profiles": { "lean": [ "gamma" ] } })");
    const std::string configArgument = "--config=" + config.string();
    const TestArguments arguments({ configArgument, "--profile=lean" });
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;

    ASSERT_TRUE(content.Start(core, k_AppBundles).Succeeded());
    ASSERT_EQ(content.Bundles().size(), 1u);
    EXPECT_EQ(content.FindBundle(bootstrap_tests::k_HashFillRequest.FileName), nullptr);
}

TEST(ContentStage, AMissingBundleFailsAndClosesWhatOpened)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_MissingBundleModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;
    const BootResult result = content.Start(core, k_AppBundles);

    EXPECT_EQ(result.Code, BootResultCode::BundleNotFound);
    EXPECT_EQ(result.FailedStage, Stage::Content);
    EXPECT_EQ(result.Module, "missing_bundle");
    EXPECT_EQ(result.Subject, "missing.ldbundle");
    EXPECT_NE(result.Source.find("C:/nowhere/missing.ldbundle"), std::string::npos);
    EXPECT_FALSE(content.Started());
    EXPECT_TRUE(content.Bundles().empty());
}

TEST(ContentStage, OneNameForTwoBundlesFails)
{
    // every cook writes ShaderLibrary.ldmanifest, so a copied request that keeps the old name is an easy mistake
    const std::array<BundleRequest, 2> requests
    {
        bootstrap_tests::k_BootNeedsRequest,
        BundleRequest
        {
            .FileName = bootstrap_tests::k_BootNeedsRequest.FileName,
            .BuildTreePath = bootstrap_tests::k_HashFillRequest.BuildTreePath,
            .Profile = rhi::k_SpirvProfileName,
        },
    };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), std::span<const ModuleDescriptor>{}, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;
    const BootResult result = content.Start(core, requests);

    EXPECT_EQ(result.Code, BootResultCode::BundleNameConflict);
    EXPECT_EQ(result.Subject, bootstrap_tests::k_BootNeedsRequest.FileName);
    EXPECT_NE(result.Source.find(bootstrap_tests::k_HashFillRequest.BuildTreePath), std::string::npos);
    EXPECT_FALSE(content.Started());
}

TEST(ContentStage, AFileThatIsNotABundleFails)
{
    const std::filesystem::path path = bootstrap_tests::WriteConfig("bootstrap_not_a_bundle.ldbundle", "not a bundle");
    const std::string pathText = path.string();
    const std::array<BundleRequest, 1> requests
    {
        BundleRequest
        {
            .FileName = "bootstrap_not_a_bundle.ldbundle",
            .BuildTreePath = pathText,
            .Profile = rhi::k_SpirvProfileName,
        },
    };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), std::span<const ModuleDescriptor>{}, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;
    const BootResult result = content.Start(core, requests);

    EXPECT_EQ(result.Code, BootResultCode::BundleInvalid);
    EXPECT_TRUE(result.Module.empty());
    EXPECT_EQ(result.Subject, pathText);
    EXPECT_FALSE(content.Started());
}

TEST(ContentStage, AProfileThatTheBundleLacksFails)
{
    const std::array<BundleRequest, 1> requests
    {
        BundleRequest
        {
            .FileName = bootstrap_tests::k_BootNeedsRequest.FileName,
            .BuildTreePath = bootstrap_tests::k_BootNeedsRequest.BuildTreePath,
            .Profile = "wgsl",
        },
    };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), std::span<const ModuleDescriptor>{}, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;
    const BootResult result = content.Start(core, requests);

    EXPECT_EQ(result.Code, BootResultCode::BundleInvalid);
    EXPECT_NE(result.Source.find(rhi::ToString(rhi::ShaderBundleError::ProfileNotInBundle)), std::string::npos);
    EXPECT_NE(result.Source.find("wgsl"), std::string::npos);
}

TEST(ContentStage, StopClosesTheBundlesAndStartOpensThemAgain)
{
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), std::span<const ModuleDescriptor>{}, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;

    ASSERT_TRUE(content.Start(core, k_AppBundles).Succeeded());

    content.Stop();

    EXPECT_FALSE(content.Started());
    EXPECT_TRUE(content.Bundles().empty());
    ASSERT_TRUE(content.Start(core, k_AppBundles).Succeeded());
    EXPECT_EQ(content.Bundles().size(), 1u);
}

#if BOOTSTRAP_TIMINGS_ENABLED
TEST(ContentStage, RecordsOneSpanPerOpenedBundle)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_HashFillUserModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Gpu).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    ContentStage content;

    ASSERT_TRUE(content.Start(core, k_AppBundles).Succeeded());

    std::vector<std::string_view> bundleSpans;
    int32_t contentSpans = 0;

    for (const bootstrap::BootSpan& span : launch.Timings().Spans())
    {
        if (span.OwningStage != Stage::Content)
        {
            continue;
        }

        ++contentSpans;
        EXPECT_GE(span.Nanoseconds, 0);

        if (span.Kind == bootstrap::BootSpanKind::Bundle)
        {
            bundleSpans.push_back(span.Name);
        }
    }

    // the stage span and two bundle spans: the repeated request records nothing
    EXPECT_EQ(contentSpans, 3);
    ASSERT_EQ(bundleSpans.size(), 2u);
    EXPECT_EQ(bundleSpans[0], bootstrap_tests::k_BootNeedsRequest.FileName);
    EXPECT_EQ(bundleSpans[1], bootstrap_tests::k_HashFillRequest.FileName);
}
#endif
