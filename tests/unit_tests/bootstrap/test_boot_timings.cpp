#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include "test_modules.hpp"
#include <gtest/gtest.h>
#include <array>
#include <string>

using bootstrap::BootSpan;
using bootstrap::BootSpanKind;
using bootstrap::BootTimings;
using bootstrap::CoreStage;
using bootstrap::LaunchStage;
using bootstrap::ModuleDescriptor;
using bootstrap::Stage;
using bootstrap_tests::TestArguments;

#if BOOTSTRAP_TIMINGS_ENABLED
TEST(BootTimings, StagesAndModulesRecordOneSpanEach)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_AlphaModule, bootstrap_tests::k_BetaModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    const std::span<const BootSpan> spans = launch.Timings().Spans();

    ASSERT_EQ(spans.size(), 4u);
    EXPECT_EQ(spans[0].Kind, BootSpanKind::Stage);
    EXPECT_EQ(spans[0].Name, "Launch");
    EXPECT_EQ(spans[1].Kind, BootSpanKind::Stage);
    EXPECT_EQ(spans[1].Name, "Core");
    EXPECT_EQ(spans[2].Kind, BootSpanKind::Module);
    EXPECT_EQ(spans[2].Name, "alpha");
    EXPECT_EQ(spans[2].OwningStage, Stage::Core);
    EXPECT_EQ(spans[3].Name, "beta");

    for (const BootSpan& span : spans)
    {
        EXPECT_GE(span.Nanoseconds, 0) << span.Name;
    }

    // a module span lies inside its stage span
    EXPECT_GE(spans[2].StartNanoseconds, spans[1].StartNanoseconds);
    EXPECT_LE(spans[3].StartNanoseconds + spans[3].Nanoseconds, spans[1].StartNanoseconds + spans[1].Nanoseconds);
}

TEST(BootTimings, LaunchStartClearsTheEarlierBoot)
{
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), {}, Stage::Core).Succeeded());
    ASSERT_TRUE(launch.Start(arguments.Span(), {}, Stage::Core).Succeeded());
    EXPECT_EQ(launch.Timings().Spans().size(), 1u);
}

TEST(BootTimings, FullArrayDropsSpans)
{
    BootTimings timings;

    for (int32_t spanIndex = 0; spanIndex < bootstrap::k_MaxBootSpans; ++spanIndex)
    {
        const int32_t begun = timings.Begin(Stage::Core, BootSpanKind::Step, "step");

        EXPECT_EQ(begun, spanIndex);
        timings.End(begun);
    }

    const int32_t dropped = timings.Begin(Stage::Core, BootSpanKind::Step, "dropped");

    EXPECT_EQ(dropped, -1);
    timings.End(dropped);
    EXPECT_EQ(timings.DroppedSpans(), 1);
    EXPECT_EQ(std::ssize(timings.Spans()), bootstrap::k_MaxBootSpans);
}

TEST(BootTimings, TableListsStagesModulesAndTheTotal)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_AlphaModule };
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), linked, Stage::Core).Succeeded());

    CoreStage core;

    ASSERT_TRUE(core.Start(launch).Succeeded());

    const std::string table = bootstrap::FormatBootTimings(launch.Timings(), "no device");

    EXPECT_TRUE(table.starts_with("boot timings (")) << table;
    EXPECT_NE(table.find("no device"), std::string::npos) << table;
    EXPECT_NE(table.find("\n  Launch"), std::string::npos) << table;
    EXPECT_NE(table.find("\n  Core"), std::string::npos) << table;
    EXPECT_NE(table.find("\n    module  alpha"), std::string::npos) << table;
    EXPECT_NE(table.find("\n  total"), std::string::npos) << table;
}
#else
TEST(BootTimings, CompiledOutBuildRecordsNothing)
{
    const TestArguments arguments({});
    LaunchStage launch;

    ASSERT_TRUE(launch.Start(arguments.Span(), {}, Stage::Core).Succeeded());
    EXPECT_TRUE(launch.Timings().Spans().empty());
    EXPECT_TRUE(bootstrap::FormatBootTimings(launch.Timings(), "no device").starts_with("boot timings are compiled out"));
}
#endif
