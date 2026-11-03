#include <gtest/gtest.h>
#include "Declarations.hpp"
#include "GraphBuilder.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "Records.hpp"
#include "SequenceBuilder.hpp"
#include <cstdint>
#include <source_location>
#include <string>
#include <variant>

namespace
{

    constexpr loom::ResourceKey<loom::Image> k_Spectrum{ "Test.Spectrum" };
    constexpr loom::ResourceKey<loom::Buffer> k_Args{ "Test.Args" };
    constexpr loom::PassKey k_Init{ "Test.Init" };
    constexpr loom::PassKey k_Update{ "Test.Update" };
    constexpr loom::PassKey k_Resolve{ "Test.Resolve" };
    constexpr loom::SequenceKey k_Tick{ "Test.Tick" };
    constexpr loom::AnchorKey k_Done{ "Test.Done" };
    constexpr loom::AnchorKey k_Middle{ "Test.Middle" };
    constexpr loom::ParamsKey<float> k_Time{ "Test.Time" };

    struct SpectrumParams
    {
        float WindSpeed;
        float Fetch;
        uint32_t Seed;
    };

    constexpr loom::ParamsKey<SpectrumParams> k_SpectrumParams{ "Test.SpectrumParams" };

    uint32_t LineOf(const std::source_location& where)
    {
        return where.line();
    }

}

// The S4 trap: if a template captured its own location, every line here would be a line in GraphBuilder.hpp.
TEST(LoomBuilder, LocationsAreTheCallersLines)
{
    loom::GraphBuilder graph{ "Test", "TestShaders" };

    const uint32_t addLine = __LINE__ + 1;
    graph.AddDispatch(k_Init, loom::RunPolicy::Once, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder& pass) {
        pass.Shader("Ocean", "InitCS");
        pass.Bind("h0K", k_Spectrum, loom::AccessModifier::WriteOnly);
    });

    const loom::GraphRecord& record = graph.Record();
    ASSERT_EQ(record.Passes.size(), 1u);

    const loom::PassRecord& pass = record.Passes[0];
    EXPECT_EQ(LineOf(pass.Where), addLine);
    ASSERT_EQ(pass.Shaders.size(), 1u);
    EXPECT_EQ(LineOf(pass.Shaders[0].Where), addLine + 1);
    ASSERT_EQ(pass.Binds.size(), 1u);
    EXPECT_EQ(LineOf(pass.Binds[0].Where), addLine + 2);
}

TEST(LoomBuilder, NestedLocationsAreTheCallersLines)
{
    loom::GraphBuilder graph{ "Test", "TestShaders" };

    const uint32_t sequenceLine = __LINE__ + 1;
    graph.AddSequence(k_Tick, [&](loom::SequenceBuilder& tick) {
        tick.AddDispatch(k_Update, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder& pass) {
            pass.DispatchThreadsOver(k_Spectrum);
        });
    });

    const loom::GraphRecord& record = graph.Record();
    ASSERT_EQ(record.Sequences.size(), 1u);
    EXPECT_EQ(LineOf(record.Sequences[0].Where), sequenceLine);
    ASSERT_EQ(record.Sequences[0].Passes.size(), 1u);
    EXPECT_EQ(LineOf(record.Sequences[0].Passes[0].Where), sequenceLine + 1);
    EXPECT_EQ(LineOf(record.Sequences[0].Passes[0].Dispatches[0].Where), sequenceLine + 2);
}

TEST(LoomBuilder, RecordsWhatEachCallSaid)
{
    loom::GraphBuilder graph{ "Test", "TestShaders" };
    using namespace loom;

    graph.AddDispatch(k_Init, loom::RunPolicy::WhenInputsChange, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder& pass) {
        pass.Shader("Ocean", "InitCS");
        pass.Pin("IFFT_SIZE", loom::AxisValue::Unsigned(512u));
        pass.After(loom::BeginFrameAnchor);
        pass.Before(k_Done);
        pass.Bind("h0K", k_Spectrum);
        pass.Bind("Args", k_Args, loom::AccessModifier::ReadOnly);
        pass.BindPrevious("Last", k_Spectrum);
        pass.Params(k_SpectrumParams);
        pass.DispatchThreads(64, 32, 1);
        pass.DispatchIndirect(k_Args);
    });

    const loom::PassRecord& pass = graph.Record().Passes[0];
    EXPECT_EQ(pass.Key.Hash, k_Init.Hash);
    EXPECT_EQ(pass.Kind, loom::PassKind::Dispatch);
    EXPECT_EQ(pass.Policy, loom::RunPolicy::WhenInputsChange);
    EXPECT_EQ(pass.Affinity, loom::QueueAffinity::Graphics);

    EXPECT_EQ(pass.Shaders[0].ModuleName, "Ocean");
    EXPECT_EQ(pass.Shaders[0].EntryPoint, "InitCS");
    EXPECT_TRUE(pass.Shaders[0].FragmentEntryPoint.empty());

    ASSERT_EQ(pass.Pins.size(), 1u);
    EXPECT_EQ(pass.Pins[0].AxisName, "IFFT_SIZE");
    EXPECT_EQ(std::get<uint32_t>(pass.Pins[0].Value.Value), 512u);

    EXPECT_EQ(pass.After[0].Anchor.Hash, loom::BeginFrameAnchor.Hash);
    EXPECT_EQ(pass.Before[0].Anchor.Hash, k_Done.Hash);

    ASSERT_EQ(pass.Binds.size(), 3u);
    EXPECT_EQ(pass.Binds[0].Kind, loom::ResourceKind::Image);
    EXPECT_EQ(pass.Binds[0].Narrowing, loom::BindNarrowing::Reflected);
    EXPECT_FALSE(pass.Binds[0].Previous);
    EXPECT_EQ(pass.Binds[1].Kind, loom::ResourceKind::Buffer);
    EXPECT_EQ(pass.Binds[1].Narrowing, loom::BindNarrowing::ReadOnly);
    EXPECT_TRUE(pass.Binds[2].Previous);
    EXPECT_EQ(pass.Binds[2].Narrowing, loom::BindNarrowing::ReadOnly);

    ASSERT_EQ(pass.Params.size(), 1u);
    EXPECT_EQ(pass.Params[0].ByteSize, sizeof(SpectrumParams));
    EXPECT_EQ(pass.Params[0].Alignment, alignof(SpectrumParams));

    // Two dispatch statements are recorded as two, so Freeze can report LM106.
    ASSERT_EQ(pass.Dispatches.size(), 2u);
    EXPECT_EQ(pass.Dispatches[0].Form, loom::DispatchForm::Threads);
    EXPECT_EQ(pass.Dispatches[0].X, 64);
    EXPECT_EQ(pass.Dispatches[0].Y, 32);
    EXPECT_EQ(pass.Dispatches[0].Z, 1);
    EXPECT_EQ(pass.Dispatches[1].Form, loom::DispatchForm::Indirect);
    EXPECT_EQ(pass.Dispatches[1].Resource.Hash, k_Args.Hash);
}

TEST(LoomBuilder, SequenceKeepsSourceOrderAndAnchorPositions)
{
    loom::GraphBuilder graph{ "Test", "TestShaders" };

    graph.AddSequence(k_Tick, [&](loom::SequenceBuilder& tick) {
        tick.After(loom::BeginFrameAnchor);
        tick.Before(k_Done);
        tick.AddDispatch(k_Update, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder& pass) {
            pass.Params(k_Time);
        });
        tick.ExposeAnchor(k_Middle);
        tick.AddDispatch(k_Resolve, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder&) {
        });
    });

    const loom::SequenceRecord& sequence = graph.Record().Sequences[0];
    EXPECT_EQ(sequence.Key.Hash, k_Tick.Hash);
    ASSERT_EQ(sequence.Passes.size(), 2u);
    EXPECT_EQ(sequence.Passes[0].Key.Hash, k_Update.Hash);
    EXPECT_EQ(sequence.Passes[1].Key.Hash, k_Resolve.Hash);

    // The anchor sits after Update and before Resolve.
    ASSERT_EQ(sequence.ExposedAnchors.size(), 1u);
    EXPECT_EQ(sequence.ExposedAnchors[0].Position, 1);

    EXPECT_EQ(sequence.After.size(), 1u);
    EXPECT_EQ(sequence.Before.size(), 1u);
    EXPECT_TRUE(graph.Record().Passes.empty());
}

TEST(LoomBuilder, DeclarationOrderCountsAcrossTheGraph)
{
    loom::GraphBuilder graph{ "Test", "TestShaders" };

    graph.AddDispatch(k_Init, loom::RunPolicy::Once, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder&) {});
    graph.AddSequence(k_Tick, [&](loom::SequenceBuilder& tick) {
        tick.AddDispatch(k_Update, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder&) {});
        tick.AddDispatch(k_Resolve, loom::RunPolicy::EveryFrame, loom::QueueAffinity::Graphics, [&](loom::DispatchBuilder&) {});
    });

    const loom::GraphRecord& record = graph.Record();
    EXPECT_EQ(record.Passes[0].DeclarationOrder, 0);
    EXPECT_EQ(record.Sequences[0].Passes[0].DeclarationOrder, 1);
    EXPECT_EQ(record.Sequences[0].Passes[1].DeclarationOrder, 2);
    EXPECT_EQ(record.NextDeclarationOrder, 3);
}

TEST(LoomBuilder, GraphLevelDeclarations)
{
    constexpr loom::FrameTargetKey k_Swapchain{ "Test.Swapchain" };

    loom::GraphBuilder graph{ "Test", "TestShaders" };
    graph.Pin("IFFT_SIZE", loom::AxisValue::EnumCase("Large"));
    graph.Declare(loom::BufferDeclaration{ .Key = k_Args, .Lifetime = loom::ResourceLifetime::Frame,
        .Size = loom::SizeSource::Bytes(256), .Cpu = loom::CpuAccess::None });
    graph.Import(k_Swapchain);
    graph.DeclareAnchor(k_Done);
    graph.OrderAnchors(k_Middle, k_Done);

    const loom::GraphRecord& record = graph.Record();
    EXPECT_EQ(record.Name, "Test");
    EXPECT_EQ(record.ShaderLibraryName, "TestShaders");
    EXPECT_EQ(std::get<loom::AxisValue::EnumName>(record.Pins[0].Value.Value).Name, "Large");
    ASSERT_EQ(record.Buffers.size(), 1u);
    EXPECT_EQ(record.Buffers[0].Value.Size.Size, 256);
    EXPECT_EQ(record.FrameTargets[0].Target.Hash, k_Swapchain.Hash);
    EXPECT_EQ(record.Anchors[0].Anchor.Hash, k_Done.Hash);
    EXPECT_EQ(record.AnchorOrders[0].Earlier.Hash, k_Middle.Hash);
    EXPECT_EQ(record.AnchorOrders[0].Later.Hash, k_Done.Hash);
}

// A field left out of a declaration keeps its "not set" value, so Freeze can name it (LM101, LM109).
TEST(LoomBuilder, MissingFieldsKeepTheirUnsetValues)
{
    loom::GraphBuilder graph{ "Test", "TestShaders" };
    graph.Declare(loom::ImageDeclaration{ .Key = k_Spectrum });

    const loom::ImageDeclaration& image = graph.Record().Images[0].Value;
    EXPECT_EQ(image.Lifetime, loom::ResourceLifetime::Invalid);
    EXPECT_EQ(image.Extent.Kind, loom::ExtentSource::Kind::Invalid);
    EXPECT_EQ(image.Mips.Kind, loom::MipSource::Kind::Invalid);
    EXPECT_EQ(image.Layers, -1);
    EXPECT_EQ(image.Cpu, loom::CpuAccess::Invalid);
}
