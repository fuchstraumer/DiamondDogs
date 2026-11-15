#include "resolve/ResolvedGraph.hpp"
#include "resolve/ResolveContext.hpp"
#include "freeze/FrozenGraph.hpp"
#include "GraphBuilder.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "PassBuilders.hpp"
#include "Records.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestDevice.hpp"
#include "ShaderManifestIndex.hpp"
#include "freeze_test_helpers.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>
#include <gtest/gtest.h>

// The Resolve stage against the test bundle (tests/unit_tests/loom/shaders), with no device. The device is a fake
// capability table. Each error test builds a graph with exactly one broken pass.
//
// The tests named  wait for the author's part of Resolve (Shaders.cpp, Pins.cpp, Variants.cpp). Turn each on
// when its step is written. They state the agreed behavior; a test may change where a behavior choice is still open.

using namespace freeze_tests;

namespace
{

    constexpr std::string_view k_Module = "LoomResolveTest";
    constexpr std::string_view k_OtherModule = "LoomResolveOther";
    constexpr std::string_view k_ComputeEntry = "ResolveCS";
    constexpr std::string_view k_VertexEntry = "ResolveVS";
    constexpr std::string_view k_FragmentEntry = "ResolvePS";
    constexpr std::string_view k_SpirvProfile = "spirv";
    constexpr std::string_view k_ModernProfile = "spirv-modern-mutable";

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

    const lodestone::ManifestIndex& TestIndex(std::string_view module_name)
    {
        const rhi::ShaderBundleResult<const lodestone::ManifestIndex*> index =
            TestBundle(k_SpirvProfile).ModuleIndex(module_name);
        EXPECT_TRUE(index.has_value());
        return **index;
    }

    // A device that supports every capability except the names in Missing.
    struct FakeDevice
    {
        std::vector<std::string_view> Missing;
    };

    bool FakeSupports(void* user, std::string_view capability_name)
    {
        const FakeDevice* device = static_cast<const FakeDevice*>(user);
        return std::ranges::find(device->Missing, capability_name) == device->Missing.end();
    }

    // Large enough for every instance in the test bundle, so only capabilities reject.
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

    loom::PinRecord MakePin(std::string_view axis_name, loom::AxisValue value)
    {
        return loom::PinRecord{ .AxisName = std::string{ axis_name }, .Value = value, .Where = {} };
    }

    // One axis value of a variant, by axis name.
    lodestone::QueryAxisValue ValueOf(const lodestone::ManifestIndex& index,
                                      lodestone::VariantKey key,
                                      std::string_view axis_name)
    {
        const std::vector<std::string_view> names = index.AxisNames();
        const auto found = std::ranges::find(names, axis_name);
        EXPECT_NE(found, names.end()) << axis_name;
        const std::vector<lodestone::QueryAxisValue> values = index.Decode(key);
        return values[static_cast<size_t>(std::distance(names.begin(), found))];
    }

    // The capability names that ResolveCS needs with USE_WAVE_OPS on and not with it off. Derived from the bundle, so
    // the tests do not depend on the names that one compiler version emits.
    std::vector<std::string_view> WaveOnlyCapabilities()
    {
        const lodestone::ManifestIndex& index = TestIndex(k_Module);
        const lodestone::manifest::EnvironmentView& environment = index.View();
        const uint32_t entry = *environment.Module().FindEntryPointIndex(k_ComputeEntry);

        const auto firstKey = [&](bool wave_ops) -> lodestone::VariantKey
        {
            const lodestone::QueryResult<std::vector<lodestone::VariantKey>> keys =
                index.Query().Where("USE_WAVE_OPS", wave_ops).Keys();
            EXPECT_TRUE(keys.has_value() && !keys->empty());
            return keys->front();
        };

        const lodestone::manifest::EntryPointInstanceView on = environment.VariantByKey(firstKey(true))->EntryPoint(entry);
        const lodestone::manifest::EntryPointInstanceView off = environment.VariantByKey(firstKey(false))->EntryPoint(entry);

        std::vector<std::string_view> names;
        const uint32_t nameCount = static_cast<uint32_t>(environment.CapabilityNames().size());

        for (uint32_t capability = 0u; capability < nameCount; ++capability)
        {
            if (on.RequiresCapability(capability) && !off.RequiresCapability(capability))
            {
                names.emplace_back(environment.CapabilityName(capability));
            }
        }

        return names;
    }

    loom::FrozenPass MakeShaderPass(loom::PassKind kind, std::string_view entry, std::string_view fragment_entry)
    {
        loom::FrozenPass pass{};
        pass.Kind = kind;
        pass.Shader.ModuleName = std::string{ k_Module };
        pass.Shader.EntryPoint = std::string{ entry };
        pass.Shader.FragmentEntryPoint = std::string{ fragment_entry };
        return pass;
    }

    void ResolveCompute(loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, k_ComputeEntry);
        pass.DispatchThreads(64, 1, 1);
    }

    void RecordNothing(loom::RecordContext&)
    {
    }

    // Freezes the graph and resolves it against the test bundle and the fake device.
    struct ResolveRun
    {
        std::optional<loom::FrozenGraph> Frozen;
        std::optional<loom::ResolvedGraph> Resolved;
        loom::CollectingSink Sink;
    };

    void RunResolve(const loom::GraphBuilder& graph, const FakeDevice& device, std::string_view profile, ResolveRun& run)
    {
        run.Frozen = loom::Freeze(graph.Record(), run.Sink);
        ASSERT_TRUE(run.Frozen.has_value()) << run.Sink.Diagnostics().size() << " Freeze diagnostics";

        const loom::ResolveInputs inputs
        {
            .Bundle = TestBundle(profile),
            .Supports = &FakeSupports,
            .SupportsUser = const_cast<FakeDevice*>(&device),
            .Limits = RoomyLimits(),
        };

        run.Resolved = loom::Resolve(*run.Frozen, inputs, run.Sink);
    }

    int32_t CountOf(const loom::CollectingSink& sink, loom::DiagnosticCode code)
    {
        return static_cast<int32_t>(std::ranges::count(sink.Diagnostics(), code, &loom::Diagnostic::Code));
    }

    // The code of the one diagnostic, or Invalid when the sink holds another count. Unlike OnlyDiagnostic, an empty
    // sink fails the test instead of reading past the end.
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

// Plumbing: the bundle, the helpers and the fake device. These run today.

TEST(ResolvePlumbing, BundleHasBothModulesInBothProfiles)
{
    for (const std::string_view profile : { k_SpirvProfile, k_ModernProfile })
    {
        const std::vector<std::string_view> names = TestBundle(profile).ModuleNames();
        EXPECT_NE(std::ranges::find(names, k_Module), names.end()) << profile;
        EXPECT_NE(std::ranges::find(names, k_OtherModule), names.end()) << profile;
    }
}

TEST(ResolvePlumbing, EntryPointNamesFollowThePassKind)
{
    const loom::FrozenPass dispatch = MakeShaderPass(loom::PassKind::Dispatch, k_ComputeEntry, "");
    const loom::FrozenPass draw = MakeShaderPass(loom::PassKind::Draw, k_VertexEntry, k_FragmentEntry);
    const loom::FrozenPass depthOnly = MakeShaderPass(loom::PassKind::Draw, k_VertexEntry, "");

    EXPECT_EQ(loom::resolve::EntryPointNames(dispatch), (std::vector<std::string_view>{ k_ComputeEntry }));
    EXPECT_EQ(loom::resolve::EntryPointNames(draw), (std::vector<std::string_view>{ k_VertexEntry, k_FragmentEntry }));
    EXPECT_EQ(loom::resolve::EntryPointNames(depthOnly), (std::vector<std::string_view>{ k_VertexEntry }));
}

// Each pin domain reaches the right Where overload: with every relevant axis pinned, SingleFor finds exactly the
// variant that holds those values.
TEST(ResolvePlumbing, WherePinUsesEachDomain)
{
    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const lodestone::PinMode required = lodestone::PinMode::Required;

    const std::vector<loom::PinRecord> computePins
    {
        MakePin("DOUBLE_PASS", loom::AxisValue::Boolean(true)),
        MakePin("TAPS", loom::AxisValue::Unsigned(2u)),
        MakePin("USE_WAVE_OPS", loom::AxisValue::Boolean(false)),
    };

    lodestone::ManifestQueryBuilder compute = index.Query();

    for (const loom::PinRecord& pin : computePins)
    {
        compute = loom::resolve::WherePin(compute, pin, required);
    }

    const std::vector<std::string_view> computeNames{ k_ComputeEntry };
    const auto computeKey = compute.SingleFor(computeNames);
    ASSERT_TRUE(computeKey.has_value()) << static_cast<int32_t>(computeKey.error().Code);
    EXPECT_EQ(ValueOf(index, *computeKey, "DOUBLE_PASS").IntegralValue, 1u);
    EXPECT_EQ(ValueOf(index, *computeKey, "TAPS").IntegralValue, 2u);
    EXPECT_EQ(ValueOf(index, *computeKey, "USE_WAVE_OPS").IntegralValue, 0u);

    const std::vector<loom::PinRecord> fragmentPins
    {
        MakePin("SHADE", loom::AxisValue::Interface("ShadeWarm")),
        MakePin("CURVE", loom::AxisValue::EnumCase("Filmic")),
    };

    lodestone::ManifestQueryBuilder fragment = index.Query();

    for (const loom::PinRecord& pin : fragmentPins)
    {
        fragment = loom::resolve::WherePin(fragment, pin, required);
    }

    const std::vector<std::string_view> fragmentNames{ k_FragmentEntry };
    const auto fragmentKey = fragment.SingleFor(fragmentNames);
    ASSERT_TRUE(fragmentKey.has_value()) << static_cast<int32_t>(fragmentKey.error().Code);
    EXPECT_EQ(ValueOf(index, *fragmentKey, "SHADE").Name, "ShadeWarm");
    EXPECT_EQ(ValueOf(index, *fragmentKey, "CURVE").Name, "Filmic");
}

// The pin mode on a gated axis. WAVE_SIZE is active only when USE_WAVE_OPS is on. Required asks for the gate too, so
// it contradicts USE_WAVE_OPS = false; WhenActive keeps the variant where the gate is off.
TEST(ResolvePlumbing, PinModeDecidesTheGate)
{
    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const loom::PinRecord waveSize = MakePin("WAVE_SIZE", loom::AxisValue::Unsigned(16u));
    const lodestone::ManifestQueryBuilder base = index.Query()
        .Where("DOUBLE_PASS", false)
        .Where("TAPS", 1u)
        .Where("USE_WAVE_OPS", false);
    const std::vector<std::string_view> names{ k_ComputeEntry };

    const auto required = loom::resolve::WherePin(base, waveSize, lodestone::PinMode::Required).SingleFor(names);
    ASSERT_FALSE(required.has_value());
    EXPECT_EQ(required.error().Code, lodestone::QueryErrorCode::NoVariantForConstraints);

    const auto whenActive = loom::resolve::WherePin(base, waveSize, lodestone::PinMode::WhenActive).SingleFor(names);
    ASSERT_TRUE(whenActive.has_value()) << static_cast<int32_t>(whenActive.error().Code);
    EXPECT_EQ(ValueOf(index, *whenActive, "USE_WAVE_OPS").IntegralValue, 0u);
}

TEST(ResolvePlumbing, CapabilityRuleTakesOnlyBooleanCapabilityAxes)
{
    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const lodestone::manifest::ModuleView& module = index.View().Module();
    int32_t preferred = 0;

    for (uint32_t axis = 0u; axis < module.AxisCount(); ++axis)
    {
        const bool prefers = loom::resolve::PrefersCapableValue(module, axis);
        const std::string_view name = loom::resolve::AxisName(module, axis);

        // USE_WAVE_OPS is the one boolean capability axis. WAVE_SIZE is tuning, DOUBLE_PASS is a technique boolean.
        EXPECT_EQ(prefers, name == "USE_WAVE_OPS") << name;
        preferred += prefers ? 1 : 0;
    }

    EXPECT_EQ(preferred, 1);
}

TEST(ResolvePlumbing, AxisNameReadsTheBundle)
{
    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const lodestone::manifest::ModuleView& module = index.View().Module();
    const std::vector<std::string_view> expected = index.AxisNames();

    ASSERT_EQ(module.AxisCount(), static_cast<uint32_t>(expected.size()));

    for (uint32_t axis = 0u; axis < module.AxisCount(); ++axis)
    {
        EXPECT_EQ(loom::resolve::AxisName(module, axis), expected[axis]);
    }
}

// The fake device and the capability axis: without the wave capabilities, ForDevice removes every wave variant.
TEST(ResolvePlumbing, FakeDeviceRemovesTheWaveVariants)
{
    const std::vector<std::string_view> waveOnly = WaveOnlyCapabilities();
    ASSERT_FALSE(waveOnly.empty()) << "the capability axis changes no capability";

    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const lodestone::ManifestQueryBuilder wave = index.Query()
        .Where("DOUBLE_PASS", false)
        .Where("TAPS", 1u)
        .Where("USE_WAVE_OPS", true)
        .Where("WAVE_SIZE", 32u);
    const std::vector<std::string_view> names{ k_ComputeEntry };

    FakeDevice capable{};
    const auto fits = wave.ForDevice(&FakeSupports, &capable, RoomyLimits()).SingleFor(names);
    EXPECT_TRUE(fits.has_value());

    FakeDevice noWaves{ .Missing = waveOnly };
    const auto rejected = wave.ForDevice(&FakeSupports, &noWaves, RoomyLimits()).SingleFor(names);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().Code, lodestone::QueryErrorCode::NoVariantFitsDevice);
}

// The driver: a Record pass has no shader, so it needs none of the steps.
TEST(Resolve, RecordPassNeedsNoShader)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Declare(MakeBuffer());

    const auto build = [](loom::RecordBuilder& pass)
    {
        pass.Uses(k_Data, loom::ResourceAccess::ReadWrite, loom::BufferUse::Storage);
        pass.Record(Delegate<void(loom::RecordContext&)>::Create<&RecordNothing>());
    };

    graph.AddRecord(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    ASSERT_EQ(run.Resolved->Passes.size(), 1u);
    EXPECT_EQ(run.Resolved->Passes[0].Environment, nullptr);
    EXPECT_EQ(run.Resolved->Passes[0].EntryPoint, loom::k_NoEntryPoint);
}

// The author's part. Each test states the agreed behavior.

// Every relevant axis pinned: one variant, the right entry point, no diagnostic.
TEST(Resolve, DispatchResolvesOneVariant)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(true));
        pass.Pin("TAPS", loom::AxisValue::Unsigned(4u));
        pass.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(false));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());

    const loom::ResolvedPass& resolved = run.Resolved->Passes[0];
    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    EXPECT_EQ(resolved.Environment, &index.View());
    EXPECT_EQ(resolved.EntryPoint, static_cast<int32_t>(*index.View().Module().FindEntryPointIndex(k_ComputeEntry)));
    EXPECT_EQ(resolved.FragmentEntryPoint, loom::k_NoEntryPoint);
    EXPECT_EQ(ValueOf(index, resolved.Variant, "DOUBLE_PASS").IntegralValue, 1u);
    EXPECT_EQ(ValueOf(index, resolved.Variant, "TAPS").IntegralValue, 4u);
    EXPECT_EQ(ValueOf(index, resolved.Variant, "USE_WAVE_OPS").IntegralValue, 0u);
}

// The same graph on the other profile selects the same axis values (keys are per environment, so compare values).
TEST(Resolve, BothProfilesSelectTheSameValues)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
        pass.Pin("TAPS", loom::AxisValue::Unsigned(2u));
        pass.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(false));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun spirv;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, spirv);
    ResolveRun modern;
    RunResolve(graph, FakeDevice{}, k_ModernProfile, modern);

    ASSERT_TRUE(spirv.Resolved.has_value());
    ASSERT_TRUE(modern.Resolved.has_value());

    const lodestone::ManifestIndex& modernIndex = **TestBundle(k_ModernProfile).ModuleIndex(k_Module);
    EXPECT_EQ(ValueOf(modernIndex, modern.Resolved->Passes[0].Variant, "TAPS").IntegralValue, 2u);
    EXPECT_EQ(ValueOf(TestIndex(k_Module), spirv.Resolved->Passes[0].Variant, "TAPS").IntegralValue, 2u);
}

// A draw: the vertex and fragment entry points, an interface pin and an enum pin. The compute axes are not relevant
// to either stage, so they need no pin and give no LM205.
TEST(Resolve, DrawResolvesBothStages)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.Declare(MakeImageOfTarget(k_Backbuffer));

    const auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders(k_Module, k_VertexEntry, k_FragmentEntry);
        pass.Pin("SHADE", loom::AxisValue::Interface("ShadeWarm"));
        pass.Pin("CURVE", loom::AxisValue::EnumCase("Filmic"));
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());

    const loom::ResolvedPass& resolved = run.Resolved->Passes[0];
    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const lodestone::manifest::ModuleView& module = index.View().Module();
    EXPECT_EQ(resolved.EntryPoint, static_cast<int32_t>(*module.FindEntryPointIndex(k_VertexEntry)));
    EXPECT_EQ(resolved.FragmentEntryPoint, static_cast<int32_t>(*module.FindEntryPointIndex(k_FragmentEntry)));
    EXPECT_EQ(ValueOf(index, resolved.Variant, "SHADE").Name, "ShadeWarm");
    EXPECT_EQ(ValueOf(index, resolved.Variant, "CURVE").Name, "Filmic");
}

// A depth-only draw: the vertex entry point reads no axis, so nothing needs a pin.
TEST(Resolve, DepthOnlyDrawNeedsNoPin)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.Declare(MakeImageOfTarget(k_Backbuffer));

    const auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders(k_Module, k_VertexEntry, "");
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(run.Resolved->Passes[0].FragmentEntryPoint, loom::k_NoEntryPoint);
}

// LM201, module: the suggestion is the nearest module name.
TEST(Resolve, UnknownModuleIsLM201)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("LoomResolveTset", k_ComputeEntry);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    ASSERT_EQ(run.Sink.Diagnostics().size(), 1u);
    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics()[0];
    EXPECT_EQ(diagnostic.Code, loom::DiagnosticCode::LM201);
    EXPECT_EQ(diagnostic.Pass.Hash, k_First.Hash);
    EXPECT_EQ(diagnostic.Suggestion, k_Module);
}

// LM201, entry point: the suggestion is the nearest entry point of the module.
TEST(Resolve, UnknownEntryPointIsLM201)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, "ResolveCs");
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    ASSERT_EQ(run.Sink.Diagnostics().size(), 1u);
    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics()[0];
    EXPECT_EQ(diagnostic.Code, loom::DiagnosticCode::LM201);
    EXPECT_EQ(diagnostic.Suggestion, k_ComputeEntry);
}

// LM202: a dispatch that names a vertex entry point.
TEST(Resolve, DispatchOfAVertexEntryIsLM202)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, k_VertexEntry);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    EXPECT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM202);
}

// LM202: a draw whose fragment entry point is a compute entry point.
TEST(Resolve, DrawOfAComputeEntryIsLM202)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.Declare(MakeImageOfTarget(k_Backbuffer));

    const auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders(k_Module, k_VertexEntry, k_ComputeEntry);
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    EXPECT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM202);
}

// The merge: a pass pin refines a graph pin on the same axis (the pass value wins, no error).
TEST(Resolve, PassPinWinsOverGraphPin)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Pin("TAPS", loom::AxisValue::Unsigned(1u));
    graph.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
    graph.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(false));

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("TAPS", loom::AxisValue::Unsigned(4u));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(ValueOf(TestIndex(k_Module), run.Resolved->Passes[0].Variant, "TAPS").IntegralValue, 4u);
}

// The merge: a graph pin on an axis that one module declares is dropped for the other module, with no diagnostic.
TEST(Resolve, GraphPinSkipsAModuleThatLacksTheAxis)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Pin("TAPS", loom::AxisValue::Unsigned(2u));
    graph.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
    graph.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(false));

    const auto buildOther = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_OtherModule, "OtherCS");
        pass.Pin("INVERT", loom::AxisValue::Boolean(true));
        pass.Pin("SMOOTH", loom::AxisValue::Boolean(false));
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, ResolveCompute);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, buildOther);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(ValueOf(TestIndex(k_Module), run.Resolved->Passes[0].Variant, "TAPS").IntegralValue, 2u);
    EXPECT_EQ(ValueOf(TestIndex(k_OtherModule), run.Resolved->Passes[1].Variant, "INVERT").IntegralValue, 1u);
}

// LM206: a graph pin that no resolved module declares is a warning, and the graph still resolves.
TEST(Resolve, UndeclaredGraphPinIsLM206)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Pin("TAPZ", loom::AxisValue::Unsigned(2u));

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
        pass.Pin("TAPS", loom::AxisValue::Unsigned(1u));
        pass.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(false));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    ASSERT_EQ(run.Sink.Diagnostics().size(), 1u);
    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics()[0];
    EXPECT_EQ(diagnostic.Code, loom::DiagnosticCode::LM206);
    EXPECT_EQ(diagnostic.Severity, loom::DiagnosticSeverity::Warning);
}

// LM203: the pins together match no variant. WAVE_SIZE (pinned as Required) needs USE_WAVE_OPS on, and the pass pins
// it off.
TEST(Resolve, ContradictoryPinsAreLM203)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
        pass.Pin("TAPS", loom::AxisValue::Unsigned(1u));
        pass.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(false));
        pass.Pin("WAVE_SIZE", loom::AxisValue::Unsigned(16u));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    EXPECT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM203);
}

// LM205 and the capability rule on a capable device. USE_WAVE_OPS takes true by the rule (no warning); the other
// relevant axes take their declared defaults with one LM205 each: DOUBLE_PASS, TAPS, and WAVE_SIZE (active once the
// rule turns the gate on). SHADE and CURVE are not relevant to ResolveCS, so they get no warning.
TEST(Resolve, UnpinnedAxesTakeDefaultsOnACapableDevice)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, ResolveCompute);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM205), 3);
    EXPECT_EQ(run.Sink.Diagnostics().size(), 3u);

    const lodestone::ManifestIndex& index = TestIndex(k_Module);
    const lodestone::VariantKey key = run.Resolved->Passes[0].Variant;
    EXPECT_EQ(ValueOf(index, key, "USE_WAVE_OPS").IntegralValue, 1u);
    EXPECT_EQ(ValueOf(index, key, "DOUBLE_PASS").IntegralValue, 0u);
    EXPECT_EQ(ValueOf(index, key, "TAPS").IntegralValue, 1u);
    EXPECT_EQ(ValueOf(index, key, "WAVE_SIZE").IntegralValue, 32u);
}

// The same pass on a device without wave ops: ForDevice removes the wave variants, so USE_WAVE_OPS no longer varies
// and WAVE_SIZE is inactive. Two LM205 warnings remain.
TEST(Resolve, UnpinnedAxesTakeDefaultsWithoutWaveOps)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, ResolveCompute);

    ResolveRun run;
    RunResolve(graph, FakeDevice{ .Missing = WaveOnlyCapabilities() }, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM205), 2);
    EXPECT_EQ(run.Sink.Diagnostics().size(), 2u);
    EXPECT_EQ(ValueOf(TestIndex(k_Module), run.Resolved->Passes[0].Variant, "USE_WAVE_OPS").IntegralValue, 0u);
}

// LM207: the pins ask for wave ops, and the device cannot run them.
TEST(Resolve, PinnedWaveOpsOnADeviceWithoutThemIsLM207)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
        pass.Pin("TAPS", loom::AxisValue::Unsigned(1u));
        pass.Pin("USE_WAVE_OPS", loom::AxisValue::Boolean(true));
        pass.Pin("WAVE_SIZE", loom::AxisValue::Unsigned(32u));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{ .Missing = WaveOnlyCapabilities() }, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM207);

    // the pass key and the bundle's capability name, so the report says which pass and which capability
    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics()[0];
    EXPECT_EQ(diagnostic.Pass.Hash, k_First.Hash);
    EXPECT_FALSE(diagnostic.Binding.empty());
}

// Defaults on a technique gate pair. SMOOTH (default off) and SMOOTH_TAPS (default 4, active when SMOOTH is on) take
// their defaults in the same round. Both go in as WhenActive, so the SMOOTH_TAPS default does not ask for the gate,
// and the pass resolves with SMOOTH off. As Required, the two defaults contradict: a false LM203.
TEST(Resolve, GatedDefaultDoesNotSwitchItsGateOn)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto build = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_OtherModule, "OtherCS");
        pass.Pin("INVERT", loom::AxisValue::Boolean(false));
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM203), 0);
    EXPECT_GE(CountOf(run.Sink, loom::DiagnosticCode::LM205), 1);
    EXPECT_EQ(ValueOf(TestIndex(k_OtherModule), run.Resolved->Passes[0].Variant, "SMOOTH").IntegralValue, 0u);
}

// LM203 carries Lodestone's suggestion: a misspelled enum case names the nearest declared case.
TEST(Resolve, MisspelledEnumPinIsLM203WithSuggestion)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Import(k_Backbuffer);
    graph.Declare(MakeImageOfTarget(k_Backbuffer));

    const auto build = [](loom::DrawBuilder& pass)
    {
        pass.Shaders(k_Module, k_VertexEntry, k_FragmentEntry);
        pass.Pin("SHADE", loom::AxisValue::Interface("ShadeWarm"));
        pass.Pin("CURVE", loom::AxisValue::EnumCase("Filmik"));
        pass.Color(0, k_Albedo, loom::ColorLoad::DontCare(), loom::AttachmentStore::Store, loom::BlendMode::Opaque);
        pass.Draw(3, 1, 0, 0);
    };

    graph.AddDraw(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    ASSERT_EQ(OnlyCode(run.Sink), loom::DiagnosticCode::LM203);

    const loom::Diagnostic& diagnostic = run.Sink.Diagnostics()[0];
    EXPECT_EQ(diagnostic.Binding, "CURVE");
    EXPECT_EQ(diagnostic.Suggestion, "Filmic");
}

// A graph pin on the gated axis WAVE_SIZE goes in as WhenActive. On a device without wave ops it constrains nothing,
// so the pass resolves with wave ops off, and no LM207.
TEST(Resolve, GraphPinOnAGatedAxisDoesNotSwitchTheGateOn)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };
    graph.Pin("WAVE_SIZE", loom::AxisValue::Unsigned(32u));

    const auto build = [](loom::DispatchBuilder& pass)
    {
        ResolveCompute(pass);
        pass.Pin("DOUBLE_PASS", loom::AxisValue::Boolean(false));
        pass.Pin("TAPS", loom::AxisValue::Unsigned(1u));
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, build);

    ResolveRun run;
    RunResolve(graph, FakeDevice{ .Missing = WaveOnlyCapabilities() }, k_SpirvProfile, run);

    ASSERT_TRUE(run.Resolved.has_value());
    EXPECT_TRUE(run.Sink.Diagnostics().empty());
    EXPECT_EQ(ValueOf(TestIndex(k_Module), run.Resolved->Passes[0].Variant, "USE_WAVE_OPS").IntegralValue, 0u);
}

// Every broken pass reports, not only the first one.
TEST(Resolve, EveryBrokenPassReports)
{
    loom::GraphBuilder graph{ "Test.Graph", "Test.Library" };

    const auto buildFirst = [](loom::DispatchBuilder& pass)
    {
        pass.Shader("NoSuchModule", k_ComputeEntry);
        pass.DispatchThreads(64, 1, 1);
    };

    const auto buildSecond = [](loom::DispatchBuilder& pass)
    {
        pass.Shader(k_Module, k_VertexEntry);
        pass.DispatchThreads(64, 1, 1);
    };

    graph.AddDispatch(k_First, k_EveryFrame, k_Graphics, buildFirst);
    graph.AddDispatch(k_Second, k_EveryFrame, k_Graphics, buildSecond);

    ResolveRun run;
    RunResolve(graph, FakeDevice{}, k_SpirvProfile, run);

    EXPECT_FALSE(run.Resolved.has_value());
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM201), 1);
    EXPECT_EQ(CountOf(run.Sink, loom::DiagnosticCode::LM202), 1);
}
