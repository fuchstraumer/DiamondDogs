#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test_executor_kinds.hpp"
#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "Executor.hpp"
#include "ExecutorTypes.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "RecipeTable.hpp"
#include "async/Scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using testing::ElementsAre;

namespace
{
    constexpr int32_t k_MaxTeardownTicks = 100;

    // Owns the registry, the recipe table, the scheduler and the executor of one test. Its destructor does the
    // teardown that the executor needs: Shutdown, then ticks until Idle, then the executor is destroyed. Declare
    // every Root after the fixture, so that each Root is destroyed first.
    class ExecutorFixture
    {
    public:
        ExecutorFixture();
        ~ExecutorFixture();
        ExecutorFixture(const ExecutorFixture&) = delete;
        ExecutorFixture& operator=(const ExecutorFixture&) = delete;

        // Ingests a graph that must be accepted, and returns the recipe of each authored node. A refusal fails the
        // test and ends the process, because the ids would be empty.
        std::vector<cc::RecipeId> IngestOrFail(const cc::AuthoredGraph& graph);

        void Tick();
        cc::Executor& GetExecutor();
        const cc::Executor& GetExecutor() const;

    private:
        cc::KindRegistry registry;
        cc::RecipeTable recipes;
        foundation::Scheduler scheduler;
        std::unique_ptr<cc::Executor> executor;
    };

    // Each function adds one node and returns its authored index.
    uint32_t AddNumber(cc::AuthoredGraph& graph, float value);
    uint32_t AddOffset(cc::AuthoredGraph& graph, float delta);
    uint32_t AddSlow(cc::AuthoredGraph& graph, float delta);
    uint32_t AddSum(cc::AuthoredGraph& graph, int32_t tag);
    uint32_t AddPair(cc::AuthoredGraph& graph, int32_t tag);
    uint32_t AddFail(cc::AuthoredGraph& graph, int32_t tag);
    uint32_t AddForget(cc::AuthoredGraph& graph, int32_t tag);

    // Connects the output "Value" of one node to a slot of another.
    void LinkValue(cc::AuthoredGraph& graph, uint32_t from_node, uint32_t to_node, std::string_view slot);

    // The value of the output at the index in the view of a root.
    float ValueOf(const cc::Executor& executor, const cc::RootView& view, size_t output);

    // nodes: Number 1, Offset +10 from it, Offset +20 from it, Sum of the two Offsets (left is the +10 one)
    struct DiamondNodes
    {
        uint32_t Source{ 0 };
        uint32_t Left{ 0 };
        uint32_t Right{ 0 };
        uint32_t Join{ 0 };
    };

    DiamondNodes AddDiamond(cc::AuthoredGraph& graph);

    constexpr float k_DiamondValue = 32.0f;
}

TEST(ExecutorTest, ADiamondRunsEachNodeOnceInOneTick)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const DiamondNodes nodes = AddDiamond(graph);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[nodes.Join], cc::Transience::Committed);
    fixture.Tick();

    const cctest::RunCounts& counts = cctest::Counts();
    const cc::RootView view = root.View();

    EXPECT_EQ(counts.Number, 1);
    EXPECT_EQ(counts.Offset, 2);
    EXPECT_EQ(counts.Sum, 1);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
    EXPECT_FALSE(view.Stale);
    EXPECT_EQ(view.Shown, ids[nodes.Join]);
    EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::Invalid);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), k_DiamondValue);
}

TEST(ExecutorTest, ARetargetToAFinishedRecipeRunsNothingAndIsReadyAtOnce)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const DiamondNodes nodes = AddDiamond(graph);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root first{ fixture.GetExecutor() };
    cc::Root second{ fixture.GetExecutor() };

    first.Retarget(ids[nodes.Join], cc::Transience::Committed);
    fixture.Tick();
    cctest::ResetRuns();

    first.Retarget(ids[nodes.Join], cc::Transience::Committed);
    second.Retarget(ids[nodes.Join], cc::Transience::Committed);

    const cc::RootView firstView = first.View();
    const cc::RootView secondView = second.View();

    EXPECT_FALSE(firstView.Stale);
    EXPECT_FALSE(secondView.Stale);
    ASSERT_EQ(secondView.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), secondView, 0), k_DiamondValue);

    fixture.Tick();

    const cctest::RunCounts& counts = cctest::Counts();

    EXPECT_EQ(counts.Number + counts.Offset + counts.Sum, 0);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
}

TEST(ExecutorTest, AFailedNodeIsNotRetriedAndItsDependentInheritsTheOrigin)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t failing = AddFail(graph, 1);
    const uint32_t dependent = AddOffset(graph, 5.0f);
    const uint32_t other = AddNumber(graph, 7.0f);
    LinkValue(graph, failing, dependent, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root viewport{ fixture.GetExecutor() };
    cc::Root inspector{ fixture.GetExecutor() };

    viewport.Retarget(ids[dependent], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView failedView = viewport.View();

    EXPECT_EQ(cctest::Counts().Fail, 1);
    EXPECT_EQ(cctest::Counts().Offset, 0);
    EXPECT_EQ(failedView.Error.Code, cc::ContentErrorCode::InputFailed);
    EXPECT_EQ(failedView.Error.Origin, ids[failing]);
    EXPECT_TRUE(failedView.Outputs.empty());

    viewport.Retarget(ids[other], cc::Transience::Committed);
    fixture.Tick();
    viewport.Retarget(ids[dependent], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView againView = viewport.View();

    EXPECT_EQ(cctest::Counts().Fail, 1);
    EXPECT_EQ(cctest::Counts().Offset, 0);
    EXPECT_EQ(againView.Error.Code, cc::ContentErrorCode::InputFailed);
    EXPECT_EQ(againView.Error.Origin, ids[failing]);

    inspector.Retarget(ids[failing], cc::Transience::Committed);

    const cc::RootView originView = inspector.View();

    EXPECT_EQ(originView.Error.Code, cc::ContentErrorCode::KindFailed);
    EXPECT_EQ(originView.Error.Origin, ids[failing]);
    EXPECT_EQ(originView.Error.Failure, againView.Error.Failure);
    EXPECT_EQ(cctest::Counts().Fail, 1);

    const std::span<const cc::Diagnostic> diagnostics = fixture.GetExecutor().FailureDiagnostics(originView.Error.Failure);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Message, "exec.Fail always fails");
}

TEST(ExecutorTest, ANewConsumerOfAFailedRecipeFailsAtOnceInRetarget)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t failing = AddFail(graph, 1);
    const uint32_t firstConsumer = AddOffset(graph, 5.0f);
    const uint32_t secondConsumer = AddOffset(graph, 6.0f);
    LinkValue(graph, failing, firstConsumer, "Source");
    LinkValue(graph, failing, secondConsumer, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root first{ fixture.GetExecutor() };
    cc::Root second{ fixture.GetExecutor() };

    first.Retarget(ids[firstConsumer], cc::Transience::Committed);
    fixture.Tick();
    second.Retarget(ids[secondConsumer], cc::Transience::Committed);

    const cc::RootView view = second.View();

    EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::InputFailed);
    EXPECT_EQ(view.Error.Origin, ids[failing]);
    EXPECT_EQ(view.Error.Failure, first.View().Error.Failure);
    EXPECT_TRUE(fixture.GetExecutor().Idle());

    fixture.Tick();

    EXPECT_EQ(cctest::Counts().Fail, 1);
    EXPECT_EQ(cctest::Counts().Offset, 0);
}

TEST(ExecutorTest, ARetargetKeepsTheRowsThatTheOldAndTheNewTargetShare)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t number = AddNumber(graph, 100.0f);
    const uint32_t slow = AddSlow(graph, 0.5f);
    const uint32_t left = AddOffset(graph, 10.0f);
    const uint32_t right = AddOffset(graph, 20.0f);
    const uint32_t oldJoin = AddSum(graph, 1);
    const uint32_t newJoin = AddSum(graph, 2);
    LinkValue(graph, number, slow, "Source");
    LinkValue(graph, slow, left, "Source");
    LinkValue(graph, slow, right, "Source");
    LinkValue(graph, left, oldJoin, "Left");
    LinkValue(graph, right, oldJoin, "Right");
    // the same inputs in the other slot order, so that a cancel and a restart show in the order of the runs
    LinkValue(graph, right, newJoin, "Left");
    LinkValue(graph, left, newJoin, "Right");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[oldJoin], cc::Transience::Committed);
    fixture.Tick();

    EXPECT_EQ(cctest::Counts().Slow, 1);
    EXPECT_FALSE(fixture.GetExecutor().Idle());

    root.Retarget(ids[newJoin], cc::Transience::Committed);
    fixture.Tick();

    const cctest::RunCounts& counts = cctest::Counts();
    const cc::RootView view = root.View();

    EXPECT_EQ(counts.Number, 1);
    EXPECT_EQ(counts.Slow, 1);
    EXPECT_EQ(counts.Offset, 2);
    EXPECT_EQ(counts.Sum, 1);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
    EXPECT_FALSE(view.Stale);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), 231.0f);
    EXPECT_THAT(cctest::RunLog(), ElementsAre(100.0f, 0.5f, 20.0f, 10.0f, 2.0f));
}

TEST(ExecutorTest, AWaitingRowThatLosesItsOnlyRootStartsNoWork)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t number = AddNumber(graph, 1.0f);
    const uint32_t slow = AddSlow(graph, 1.0f);
    const uint32_t first = AddOffset(graph, 10.0f);
    const uint32_t second = AddOffset(graph, 20.0f);
    LinkValue(graph, number, slow, "Source");
    LinkValue(graph, slow, first, "Source");
    LinkValue(graph, first, second, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[second], cc::Transience::Committed);
    fixture.Tick();

    EXPECT_EQ(cctest::Counts().Slow, 1);
    EXPECT_FALSE(fixture.GetExecutor().Idle());

    root.Clear();
    fixture.Tick();

    EXPECT_EQ(cctest::Counts().Slow, 1);
    EXPECT_EQ(cctest::Counts().Offset, 0);
    EXPECT_TRUE(fixture.GetExecutor().Idle());

    // the Slow row ran to the end and kept its result
    root.Retarget(ids[slow], cc::Transience::Committed);

    const cc::RootView view = root.View();

    EXPECT_FALSE(view.Stale);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), 2.0f);
    EXPECT_EQ(cctest::Counts().Slow, 1);
}

TEST(ExecutorTest, AReadyRowThatIsWantedAgainIsSpawnedOnce)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t number = AddNumber(graph, 3.0f);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[number], cc::Transience::Committed);
    root.Clear();
    root.Retarget(ids[number], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView view = root.View();

    EXPECT_EQ(cctest::Counts().Number, 1);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
    EXPECT_FALSE(view.Stale);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), 3.0f);
}

TEST(ExecutorTest, AReadyRowThatNobodyWantsAnymoreDoesNoWork)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t number = AddNumber(graph, 3.0f);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[number], cc::Transience::Committed);
    root.Clear();
    fixture.Tick();

    EXPECT_EQ(cctest::Counts().Number, 0);
    EXPECT_TRUE(fixture.GetExecutor().Idle());

    // the cancelled row is Idle again, so a new request runs it
    root.Retarget(ids[number], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView view = root.View();

    EXPECT_EQ(cctest::Counts().Number, 1);
    EXPECT_FALSE(view.Stale);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), 3.0f);
}

TEST(ExecutorTest, ARootKeepsItsLastGoodOutputsWhileTheNewTargetFails)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t number = AddNumber(graph, 5.0f);
    const uint32_t failing = AddFail(graph, 1);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[number], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView goodView = root.View();

    ASSERT_EQ(goodView.Outputs.size(), 1u);
    const cc::ArtifactRef goodOutput = goodView.Outputs[0];

    root.Retarget(ids[failing], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView failedView = root.View();

    EXPECT_TRUE(failedView.Stale);
    EXPECT_EQ(failedView.Shown, ids[number]);
    EXPECT_EQ(failedView.Target, ids[failing]);
    EXPECT_EQ(failedView.Error.Code, cc::ContentErrorCode::KindFailed);
    EXPECT_EQ(failedView.Error.Origin, ids[failing]);
    ASSERT_EQ(failedView.Outputs.size(), 1u);
    EXPECT_EQ(failedView.Outputs[0], goodOutput);
    EXPECT_TRUE(fixture.GetExecutor().Artifacts().IsLive(goodOutput));
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), failedView, 0), 5.0f);
}

TEST(ExecutorTest, AKindThatSetsNoOutputFailsWithMissingOutput)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t forgetful = AddForget(graph, 1);
    const uint32_t dependent = AddOffset(graph, 1.0f);
    LinkValue(graph, forgetful, dependent, "Source");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };
    cc::Root downstream{ fixture.GetExecutor() };

    root.Retarget(ids[forgetful], cc::Transience::Committed);
    downstream.Retarget(ids[dependent], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView view = root.View();
    const cc::RootView downstreamView = downstream.View();

    EXPECT_EQ(cctest::Counts().Forget, 1);
    EXPECT_EQ(cctest::Counts().Offset, 0);
    EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::MissingOutput);
    EXPECT_EQ(view.Error.Origin, ids[forgetful]);
    EXPECT_TRUE(view.Outputs.empty());
    EXPECT_EQ(downstreamView.Error.Code, cc::ContentErrorCode::InputFailed);
    EXPECT_EQ(downstreamView.Error.Origin, ids[forgetful]);
    EXPECT_EQ(fixture.GetExecutor().Artifacts().LiveCount(), 0);
}

TEST(ExecutorTest, ANodeThatReadsTwoOutputsOfOneSourceRunsOnce)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t pair = AddPair(graph, 3);
    const uint32_t sum = AddSum(graph, 1);
    cctest::Connect(graph, pair, "A", sum, "Left");
    cctest::Connect(graph, pair, "B", sum, "Right");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[sum], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView view = root.View();

    EXPECT_EQ(cctest::Counts().Pair, 1);
    EXPECT_EQ(cctest::Counts().Sum, 1);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
    EXPECT_FALSE(view.Stale);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), 6.5f);
}

TEST(ExecutorTest, ANodeThatReadsTwoOutputsOfAFailedSourceFailsOnce)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t forgetful = AddForget(graph, 1);
    const uint32_t sum = AddSum(graph, 1);
    LinkValue(graph, forgetful, sum, "Left");
    LinkValue(graph, forgetful, sum, "Right");
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[sum], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView view = root.View();

    EXPECT_EQ(cctest::Counts().Sum, 0);
    EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::InputFailed);
    EXPECT_EQ(view.Error.Origin, ids[forgetful]);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
}

// More rows than a RecipeList holds inline, so the walk lists of failDependents and drainUnwants move to the heap.
TEST(ExecutorTest, AFailureThatFansOutPastTheInlineListSizeFailsEveryRowBelowIt)
{
    constexpr int32_t k_FanOut = 24;

    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const uint32_t failing = AddFail(graph, 1);
    std::vector<uint32_t> leaves;

    // two levels, so the cascade must carry the failure through rows that it pushed itself
    for (int32_t branch = 0; branch < k_FanOut; ++branch)
    {
        const uint32_t middle = AddOffset(graph, static_cast<float>(branch));
        const uint32_t leaf = AddOffset(graph, static_cast<float>(branch) + 0.5f);
        LinkValue(graph, failing, middle, "Source");
        LinkValue(graph, middle, leaf, "Source");
        leaves.push_back(leaf);
    }

    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    std::vector<std::unique_ptr<cc::Root>> roots;

    for (const uint32_t leaf : leaves)
    {
        roots.push_back(std::make_unique<cc::Root>(fixture.GetExecutor()));
        roots.back()->Retarget(ids[leaf], cc::Transience::Committed);
    }

    fixture.Tick();

    EXPECT_EQ(cctest::Counts().Fail, 1);
    EXPECT_EQ(cctest::Counts().Offset, 0);
    EXPECT_TRUE(fixture.GetExecutor().Idle());

    for (const std::unique_ptr<cc::Root>& root : roots)
    {
        const cc::RootView view = root->View();

        EXPECT_EQ(view.Error.Code, cc::ContentErrorCode::InputFailed);
        EXPECT_EQ(view.Error.Origin, ids[failing]);
    }

    // the roots go before the fixture, which tears the executor down
    roots.clear();
}

TEST(ExecutorTest, AChainOfTenThousandRowsRunsInOneTickWithoutOverflowingTheStack)
{
    constexpr int32_t k_ChainLength = 10000;

    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    uint32_t previous = AddNumber(graph, 0.0f);

    for (int32_t link = 0; link < k_ChainLength; ++link)
    {
        const uint32_t next = AddOffset(graph, 1.0f);
        LinkValue(graph, previous, next, "Source");
        previous = next;
    }

    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root root{ fixture.GetExecutor() };

    root.Retarget(ids[previous], cc::Transience::Committed);
    fixture.Tick();

    const cc::RootView view = root.View();

    EXPECT_EQ(cctest::Counts().Number, 1);
    EXPECT_EQ(cctest::Counts().Offset, k_ChainLength);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
    EXPECT_FALSE(view.Stale);
    ASSERT_EQ(view.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), view, 0), static_cast<float>(k_ChainLength));
}

TEST(ExecutorTest, AfterEveryRootIsDestroyedTheTableHoldsTheOutputsOfTheSucceededRows)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const DiamondNodes nodes = AddDiamond(graph);
    const uint32_t pair = AddPair(graph, 3);
    const uint32_t failing = AddFail(graph, 1);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    constexpr int32_t k_DiamondOutputs = 4;
    constexpr int32_t k_PairOutputs = 2;
    constexpr int32_t k_SucceededOutputs = k_DiamondOutputs + k_PairOutputs;

    {
        cc::Root diamondRoot{ fixture.GetExecutor() };
        cc::Root pairRoot{ fixture.GetExecutor() };
        cc::Root failingRoot{ fixture.GetExecutor() };

        diamondRoot.Retarget(ids[nodes.Join], cc::Transience::Committed);
        pairRoot.Retarget(ids[pair], cc::Transience::Committed);
        failingRoot.Retarget(ids[failing], cc::Transience::Committed);
        fixture.Tick();

        EXPECT_EQ(fixture.GetExecutor().Artifacts().LiveCount(), k_SucceededOutputs);
        EXPECT_EQ(diamondRoot.View().Outputs.size(), 1u);
        EXPECT_EQ(pairRoot.View().Outputs.size(), 2u);
        EXPECT_TRUE(failingRoot.View().Outputs.empty());
    }

    // the rows keep their cache, and the roots no longer show it
    EXPECT_EQ(fixture.GetExecutor().Artifacts().LiveCount(), k_SucceededOutputs);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
}

TEST(ExecutorTest, ARetargetWorksWithCommittedAndWithTransient)
{
    cctest::ResetRuns();
    ExecutorFixture fixture;
    cc::AuthoredGraph graph;
    const DiamondNodes nodes = AddDiamond(graph);
    const uint32_t number = AddNumber(graph, 9.0f);
    const std::vector<cc::RecipeId> ids = fixture.IngestOrFail(graph);
    cc::Root committed{ fixture.GetExecutor() };
    cc::Root transient{ fixture.GetExecutor() };

    committed.Retarget(ids[nodes.Join], cc::Transience::Committed);
    transient.Retarget(ids[number], cc::Transience::Transient);
    fixture.Tick();

    const cc::RootView committedView = committed.View();
    const cc::RootView transientView = transient.View();

    ASSERT_EQ(committedView.Outputs.size(), 1u);
    ASSERT_EQ(transientView.Outputs.size(), 1u);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), committedView, 0), k_DiamondValue);
    EXPECT_EQ(ValueOf(fixture.GetExecutor(), transientView, 0), 9.0f);

    // a Transient request for a row that a Committed request started, and the other way round
    transient.Retarget(ids[nodes.Join], cc::Transience::Transient);
    committed.Retarget(ids[number], cc::Transience::Committed);

    EXPECT_FALSE(transient.View().Stale);
    EXPECT_FALSE(committed.View().Stale);
    EXPECT_TRUE(fixture.GetExecutor().Idle());
}

namespace
{
    ExecutorFixture::ExecutorFixture() :
      registry{ cctest::MakeFrozenExecutorRegistry() },
      executor{ std::make_unique<cc::Executor>(scheduler, registry, recipes) }
    {}

    ExecutorFixture::~ExecutorFixture()
    {
        executor->Shutdown();

        for (int32_t tick = 0; tick < k_MaxTeardownTicks && !executor->Idle(); ++tick)
        {
            scheduler.Tick();
        }

        // one more, so that the scheduler destroys the roots of the runs that finished
        scheduler.Tick();

        EXPECT_TRUE(executor->Idle());

        executor.reset();
    }

    std::vector<cc::RecipeId> ExecutorFixture::IngestOrFail(const cc::AuthoredGraph& graph)
    {
        std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> result =
            cc::Ingest(graph, cc::Quality::Final, registry, recipes);

        if (!result.has_value())
        {
            ADD_FAILURE() << "the test graph was refused";
            std::abort();
        }

        return std::move(result->NodeRecipes);
    }

    void ExecutorFixture::Tick()
    {
        scheduler.Tick();
    }

    cc::Executor& ExecutorFixture::GetExecutor()
    {
        return *executor;
    }

    const cc::Executor& ExecutorFixture::GetExecutor() const
    {
        return *executor;
    }

    uint32_t AddNumber(cc::AuthoredGraph& graph, float value)
    {
        return cctest::AddNode(graph, cctest::NumberKind::Name, cc::ParamsBox::Make(cctest::NumberParams{ value }));
    }

    uint32_t AddOffset(cc::AuthoredGraph& graph, float delta)
    {
        return cctest::AddNode(graph, cctest::OffsetKind::Name, cc::ParamsBox::Make(cctest::DeltaParams{ delta }));
    }

    uint32_t AddSlow(cc::AuthoredGraph& graph, float delta)
    {
        return cctest::AddNode(graph, cctest::SlowKind::Name, cc::ParamsBox::Make(cctest::DeltaParams{ delta }));
    }

    uint32_t AddSum(cc::AuthoredGraph& graph, int32_t tag)
    {
        return cctest::AddNode(graph, cctest::SumKind::Name, cc::ParamsBox::Make(cctest::TagParams{ tag }));
    }

    uint32_t AddPair(cc::AuthoredGraph& graph, int32_t tag)
    {
        return cctest::AddNode(graph, cctest::PairKind::Name, cc::ParamsBox::Make(cctest::TagParams{ tag }));
    }

    uint32_t AddFail(cc::AuthoredGraph& graph, int32_t tag)
    {
        return cctest::AddNode(graph, cctest::FailKind::Name, cc::ParamsBox::Make(cctest::TagParams{ tag }));
    }

    uint32_t AddForget(cc::AuthoredGraph& graph, int32_t tag)
    {
        return cctest::AddNode(graph, cctest::ForgetKind::Name, cc::ParamsBox::Make(cctest::TagParams{ tag }));
    }

    void LinkValue(cc::AuthoredGraph& graph, uint32_t from_node, uint32_t to_node, std::string_view slot)
    {
        cctest::Connect(graph, from_node, "Value", to_node, slot);
    }

    float ValueOf(const cc::Executor& executor, const cc::RootView& view, size_t output)
    {
        const cc::ArtifactTable& artifacts = executor.Artifacts();
        const std::span<const cc::ArtifactRef> outputs = view.Outputs;
        const cc::ArtifactRef reference = outputs[output];

        return cctest::FloatOf(artifacts.Get(reference));
    }

    DiamondNodes AddDiamond(cc::AuthoredGraph& graph)
    {
        DiamondNodes nodes;
        nodes.Source = AddNumber(graph, 1.0f);
        nodes.Left = AddOffset(graph, 10.0f);
        nodes.Right = AddOffset(graph, 20.0f);
        nodes.Join = AddSum(graph, 1);
        LinkValue(graph, nodes.Source, nodes.Left, "Source");
        LinkValue(graph, nodes.Source, nodes.Right, "Source");
        LinkValue(graph, nodes.Left, nodes.Join, "Left");
        LinkValue(graph, nodes.Right, nodes.Join, "Right");

        return nodes;
    }
}
