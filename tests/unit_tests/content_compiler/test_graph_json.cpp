#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "FieldKinds.hpp"
#include "GraphJson.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using testing::ElementsAre;
using testing::HasSubstr;

namespace
{
    constexpr std::string_view k_ExampleFile = R"json(
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
      { "from": "ground", "output": "Field", "to": "total", "slot": "Left" },
      { "from": "raised", "output": "Field", "to": "total", "slot": "Right" }
    ]
  }
}
)json";

    // Returns a frozen registry that has the three field kinds.
    cc::KindRegistry MakeFieldRegistry();

    // Reads a file that must give no graph, and returns its diagnostics. A graph fails the test.
    std::vector<cc::Diagnostic> ReadFailure(std::string_view text, const cc::KindRegistry& registry);

    // Reads a file that must give a graph. A refusal fails the test, and the file is empty.
    cc::GraphFile ReadSuccess(std::string_view text, const cc::KindRegistry& registry);

    // Wraps the nodes and the edges, each a JSON array body, in a valid envelope.
    std::string MakeFile(std::string_view nodes, std::string_view edges);

    void ExpectEdge(const cc::AuthoredEdge& edge, uint32_t from_node, std::string_view output, uint32_t to_node,
                    std::string_view slot);
}

TEST(GraphJsonTest, TheExampleFileReadsIntoThreeNodesAndThreeEdges)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const cc::GraphFile file = ReadSuccess(k_ExampleFile, registry);

    ASSERT_EQ(file.Graph.Nodes.size(), 3u);
    ASSERT_EQ(file.Graph.Edges.size(), 3u);
    EXPECT_THAT(file.NodeNames, ElementsAre("ground", "raised", "total"));
    EXPECT_EQ(file.Graph.Nodes[0].KindName, "field.Const");
    EXPECT_EQ(file.Graph.Nodes[1].KindName, "field.ScaleBias");
    EXPECT_EQ(file.Graph.Nodes[2].KindName, "field.Add");

    const cc::field::ConstParams* constParams = file.Graph.Nodes[0].Params.As<cc::field::ConstParams>();
    const cc::field::ScaleBiasParams* scaleBiasParams = file.Graph.Nodes[1].Params.As<cc::field::ScaleBiasParams>();

    ASSERT_NE(constParams, nullptr);
    EXPECT_EQ(constParams->Width, 4u);
    EXPECT_EQ(constParams->Height, 2u);
    EXPECT_EQ(constParams->Value, 1.0f);
    ASSERT_NE(scaleBiasParams, nullptr);
    EXPECT_EQ(scaleBiasParams->Scale, 2.0f);
    EXPECT_EQ(scaleBiasParams->Bias, 0.5f);
    ExpectEdge(file.Graph.Edges[0], 0, "Field", 1, "Source");
    ExpectEdge(file.Graph.Edges[1], 0, "Field", 2, "Left");
    ExpectEdge(file.Graph.Edges[2], 1, "Field", 2, "Right");
}

TEST(GraphJsonTest, ANodeWithNoParamsMemberGetsTheDefaultValueOfItsParamsType)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "scaled", "kind": "field.ScaleBias" },
        { "name": "sum", "kind": "field.Add" }
    )json", "");
    const cc::GraphFile file = ReadSuccess(text, registry);

    ASSERT_EQ(file.Graph.Nodes.size(), 2u);

    const cc::field::ScaleBiasParams* scaleBiasParams = file.Graph.Nodes[0].Params.As<cc::field::ScaleBiasParams>();

    ASSERT_NE(scaleBiasParams, nullptr);
    EXPECT_EQ(scaleBiasParams->Scale, 1.0f);
    EXPECT_EQ(scaleBiasParams->Bias, 0.0f);
    EXPECT_FALSE(file.Graph.Nodes[1].Params.IsEmpty());
    EXPECT_NE(file.Graph.Nodes[1].Params.As<cc::field::AddParams>(), nullptr);
}

TEST(GraphJsonTest, AMisspelledParamsFieldIsAnInvalidParamsError)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "ground", "kind": "field.Const", "params": { "Width": 1, "Height": 1 } },
        { "name": "raised", "kind": "field.ScaleBias", "params": { "Scael": 2.0 } }
    )json", "");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::InvalidParams);
    EXPECT_EQ(diagnostics[0].Level, cc::Severity::Error);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(1u));
    EXPECT_THAT(diagnostics[0].FieldPath, HasSubstr("Scael"));
}

TEST(GraphJsonTest, AParamsValueOfTheWrongJsonTypeIsAnInvalidParamsError)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "ground", "kind": "field.Const", "params": { "Width": "four", "Height": 1 } }
    )json", "");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

    ASSERT_FALSE(diagnostics.empty());
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::InvalidParams);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u));
}

TEST(GraphJsonTest, TwoNodesWithOneNameAreADuplicateNodeNameWithBothIndices)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "same", "kind": "field.Add" },
        { "name": "other", "kind": "field.Add" },
        { "name": "same", "kind": "field.Add" }
    )json", "");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::DuplicateNodeName);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u, 2u));
}

TEST(GraphJsonTest, AnEdgeToANodeThatTheFileLacksIsAnUnknownNodeName)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "sum", "kind": "field.Add" }
    )json", R"json(
        { "from": "sum", "output": "Field", "to": "ghost", "slot": "Left" }
    )json");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::UnknownNodeName);
    EXPECT_TRUE(diagnostics[0].Nodes.empty());
    EXPECT_THAT(diagnostics[0].Message, HasSubstr("ghost"));
}

TEST(GraphJsonTest, AKindThatIsNotRegisteredIsAnUnknownKindForThatNode)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "first", "kind": "field.Add" },
        { "name": "second", "kind": "field.Nope" }
    )json", "");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::UnknownKind);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(1u));
}

TEST(GraphJsonTest, AWrongEnvelopeGivesOneDiagnosticWithTheCodeOfItsCause)
{
    const cc::KindRegistry registry = MakeFieldRegistry();

    struct EnvelopeCase
    {
        std::string_view Text;
        cc::DiagnosticCode Code;
    };

    const std::vector<EnvelopeCase> cases
    {
        { "this is not json", cc::DiagnosticCode::MalformedGraphFile },
        { "[1, 2]", cc::DiagnosticCode::MalformedGraphFile },
        { R"json({ "schema": "cc.Other", "version": 1, "data": { "nodes": [], "edges": [] } })json",
          cc::DiagnosticCode::UnsupportedGraphFile },
        { R"json({ "schema": "cc.AuthoredGraph", "version": 2, "data": { "nodes": [], "edges": [] } })json",
          cc::DiagnosticCode::UnsupportedGraphFile },
        { R"json({ "schema": "cc.AuthoredGraph", "version": "1", "data": { "nodes": [], "edges": [] } })json",
          cc::DiagnosticCode::MalformedGraphFile },
        { R"json({ "schema": "cc.AuthoredGraph", "version": 1 })json", cc::DiagnosticCode::MalformedGraphFile },
        { R"json({ "schema": "cc.AuthoredGraph", "version": 1, "data": { "nodes": [] } })json",
          cc::DiagnosticCode::MalformedGraphFile },
    };

    for (const EnvelopeCase& envelopeCase : cases)
    {
        const std::vector<cc::Diagnostic> diagnostics = ReadFailure(envelopeCase.Text, registry);

        ASSERT_EQ(diagnostics.size(), 1u) << envelopeCase.Text;
        EXPECT_EQ(diagnostics[0].Code, envelopeCase.Code) << envelopeCase.Text;
    }
}

TEST(GraphJsonTest, AnUnknownMemberAtEachLevelIsAMalformedGraphFileError)
{
    const cc::KindRegistry registry = MakeFieldRegistry();

    const std::string topLevel = R"json({ "schema": "cc.AuthoredGraph", "version": 1, "extra": 1,
                                          "data": { "nodes": [], "edges": [] } })json";
    const std::string inData = R"json({ "schema": "cc.AuthoredGraph", "version": 1,
                                        "data": { "nodes": [], "edges": [], "extra": 1 } })json";
    const std::string inNode = MakeFile(R"json({ "name": "sum", "kind": "field.Add", "extra": 1 })json", "");
    const std::string inEdge = MakeFile(R"json({ "name": "sum", "kind": "field.Add" })json",
                                        R"json({ "from": "sum", "output": "Field", "to": "sum", "slot": "Left",
                                                 "extra": 1 })json");
    const std::string inAll = R"json({ "schema": "cc.AuthoredGraph", "version": 1, "extra": 1,
        "data": {
            "nodes": [ { "name": "sum", "kind": "field.Add", "extra": 1 } ],
            "edges": [ { "from": "sum", "output": "Field", "to": "sum", "slot": "Left", "extra": 1 } ],
            "extra": 1
        } })json";

    const std::string singleCases[]{ topLevel, inData, inNode, inEdge };

    for (const std::string& text : singleCases)
    {
        const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

        ASSERT_EQ(diagnostics.size(), 1u) << text;
        EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::MalformedGraphFile) << text;
    }

    const std::vector<cc::Diagnostic> allDiagnostics = ReadFailure(inAll, registry);

    EXPECT_EQ(allDiagnostics.size(), 4u);
}

TEST(GraphJsonTest, TheReaderCollectsEveryDiagnosticInOneCall)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "twin", "kind": "field.Add" },
        { "name": "twin", "kind": "field.Add" },
        { "name": "stranger", "kind": "field.Nope" }
    )json", R"json(
        { "from": "twin", "output": "Field", "to": "missing", "slot": "Left" }
    )json");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);
    const std::vector<cc::DiagnosticCode> codes = cctest::CodesOf(diagnostics);

    EXPECT_THAT(codes, ElementsAre(cc::DiagnosticCode::DuplicateNodeName, cc::DiagnosticCode::UnknownKind,
                                   cc::DiagnosticCode::UnknownNodeName));
}

TEST(GraphJsonTest, AParamsMemberThatIsNotAnObjectIsMalformedAndNotInvalidParams)
{
    const cc::KindRegistry registry = MakeFieldRegistry();
    const std::string text = MakeFile(R"json(
        { "name": "number", "kind": "field.ScaleBias", "params": 5 },
        { "name": "list", "kind": "field.ScaleBias", "params": [1] }
    )json", "");
    const std::vector<cc::Diagnostic> diagnostics = ReadFailure(text, registry);

    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_EQ(diagnostics[0].Code, cc::DiagnosticCode::MalformedGraphFile);
    EXPECT_THAT(diagnostics[0].Nodes, ElementsAre(0u));
    EXPECT_EQ(diagnostics[1].Code, cc::DiagnosticCode::MalformedGraphFile);
    EXPECT_THAT(diagnostics[1].Nodes, ElementsAre(1u));
}

namespace
{
    cc::KindRegistry MakeFieldRegistry()
    {
        cc::KindRegistry registry;
        const std::vector<cc::Diagnostic> diagnostics = cc::field::RegisterFieldKinds(registry);

        EXPECT_TRUE(diagnostics.empty());

        registry.Freeze();

        return registry;
    }

    std::vector<cc::Diagnostic> ReadFailure(std::string_view text, const cc::KindRegistry& registry)
    {
        std::expected<cc::GraphFile, std::vector<cc::Diagnostic>> result = cc::ReadGraphJson(text, registry);

        if (result.has_value())
        {
            ADD_FAILURE() << "the file was read, and the test expected a refusal";

            return {};
        }

        return std::move(result.error());
    }

    cc::GraphFile ReadSuccess(std::string_view text, const cc::KindRegistry& registry)
    {
        std::expected<cc::GraphFile, std::vector<cc::Diagnostic>> result = cc::ReadGraphJson(text, registry);

        if (!result.has_value())
        {
            ADD_FAILURE() << "the file was refused with " << result.error().size() << " diagnostics";

            return {};
        }

        return std::move(*result);
    }

    std::string MakeFile(std::string_view nodes, std::string_view edges)
    {
        std::string text = R"json({ "schema": "cc.AuthoredGraph", "version": 1, "data": { "nodes": [)json";
        text += nodes;
        text += R"json(], "edges": [)json";
        text += edges;
        text += "] } }";

        return text;
    }

    void ExpectEdge(const cc::AuthoredEdge& edge, uint32_t from_node, std::string_view output, uint32_t to_node,
                    std::string_view slot)
    {
        EXPECT_EQ(edge.FromNode, from_node);
        EXPECT_EQ(edge.FromOutput, output);
        EXPECT_EQ(edge.ToNode, to_node);
        EXPECT_EQ(edge.ToSlot, slot);
    }
}
