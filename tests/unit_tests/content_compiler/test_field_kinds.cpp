#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "Executor.hpp"
#include "ExecutorTypes.hpp"
#include "FieldKinds.hpp"
#include "GraphJson.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "RecipeTable.hpp"
#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using testing::ElementsAre;
using testing::HasSubstr;
using testing::StartsWith;

namespace
{
    constexpr int32_t k_MaxTicks = 100;

    // The example file of the README. @SCALE@ stands for the Scale of the field.ScaleBias node.
    constexpr std::string_view k_ExampleFile = R"json(
{
  "schema": "cc.AuthoredGraph",
  "version": 1,
  "data": {
    "nodes": [
      { "name": "ground", "kind": "field.Const", "params": { "Width": 4, "Height": 2, "Value": 1.0 } },
      { "name": "raised", "kind": "field.ScaleBias", "params": { "Scale": @SCALE@, "Bias": 0.5 } },
      { "name": "total", "kind": "field.Add" }
    ],
    "edges": [
      { "from": "ground", "output": "Field", "to": "raised", "slot": "Source" },
      { "from": "ground", "output": "Field", "to": "total", "slot": "Left" },
      { "from": "raised", "output": "Field", "to": "total", "slot": "Right" }
    ]
  }
}
)json";

    // the same file with no edge into the Right slot of total
    constexpr std::string_view k_FileWithoutRightEdge = R"json(
{
  "schema": "cc.AuthoredGraph",
  "version": 1,
  "data": {
    "nodes": [
      { "name": "ground", "kind": "field.Const", "params": { "Width": 4, "Height": 2, "Value": 1.0 } },
      { "name": "raised", "kind": "field.ScaleBias", "params": { "Scale": 2.0, "Bias": 0.5 } },
      { "name": "total", "kind": "field.Add" }
    ],
    "edges": [
      { "from": "ground", "output": "Field", "to": "raised", "slot": "Source" },
      { "from": "ground", "output": "Field", "to": "total", "slot": "Left" }
    ]
  }
}
)json";

    struct RampParams
    {
        uint32_t Width{ 0 };
        uint32_t Height{ 0 };
    };

    struct BytesParams
    {
    };

    // no inputs; a field whose sample i is i
    struct RampKind
    {
        static constexpr std::string_view Name{ "fieldtest.Ramp" };
        static constexpr uint32_t Version{ 1 };
        using Params = RampParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ cc::field::k_FieldOutput, cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // no inputs; four bytes that are not a field
    struct BytesKind
    {
        static constexpr std::string_view Name{ "fieldtest.Bytes" };
        static constexpr uint32_t Version{ 1 };
        using Params = BytesParams;
        static constexpr std::array<cc::Slot, 0> Slots{};
        static constexpr std::array Outputs
        {
            cc::Output{ cc::field::k_FieldOutput, cc::ArtifactKind::CpuBytes },
        };

        static foundation::Task<cc::KindResult> Run(const Params& params, cc::Inputs inputs, cc::Context& context);
    };

    // Returns a frozen registry that has the three field kinds, the Ramp and the Bytes.
    cc::KindRegistry MakeFieldTestRegistry();

    // Owns the registry, the recipe table, the scheduler and the executor of one test. The destructor shuts the
    // executor down and ticks until it is Idle. Declare each Root after the fixture, so that each Root dies first.
    class FieldFixture
    {
    public:
        FieldFixture();
        ~FieldFixture();
        FieldFixture(const FieldFixture&) = delete;
        FieldFixture& operator=(const FieldFixture&) = delete;

        // Reads a graph file that must be read. A refusal fails the test, and the file is empty.
        cc::GraphFile ReadOrFail(std::string_view text) const;
        std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> Ingest(const cc::AuthoredGraph& graph,
                                                                          cc::Quality quality);
        // A refusal fails the test, and the result is empty.
        std::vector<cc::RecipeId> IngestOrFail(const cc::AuthoredGraph& graph, cc::Quality quality);

        // Ticks at least once, and until the executor is Idle.
        void TickUntilIdle();
        cc::Executor& GetExecutor();
        const cc::Executor& GetExecutor() const;

    private:
        cc::KindRegistry registry;
        cc::RecipeTable recipes;
        foundation::Scheduler scheduler;
        std::unique_ptr<cc::Executor> executor;
    };

    uint32_t AddConst(cc::AuthoredGraph& graph, uint32_t width, uint32_t height, float value);
    uint32_t AddScaleBias(cc::AuthoredGraph& graph, float scale, float bias);
    uint32_t AddAdd(cc::AuthoredGraph& graph);
    uint32_t AddRamp(cc::AuthoredGraph& graph, uint32_t width, uint32_t height);
    uint32_t AddBytes(cc::AuthoredGraph& graph);

    // Connects the output Field of one node to a slot of another.
    void Link(cc::AuthoredGraph& graph, uint32_t from_node, uint32_t to_node, std::string_view slot);

    // The field in the first output of the view of a root. A view with no field fails the test.
    cc::field::FieldSamples FieldOf(const cc::Executor& executor, const cc::RootView& view);

    // The first diagnostic of the failure, or an empty one after a test failure.
    cc::Diagnostic FirstFailureDiagnostic(const cc::Executor& executor, const cc::ContentError& error);

    std::string ReplaceAll(std::string text, std::string_view from, std::string_view to);
    std::string ExampleFile(std::string_view scale);
    void ExpectEveryRampSample(const cc::field::FieldSamples& field, float scale, float bias);
}

namespace foundation::reflect
{
    template<>
    struct Describe<RampParams>
    {
        using T = RampParams;

        static constexpr std::string_view Name = "fieldtest.RampParams";
        static constexpr std::tuple Fields
        {
            Field<T, uint32_t>{ "Width", &T::Width },
            Field<T, uint32_t>{ "Height", &T::Height },
        };
    };

    template<>
    struct Describe<BytesParams>
    {
        static constexpr std::string_view Name = "fieldtest.BytesParams";
        static constexpr std::tuple Fields{};
    };
}

TEST(FieldArtifactTest, AFieldRoundTripsThroughItsBytes)
{
    cc::field::FieldSamples field;
    field.Width = 3;
    field.Height = 2;
    field.Samples = { 0.5f, -1.0f, 2.25f, 8.0f, 0.0f, 100.0f };

    const cc::Artifact artifact = cc::field::MakeFieldArtifact(field);

    EXPECT_EQ(artifact.Kind, cc::ArtifactKind::CpuBytes);
    EXPECT_EQ(artifact.Bytes.size(), 8u + 4u * 3u * 2u);

    const std::expected<cc::field::FieldSamples, std::string> read = cc::field::ReadFieldArtifact(artifact);

    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->Width, 3u);
    EXPECT_EQ(read->Height, 2u);
    EXPECT_EQ(read->Samples, field.Samples);
}

TEST(FieldArtifactTest, AnArtifactThatIsNotAFieldIsRefused)
{
    cc::field::FieldSamples field;
    field.Width = 2;
    field.Height = 2;
    field.Samples = { 1.0f, 2.0f, 3.0f, 4.0f };

    cc::Artifact wrongKind = cc::field::MakeFieldArtifact(field);
    wrongKind.Kind = cc::ArtifactKind::GpuBuffer;

    cc::Artifact sevenBytes;
    sevenBytes.Kind = cc::ArtifactKind::CpuBytes;
    sevenBytes.Bytes.assign(7, std::byte{ 0 });

    cc::Artifact tooLong = cc::field::MakeFieldArtifact(field);
    tooLong.Bytes.insert(tooLong.Bytes.end(), 4, std::byte{ 0 });

    const uint32_t hugeSide = 65536;
    cc::Artifact hugeHeader;
    hugeHeader.Kind = cc::ArtifactKind::CpuBytes;
    hugeHeader.Bytes.resize(8);
    std::memcpy(hugeHeader.Bytes.data(), &hugeSide, sizeof(uint32_t));
    std::memcpy(hugeHeader.Bytes.data() + sizeof(uint32_t), &hugeSide, sizeof(uint32_t));

    const cc::Artifact refused[]{ wrongKind, sevenBytes, tooLong, hugeHeader };

    for (const cc::Artifact& artifact : refused)
    {
        const std::expected<cc::field::FieldSamples, std::string> read = cc::field::ReadFieldArtifact(artifact);

        ASSERT_FALSE(read.has_value());
        EXPECT_THAT(read.error(), HasSubstr("not a field"));
    }
}

TEST(FieldKindsTest, ConstMakesAFieldOfOneValue)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t node = AddConst(graph, 3, 2, 1.5f);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 1u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[node], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::field::FieldSamples field = FieldOf(fixture.GetExecutor(), root.View());

    EXPECT_EQ(field.Width, 3u);
    EXPECT_EQ(field.Height, 2u);
    EXPECT_THAT(field.Samples, ElementsAre(1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f));
}

TEST(FieldKindsTest, ConstOfSizeZeroFailsAndItsDependentFailsWithTheSameOrigin)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t ground = AddConst(graph, 0, 2, 1.0f);
    const uint32_t raised = AddScaleBias(graph, 2.0f, 0.0f);
    Link(graph, ground, raised, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 2u);
    cc::Root dependentRoot{ fixture.GetExecutor() };
    cc::Root originRoot{ fixture.GetExecutor() };

    dependentRoot.Retarget(ids[raised], cc::Transience::Committed);
    originRoot.Retarget(ids[ground], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::RootView dependentView = dependentRoot.View();
    const cc::RootView originView = originRoot.View();

    ASSERT_EQ(originView.Error.Code, cc::ContentErrorCode::KindFailed);
    EXPECT_EQ(originView.Error.Origin, ids[ground]);
    EXPECT_EQ(dependentView.Error.Code, cc::ContentErrorCode::InputFailed);
    EXPECT_EQ(dependentView.Error.Origin, ids[ground]);

    const std::span<const cc::Diagnostic> diagnostics = fixture.GetExecutor().FailureDiagnostics(originView.Error.Failure);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Level, cc::Severity::Error);
    EXPECT_THAT(diagnostics[0].Message, HasSubstr("field.Const"));
}

TEST(FieldKindsTest, ScaleBiasMultipliesThenAdds)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t ramp = AddRamp(graph, 4, 3);
    const uint32_t scaled = AddScaleBias(graph, 3.0f, 0.5f);
    Link(graph, ramp, scaled, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 2u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[scaled], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::field::FieldSamples field = FieldOf(fixture.GetExecutor(), root.View());

    EXPECT_EQ(field.Width, 4u);
    EXPECT_EQ(field.Height, 3u);
    ExpectEveryRampSample(field, 3.0f, 0.5f);
}

TEST(FieldKindsTest, AddSumsTwoFieldsOfOneSize)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t ramp = AddRamp(graph, 4, 3);
    const uint32_t ten = AddConst(graph, 4, 3, 10.0f);
    const uint32_t sum = AddAdd(graph);
    Link(graph, ramp, sum, "Left");
    Link(graph, ten, sum, "Right");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 3u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[sum], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::field::FieldSamples field = FieldOf(fixture.GetExecutor(), root.View());

    EXPECT_EQ(field.Width, 4u);
    EXPECT_EQ(field.Height, 3u);
    ExpectEveryRampSample(field, 1.0f, 10.0f);
}

TEST(FieldKindsTest, AddFailsForTwoFieldsOfDifferentSizesAndTheMessageGivesBothSizes)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t small = AddRamp(graph, 2, 2);
    const uint32_t large = AddConst(graph, 4, 4, 1.0f);
    const uint32_t sum = AddAdd(graph);
    Link(graph, small, sum, "Left");
    Link(graph, large, sum, "Right");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 3u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[sum], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::RootView view = root.View();
    const cc::Diagnostic diagnostic = FirstFailureDiagnostic(fixture.GetExecutor(), view.Error);

    EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::KindFailed);
    EXPECT_EQ(view.Error.Origin, ids[sum]);
    EXPECT_THAT(diagnostic.Message, HasSubstr("2 x 2"));
    EXPECT_THAT(diagnostic.Message, HasSubstr("4 x 4"));
}

TEST(FieldKindsTest, AnInputThatIsNotAFieldFailsTheKindWithTheNameOfTheSlot)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t bytes = AddBytes(graph);
    const uint32_t scaled = AddScaleBias(graph, 2.0f, 0.0f);
    Link(graph, bytes, scaled, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 2u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[scaled], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::RootView view = root.View();
    const cc::Diagnostic diagnostic = FirstFailureDiagnostic(fixture.GetExecutor(), view.Error);

    EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::KindFailed);
    EXPECT_THAT(diagnostic.Message, StartsWith("field.ScaleBias: Source: "));
    EXPECT_THAT(diagnostic.Message, HasSubstr("not a field"));
}

TEST(FieldKindsTest, RegisteringTheFieldKindsTwiceReportsEachKindTheSecondTime)
{
    cc::KindRegistry registry;

    const std::vector<cc::Diagnostic> first = cc::field::RegisterFieldKinds(registry);
    const std::vector<cc::Diagnostic> second = cc::field::RegisterFieldKinds(registry);

    EXPECT_TRUE(first.empty());
    EXPECT_EQ(registry.Count(), 3u);
    EXPECT_THAT(cctest::CodesOf(second), ElementsAre(cc::DiagnosticCode::DuplicateKindName,
                                                     cc::DiagnosticCode::DuplicateKindName,
                                                     cc::DiagnosticCode::DuplicateKindName));
}

TEST(FieldChainTest, AFileReadsIngestsAndRunsToAField)
{
    FieldFixture fixture;
    const cc::GraphFile file = fixture.ReadOrFail(ExampleFile("2.0"));
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(file.Graph, cc::Quality::Preview);
    ASSERT_EQ(ids.size(), 3u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[2], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::RootView view = root.View();
    const cc::field::FieldSamples field = FieldOf(fixture.GetExecutor(), view);

    EXPECT_FALSE(view.Stale);
    EXPECT_EQ(view.Shown, ids[2]);
    EXPECT_EQ(field.Width, 4u);
    EXPECT_EQ(field.Height, 2u);
    ASSERT_EQ(field.Samples.size(), 8u);
    EXPECT_THAT(field.Samples, testing::Each(3.5f));
    EXPECT_EQ(fixture.GetExecutor().RunsStarted(), uint64_t{ 3 });
}

TEST(FieldChainTest, AnEditRunsOnlyTheNodesThatChanged)
{
    FieldFixture fixture;
    const cc::GraphFile before = fixture.ReadOrFail(ExampleFile("2.0"));
    const std::vector<cc::RecipeId> idsBefore = fixture.IngestOrFail(before.Graph, cc::Quality::Preview);
    ASSERT_EQ(idsBefore.size(), 3u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(idsBefore[2], cc::Transience::Committed);
    fixture.TickUntilIdle();

    EXPECT_EQ(fixture.GetExecutor().RunsStarted(), uint64_t{ 3 });

    const cc::GraphFile after = fixture.ReadOrFail(ExampleFile("3.0"));
    const std::vector<cc::RecipeId> idsAfter = fixture.IngestOrFail(after.Graph, cc::Quality::Preview);
    ASSERT_EQ(idsAfter.size(), 3u);

    EXPECT_EQ(idsAfter[0], idsBefore[0]);
    EXPECT_NE(idsAfter[1], idsBefore[1]);
    EXPECT_NE(idsAfter[2], idsBefore[2]);

    root.Retarget(idsAfter[2], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::RootView view = root.View();
    const cc::field::FieldSamples field = FieldOf(fixture.GetExecutor(), view);

    EXPECT_EQ(fixture.GetExecutor().RunsStarted(), uint64_t{ 5 });
    EXPECT_FALSE(view.Stale);
    EXPECT_EQ(view.Shown, idsAfter[2]);
    EXPECT_THAT(field.Samples, testing::Each(4.5f));
}

TEST(FieldChainTest, RenamingANodeChangesNoRecipeAndRunsNothing)
{
    FieldFixture fixture;
    const cc::GraphFile before = fixture.ReadOrFail(ExampleFile("2.0"));
    const std::vector<cc::RecipeId> idsBefore = fixture.IngestOrFail(before.Graph, cc::Quality::Preview);
    ASSERT_EQ(idsBefore.size(), 3u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(idsBefore[2], cc::Transience::Committed);
    fixture.TickUntilIdle();

    const cc::GraphFile after = fixture.ReadOrFail(ReplaceAll(ExampleFile("2.0"), "\"ground\"", "\"base\""));
    const std::vector<cc::RecipeId> idsAfter = fixture.IngestOrFail(after.Graph, cc::Quality::Preview);

    ASSERT_EQ(after.NodeNames.size(), 3u);
    EXPECT_EQ(after.NodeNames[0], "base");
    EXPECT_EQ(idsAfter, idsBefore);

    root.Retarget(idsAfter[2], cc::Transience::Committed);
    fixture.TickUntilIdle();

    EXPECT_EQ(fixture.GetExecutor().RunsStarted(), uint64_t{ 3 });
    EXPECT_FALSE(root.View().Stale);
}

TEST(FieldChainTest, AFileThatReadsButDoesNotIngestGivesDiagnosticsThatTheNodeNamesExplain)
{
    FieldFixture fixture;
    const cc::GraphFile file = fixture.ReadOrFail(k_FileWithoutRightEdge);
    const std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> result =
        fixture.Ingest(file.Graph, cc::Quality::Preview);

    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error().size(), 1u);

    const cc::Diagnostic& diagnostic = result.error()[0];

    EXPECT_EQ(diagnostic.Code, cc::DiagnosticCode::MissingRequiredSlot);
    ASSERT_EQ(diagnostic.Nodes.size(), 1u);
    ASSERT_LT(diagnostic.Nodes[0], file.NodeNames.size());
    EXPECT_EQ(file.NodeNames[diagnostic.Nodes[0]], "total");
}

TEST(FieldChainTest, ARunsStartedCountIgnoresAReadyRowThatIsCancelledBeforeItsRunStarts)
{
    FieldFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t node = AddConst(graph, 2, 2, 1.0f);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph, cc::Quality::Final);
    ASSERT_EQ(ids.size(), 1u);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[node], cc::Transience::Committed);
    root.Clear();
    fixture.TickUntilIdle();

    EXPECT_EQ(fixture.GetExecutor().RunsStarted(), uint64_t{ 0 });
    EXPECT_EQ(fixture.GetExecutor().Artifacts().LiveCount(), 0);
}

namespace
{
    foundation::Task<cc::KindResult> RampKind::Run(const Params& params, cc::Inputs, cc::Context& context)
    {
        cc::field::FieldSamples field;
        field.Width = params.Width;
        field.Height = params.Height;
        field.Samples.resize(static_cast<size_t>(params.Width) * params.Height);

        for (size_t index = 0; index < field.Samples.size(); ++index)
        {
            field.Samples[index] = static_cast<float>(index);
        }

        context.SetOutput(0, cc::field::MakeFieldArtifact(field));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    foundation::Task<cc::KindResult> BytesKind::Run(const Params&, cc::Inputs, cc::Context& context)
    {
        cc::Artifact artifact;
        artifact.Kind = cc::ArtifactKind::CpuBytes;
        artifact.Bytes.assign(4, std::byte{ 7 });

        context.SetOutput(0, std::move(artifact));

        co_return cc::KindResult{ .Status = cc::KindStatus::Succeeded };
    }

    cc::KindRegistry MakeFieldTestRegistry()
    {
        cc::KindRegistry registry;

        const std::vector<cc::Diagnostic> fieldDiagnostics = cc::field::RegisterFieldKinds(registry);
        const bool rampRegistered = registry.Register<RampKind>().has_value();
        const bool bytesRegistered = registry.Register<BytesKind>().has_value();

        EXPECT_TRUE(fieldDiagnostics.empty());
        EXPECT_TRUE(rampRegistered);
        EXPECT_TRUE(bytesRegistered);

        registry.Freeze();

        return registry;
    }

    FieldFixture::FieldFixture() :
      registry{ MakeFieldTestRegistry() },
      executor{ std::make_unique<cc::Executor>(scheduler, registry, recipes) }
    {}

    FieldFixture::~FieldFixture()
    {
        executor->Shutdown();

        for (int32_t tick = 0; tick < k_MaxTicks && !executor->Idle(); ++tick)
        {
            scheduler.Tick();
        }

        // one more, so that the scheduler destroys the roots of the runs that finished
        scheduler.Tick();

        EXPECT_TRUE(executor->Idle());

        executor.reset();
    }

    cc::GraphFile FieldFixture::ReadOrFail(std::string_view text) const
    {
        std::expected<cc::GraphFile, std::vector<cc::Diagnostic>> result = cc::ReadGraphJson(text, registry);

        if (!result.has_value())
        {
            ADD_FAILURE() << "the graph file was refused with " << result.error().size() << " diagnostics";

            return {};
        }

        return std::move(*result);
    }

    std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> FieldFixture::Ingest(const cc::AuthoredGraph& graph,
                                                                                    cc::Quality quality)
    {
        return cc::Ingest(graph, quality, registry, recipes);
    }

    std::vector<cc::RecipeId> FieldFixture::IngestOrFail(const cc::AuthoredGraph& graph, cc::Quality quality)
    {
        std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> result = Ingest(graph, quality);

        if (!result.has_value())
        {
            ADD_FAILURE() << "the graph was refused with " << result.error().size() << " diagnostics";

            return {};
        }

        return std::move(result->NodeRecipes);
    }

    void FieldFixture::TickUntilIdle()
    {
        for (int32_t tick = 0; tick < k_MaxTicks; ++tick)
        {
            scheduler.Tick();

            if (executor->Idle())
            {
                return;
            }
        }

        ADD_FAILURE() << "the executor was not Idle after " << k_MaxTicks << " ticks";
    }

    cc::Executor& FieldFixture::GetExecutor()
    {
        return *executor;
    }

    const cc::Executor& FieldFixture::GetExecutor() const
    {
        return *executor;
    }

    uint32_t AddConst(cc::AuthoredGraph& graph, uint32_t width, uint32_t height, float value)
    {
        const cc::field::ConstParams params{ width, height, value };

        return cctest::AddNode(graph, cc::field::ConstKind::Name, cc::ParamsBox::Make(params));
    }

    uint32_t AddScaleBias(cc::AuthoredGraph& graph, float scale, float bias)
    {
        const cc::field::ScaleBiasParams params{ scale, bias };

        return cctest::AddNode(graph, cc::field::ScaleBiasKind::Name, cc::ParamsBox::Make(params));
    }

    uint32_t AddAdd(cc::AuthoredGraph& graph)
    {
        return cctest::AddNode(graph, cc::field::AddKind::Name, cc::ParamsBox::Make(cc::field::AddParams{}));
    }

    uint32_t AddRamp(cc::AuthoredGraph& graph, uint32_t width, uint32_t height)
    {
        return cctest::AddNode(graph, RampKind::Name, cc::ParamsBox::Make(RampParams{ width, height }));
    }

    uint32_t AddBytes(cc::AuthoredGraph& graph)
    {
        return cctest::AddNode(graph, BytesKind::Name, cc::ParamsBox::Make(BytesParams{}));
    }

    void Link(cc::AuthoredGraph& graph, uint32_t from_node, uint32_t to_node, std::string_view slot)
    {
        cctest::Connect(graph, from_node, cc::field::k_FieldOutput, to_node, slot);
    }

    cc::field::FieldSamples FieldOf(const cc::Executor& executor, const cc::RootView& view)
    {
        const cc::ArtifactTable& artifacts = executor.Artifacts();
        const std::span<const cc::ArtifactRef> outputs = view.Outputs;

        if (outputs.empty())
        {
            ADD_FAILURE() << "the root shows no output";

            return {};
        }

        const cc::ArtifactRef reference = outputs[0];
        std::expected<cc::field::FieldSamples, std::string> field =
            cc::field::ReadFieldArtifact(artifacts.Get(reference));

        if (!field.has_value())
        {
            ADD_FAILURE() << "the output is not a field: " << field.error();

            return {};
        }

        return std::move(*field);
    }

    cc::Diagnostic FirstFailureDiagnostic(const cc::Executor& executor, const cc::ContentError& error)
    {
        if (error.Code == cc::ContentErrorCode::Invalid)
        {
            ADD_FAILURE() << "the root shows no failure";

            return {};
        }

        const std::span<const cc::Diagnostic> diagnostics = executor.FailureDiagnostics(error.Failure);

        if (diagnostics.empty())
        {
            ADD_FAILURE() << "the failure has no diagnostic";

            return {};
        }

        return diagnostics[0];
    }

    std::string ReplaceAll(std::string text, std::string_view from, std::string_view to)
    {
        size_t position = text.find(from);

        while (position != std::string::npos)
        {
            text.replace(position, from.size(), to);
            position = text.find(from, position + to.size());
        }

        return text;
    }

    std::string ExampleFile(std::string_view scale)
    {
        return ReplaceAll(std::string{ k_ExampleFile }, "@SCALE@", scale);
    }

    void ExpectEveryRampSample(const cc::field::FieldSamples& field, float scale, float bias)
    {
        ASSERT_EQ(field.Samples.size(), static_cast<size_t>(field.Width) * field.Height);

        for (size_t index = 0; index < field.Samples.size(); ++index)
        {
            EXPECT_EQ(field.Samples[index], static_cast<float>(index) * scale + bias) << "sample " << index;
        }
    }
}
