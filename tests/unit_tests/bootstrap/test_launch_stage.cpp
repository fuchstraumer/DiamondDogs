#include "BootResult.hpp"
#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include "test_modules.hpp"
#include <gtest/gtest.h>
#include <array>
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

using bootstrap::BootResult;
using bootstrap::BootResultCode;
using bootstrap::LaunchStage;
using bootstrap::ModuleDescriptor;
using bootstrap::Stage;
using bootstrap_tests::TestArguments;

namespace
{
    [[nodiscard]] std::vector<std::string_view> SelectedNames(const LaunchStage& launch);
    [[nodiscard]] std::string ConfigArgument(std::string_view file_name, std::string_view text);

    constexpr std::string_view k_ProfilesConfig = R"({
        "Profiles":
        {
            "tools": [ "beta", "alpha" ],
            "alpha_only": [ "alpha" ],
            "beta_only": [ "beta" ],
            "misspelled": [ "alpah" ],
            "repeated": [ "alpha", "alpha" ],
            "not_a_list": "alpha",
            "not_names": [ "alpha", 3 ]
        }
    })";
}

TEST(LaunchStage, NoProfileSelectsEveryLinkedModuleInListOrder)
{
    const std::array<ModuleDescriptor, 3> linked{ bootstrap_tests::k_AlphaModule,
                                                  bootstrap_tests::k_BetaModule,
                                                  bootstrap_tests::k_GammaModule };
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    EXPECT_EQ(SelectedNames(launch), (std::vector<std::string_view>{ "alpha", "beta", "gamma" }));
    EXPECT_TRUE(launch.ProfileName().empty());
    EXPECT_TRUE(launch.Config().Path.empty());
    EXPECT_EQ(launch.LastStage(), Stage::Gpu);
}

TEST(LaunchStage, ArgumentsStayUnchangedForModules)
{
    const TestArguments arguments({ "--tool-port=0", "--boot-timings", "--frames=12" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), {}, Stage::Core);

    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    ASSERT_EQ(launch.Arguments().size(), 4u);
    EXPECT_EQ(launch.Arguments()[1], "--tool-port=0");
    EXPECT_EQ(launch.Arguments()[3], "--frames=12");
    EXPECT_TRUE(launch.BootTimingsRequested());
}

TEST(LaunchStage, GpuModuleFailsWhenTheExecutableStopsAfterCore)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_GammaModule };
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Core);

    EXPECT_EQ(result.Code, BootResultCode::ModuleAfterLastStage);
    EXPECT_EQ(result.FailedStage, Stage::Launch);
    EXPECT_EQ(result.Module, "gamma");
}

TEST(LaunchStage, ProfileSelectsItsModulesInLinkedOrder)
{
    const std::array<ModuleDescriptor, 3> linked{ bootstrap_tests::k_AlphaModule,
                                                  bootstrap_tests::k_BetaModule,
                                                  bootstrap_tests::k_GammaModule };
    const std::string config = ConfigArgument("bootstrap_tests_profiles.json", k_ProfilesConfig);
    const TestArguments arguments({ config, "--profile=tools" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    EXPECT_EQ(SelectedNames(launch), (std::vector<std::string_view>{ "alpha", "beta" }));
    EXPECT_EQ(launch.ProfileName(), "tools");
    EXPECT_FALSE(launch.Config().Text.empty());
}

TEST(LaunchStage, EmptyProfileSelectsNothing)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_AlphaModule };
    const std::string config = ConfigArgument("bootstrap_tests_empty.json", R"({ "Profiles": { "demo": [] } })");
    const TestArguments arguments({ config, "--profile=demo" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    EXPECT_TRUE(launch.Selected().empty());
}

TEST(LaunchStage, UnknownProfileSuggestsTheNearestNames)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_AlphaModule };
    const std::string config = ConfigArgument("bootstrap_tests_profiles.json", k_ProfilesConfig);
    const TestArguments arguments({ config, "--profile=tool" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::UnknownProfile);
    EXPECT_EQ(result.Subject, "tool");
    EXPECT_TRUE(std::ranges::contains(result.Suggestions, "tools"));
}

TEST(LaunchStage, UnknownModuleInProfileSuggestsTheNearestNames)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_BetaModule };
    const std::string config = ConfigArgument("bootstrap_tests_profiles.json", k_ProfilesConfig);
    const TestArguments arguments({ config, "--profile=misspelled" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::UnknownModule);
    EXPECT_EQ(result.Subject, "alpah");
    EXPECT_EQ(result.Source, "profile misspelled");
    ASSERT_FALSE(result.Suggestions.empty());
    EXPECT_EQ(result.Suggestions.front(), "alpha");
}

TEST(LaunchStage, RepeatedModuleInProfileFails)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_AlphaModule };
    const std::string config = ConfigArgument("bootstrap_tests_profiles.json", k_ProfilesConfig);
    const TestArguments arguments({ config, "--profile=repeated" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::DuplicateModule);
    EXPECT_EQ(result.Subject, "alpha");
}

TEST(LaunchStage, DependencyOutsideTheProfileFails)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_BetaModule };
    const std::string config = ConfigArgument("bootstrap_tests_profiles.json", k_ProfilesConfig);
    const TestArguments arguments({ config, "--profile=beta_only" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::DependencyNotSelected);
    EXPECT_EQ(result.Module, "beta");
    EXPECT_EQ(result.Subject, "alpha");
}

TEST(LaunchStage, DependencyLaterInTheListFails)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_BetaModule, bootstrap_tests::k_AlphaModule };
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::DependencyNotEarlier);
    EXPECT_EQ(result.Module, "beta");
    EXPECT_EQ(result.Subject, "alpha");
}

TEST(LaunchStage, CoreModuleThatDependsOnAGpuModuleFails)
{
    // gamma is earlier in the list, but it starts in a later stage
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_GammaModule, bootstrap_tests::k_NeedsGammaModule };
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::DependencyNotEarlier);
    EXPECT_EQ(result.Module, "needs_gamma");
    EXPECT_EQ(result.Subject, "gamma");
}

TEST(LaunchStage, UnlinkedDependencyFailsWithSuggestions)
{
    ModuleDescriptor misspelled = bootstrap_tests::k_BetaModule;
    constexpr std::array<std::string_view, 1> k_Misspelled{ "alpah" };

    misspelled.DependsOn = k_Misspelled;

    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, misspelled };
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::UnknownModule);
    EXPECT_EQ(result.Module, "beta");
    EXPECT_EQ(result.Subject, "alpah");
    EXPECT_TRUE(std::ranges::contains(result.Suggestions, "alpha"));
}

TEST(LaunchStage, DescriptorWithBothOrNeitherStartFunctionFails)
{
    ModuleDescriptor both = bootstrap_tests::k_AlphaModule;

    both.StartGpu = bootstrap_tests::k_GammaModule.StartGpu;

    ModuleDescriptor neither = bootstrap_tests::k_AlphaModule;

    neither.StartCore = nullptr;

    const TestArguments arguments({});

    for (const ModuleDescriptor& descriptor : { both, neither })
    {
        const std::array<ModuleDescriptor, 1> linked{ descriptor };
        LaunchStage launch;
        const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

        EXPECT_EQ(result.Code, BootResultCode::InvalidDescriptor);
        EXPECT_EQ(result.Module, "alpha");
    }
}

TEST(LaunchStage, RepeatedNameInTheLinkedListFails)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_AlphaModule };
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), linked, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::DuplicateModule);
    EXPECT_EQ(result.Source, "the linked module list");
}

TEST(LaunchStage, ProfileWithoutConfigFails)
{
    const TestArguments arguments({ "--profile=tools" });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), {}, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::ProfileWithoutConfig);
}

TEST(LaunchStage, MissingConfigFileFails)
{
    const std::filesystem::path missing = std::filesystem::temp_directory_path() / "bootstrap_tests_missing.json";

    std::filesystem::remove(missing);

    const std::string config = "--config=" + missing.string();
    const TestArguments arguments({ config });
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), {}, Stage::Gpu);

    EXPECT_EQ(result.Code, BootResultCode::ConfigUnreadable);
    EXPECT_EQ(result.Subject, missing.string());
}

TEST(LaunchStage, InvalidConfigFails)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_AlphaModule };
    const std::string profiles = ConfigArgument("bootstrap_tests_profiles.json", k_ProfilesConfig);
    const std::string notJson = ConfigArgument("bootstrap_tests_not_json.json", "{ \"Profiles\": ");
    const std::string notObject = ConfigArgument("bootstrap_tests_profiles_list.json", R"({ "Profiles": [] })");

    const TestArguments brokenText({ notJson });
    const TestArguments profilesList({ notObject, "--profile=demo" });
    const TestArguments profileNotList({ profiles, "--profile=not_a_list" });
    const TestArguments profileNotNames({ profiles, "--profile=not_names" });

    for (const TestArguments* arguments : { &brokenText, &profilesList, &profileNotList, &profileNotNames })
    {
        LaunchStage launch;
        const BootResult result = launch.Start(arguments->Span(), linked, Stage::Gpu);

        EXPECT_EQ(result.Code, BootResultCode::ConfigInvalid) << bootstrap::FormatFailure(result);
    }
}

TEST(LaunchStage, MalformedFlagsFail)
{
    const TestArguments emptyConfig({ "--config=" });
    const TestArguments repeatedProfile({ "--profile=a", "--profile=b" });

    for (const TestArguments* arguments : { &emptyConfig, &repeatedProfile })
    {
        LaunchStage launch;
        const BootResult result = launch.Start(arguments->Span(), {}, Stage::Gpu);

        EXPECT_EQ(result.Code, BootResultCode::MalformedArgument) << bootstrap::FormatFailure(result);
    }
}

TEST(LaunchStage, FindBesideFallsBackToTheBuildTreePath)
{
    const TestArguments arguments({});
    LaunchStage launch;
    const BootResult result = launch.Start(arguments.Span(), {}, Stage::Core);

    ASSERT_TRUE(result.Succeeded());

    const std::filesystem::path beside = launch.FindBeside("bootstrap_tests.exe", "unused");
    const std::filesystem::path fallback = launch.FindBeside("no_such_file.ldmanifest", "build/tree/path");

    EXPECT_EQ(beside.filename(), "bootstrap_tests.exe");
    EXPECT_TRUE(std::filesystem::exists(beside));
    EXPECT_EQ(fallback, std::filesystem::path{ "build/tree/path" });
}

namespace
{
    std::vector<std::string_view> SelectedNames(const LaunchStage& launch)
    {
        std::vector<std::string_view> names;

        for (const ModuleDescriptor* descriptor : launch.Selected())
        {
            names.push_back(descriptor->Name);
        }

        return names;
    }

    std::string ConfigArgument(std::string_view file_name, std::string_view text)
    {
        const std::filesystem::path path = bootstrap_tests::WriteConfig(file_name, text);

        return "--config=" + path.string();
    }
}
