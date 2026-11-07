#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "Ingest.hpp"
#include "IngestValidation.hpp"
#include "KindRegistry.hpp"
#include "RecipeTable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

using testing::ElementsAre;

namespace
{
    constexpr float k_NaN = std::numeric_limits<float>::quiet_NaN();

    // Ingests a graph that must be accepted. A refusal fails the test and returns ids that are all zero.
    cc::IngestResult IngestOrFail(const cc::AuthoredGraph& graph, cc::Quality quality, const cc::KindRegistry& registry,
                                  cc::RecipeTable& table);

    // Ingests a graph that must be refused. An acceptance fails the test and returns no diagnostic.
    std::vector<cc::Diagnostic> RefusalOf(const cc::AuthoredGraph& graph, const cc::KindRegistry& registry,
                                          cc::RecipeTable& table);

    cc::ParamsBox ConstBox();
    cc::ParamsBox ScaleBox(float factor);

    // nodes: 0 Const, 1 Scale (from 0), 2 Scale (from 1) with the given factor, 3 Scale (from 2), 4 Scale (from 0)
    cc::AuthoredGraph MakeChain(float middle_factor);

    // nodes: 0 A, 1 B (from A), 2 C (from A), 3 D (from B and C)
    cc::AuthoredGraph MakeDiamond();

    // the diamond with A written twice: nodes 0 and 1 are equal, B is built from 0 and C from 1
    cc::AuthoredGraph MakeDiamondWithRepeatedSource();

    std::set<uint32_t> ValuesOf(const std::vector<cc::RecipeId>& ids);
}

TEST(IngestTest, ADiamondInternsItsSharedNodeOnce)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::IngestResult result = IngestOrFail(MakeDiamond(), cc::Quality::Final, registry, table);

    ASSERT_EQ(result.NodeRecipes.size(), 4u);
    EXPECT_EQ(table.Count(), 4u);
    EXPECT_EQ(ValuesOf(result.NodeRecipes).size(), 4u);

    const cc::RecipeId shared = result.NodeRecipes[0];
    const cc::Recipe& firstConsumer = table.Get(result.NodeRecipes[1]);
    const cc::Recipe& secondConsumer = table.Get(result.NodeRecipes[2]);

    ASSERT_EQ(firstConsumer.Inputs.size(), 1u);
    ASSERT_EQ(secondConsumer.Inputs.size(), 1u);
    EXPECT_EQ(firstConsumer.Inputs[0].Source, shared);
    EXPECT_EQ(secondConsumer.Inputs[0].Source, shared);
}

TEST(IngestTest, TwoEqualAuthoredSourcesInternAsOneRecipe)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::IngestResult result = IngestOrFail(MakeDiamondWithRepeatedSource(), cc::Quality::Final, registry, table);

    ASSERT_EQ(result.NodeRecipes.size(), 5u);
    EXPECT_EQ(result.NodeRecipes[0], result.NodeRecipes[1]);
    EXPECT_EQ(table.Count(), 4u);
}

TEST(IngestTest, TheSameGraphTwiceGivesTheSameIdsAndDoesNotGrowTheTable)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;
    const cc::AuthoredGraph graph = MakeChain(2.0f);

    const cc::IngestResult first = IngestOrFail(graph, cc::Quality::Preview, registry, table);
    const size_t countAfterFirst = table.Count();
    const cc::IngestResult second = IngestOrFail(graph, cc::Quality::Preview, registry, table);

    EXPECT_EQ(first.NodeRecipes, second.NodeRecipes);
    EXPECT_EQ(table.Count(), countAfterFirst);
    EXPECT_EQ(countAfterFirst, 5u);
}

TEST(IngestTest, ChangingAMiddleParamChangesThatNodeAndEveryNodeDownstreamOnly)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::IngestResult before = IngestOrFail(MakeChain(2.0f), cc::Quality::Preview, registry, table);
    const cc::IngestResult after = IngestOrFail(MakeChain(2.5f), cc::Quality::Preview, registry, table);

    // upstream (0, 1) and the side branch (4) keep their ids
    EXPECT_EQ(before.NodeRecipes[0], after.NodeRecipes[0]);
    EXPECT_EQ(before.NodeRecipes[1], after.NodeRecipes[1]);
    EXPECT_EQ(before.NodeRecipes[4], after.NodeRecipes[4]);

    // the changed node (2) and its consumer (3) get new ids
    EXPECT_NE(before.NodeRecipes[2], after.NodeRecipes[2]);
    EXPECT_NE(before.NodeRecipes[3], after.NodeRecipes[3]);

    // five recipes, plus the two new ones
    EXPECT_EQ(table.Count(), 7u);
}

TEST(IngestTest, PreviewAndFinalGiveDisjointIds)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;
    const cc::AuthoredGraph graph = MakeChain(2.0f);

    const cc::IngestResult preview = IngestOrFail(graph, cc::Quality::Preview, registry, table);
    const cc::IngestResult finalResult = IngestOrFail(graph, cc::Quality::Final, registry, table);

    std::set<uint32_t> shared;
    const std::set<uint32_t> previewIds = ValuesOf(preview.NodeRecipes);
    const std::set<uint32_t> finalIds = ValuesOf(finalResult.NodeRecipes);
    std::ranges::set_intersection(previewIds, finalIds, std::inserter(shared, shared.begin()));

    EXPECT_TRUE(shared.empty());
    EXPECT_EQ(table.Count(), 10u);
    EXPECT_EQ(table.Get(preview.NodeRecipes[2]).Level, cc::Quality::Preview);
    EXPECT_EQ(table.Get(finalResult.NodeRecipes[2]).Level, cc::Quality::Final);
}

TEST(IngestTest, ConnectingAnOptionalSlotChangesTheRecipe)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph without;
    const uint32_t a = cctest::AddNode(without, "test.Const", ConstBox());
    const uint32_t b = cctest::AddNode(without, "test.Scale", ScaleBox(1.0f));
    const uint32_t blend = cctest::AddNode(without, "test.Blend", cc::ParamsBox::Make(cctest::BlendParams{}));
    cctest::Connect(without, a, "Value", blend, "A");
    cctest::Connect(without, a, "Value", b, "Source");
    cctest::Connect(without, b, "Value", blend, "B");

    cc::AuthoredGraph with = without;
    cctest::Connect(with, a, "Value", blend, "Mask");

    const cc::IngestResult plain = IngestOrFail(without, cc::Quality::Final, registry, table);
    const cc::IngestResult masked = IngestOrFail(with, cc::Quality::Final, registry, table);

    const cc::Recipe& plainBlend = table.Get(plain.NodeRecipes[blend]);
    const cc::Recipe& maskedBlend = table.Get(masked.NodeRecipes[blend]);

    EXPECT_NE(plain.NodeRecipes[blend], masked.NodeRecipes[blend]);
    EXPECT_EQ(plainBlend.Inputs.size(), 2u);
    ASSERT_EQ(maskedBlend.Inputs.size(), 3u);
    EXPECT_EQ(maskedBlend.Inputs[2].Slot, "Mask");
}

TEST(IngestTest, TheOrderOfTheAuthoredNodesDoesNotChangeTheRecipes)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    // the diamond, written backwards: node 0 is D and node 3 is A
    cc::AuthoredGraph backwards;
    const uint32_t d = cctest::AddNode(backwards, "test.Blend", cc::ParamsBox::Make(cctest::BlendParams{}));
    const uint32_t c = cctest::AddNode(backwards, "test.Scale", ScaleBox(3.0f));
    const uint32_t b = cctest::AddNode(backwards, "test.Scale", ScaleBox(2.0f));
    const uint32_t a = cctest::AddNode(backwards, "test.Const", ConstBox());
    cctest::Connect(backwards, a, "Value", b, "Source");
    cctest::Connect(backwards, a, "Value", c, "Source");
    cctest::Connect(backwards, b, "Value", d, "A");
    cctest::Connect(backwards, c, "Value", d, "B");

    const cc::IngestResult forward = IngestOrFail(MakeDiamond(), cc::Quality::Final, registry, table);
    const cc::IngestResult reversed = IngestOrFail(backwards, cc::Quality::Final, registry, table);

    EXPECT_EQ(table.Count(), 4u);
    EXPECT_EQ(forward.NodeRecipes[0], reversed.NodeRecipes[a]);
    EXPECT_EQ(forward.NodeRecipes[1], reversed.NodeRecipes[b]);
    EXPECT_EQ(forward.NodeRecipes[2], reversed.NodeRecipes[c]);
    EXPECT_EQ(forward.NodeRecipes[3], reversed.NodeRecipes[d]);
}

TEST(IngestTest, NodesAreInternedInTopologicalOrderWithTheLowestIndexFirst)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    // node 0 needs node 2, so Kahn's algorithm takes 1, then 2, then 0
    cc::AuthoredGraph graph;
    const uint32_t consumer = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::AddNode(graph, "test.Const", ConstBox());
    cctest::ConstParams other;
    other.Seed = 1;
    const uint32_t producer = cctest::AddNode(graph, "test.Const", cc::ParamsBox::Make(other));
    cctest::Connect(graph, producer, "Value", consumer, "Source");

    const cc::IngestResult result = IngestOrFail(graph, cc::Quality::Final, registry, table);

    EXPECT_EQ(result.NodeRecipes[1].Value, 1u);
    EXPECT_EQ(result.NodeRecipes[2].Value, 2u);
    EXPECT_EQ(result.NodeRecipes[0].Value, 3u);
}

TEST(IngestTest, AThreeNodeCycleIsRefusedWithExactlyThoseNodes)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    // a graph that is good, so the table is not empty when the bad one comes
    IngestOrFail(MakeChain(2.0f), cc::Quality::Final, registry, table);
    const size_t countBefore = table.Count();

    // 0 is a source; 1 -> 2 -> 3 -> 1 is the cycle; 4 sits downstream of the cycle and is not on it
    cc::AuthoredGraph graph;
    cctest::AddNode(graph, "test.Const", ConstBox());
    const uint32_t first = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    const uint32_t second = cctest::AddNode(graph, "test.Scale", ScaleBox(2.0f));
    const uint32_t third = cctest::AddNode(graph, "test.Scale", ScaleBox(3.0f));
    const uint32_t downstream = cctest::AddNode(graph, "test.Scale", ScaleBox(4.0f));
    cctest::Connect(graph, first, "Value", second, "Source");
    cctest::Connect(graph, second, "Value", third, "Source");
    cctest::Connect(graph, third, "Value", first, "Source");
    cctest::Connect(graph, third, "Value", downstream, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::CycleDetected);
    EXPECT_EQ(diagnostics[0].Level, cc::Severity::Error);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(1u, 2u, 3u));
    EXPECT_EQ(table.Count(), countBefore);
}

TEST(IngestTest, ANodeThatFeedsItselfIsACycleOfOne)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t node = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(graph, node, "Value", node, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::CycleDetected);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, ANaNInANestedFieldIsRefusedWithItsPath)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    IngestOrFail(MakeChain(2.0f), cc::Quality::Final, registry, table);
    const size_t countBefore = table.Count();

    cctest::ConstParams params;
    params.Nested.Amplitude = k_NaN;

    cc::AuthoredGraph graph;
    cctest::AddNode(graph, "test.Const", ConstBox());
    cctest::AddNode(graph, "test.Const", cc::ParamsBox::Make(params));

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::NaNParameter);
    EXPECT_EQ(diagnostics[0].FieldPath, "Nested.Amplitude");
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(1u));
    EXPECT_EQ(table.Count(), countBefore);
}

TEST(IngestTest, ANaNInAVectorElementIsRefusedWithItsIndexedPath)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cctest::ConstParams params;
    params.Nested.Weights = { 1.0f, 2.0f, k_NaN };

    cc::AuthoredGraph graph;
    cctest::AddNode(graph, "test.Const", cc::ParamsBox::Make(params));

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].FieldPath, "Nested.Weights[2]");
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, AnUnknownKindIsRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    cctest::AddNode(graph, "test.Const", ConstBox());
    cctest::AddNode(graph, "test.Missing", ConstBox());

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::UnknownKind);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(1u));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, ParamsOfAnotherTypeAreRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    cctest::AddNode(graph, "test.Const", cc::ParamsBox::Make(cctest::UnrelatedParams{}));
    cctest::AddNode(graph, "test.Const", cc::ParamsBox{});

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::ParamsTypeMismatch);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u));
    EXPECT_EQ(diagnostics[1].Code, cc::DiagnosticCode::ParamsTypeMismatch);
    EXPECT_THAT(diagnostics[1].Nodes, ElementsAre(1u));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, AnEdgeToANodeThatDoesNotExistIsRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t source = cctest::AddNode(graph, "test.Const", ConstBox());
    cctest::Connect(graph, source, "Value", 9, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::DanglingEdge);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, AnEdgeFromANodeThatDoesNotExistIsRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t consumer = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(graph, 7, "Value", consumer, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    // the dangling edge fills nothing, so the required slot is missing too
    EXPECT_THAT(cctest::CodesOf(diagnostics),
                ElementsAre(cc::DiagnosticCode::DanglingEdge, cc::DiagnosticCode::MissingRequiredSlot));
    ASSERT_FALSE(diagnostics.empty());
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, AnEdgeIntoASlotThatDoesNotExistIsRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t source = cctest::AddNode(graph, "test.Const", ConstBox());
    const uint32_t consumer = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(graph, source, "Value", consumer, "Nope");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::UnknownSlot);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(consumer));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, TwoEdgesIntoOneSlotAreRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cctest::ConstParams other;
    other.Seed = 4;

    cc::AuthoredGraph graph;
    const uint32_t first = cctest::AddNode(graph, "test.Const", ConstBox());
    const uint32_t second = cctest::AddNode(graph, "test.Const", cc::ParamsBox::Make(other));
    const uint32_t consumer = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(graph, first, "Value", consumer, "Source");
    cctest::Connect(graph, second, "Value", consumer, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::SlotAlreadyConnected);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(consumer));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, ARequiredSlotWithNoEdgeIsRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t source = cctest::AddNode(graph, "test.Const", ConstBox());
    const uint32_t blend = cctest::AddNode(graph, "test.Blend", cc::ParamsBox::Make(cctest::BlendParams{}));
    cctest::Connect(graph, source, "Value", blend, "A");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    // B is required and has no edge. Mask is optional and is not reported.
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::MissingRequiredSlot);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(blend));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, AProducerOfTheWrongArtifactKindIsRefused)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    // Const makes CPU bytes, and Sharpen takes a GPU image
    cc::AuthoredGraph graph;
    const uint32_t source = cctest::AddNode(graph, "test.Const", ConstBox());
    const uint32_t sharpen = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    cctest::Connect(graph, source, "Value", sharpen, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::SlotKindMismatch);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(sharpen));
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, AMatchingGpuProducerIsAccepted)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t image = cctest::AddNode(graph, "test.Image", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t sharpen = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    cctest::Connect(graph, image, "Image", sharpen, "Source");

    const cc::IngestResult result = IngestOrFail(graph, cc::Quality::Final, registry, table);

    EXPECT_EQ(result.NodeRecipes.size(), 2u);
}

TEST(IngestTest, TwoConsumersThatReadDifferentOutputsOfOneProducerGetDifferentKeysAndIds)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    // the two consumers are equal except for the output that they read
    cc::AuthoredGraph graph;
    const uint32_t multi = cctest::AddNode(graph, "test.Multi", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t readsHeight = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    const uint32_t readsNormal = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    cctest::Connect(graph, multi, "Height", readsHeight, "Source");
    cctest::Connect(graph, multi, "Normal", readsNormal, "Source");

    const cc::IngestResult result = IngestOrFail(graph, cc::Quality::Final, registry, table);

    // a refused graph returns ids of zero, which Get must not see
    ASSERT_EQ(table.Count(), 3u);
    EXPECT_NE(result.NodeRecipes[readsHeight], result.NodeRecipes[readsNormal]);
    EXPECT_NE(table.Get(result.NodeRecipes[readsHeight]).Key, table.Get(result.NodeRecipes[readsNormal]).Key);
    EXPECT_EQ(table.Count(), 3u);
}

TEST(IngestTest, AnUnknownOutputIsRefusedAndLeavesTheRequiredSlotMissing)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t multi = cctest::AddNode(graph, "test.Multi", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t sharpen = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    cctest::Connect(graph, multi, "Specular", sharpen, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    EXPECT_THAT(cctest::CodesOf(diagnostics),
                ElementsAre(cc::DiagnosticCode::UnknownOutput, cc::DiagnosticCode::MissingRequiredSlot));
    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(multi));
    EXPECT_THAT(diagnostics[1].Nodes, ElementsAre(sharpen));
    EXPECT_NE(diagnostics[0].Message.find("\"Specular\""), std::string::npos);
    EXPECT_NE(diagnostics[0].Message.find("test.Multi"), std::string::npos);
    EXPECT_NE(diagnostics[0].Message.find("\"Height\", \"Normal\", \"Stats\""), std::string::npos);
    EXPECT_EQ(table.Count(), 0u);
}

TEST(IngestTest, TheSlotIsCheckedAgainstTheNamedOutputAndNotAgainstTheFirstOutput)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();

    // Multi makes GPU images as outputs 0 and 1, and CPU bytes as output 2. Scale takes CPU bytes.
    cc::AuthoredGraph refused;
    const uint32_t imageProducer = cctest::AddNode(refused, "test.Multi", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t imageConsumer = cctest::AddNode(refused, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(refused, imageProducer, "Height", imageConsumer, "Source");

    cc::AuthoredGraph accepted;
    const uint32_t bytesProducer = cctest::AddNode(accepted, "test.Multi", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t bytesConsumer = cctest::AddNode(accepted, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(accepted, bytesProducer, "Stats", bytesConsumer, "Source");

    cc::RecipeTable refusedTable;
    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(refused, registry, refusedTable);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::SlotKindMismatch);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(imageConsumer));

    cc::RecipeTable acceptedTable;
    const cc::IngestResult result = IngestOrFail(accepted, cc::Quality::Final, registry, acceptedTable);

    EXPECT_EQ(result.NodeRecipes.size(), 2u);
}

TEST(IngestTest, ReorderingTheOutputsOfAKindDoesNotChangeTheKeyOfAnEdgeByName)
{
    const cc::KindRegistry original = cctest::MakeFrozenRegistry();
    const cc::KindRegistry reordered = cctest::MakeFrozenRegistryWithReorderedMulti();

    // Height is output 0 of the original kind and output 2 of the reordered kind
    cc::AuthoredGraph graph;
    const uint32_t multi = cctest::AddNode(graph, "test.Multi", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t sharpen = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    cctest::Connect(graph, multi, "Height", sharpen, "Source");

    cc::RecipeTable originalTable;
    cc::RecipeTable reorderedTable;
    const cc::IngestResult originalResult = IngestOrFail(graph, cc::Quality::Final, original, originalTable);
    const cc::IngestResult reorderedResult = IngestOrFail(graph, cc::Quality::Final, reordered, reorderedTable);

    // a refused graph returns ids of zero, which Get must not see
    ASSERT_EQ(originalTable.Count(), 2u);
    ASSERT_EQ(reorderedTable.Count(), 2u);
    EXPECT_EQ(originalTable.Get(originalResult.NodeRecipes[multi]).Key,
              reorderedTable.Get(reorderedResult.NodeRecipes[multi]).Key);
    EXPECT_EQ(originalTable.Get(originalResult.NodeRecipes[sharpen]).Key,
              reorderedTable.Get(reorderedResult.NodeRecipes[sharpen]).Key);
    EXPECT_EQ(originalTable.Get(originalResult.NodeRecipes[sharpen]).Inputs[0].Output, 0);
    EXPECT_EQ(reorderedTable.Get(reorderedResult.NodeRecipes[sharpen]).Inputs[0].Output, 2);
}

TEST(IngestTest, TheRecipeInputStoresTheIndexOfANonFirstOutput)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cc::AuthoredGraph graph;
    const uint32_t multi = cctest::AddNode(graph, "test.Multi", cc::ParamsBox::Make(cctest::ImageParams{ 256 }));
    const uint32_t readsNormal = cctest::AddNode(graph, "test.Sharpen", ScaleBox(1.0f));
    const uint32_t readsStats = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(graph, multi, "Normal", readsNormal, "Source");
    cctest::Connect(graph, multi, "Stats", readsStats, "Source");

    const cc::IngestResult result = IngestOrFail(graph, cc::Quality::Final, registry, table);

    // a refused graph returns ids of zero, which Get must not see
    ASSERT_EQ(table.Count(), 3u);

    const cc::Recipe& normalRecipe = table.Get(result.NodeRecipes[readsNormal]);
    const cc::Recipe& statsRecipe = table.Get(result.NodeRecipes[readsStats]);

    ASSERT_EQ(normalRecipe.Inputs.size(), 1u);
    ASSERT_EQ(statsRecipe.Inputs.size(), 1u);
    EXPECT_EQ(normalRecipe.Inputs[0].Output, 1);
    EXPECT_EQ(statsRecipe.Inputs[0].Output, 2);
    EXPECT_EQ(normalRecipe.Inputs[0].Source, result.NodeRecipes[multi]);
}

TEST(IngestTest, AGraphWithSeveralDifferentErrorsReportsAllOfThem)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cctest::ConstParams nanParams;
    nanParams.Value = k_NaN;

    // 0: unknown kind; 1: NaN; 2: Scale with no source; 3 -> 4 -> 3: a cycle
    cc::AuthoredGraph graph;
    cctest::AddNode(graph, "test.Missing", ConstBox());
    cctest::AddNode(graph, "test.Const", cc::ParamsBox::Make(nanParams));
    cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    const uint32_t loopA = cctest::AddNode(graph, "test.Scale", ScaleBox(2.0f));
    const uint32_t loopB = cctest::AddNode(graph, "test.Scale", ScaleBox(3.0f));
    cctest::Connect(graph, loopA, "Value", loopB, "Source");
    cctest::Connect(graph, loopB, "Value", loopA, "Source");

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    EXPECT_THAT(cctest::CodesOf(diagnostics),
                ElementsAre(cc::DiagnosticCode::UnknownKind, cc::DiagnosticCode::NaNParameter,
                            cc::DiagnosticCode::MissingRequiredSlot, cc::DiagnosticCode::CycleDetected));
    EXPECT_EQ(table.Count(), 0u);
}

// A real collision cannot be found, so the test forces one: it stores a recipe under the key that the second node of
// the graph will get, with other bytes.
TEST(IngestTest, AKeyCollisionOnALaterNodeRefusesTheGraphAndAddsNothing)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();

    cc::AuthoredGraph graph;
    const uint32_t source = cctest::AddNode(graph, "test.Const", ConstBox());
    const uint32_t consumer = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
    cctest::Connect(graph, source, "Value", consumer, "Source");

    // a scratch table tells what the keys of the two nodes are
    cc::RecipeTable scratch;
    const cc::IngestResult scratchResult = IngestOrFail(graph, cc::Quality::Final, registry, scratch);
    const foundation::content::RecipeKey sourceKey = scratch.Get(scratchResult.NodeRecipes[source]).Key;
    const foundation::content::RecipeKey consumerKey = scratch.Get(scratchResult.NodeRecipes[consumer]).Key;

    cc::RecipeTable table;
    cc::Recipe seed
    {
        .Key = consumerKey,
        .Kind = *registry.Find("test.Scale"),
        .Level = cc::Quality::Final,
        .Params = ScaleBox(1.0f),
        .Inputs = {},
    };
    ASSERT_TRUE(table.InternComputed(std::move(seed), { std::byte{ 0xFF } }).has_value());

    const std::vector<cc::Diagnostic> diagnostics = RefusalOf(graph, registry, table);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::KeyCollision);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(consumer));

    // the source node comes first in topological order and must not be in the table either
    EXPECT_EQ(table.Count(), 1u);
    EXPECT_FALSE(table.Find(sourceKey).has_value());
}

TEST(IngestTest, TwoNodesWithOneKeyAndOtherBytesAreACollisionInsideTheGraph)
{
    const foundation::content::RecipeKey sharedKey{ 5, 6 };
    const foundation::content::RecipeKey otherKey{ 7, 8 };
    const std::vector<std::byte> bytes{ std::byte{ 1 }, std::byte{ 2 } };
    const std::vector<std::byte> otherBytes{ std::byte{ 9 } };

    // node 2 repeats node 0 exactly (one recipe, fine); node 3 has the key of node 0 and other bytes
    const std::vector<cc::PreparedRecipe> prepared
    {
        cc::PreparedRecipe{ sharedKey, bytes },
        cc::PreparedRecipe{ otherKey, bytes },
        cc::PreparedRecipe{ sharedKey, bytes },
        cc::PreparedRecipe{ sharedKey, otherBytes },
    };

    const std::vector<cc::detail::BatchCollision> collisions = cc::detail::FindBatchCollisions(prepared);

    ASSERT_EQ(collisions.size(), 1u);
    EXPECT_EQ(collisions[0].Node, 3u);
    EXPECT_EQ(collisions[0].Earlier, 0u);
}

TEST(IngestTest, AnEmptyGraphIsAccepted)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::IngestResult result = IngestOrFail(cc::AuthoredGraph{}, cc::Quality::Final, registry, table);

    EXPECT_TRUE(result.NodeRecipes.empty());
    EXPECT_EQ(table.Count(), 0u);
}

namespace
{
    cc::IngestResult IngestOrFail(const cc::AuthoredGraph& graph, cc::Quality quality, const cc::KindRegistry& registry,
                                  cc::RecipeTable& table)
    {
        std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> result = cc::Ingest(graph, quality, registry, table);

        EXPECT_TRUE(result.has_value());

        if (!result.has_value())
        {
            // sized like a real result, so that the indexing in the test body stays in range
            cc::IngestResult failed;
            failed.NodeRecipes.assign(graph.Nodes.size(), cc::RecipeId{});

            return failed;
        }

        return std::move(*result);
    }

    std::vector<cc::Diagnostic> RefusalOf(const cc::AuthoredGraph& graph, const cc::KindRegistry& registry,
                                          cc::RecipeTable& table)
    {
        std::expected<cc::IngestResult, std::vector<cc::Diagnostic>> result =
            cc::Ingest(graph, cc::Quality::Final, registry, table);

        EXPECT_FALSE(result.has_value());

        if (result.has_value())
        {
            return {};
        }

        return std::move(result.error());
    }

    cc::ParamsBox ConstBox()
    {
        return cc::ParamsBox::Make(cctest::ConstParams{});
    }

    cc::ParamsBox ScaleBox(float factor)
    {
        return cc::ParamsBox::Make(cctest::ScaleParams{ factor });
    }

    cc::AuthoredGraph MakeChain(float middle_factor)
    {
        cc::AuthoredGraph graph;
        const uint32_t source = cctest::AddNode(graph, "test.Const", ConstBox());
        const uint32_t first = cctest::AddNode(graph, "test.Scale", ScaleBox(1.0f));
        const uint32_t middle = cctest::AddNode(graph, "test.Scale", ScaleBox(middle_factor));
        const uint32_t last = cctest::AddNode(graph, "test.Scale", ScaleBox(4.0f));
        const uint32_t side = cctest::AddNode(graph, "test.Scale", ScaleBox(5.0f));
        cctest::Connect(graph, source, "Value", first, "Source");
        cctest::Connect(graph, first, "Value", middle, "Source");
        cctest::Connect(graph, middle, "Value", last, "Source");
        cctest::Connect(graph, source, "Value", side, "Source");

        return graph;
    }

    cc::AuthoredGraph MakeDiamond()
    {
        cc::AuthoredGraph graph;
        const uint32_t a = cctest::AddNode(graph, "test.Const", ConstBox());
        const uint32_t b = cctest::AddNode(graph, "test.Scale", ScaleBox(2.0f));
        const uint32_t c = cctest::AddNode(graph, "test.Scale", ScaleBox(3.0f));
        const uint32_t d = cctest::AddNode(graph, "test.Blend", cc::ParamsBox::Make(cctest::BlendParams{}));
        cctest::Connect(graph, a, "Value", b, "Source");
        cctest::Connect(graph, a, "Value", c, "Source");
        cctest::Connect(graph, b, "Value", d, "A");
        cctest::Connect(graph, c, "Value", d, "B");

        return graph;
    }

    cc::AuthoredGraph MakeDiamondWithRepeatedSource()
    {
        cc::AuthoredGraph graph;
        const uint32_t firstA = cctest::AddNode(graph, "test.Const", ConstBox());
        const uint32_t secondA = cctest::AddNode(graph, "test.Const", ConstBox());
        const uint32_t b = cctest::AddNode(graph, "test.Scale", ScaleBox(2.0f));
        const uint32_t c = cctest::AddNode(graph, "test.Scale", ScaleBox(3.0f));
        const uint32_t d = cctest::AddNode(graph, "test.Blend", cc::ParamsBox::Make(cctest::BlendParams{}));
        cctest::Connect(graph, firstA, "Value", b, "Source");
        cctest::Connect(graph, secondA, "Value", c, "Source");
        cctest::Connect(graph, b, "Value", d, "A");
        cctest::Connect(graph, c, "Value", d, "B");

        return graph;
    }

    std::set<uint32_t> ValuesOf(const std::vector<cc::RecipeId>& ids)
    {
        std::set<uint32_t> values;

        for (const cc::RecipeId id : ids)
        {
            values.insert(id.Value);
        }

        return values;
    }
}
