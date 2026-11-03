#include "Declarations.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "ResourceTypes.hpp"
#include "freeze_test_helpers.hpp"
#include <cstdint>
#include <optional>
#include <source_location>
#include <gtest/gtest.h>

// Freeze's own tests: Publish and Check, one broken graph per LM1xx code, each checking the code and the line of the
// call. Resolve's tests are in test_freeze_resolve.cpp, and the draw and Record statements in test_freeze_draw.cpp.

using namespace freeze_tests;

TEST(FrozenGraph, IndicesStartInvalid)
{
    const loom::FrozenPass pass{};

    EXPECT_FALSE(pass.Sequence.IsValid());
    EXPECT_FALSE(pass.Dispatch.Resource.IsValid());
    EXPECT_TRUE(loom::k_BeginFrameAnchorIndex.IsValid());
    EXPECT_NE(loom::k_BeginFrameAnchorIndex, loom::k_EndFrameAnchorIndex);
}

TEST(Freeze, BuiltinAnchorsComeFirst)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.DeclareAnchor(loom::AnchorKey{ "Test.Anchor" });

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_TRUE(frozen.has_value());
    ASSERT_EQ(frozen->Anchors.size(), 3u);
    EXPECT_EQ(frozen->Anchors[loom::k_BeginFrameAnchorIndex.Value].Key.Hash, loom::BeginFrameAnchor.Hash);
    EXPECT_EQ(frozen->Anchors[loom::k_EndFrameAnchorIndex.Value].Key.Hash, loom::EndFrameAnchor.Hash);
    EXPECT_EQ(frozen->Anchors[2].Origin, loom::AnchorOrigin::Declared);
}

TEST(Freeze, SameKeyTwiceIsLM102WithBothLines)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const uint32_t firstLine = std::source_location::current().line() + 1;
    graph.Declare(MakeBuffer());
    const uint32_t secondLine = std::source_location::current().line() + 1;
    graph.Declare(MakeBuffer());

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM102);
    EXPECT_EQ(sink.Diagnostics()[0].Where.line(), secondLine);
    EXPECT_EQ(sink.Diagnostics()[0].OtherWhere.line(), firstLine);
}

TEST(Freeze, SameKeyAsTwoKindsIsLM107)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());
    const ImageFormat format{ ImageComponentFormats::R8, ImageDataType::UNorm };
    graph.Import(loom::ImportDeclaration{ .Key = k_DataAsImage, .Expectation = loom::ImageExpectation::AnyExtent(format, 1) });

    loom::CollectingSink sink;
    const std::optional<loom::FrozenGraph> frozen = loom::Freeze(graph.Record(), sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM107);
}

namespace
{

    constexpr loom::ResourceKey<loom::Sampler> k_Sampler{ "Test.Sampler" };

    std::optional<loom::FrozenGraph> FreezeOneSampler(const SamplerCreateInfo& info, uint32_t& line,
                                                      loom::CollectingSink& sink)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

        line = std::source_location::current().line() + 1;
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Sampler, .CreateInfo = info });

        return loom::Freeze(graph.Record(), sink);
    }

}

TEST(Freeze, DefaultSamplerIsValid)
{
    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(SamplerCreateInfo{}, line, sink);

    ASSERT_TRUE(frozen.has_value());
    EXPECT_EQ(sink.Diagnostics().size(), 0u);
    ASSERT_EQ(frozen->Samplers.size(), 1u);
}

TEST(Freeze, SamplerMaxLodBelowMinLodIsLM109AsReal)
{
    SamplerCreateInfo info{};
    info.MinLod = 4.0f;
    info.MaxLod = 2.5f;

    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(info, line, sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM109);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.FieldName, "MaxLod");
    EXPECT_EQ(sink.Diagnostics()[0].Actual.Kind, loom::DiagnosticValueKind::Real);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.Real, 2.5);
    EXPECT_EQ(sink.Diagnostics()[0].Where.line(), line);
}

TEST(Freeze, BorderAddressWithNoBorderColorIsLM101)
{
    SamplerCreateInfo info{};
    info.AddressV = SamplerCreateInfo::AddressMode::ClampToBorder;

    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(info, line, sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 1u);
    EXPECT_EQ(sink.Diagnostics()[0].Code, loom::DiagnosticCode::LM101);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.FieldName, "BorderColor");
}

// The defaults break three of the unnormalized rules: Linear mips, MaxLod 1e10, and Repeat on U and V.
TEST(Freeze, UnnormalizedSamplerReportsEachBrokenRule)
{
    SamplerCreateInfo info{};
    info.UnnormalizedCoordinates = true;

    loom::CollectingSink sink;
    uint32_t line = 0;
    const std::optional<loom::FrozenGraph> frozen = FreezeOneSampler(info, line, sink);

    ASSERT_FALSE(frozen.has_value());
    ASSERT_EQ(sink.Diagnostics().size(), 4u);
    EXPECT_EQ(sink.Diagnostics()[0].Actual.FieldName, "MipFilter");
    EXPECT_EQ(sink.Diagnostics()[1].Actual.FieldName, "MaxLod");
    EXPECT_EQ(sink.Diagnostics()[2].Actual.FieldName, "AddressU");
    EXPECT_EQ(sink.Diagnostics()[3].Actual.FieldName, "AddressV");
}
