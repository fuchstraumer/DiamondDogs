#include "BootResult.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include "async/Scheduler.hpp"
#include "test_modules.hpp"
#include <gtest/gtest.h>
#include <array>
#include <string>
#include <vector>

using bootstrap::BootResult;
using bootstrap::BootResultCode;
using bootstrap::CoreStage;
using bootstrap::LaunchStage;
using bootstrap::ModuleDescriptor;
using bootstrap::Stage;
using bootstrap_tests::TestArguments;

TEST(CoreStage, StartsCoreModulesAndSkipsGpuModules)
{
    const std::array<ModuleDescriptor, 3> linked{ bootstrap_tests::k_AlphaModule,
                                                  bootstrap_tests::k_BetaModule,
                                                  bootstrap_tests::k_GammaModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Gpu).Succeeded());

    CoreStage core;
    const BootResult result = core.Start(launch);

    ASSERT_TRUE(result.Succeeded()) << bootstrap::FormatFailure(result);
    ASSERT_TRUE(core.Started());
    ASSERT_EQ(core.Modules().size(), 2u);
    EXPECT_EQ(core.Modules()[0].KeyName(), "alpha");
    EXPECT_EQ(core.Modules()[1].KeyName(), "beta");
    EXPECT_EQ(&core.Launch(), &launch);
    EXPECT_TRUE(core.Scheduler().Idle());
    EXPECT_EQ(core.FindModuleObject("gamma"), nullptr);
}

TEST(CoreStage, RequireReturnsTheDeclaredDependency)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_BetaModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    const auto* alpha = static_cast<const bootstrap_tests::Alpha*>(core.FindModuleObject("alpha"));
    const auto* beta = static_cast<const bootstrap_tests::Beta*>(core.FindModuleObject("beta"));

    ASSERT_NE(alpha, nullptr);
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(beta->Dependency, alpha);
    EXPECT_EQ(alpha->Value, 7);
}

TEST(CoreStage, StopDestroysModulesInReverseStartOrder)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_BetaModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    bootstrap_tests::DestroyLog().clear();
    core.Stop();

    EXPECT_FALSE(core.Started());
    EXPECT_TRUE(core.Modules().empty());
    EXPECT_EQ(bootstrap_tests::DestroyLog(), (std::vector<std::string>{ "beta", "alpha" }));
}

TEST(CoreStage, StartAgainAfterStopRebuildsTheStage)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_AlphaModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());
    core.Stop();
    ASSERT_TRUE(core.Start(launch).Succeeded());
    EXPECT_EQ(core.Modules().size(), 1u);
}

TEST(CoreStage, UndeclaredDependencyFails)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule,
                                                  bootstrap_tests::k_UndeclaredModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;
    const BootResult result = core.Start(launch);

    EXPECT_EQ(result.Code, BootResultCode::UndeclaredDependency);
    EXPECT_EQ(result.FailedStage, Stage::Core);
    EXPECT_EQ(result.Module, "undeclared");
    EXPECT_EQ(result.Subject, "alpha");
}

TEST(CoreStage, FailedModuleStopsWhatStarted)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_FailingModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    bootstrap_tests::DestroyLog().clear();

    CoreStage core;
    const BootResult result = core.Start(launch);

    EXPECT_EQ(result.Code, BootResultCode::ModuleStartFailed);
    EXPECT_EQ(result.FailedStage, Stage::Core);
    EXPECT_EQ(result.Module, "failing");
    EXPECT_EQ(result.Subject, "planted");
    EXPECT_FALSE(core.Started());
    EXPECT_EQ(bootstrap_tests::DestroyLog(), (std::vector<std::string>{ "alpha" }));
}

TEST(CoreStage, ModuleThatOwnsNothingFails)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_OwnsNothingModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;
    const BootResult result = core.Start(launch);

    EXPECT_EQ(result.Code, BootResultCode::ModuleInstanceInvalid);
    EXPECT_EQ(result.Module, "owns_nothing");
}

TEST(CoreStage, ModuleThatOwnsAnotherKeyFails)
{
    // alpha's start function owns with the key "alpha", so a descriptor with another name is refused
    ModuleDescriptor renamed = bootstrap_tests::k_AlphaModule;

    renamed.Name = "renamed";

    const std::array<ModuleDescriptor, 1> linked{ renamed };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;
    const BootResult result = core.Start(launch);

    EXPECT_EQ(result.Code, BootResultCode::ModuleInstanceInvalid);
    EXPECT_EQ(result.Module, "renamed");
    EXPECT_EQ(result.Subject, "alpha");
}
