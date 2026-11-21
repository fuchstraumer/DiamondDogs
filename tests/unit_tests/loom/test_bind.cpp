#include "Declarations.hpp"
#include "PassStatements.hpp"
#include "bind/BoundGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "order/OrderedGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "SequenceBuilder.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestDevice.hpp"
#include "freeze_test_helpers.hpp"
#include "ShaderLibrary.host.hpp"
#include "utility/Delegate.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

// The Bind stage against LoomBindTest (tests/unit_tests/loom/shaders), with no device. Each error test builds a graph
// with exactly one broken statement. BindCS reads Scratch only when USE_SCRATCH is on, and its shim parameter
// `weights` is a bound buffer on spirv and a buffer device address in the push constants on spirv-modern-mutable.

using namespace freeze_tests;

namespace
{

    namespace host = loom_test_shaders;
    namespace names = loom_test_shaders::LoomBindTest::BindCS;
    using SpirvParams = names::Params<host::CookProfile::Spirv>;
    using ModernParams = names::Params<host::CookProfile::SpirvModernMutable>;

    constexpr std::string_view k_Module = "LoomBindTest";
    constexpr std::string_view k_SpirvProfile = "spirv";
    constexpr std::string_view k_ModernProfile = "spirv-modern-mutable";

    // Stub for Lodestone request C26: the generated constant for the shim is "data", its inner field. Loom matches
    // the parameter name.
    constexpr std::string_view k_WeightsName = "weights";

    constexpr loom::ResourceKey<loom::Buffer> k_Input{ "Bind.Input" };
    constexpr loom::ResourceKey<loom::Buffer> k_Output{ "Bind.Output" };
    constexpr loom::ResourceKey<loom::Buffer> k_Scratch{ "Bind.Scratch" };
    constexpr loom::ResourceKey<loom::Buffer> k_Settings{ "Bind.Settings" };
    constexpr loom::ResourceKey<loom::Buffer> k_Weights{ "Bind.Weights" };
    constexpr loom::ResourceKey<loom::Buffer> k_Tints{ "Bind.Tints" };
    constexpr loom::ResourceKey<loom::Image> k_Texture{ "Bind.Texture" };
    constexpr loom::ResourceKey<loom::Image> k_Target{ "Bind.Target" };
    constexpr loom::ResourceKey<loom::Sampler> k_Linear{ "Bind.Linear" };
    constexpr loom::ResourceKey<loom::Buffer> k_Shapes{ "Bind.Shapes" };
    constexpr loom::ResourceKey<loom::Buffer> k_Values{ "Bind.Values" };
    constexpr loom::ResourceKey<loom::Buffer> k_Result{ "Bind.Result" };
    constexpr loom::ParamsKey<SpirvParams> k_SpirvParams{ "Bind.SpirvParams" };
    constexpr loom::ParamsKey<ModernParams> k_ModernParams{ "Bind.ModernParams" };
    constexpr loom::PassKey k_Second{ "Test.Second" };

    const rhi::ShaderBundle& TestBundle(std::string_view profile)
    {
        static std::optional<rhi::ShaderBundle> spirv;
        static std::optional<rhi::ShaderBundle> modern;
        std::optional<rhi::ShaderBundle>& slot = (profile == k_SpirvProfile) ? spirv : modern;

        if (!slot.has_value())
        {
            rhi::ShaderBundleResult<rhi::ShaderBundle> opened = rhi::ShaderBundle::Open(LOOM_TEST_BUNDLE, profile);
            EXPECT_TRUE(opened.has_value()) << rhi::ToString(opened.error());
            slot.emplace(std::move(*opened));
        }

        return *slot;
    }

    // Every capability is supported: Bind's tests do not test the device.
    bool SupportsEverything(void*, std::string_view)
    {
        return true;
    }

    lodestone::manifest::DeviceLimits RoomyLimits()
    {
        return lodestone::manifest::DeviceLimits
        {
            .MaxWorkgroupMemoryBytes = 49152u,
            .MaxInvocationsPerWorkgroup = 1024u,
            .MaxWorkgroupSizeX = 1024u,
            .MaxWorkgroupSizeY = 1024u,
            .MaxWorkgroupSizeZ = 64u,
            .MaxPushConstantBytes = 256u,
        };
    }

    loom::BufferDeclaration MakeBufferOf(loom::ResourceKey<loom::Buffer> key, loom::CpuAccess cpu)
    {
        loom::BufferDeclaration buffer = MakeBuffer();
        buffer.Key = key;
        buffer.Cpu = cpu;
        return buffer;
    }

    loom::ImageDeclaration MakeImageOf(loom::ResourceKey<loom::Image> key)
    {
        loom::ImageDeclaration image = MakeImageOfTarget(k_Backbuffer);
        image.Key = key;
        return image;
    }

    void DeclareBindResources(loom::GraphBuilder& graph)
    {
        graph.Import(k_Backbuffer);

        for (const loom::ResourceKey<loom::Buffer> key : { k_Input, k_Output, k_Scratch, k_Settings, k_Weights, k_Tints })
        {
            graph.Declare(MakeBufferOf(key, loom::CpuAccess::None));
        }

        graph.Declare(MakeImageOf(k_Texture));
        graph.Declare(MakeImageOf(k_Target));
        graph.Declare(loom::SamplerDeclaration{ .Key = k_Linear, .CreateInfo = {} });
    }

    // Which statements BuildCompute writes. The default is a correct pass with USE_SCRATCH off.
    struct ComputeBinds
    {
        bool UseScratch{ false };
        bool BindScratch{ false };
        bool BindSettings{ true };
        bool BindParams{ true };
        bool ModernParams{ false };
        std::string_view ScratchName{ names::SomeVariants::Scratch };
        loom::AccessModifier InputAccess{ loom::AccessModifier::Invalid };    // Invalid: the reflected access
        loom::AccessModifier OutputAccess{ loom::AccessModifier::Invalid };
    };

    void BindBuffer(loom::DispatchBuilder& pass, std::string_view name, loom::ResourceKey<loom::Buffer> key,
                    loom::AccessModifier access)
    {
        if (access == loom::AccessModifier::Invalid)
        {
            pass.Bind(name, key);
            return;
        }

        pass.Bind(name, key, access);
    }

    void BuildCompute(loom::DispatchBuilder& pass, const ComputeBinds& binds)
    {
        pass.Shader(k_Module, "BindCS");
        pass.Pin("USE_SCRATCH", loom::AxisValue::Boolean(binds.UseScratch));
        BindBuffer(pass, names::Input, k_Input, binds.InputAccess);
        BindBuffer(pass, names::Output, k_Output, binds.OutputAccess);
        pass.Bind(names::Albedo, k_Texture);
        pass.Bind(names::Linear, k_Linear);
        pass.Bind(k_WeightsName, k_Weights);

        if (binds.BindScratch)
        {
            pass.Bind(binds.ScratchName, k_Scratch);
        }

        if (binds.BindSettings)
        {
            pass.Bind(names::Settings, k_Settings);
        }

        if (binds.BindParams && binds.ModernParams)
        {
            pass.Params(k_ModernParams);
        }
        else if (binds.BindParams)
        {
            pass.Params(k_SpirvParams);
        }

        pass.DispatchThreads(64, 1, 1);
    }

    struct BindRun
    {
        std::optional<loom::FrozenGraph> Frozen;
        std::optional<loom::ResolvedGraph> Resolved;
        std::optional<loom::BoundGraph> Bound;
        loom::CollectingSink Sink;
    };

    // Freeze, Resolve and Bind. Each stage runs only when the stage before it succeeded.
    void RunBind(const loom::GraphBuilder& graph, std::string_view profile, BindRun& run)
    {
        run.Frozen = loom::Freeze(graph.Record(), run.Sink);
        ASSERT_TRUE(run.Frozen.has_value()) << run.Sink.Diagnostics().size() << " Freeze diagnostics";

        const loom::ResolveInputs inputs
        {
            .Bundle = TestBundle(profile),
            .Supports = &SupportsEverything,
            .SupportsUser = nullptr,
            .Limits = RoomyLimits(),
        };

        run.Resolved = loom::Resolve(*run.Frozen, inputs, run.Sink);
        ASSERT_TRUE(run.Resolved.has_value()) << run.Sink.Diagnostics().size() << " Resolve diagnostics";

        run.Bound = loom::Bind(*run.Frozen, *run.Resolved, run.Sink);
    }

    void RunComputeBind(const ComputeBinds& binds, std::string_view profile, BindRun& run)
    {
        loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
        DeclareBindResources(graph);

        const auto build = [&binds](loom::DispatchBuilder& pass)
        {
            BuildCompute(pass, binds);
        };

        graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);
        RunBind(graph, profile, run);
    }

    loom::ResourceIndex IndexOf(const loom::FrozenGraph& frozen, loom::KeyRef key)
    {
        for (size_t row = 0u; row < frozen.Resources.size(); ++row)
        {
            if (frozen.Resources[row].Key == key)
            {
                return loom::ResourceIndex{ static_cast<int32_t>(row) };
            }
        }

        ADD_FAILURE() << key.Name << " is not in the frozen graph";
        return loom::ResourceIndex{};
    }

    // The slot of pass 0 that holds `key`, or null when no slot does.
    const loom::BindingSlot* SlotOf(const BindRun& run, loom::KeyRef key)
    {
        const loom::BoundPass& pass = run.Bound->Passes[0];
        const loom::ResourceIndex resource = IndexOf(*run.Frozen, key);

        for (int32_t slot = pass.FirstSlot; slot < pass.FirstSlot + pass.SlotCount; ++slot)
        {
            if (run.Bound->Slots[slot].Resource == resource)
            {
                return &run.Bound->Slots[slot];
            }
        }

        return nullptr;
    }

    // The access that Bind gives Order for the bind of `key` in pass 0.
    loom::ResourceAccess AccessOf(const BindRun& run, loom::KeyRef key)
    {
        const loom::ResourceIndex resource = IndexOf(*run.Frozen, key);
        const std::vector<loom::FrozenBind>& binds = run.Frozen->Passes[0].Binds;

        for (size_t bindIndex = 0u; bindIndex < binds.size(); ++bindIndex)
        {
            if (binds[bindIndex].Resource == resource)
            {
                return run.Bound->Accesses.Accesses[0][bindIndex];
            }
        }

        ADD_FAILURE() << key.Name << " has no bind in pass 0";
        return loom::ResourceAccess::Invalid;
    }

    void RecordNothing(loom::RecordContext&)
    {
    }

    loom::DiagnosticCode OnlyCode(const loom::CollectingSink& sink)
    {
        if (sink.Diagnostics().size() != 1u)
        {
            ADD_FAILURE() << sink.Diagnostics().size() << " diagnostics, expected 1";
            return loom::DiagnosticCode::Invalid;
        }

        return sink.Diagnostics().front().Code;
    }

}

// The slot table: one slot per binding of the selected variant, each with its key and its placement.
TEST(Bind, DispatchFillsOneSlotPerBinding)
{
    BindRun run;
    RunComputeBind(ComputeBinds{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Bound.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());

    // Input, Output, Settings, Albedo, Linear, weights. Scratch is not in the USE_SCRATCH off variant.
    const loom::BoundPass& pass = run.Bound->Passes[0];
    EXPECT_EQ(pass.FirstSlot, 0);
    EXPECT_EQ(pass.SlotCount, 6);

    for (int32_t slot = pass.FirstSlot; slot < pass.FirstSlot + pass.SlotCount; ++slot)
    {
        EXPECT_TRUE(run.Bound->Slots[slot].Resource.IsValid()) << "slot " << slot;
        EXPECT_EQ(run.Bound->Slots[slot].Form, lodestone::PlacementKind::Bound) << "slot " << slot;
        EXPECT_EQ(run.Bound->Slots[slot].Stages, lodestone::ShaderStageKind::Compute) << "slot " << slot;
    }

    const loom::BindingSlot* input = SlotOf(run, k_Input.ToRef());
    const loom::BindingSlot* output = SlotOf(run, k_Output.ToRef());
    const loom::BindingSlot* settings = SlotOf(run, k_Settings.ToRef());
    const loom::BindingSlot* texture = SlotOf(run, k_Texture.ToRef());
    const loom::BindingSlot* sampler = SlotOf(run, k_Linear.ToRef());
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    ASSERT_NE(settings, nullptr);
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(sampler, nullptr);

    EXPECT_EQ(input->Kind, lodestone::BindingKind::StorageBuffer);
    EXPECT_EQ(input->Access, loom::ResourceAccess::Read);
    EXPECT_EQ(output->Access, loom::ResourceAccess::ReadWrite);
    EXPECT_EQ(settings->Kind, lodestone::BindingKind::UniformBuffer);
    EXPECT_EQ(settings->Access, loom::ResourceAccess::Read);
    EXPECT_EQ(texture->Kind, lodestone::BindingKind::Texture);
    EXPECT_EQ(sampler->Kind, lodestone::BindingKind::Sampler);
    EXPECT_NE(input->Binding, output->Binding);

    EXPECT_EQ(AccessOf(run, k_Output.ToRef()), loom::ResourceAccess::ReadWrite);
    EXPECT_EQ(AccessOf(run, k_Input.ToRef()), loom::ResourceAccess::Read);

    EXPECT_EQ(pass.Params.Delivery, lodestone::EntryPointConstantsDelivery::PushConstant);
    EXPECT_EQ(pass.Params.ByteSize, static_cast<uint32_t>(sizeof(SpirvParams)));
    EXPECT_EQ(pass.Params.ParamsIndex, 0);
}

// A bind whose binding is in other variants only: no diagnostic, no slot, and no access for Order. The same bind in
// the variant that reads Scratch gets a slot.
TEST(Bind, BindOfAVariantOnlyBindingIsSkipped)
{
    BindRun off;
    RunComputeBind(ComputeBinds{ .BindScratch = true }, k_SpirvProfile, off);

    ASSERT_TRUE(off.Bound.has_value());
    EXPECT_TRUE(off.Sink.Diagnostics().empty());
    EXPECT_EQ(SlotOf(off, k_Scratch.ToRef()), nullptr);
    EXPECT_EQ(AccessOf(off, k_Scratch.ToRef()), loom::ResourceAccess::Invalid);

    BindRun on;
    RunComputeBind(ComputeBinds{ .UseScratch = true, .BindScratch = true }, k_SpirvProfile, on);

    ASSERT_TRUE(on.Bound.has_value());
    EXPECT_TRUE(on.Sink.Diagnostics().empty());
    EXPECT_EQ(on.Bound->Passes[0].SlotCount, 7);

    const loom::BindingSlot* scratch = SlotOf(on, k_Scratch.ToRef());
    ASSERT_NE(scratch, nullptr);
    EXPECT_EQ(scratch->Access, loom::ResourceAccess::ReadWrite);
    EXPECT_EQ(AccessOf(on, k_Scratch.ToRef()), loom::ResourceAccess::ReadWrite);
}

// LM301: a name in no variant. The suggestion comes from every variant, so a near miss of Scratch finds it even
// where the selected variant lacks it.
TEST(Bind, UnknownNameIsLM301WithSuggestion)
{
    BindRun run;
    RunComputeBind(ComputeBinds{ .BindScratch = true, .ScratchName = "Scrach" }, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM301);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, "Scrach");
    EXPECT_EQ(diagnostic.Suggestion, "Scratch");
    EXPECT_EQ(diagnostic.Pass.Hash, k_First.Hash);
    EXPECT_EQ(diagnostic.Resource, k_Scratch.ToRef());
}

// LM302: a binding of the selected variant that no Bind names.
TEST(Bind, UnboundBindingIsLM302)
{
    BindRun run;
    RunComputeBind(ComputeBinds{ .BindSettings = false }, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM302);
    EXPECT_EQ(run.Sink.Diagnostics().front().Binding, names::Settings);
    EXPECT_EQ(run.Sink.Diagnostics().front().Pass.Hash, k_First.Hash);
}

// LM302: constants that hold caller data, and no Params statement.
TEST(Bind, MissingParamsIsLM302)
{
    BindRun run;
    RunComputeBind(ComputeBinds{ .BindParams = false }, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM302);
    EXPECT_EQ(run.Sink.Diagnostics().front().Binding, "Params");
}

// LM303: Input is read-only, so a write-only narrowing adds write access.
TEST(Bind, NarrowingThatAddsAccessIsLM303)
{
    BindRun run;
    RunComputeBind(ComputeBinds{ .InputAccess = loom::AccessModifier::WriteOnly }, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM303);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Binding, names::Input);
    EXPECT_EQ(diagnostic.Expected.Text, "Read");
    EXPECT_EQ(diagnostic.Actual.Text, "Write");
}

// A narrowing that removes access is kept, in the slot and in Order's table.
TEST(Bind, NarrowingThatRemovesAccessIsKept)
{
    BindRun run;
    RunComputeBind(ComputeBinds{ .OutputAccess = loom::AccessModifier::WriteOnly }, k_SpirvProfile, run);

    ASSERT_TRUE(run.Bound.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());

    const loom::BindingSlot* output = SlotOf(run, k_Output.ToRef());
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->Access, loom::ResourceAccess::Write);
    EXPECT_EQ(AccessOf(run, k_Output.ToRef()), loom::ResourceAccess::Write);
}

// LM304: an image key on a storage buffer binding.
TEST(Bind, WrongKeyKindIsLM304)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareBindResources(graph);

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "BindCS");
        pass.Pin("USE_SCRATCH", loom::AxisValue::Boolean(false));
        pass.Bind(names::Input, k_Target);
        pass.Bind(names::Output, k_Output);
        pass.Bind(names::Settings, k_Settings);
        pass.Bind(names::Albedo, k_Texture);
        pass.Bind(names::Linear, k_Linear);
        pass.Bind(k_WeightsName, k_Weights);
        pass.Params(k_SpirvParams);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    BindRun run;
    RunBind(graph, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM304);
    EXPECT_EQ(run.Sink.Diagnostics().front().Binding, names::Input);
    EXPECT_EQ(run.Sink.Diagnostics().front().Resource, k_Target.ToRef());
}

// LM305: the Params type of the other profile. The modern block also holds the 8-byte address of `weights`.
TEST(Bind, ParamsOfTheWrongProfileIsLM305)
{
    static_assert(sizeof(SpirvParams) != sizeof(ModernParams));

    BindRun run;
    RunComputeBind(ComputeBinds{ .ModernParams = true }, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM305);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics().front();
    EXPECT_EQ(diagnostic.Expected.Integer, static_cast<int64_t>(sizeof(SpirvParams)));
    EXPECT_EQ(diagnostic.Actual.Integer, static_cast<int64_t>(sizeof(ModernParams)));
}

// LM305: Params on an entry point with no constants. OtherCS (LoomResolveOther) has none.
TEST(Bind, ParamsWithoutConstantsIsLM305)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareBindResources(graph);

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("LoomResolveOther", "OtherCS");
        pass.Pin("INVERT", loom::AxisValue::Boolean(false));
        pass.Pin("SMOOTH", loom::AxisValue::Boolean(false));
        pass.Bind(host::LoomResolveOther::OtherCS::Shapes, k_Input);
        pass.Bind(host::LoomResolveOther::OtherCS::Values, k_Output);
        pass.Params(k_SpirvParams);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    BindRun run;
    RunBind(graph, k_SpirvProfile, run);

    EXPECT_FALSE(run.Bound.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM305);
    EXPECT_EQ(run.Sink.Diagnostics().front().Expected.Integer, 0);
}

// The placement forms (G3). On spirv-modern-mutable, `weights` is an address that Loom writes into the push constants,
// after the caller's 8 bytes. On spirv it is an ordinary bound buffer.
TEST(Bind, ShimIsAPointerOnTheModernProfile)
{
    BindRun modern;
    RunComputeBind(ComputeBinds{ .ModernParams = true }, k_ModernProfile, modern);

    ASSERT_TRUE(modern.Bound.has_value());
    EXPECT_TRUE(modern.Sink.Diagnostics().empty());

    const loom::BindingSlot* pointer = SlotOf(modern, k_Weights.ToRef());
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(pointer->Form, lodestone::PlacementKind::Pointer);
    EXPECT_EQ(pointer->Container, lodestone::StoredValueContainer::PushConstants);
    EXPECT_EQ(pointer->ValueOffset, 8u);
    EXPECT_EQ(modern.Bound->Passes[0].Params.ByteSize, static_cast<uint32_t>(sizeof(ModernParams)));

    BindRun spirv;
    RunComputeBind(ComputeBinds{}, k_SpirvProfile, spirv);

    ASSERT_TRUE(spirv.Bound.has_value());
    const loom::BindingSlot* bound = SlotOf(spirv, k_Weights.ToRef());
    ASSERT_NE(bound, nullptr);
    EXPECT_EQ(bound->Form, lodestone::PlacementKind::Bound);
    EXPECT_EQ(bound->Container, lodestone::StoredValueContainer::None);
}

// A draw merges its stages: Tints is read by both, Albedo and Linear by the fragment stage only.
TEST(Bind, DrawMergesItsStages)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareBindResources(graph);

    const auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders(k_Module, "BindVS", "BindPS");
        pass.Bind(host::LoomBindTest::BindPS::Tints, k_Tints);
        pass.Bind(host::LoomBindTest::BindPS::Albedo, k_Texture);
        pass.Bind(host::LoomBindTest::BindPS::Linear, k_Linear);
        pass.Color(0, k_Target, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    BindRun run;
    RunBind(graph, k_SpirvProfile, run);

    ASSERT_TRUE(run.Bound.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(run.Bound->Passes[0].SlotCount, 3);

    const auto both = static_cast<lodestone::ShaderStageKind>(static_cast<uint32_t>(lodestone::ShaderStageKind::Vertex) |
                                                              static_cast<uint32_t>(lodestone::ShaderStageKind::Fragment));
    const loom::BindingSlot* tints = SlotOf(run, k_Tints.ToRef());
    const loom::BindingSlot* texture = SlotOf(run, k_Texture.ToRef());
    ASSERT_NE(tints, nullptr);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(tints->Stages, both);
    EXPECT_EQ(texture->Stages, lodestone::ShaderStageKind::Fragment);
}

// A Record pass has no shader, so it has no slots and no params.
TEST(Bind, RecordPassHasNoSlots)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    const auto build = [](loom::RecordBuilder& pass)
    {
        pass.Uses(k_Data, loom::ResourceAccess::ReadWrite, loom::BufferUse::Storage);
        pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
    };

    graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

    BindRun run;
    RunBind(graph, k_SpirvProfile, run);

    ASSERT_TRUE(run.Bound.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(run.Bound->Passes[0].SlotCount, 0);
    EXPECT_TRUE(run.Bound->Slots.empty());
    EXPECT_EQ(run.Bound->Passes[0].Params.ParamsIndex, -1);
}

// The whole front half with no hand-made table: Bind's accesses give Order its data edge. First writes Values (a
// write-only narrowing), and Second reads it through Shapes.
TEST(Bind, AccessesFeedOrder)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBufferOf(k_Shapes, loom::CpuAccess::Upload));
    graph.Declare(MakeBufferOf(k_Values, loom::CpuAccess::None));
    graph.Declare(MakeBufferOf(k_Result, loom::CpuAccess::Readback));

    const auto other = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("LoomResolveOther", "OtherCS");
        pass.Pin("INVERT", loom::AxisValue::Boolean(false));
        pass.Pin("SMOOTH", loom::AxisValue::Boolean(false));
        pass.DispatchThreads(64, 1, 1);
    };

    const auto first = [&other](loom::DispatchBuilder& pass)
    {
        other(pass);
        pass.Bind(host::LoomResolveOther::OtherCS::Shapes, k_Shapes);
        pass.Bind(host::LoomResolveOther::OtherCS::Values, k_Values, loom::AccessModifier::WriteOnly);
    };

    const auto second = [&other](loom::DispatchBuilder& pass)
    {
        other(pass);
        pass.Bind(host::LoomResolveOther::OtherCS::Shapes, k_Values);
        pass.Bind(host::LoomResolveOther::OtherCS::Values, k_Result, loom::AccessModifier::WriteOnly);
    };

    // declared in the reverse order, so only the data edge can put First before Second
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, second);
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, first);

    BindRun run;
    RunBind(graph, k_SpirvProfile, run);
    ASSERT_TRUE(run.Bound.has_value());

    const std::optional<loom::OrderedGraph> ordered = loom::Order(*run.Frozen, run.Bound->Accesses, run.Sink);
    ASSERT_TRUE(ordered.has_value());
    EXPECT_EQ(run.Sink.ErrorCount(), 0);
    ASSERT_EQ(ordered->Passes.size(), 2u);
    EXPECT_EQ(run.Frozen->Passes[ordered->Passes[0].Value].Key, k_First.ToRef());
    EXPECT_EQ(run.Frozen->Passes[ordered->Passes[1].Value].Key, k_Second.ToRef());

    const auto isDataFlow = [](const loom::OrderEdge& edge)
    {
        return edge.Reason == loom::EdgeReason::DataFlow;
    };

    EXPECT_EQ(std::ranges::count_if(ordered->Edges, isDataFlow), 1);
}

// Freeze's bind rules, which need no shader.

// LM306: BindPrevious reads last frame's slot of a history ring, so a resource with no history cannot give it.
TEST(BindFreeze, BindPreviousWithoutHistoryIsLM306)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "BindCS");
        pass.BindPrevious("Input", k_Data);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    EXPECT_FALSE(loom::Freeze(graph.Record(), sink).has_value());
    ASSERT_EQ(OnlyCode(sink), loom::DiagnosticCode::LM306);
    EXPECT_EQ(sink.Diagnostics().front().Binding, "Input");
    EXPECT_EQ(sink.Diagnostics().front().Resource, k_Data.ToRef());
}

TEST(BindFreeze, BindPreviousWithHistoryIsValid)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    loom::BufferDeclaration buffer = MakeBuffer();
    buffer.HistoryDepth = 1;
    buffer.NoHistoryAction = loom::OnNoHistory::Skip;
    graph.Declare(buffer);

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "BindCS");
        pass.BindPrevious("Input", k_Data);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    EXPECT_TRUE(loom::Freeze(graph.Record(), sink).has_value());
    EXPECT_TRUE(sink.Diagnostics().empty());
}

// LM113: one binding name bound twice in a pass. Where is the second bind, OtherWhere the first.
TEST(BindFreeze, RepeatedBindingNameIsLM113)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    DeclareBindResources(graph);
    uint32_t firstLine = 0u;
    uint32_t secondLine = 0u;

    const auto build = [&](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "BindCS");
        firstLine = std::source_location::current().line() + 1u;
        pass.Bind(names::Input, k_Input);
        secondLine = std::source_location::current().line() + 1u;
        pass.Bind(names::Input, k_Output);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    EXPECT_FALSE(loom::Freeze(graph.Record(), sink).has_value());
    ASSERT_EQ(OnlyCode(sink), loom::DiagnosticCode::LM113);
    EXPECT_EQ(sink.Diagnostics().front().Where.line(), secondLine);
    EXPECT_EQ(sink.Diagnostics().front().OtherWhere.line(), firstLine);
    EXPECT_EQ(sink.Diagnostics().front().Binding, names::Input);
}

// LM113: Params twice in a pass.
TEST(BindFreeze, RepeatedParamsIsLM113)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "BindCS");
        pass.Params(k_SpirvParams);
        pass.Params(k_SpirvParams);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    loom::CollectingSink sink;
    EXPECT_FALSE(loom::Freeze(graph.Record(), sink).has_value());
    EXPECT_EQ(OnlyCode(sink), loom::DiagnosticCode::LM113);
}
