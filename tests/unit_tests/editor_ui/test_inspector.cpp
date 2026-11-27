#include "EditProposal.hpp"
#include "ImGuiModule.hpp"
#include "Inspector.hpp"
#include "UiTheme.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace
{
    class HeadlessImGuiFrame
    {
    public:
        HeadlessImGuiFrame() :
            context{ ImGui::CreateContext() }
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2{ 1280.0f, 720.0f };
            io.DeltaTime = 1.0f / 60.0f;
            io.IniFilename = nullptr;
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::NewFrame();
        }

        ~HeadlessImGuiFrame()
        {
            if (ended)
            {
                ImGui::DestroyContext(context);
                return;
            }

            if (begunWindow)
            {
                ImGui::End();
            }

            ImGui::Render();
            ImGui::EndFrame();
            ImGui::DestroyContext(context);
        }

        HeadlessImGuiFrame(const HeadlessImGuiFrame&) = delete;
        HeadlessImGuiFrame& operator=(const HeadlessImGuiFrame&) = delete;

        void BeginWindow(const char* name, float width, float height)
        {
            ImGui::SetNextWindowPos(ImVec2{ 20.0f, 30.0f }, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2{ width, height }, ImGuiCond_Always);
            begunWindow = ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration);
            EXPECT_TRUE(begunWindow);
        }

        void EndFrameAndRender()
        {
            if (begunWindow)
            {
                ImGui::End();
                begunWindow = false;
            }

            ImGui::Render();
            ImGui::EndFrame();
            ended = true;
        }

    private:
        ImGuiContext* context{ nullptr };
        bool begunWindow{ false };
        bool ended{ false };
    };

    enum class SmallMode : uint8_t
    {
        Invalid = 0,
        Calm,
        Chop,
        Storm,
    };

    enum class LargeMode : uint8_t
    {
        Invalid = 0,
        PresetA,
        PresetB,
        PresetC,
        PresetD,
        PresetE,
    };

    struct NestedParams
    {
        int32_t Value{ 10 };
        float Ratio{ 0.5f };
    };

    struct VariantAltA
    {
        float Alpha{ 1.0f };
    };

    struct VariantAltB
    {
        std::string Tag{ "alt_b" };
    };

    struct TypeTaggedStruct
    {
        int32_t Score{ 42 };
    };

    struct CustomDelegatedStruct
    {
        int32_t First{ 1 };
        bool Second{ false };
    };

    struct InspectorScene
    {
        bool Enabled{ true };
        int32_t Count{ 5 };
        uint64_t Seed{ 99u };
        float Speed{ 12.5f };
        double Gain{ 2.25 };
        std::string Name{ "ocean" };
        SmallMode Mode{ SmallMode::Chop };
        LargeMode Palette{ LargeMode::PresetC };
        float AngleDeg{ 30.0f };
        float BlendPercent{ 0.75f };
        std::array<float, 3> Albedo{ 0.2f, 0.4f, 0.6f };
        std::array<float, 4> Tint{ 0.1f, 0.2f, 0.3f, 1.0f };
        std::array<int32_t, 6> LargeFixed{ 1, 2, 3, 4, 5, 6 };
        std::vector<int32_t> Items{ 10, 20 };
        NestedParams Nested{};
        std::optional<NestedParams> MaybeNested{};
        std::variant<VariantAltA, VariantAltB> Choice{ VariantAltA{} };
        TypeTaggedStruct TaggedStruct{};
        int32_t ReadOnlyValue{ 77 };
        int32_t HiddenValue{ 888 };
    };

    struct DecoratorCapture
    {
        mutable std::vector<std::string> VisitedPaths;
    };

    [[nodiscard]] std::optional<dd::ui::FieldDecoration> CaptureDecoratorCallback(
        std::string_view field_path,
        const void* user_data);
    void DummyDrawerA(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value);
    void DummyDrawerB(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value);
    void DummyDrawerC(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value);

    struct CustomDrawerState
    {
        int32_t CallCount{ 0 };
        mutable std::vector<std::string> DelegatedPaths;
    };

    CustomDrawerState* g_customDrawerState{ nullptr };
    [[nodiscard]] std::optional<dd::ui::FieldDecoration> DelegatedDecoratorCallback(
        std::string_view field_path,
        const void* user_data);
    void DelegatingCustomStructDrawer(const dd::ui::DrawContext& context,
                                      const foundation::reflect::TypeInfo& type,
                                      const void* value);
}

namespace foundation::reflect
{
    template<>
    struct Describe<SmallMode>
    {
        static constexpr std::string_view Name = "SmallMode";
        static constexpr std::array<EnumCase<SmallMode>, 3> Cases
        {
            EnumCase<SmallMode>{ "Calm", SmallMode::Calm, "" },
            EnumCase<SmallMode>{ "Chop", SmallMode::Chop, "" },
            EnumCase<SmallMode>{ "Storm", SmallMode::Storm, "" },
        };
    };

    template<>
    struct Describe<LargeMode>
    {
        static constexpr std::string_view Name = "LargeMode";
        static constexpr std::array<EnumCase<LargeMode>, 5> Cases
        {
            EnumCase<LargeMode>{ "PresetA", LargeMode::PresetA, "" },
            EnumCase<LargeMode>{ "PresetB", LargeMode::PresetB, "" },
            EnumCase<LargeMode>{ "PresetC", LargeMode::PresetC, "" },
            EnumCase<LargeMode>{ "PresetD", LargeMode::PresetD, "" },
            EnumCase<LargeMode>{ "PresetE", LargeMode::PresetE, "" },
        };
    };

    template<>
    struct Describe<NestedParams>
    {
        static constexpr std::string_view Name = "NestedParams";
        static constexpr std::tuple Fields
        {
            Field{ "Value", &NestedParams::Value, FieldMetadata{} },
            Field{
                "Ratio",
                &NestedParams::Ratio,
                FieldMetadata{
                    .Range = FieldRange{ 0.0, 1.0 },
                    .Ui = FieldUiMetadata{ .Slider = FieldRange{ 0.0, 1.0 } },
                },
            },
        };
    };

    template<>
    struct Describe<VariantAltA>
    {
        static constexpr std::string_view Name = "VariantAltA";
        static constexpr std::tuple Fields
        {
            Field{ "Alpha", &VariantAltA::Alpha, FieldMetadata{} },
        };
    };

    template<>
    struct Describe<VariantAltB>
    {
        static constexpr std::string_view Name = "VariantAltB";
        static constexpr std::tuple Fields
        {
            Field{ "Tag", &VariantAltB::Tag, FieldMetadata{} },
        };
    };

    template<>
    struct Describe<TypeTaggedStruct>
    {
        static constexpr std::string_view Name = "TypeTaggedStruct";
        static constexpr std::string_view UiWidget = "TypeTag";
        static constexpr std::tuple Fields
        {
            Field{ "Score", &TypeTaggedStruct::Score, FieldMetadata{} },
        };
    };

    template<>
    struct Describe<CustomDelegatedStruct>
    {
        static constexpr std::string_view Name = "CustomDelegatedStruct";
        static constexpr std::tuple Fields
        {
            Field{ "First", &CustomDelegatedStruct::First, FieldMetadata{} },
            Field{ "Second", &CustomDelegatedStruct::Second, FieldMetadata{} },
        };
    };

    template<>
    struct Describe<InspectorScene>
    {
        static constexpr std::string_view Name = "InspectorScene";
        static constexpr std::tuple Fields
        {
            Field{ "Enabled", &InspectorScene::Enabled, FieldMetadata{} },
            Field{
                "Count",
                &InspectorScene::Count,
                FieldMetadata{
                    .Range = FieldRange{ 0.0, 100.0 },
                    .Ui = FieldUiMetadata{ .Group = "Primary" },
                },
            },
            Field{
                "Seed",
                &InspectorScene::Seed,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Group = "Primary" } },
            },
            Field{
                "Speed",
                &InspectorScene::Speed,
                FieldMetadata{
                    .Unit = "m/s",
                    .Range = FieldRange{ 0.0, 50.0 },
                    .Ui = FieldUiMetadata{
                        .Label = "Wind Speed",
                        .Group = "Motion",
                        .Slider = FieldRange{ 0.0, 40.0 },
                    },
                },
            },
            Field{
                "Gain",
                &InspectorScene::Gain,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Group = "Motion" } },
            },
            Field{ "Name", &InspectorScene::Name, FieldMetadata{} },
            Field{ "Mode", &InspectorScene::Mode, FieldMetadata{} },
            Field{ "Palette", &InspectorScene::Palette, FieldMetadata{} },
            Field{
                "AngleDeg",
                &InspectorScene::AngleDeg,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Widget = "AngleSlider" } },
            },
            Field{
                "BlendPercent",
                &InspectorScene::BlendPercent,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Widget = "Percent" } },
            },
            Field{
                "Albedo",
                &InspectorScene::Albedo,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Widget = "ColorRgb" } },
            },
            Field{
                "Tint",
                &InspectorScene::Tint,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Widget = "ColorRgba" } },
            },
            Field{ "LargeFixed", &InspectorScene::LargeFixed, FieldMetadata{} },
            Field{ "Items", &InspectorScene::Items, FieldMetadata{} },
            Field{ "Nested", &InspectorScene::Nested, FieldMetadata{} },
            Field{ "MaybeNested", &InspectorScene::MaybeNested, FieldMetadata{} },
            Field{ "Choice", &InspectorScene::Choice, FieldMetadata{} },
            Field{
                "TaggedStruct",
                &InspectorScene::TaggedStruct,
                FieldMetadata{ .Ui = FieldUiMetadata{ .Widget = "FieldTag" } },
            },
            Field{
                "ReadOnlyValue",
                &InspectorScene::ReadOnlyValue,
                FieldMetadata{ .ReadOnly = true },
            },
            Field{
                "HiddenValue",
                &InspectorScene::HiddenValue,
                FieldMetadata{ .Hidden = true },
            },
        };
    };
}

TEST(EditProposalTests, ToStringAndProposalBufferLifecycle)
{
    EXPECT_EQ(dd::ui::ToString(dd::ui::CommitKind::Invalid), "Invalid");
    EXPECT_EQ(dd::ui::ToString(dd::ui::CommitKind::Preview), "Preview");
    EXPECT_EQ(dd::ui::ToString(dd::ui::CommitKind::Commit), "Commit");

    EXPECT_EQ(dd::ui::ToString(dd::ui::ProposalOp::Invalid), "Invalid");
    EXPECT_EQ(dd::ui::ToString(dd::ui::ProposalOp::SetScalar), "SetScalar");
    EXPECT_EQ(dd::ui::ToString(dd::ui::ProposalOp::ResizeSequence), "ResizeSequence");
    EXPECT_EQ(dd::ui::ToString(dd::ui::ProposalOp::EmplaceOptional), "EmplaceOptional");
    EXPECT_EQ(dd::ui::ToString(dd::ui::ProposalOp::ResetOptional), "ResetOptional");
    EXPECT_EQ(dd::ui::ToString(dd::ui::ProposalOp::SetVariantIndex), "SetVariantIndex");

    dd::ui::ProposalBuffer buffer;
    EXPECT_TRUE(buffer.Empty());

    buffer.Push(dd::ui::EditProposal
    {
        .TargetId = 7u,
        .Path = "Count",
        .Op = dd::ui::ProposalOp::SetScalar,
        .Commit = dd::ui::CommitKind::Commit,
        .Value = int64_t{ 12 },
    });

    EXPECT_FALSE(buffer.Empty());
    ASSERT_EQ(buffer.Proposals().size(), 1u);
    EXPECT_EQ(buffer.Proposals()[0].Path, "Count");

    buffer.Clear();
    EXPECT_TRUE(buffer.Empty());
}

TEST(WidgetRegistryTests, ResolvePriorityAndOverride)
{
    dd::ui::WidgetRegistry registry;
    registry.RegisterBuiltins();

    registry.RegisterTypeName("TypeTaggedStruct", &DummyDrawerA);
    registry.RegisterTag("TypeTag", &DummyDrawerB);
    registry.RegisterTag("FieldTag", &DummyDrawerC);

    const foundation::reflect::TypeInfo& taggedType = foundation::reflect::GetTypeInfo<TypeTaggedStruct>();
    const foundation::reflect::FieldMetadata fieldMeta
    {
        .Ui = foundation::reflect::FieldUiMetadata{ .Widget = "FieldTag" },
    };

    // Priority 1: FieldMetadata::Ui.Widget overrides TypeInfo::UiWidget and TypeInfo::Name.
    EXPECT_EQ(registry.Resolve(taggedType, &fieldMeta), &DummyDrawerC);

    // Priority 2: TypeInfo::UiWidget overrides TypeInfo::Name when metadata has no widget tag.
    EXPECT_EQ(registry.Resolve(taggedType, nullptr), &DummyDrawerB);

    // Priority 3: TypeInfo::Name resolves when no tag matches.
    registry.RegisterTag("TypeTag", nullptr);
    EXPECT_EQ(registry.Resolve(taggedType, nullptr), &DummyDrawerA);

    // Overwriting TypeName replaces previous drawer.
    registry.RegisterTypeName("TypeTaggedStruct", &DummyDrawerC);
    EXPECT_EQ(registry.Resolve(taggedType, nullptr), &DummyDrawerC);

    // Priority 4: TypeKind fallback when neither tags nor type name match.
    registry.RegisterTypeName("TypeTaggedStruct", nullptr);
    EXPECT_EQ(registry.Resolve(taggedType, nullptr),
              registry.ResolveKindFallback(foundation::reflect::TypeKind::Struct));

    EXPECT_EQ(registry.ResolveKindFallback(foundation::reflect::TypeKind::Invalid), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Bool), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Int), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Float32), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Float64), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::String), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Enum), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Sequence), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Optional), nullptr);
    EXPECT_NE(registry.ResolveKindFallback(foundation::reflect::TypeKind::Variant), nullptr);
}

TEST(EditProposalTests, ApplyEditProposalScalarsEnumsAndRefusals)
{
    InspectorScene scene{};
    const foundation::reflect::TypeInfo& rootType = foundation::reflect::GetTypeInfo<InspectorScene>();

    EXPECT_FALSE(dd::ui::ApplyEditProposal(rootType, nullptr, dd::ui::EditProposal{}));

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Enabled", .Value = false }));
    EXPECT_FALSE(scene.Enabled);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Count", .Value = int64_t{ 42 } }));
    EXPECT_EQ(scene.Count, 42);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Seed", .Value = uint64_t{ 12345u } }));
    EXPECT_EQ(scene.Seed, 12345u);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Speed", .Value = 18.5 }));
    EXPECT_FLOAT_EQ(scene.Speed, 18.5f);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Gain", .Value = 4.75 }));
    EXPECT_DOUBLE_EQ(scene.Gain, 4.75);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Name", .Value = std::string{ "pacific" } }));
    EXPECT_EQ(scene.Name, "pacific");

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Mode", .Value = std::string{ "Storm" } }));
    EXPECT_EQ(scene.Mode, SmallMode::Storm);

    // Refusals must fail and leave target values unchanged.
    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "ReadOnlyValue", .Value = int64_t{ 1 } }));
    EXPECT_EQ(scene.ReadOnlyValue, 77);

    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Count", .Value = int64_t{ 200 } }));
    EXPECT_EQ(scene.Count, 42);

    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Mode", .Value = std::string{ "Nonexistent" } }));
    EXPECT_EQ(scene.Mode, SmallMode::Storm);

    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "NoSuchField", .Value = int64_t{ 0 } }));
}

TEST(EditProposalTests, ApplyEditProposalSequenceOptionalAndVariantOps)
{
    InspectorScene scene{};
    const foundation::reflect::TypeInfo& rootType = foundation::reflect::GetTypeInfo<InspectorScene>();

    // ResizeSequence grow and shrink on std::vector, reject negative count and fixed std::array.
    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Items", .Op = dd::ui::ProposalOp::ResizeSequence, .IntArgument = 4 }));
    EXPECT_EQ(scene.Items.size(), 4u);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Items", .Op = dd::ui::ProposalOp::ResizeSequence, .IntArgument = 1 }));
    EXPECT_EQ(scene.Items.size(), 1u);

    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{
            .Path = "Items",
            .Op = dd::ui::ProposalOp::ResizeSequence,
            .IntArgument = -1,
        }));
    EXPECT_EQ(scene.Items.size(), 1u);

    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{
            .Path = "Albedo",
            .Op = dd::ui::ProposalOp::ResizeSequence,
            .IntArgument = 2,
        }));
    EXPECT_EQ(scene.Albedo.size(), 3u);

    // Optional: scalar write fails while empty, succeeds after EmplaceOptional, cleared by ResetOptional.
    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "MaybeNested.Value", .Value = int64_t{ 55 } }));

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "MaybeNested", .Op = dd::ui::ProposalOp::EmplaceOptional }));
    ASSERT_TRUE(scene.MaybeNested.has_value());

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "MaybeNested.Value", .Value = int64_t{ 55 } }));
    EXPECT_EQ(scene.MaybeNested->Value, 55);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "MaybeNested", .Op = dd::ui::ProposalOp::ResetOptional }));
    EXPECT_FALSE(scene.MaybeNested.has_value());

    // Variant: out-of-range rejected, SetVariantIndex switches alternative, then nested write succeeds.
    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{
            .Path = "Choice",
            .Op = dd::ui::ProposalOp::SetVariantIndex,
            .IntArgument = 2,
        }));
    EXPECT_FALSE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{
            .Path = "Choice",
            .Op = dd::ui::ProposalOp::SetVariantIndex,
            .IntArgument = -1,
        }));
    EXPECT_EQ(scene.Choice.index(), 0u);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{
            .Path = "Choice",
            .Op = dd::ui::ProposalOp::SetVariantIndex,
            .IntArgument = 1,
        }));
    ASSERT_EQ(scene.Choice.index(), 1u);

    EXPECT_TRUE(dd::ui::ApplyEditProposal(
        rootType,
        &scene,
        dd::ui::EditProposal{ .Path = "Choice.Tag", .Value = std::string{ "updated" } }));
    EXPECT_EQ(std::get<VariantAltB>(scene.Choice).Tag, "updated");
}

TEST(InspectorTests, HeadlessDrawAllKindsDecoratorPathsAndStackBalance)
{
    HeadlessImGuiFrame frame;
    const dd::ui::UiTheme theme = dd::ui::MakeTelemetryTheme();
    dd::ui::ApplyImGuiTheme(theme);

    dd::ui::WidgetRegistry registry;
    registry.RegisterBuiltins();
    const ImGuiFontSet emptyFonts{};
    const dd::ui::Inspector inspector(registry, theme, emptyFonts);

    InspectorScene scene{};
    scene.MaybeNested = NestedParams{ .Value = 33, .Ratio = 0.25f };
    dd::ui::ProposalBuffer proposals;
    DecoratorCapture capture;
    const dd::ui::FieldDecorator decorator{ &CaptureDecoratorCallback, &capture };

    frame.BeginWindow("InspectorTest", 720.0f, 960.0f);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const int32_t initialItemWidthDepth = window->DC.ItemWidthStack.Size;
    const int32_t initialIdStackDepth = window->IDStack.Size;

    inspector.Draw(101u, "SCENE", scene, proposals, decorator);

    EXPECT_EQ(window->DC.ItemWidthStack.Size, initialItemWidthDepth);
    EXPECT_EQ(window->IDStack.Size, initialIdStackDepth);
    EXPECT_TRUE(proposals.Empty());

    const auto hasPath = [&](std::string_view expected)
    {
        return std::ranges::find(capture.VisitedPaths, expected) != capture.VisitedPaths.end();
    };

    EXPECT_TRUE(hasPath("Enabled"));
    EXPECT_TRUE(hasPath("Count"));
    EXPECT_TRUE(hasPath("Speed"));
    EXPECT_TRUE(hasPath("Palette"));
    EXPECT_TRUE(hasPath("Albedo"));
    EXPECT_TRUE(hasPath("Items[0]"));
    EXPECT_TRUE(hasPath("Items[1]"));
    EXPECT_TRUE(hasPath("Nested.Value"));
    EXPECT_TRUE(hasPath("Nested.Ratio"));
    EXPECT_TRUE(hasPath("MaybeNested.Value"));
    EXPECT_TRUE(hasPath("MaybeNested.Ratio"));
    EXPECT_TRUE(hasPath("Choice.Alpha"));
    EXPECT_TRUE(hasPath("TaggedStruct.Score"));
    EXPECT_TRUE(hasPath("ReadOnlyValue"));
    EXPECT_FALSE(hasPath("HiddenValue"));

    frame.EndFrameAndRender();
}

TEST(InspectorTests, CustomStructDrawerDelegatesToDrawDefaultFields)
{
    HeadlessImGuiFrame frame;
    const dd::ui::UiTheme theme = dd::ui::MakeTelemetryTheme();
    dd::ui::ApplyImGuiTheme(theme);

    CustomDrawerState state{};
    g_customDrawerState = &state;

    dd::ui::WidgetRegistry registry;
    registry.RegisterBuiltins();
    registry.RegisterTypeName("CustomDelegatedStruct", &DelegatingCustomStructDrawer);

    const ImGuiFontSet emptyFonts{};
    const dd::ui::Inspector inspector(registry, theme, emptyFonts);
    const CustomDelegatedStruct target{};
    dd::ui::ProposalBuffer proposals;

    frame.BeginWindow("CustomStructTest", 640.0f, 480.0f);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const int32_t initialItemWidthDepth = window->DC.ItemWidthStack.Size;
    const int32_t initialIdStackDepth = window->IDStack.Size;

    inspector.Draw(55u, "CUSTOM", target, proposals, dd::ui::k_NoDecorator);

    EXPECT_EQ(window->DC.ItemWidthStack.Size, initialItemWidthDepth);
    EXPECT_EQ(window->IDStack.Size, initialIdStackDepth);
    EXPECT_EQ(state.CallCount, 1);
    ASSERT_EQ(state.DelegatedPaths.size(), 2u);
    EXPECT_EQ(state.DelegatedPaths[0], "First");
    EXPECT_EQ(state.DelegatedPaths[1], "Second");

    g_customDrawerState = nullptr;
    frame.EndFrameAndRender();
}

namespace
{
    std::optional<dd::ui::FieldDecoration> CaptureDecoratorCallback(std::string_view field_path,
                                                                    const void* user_data)
    {
        const auto* capture = static_cast<const DecoratorCapture*>(user_data);
        capture->VisitedPaths.emplace_back(field_path);

        if (field_path == "Speed")
        {
            return dd::ui::FieldDecoration{ .SecondaryText = "Knots", .HintText = "Ocean surface wind" };
        }

        return std::nullopt;
    }

    void DummyDrawerA(const dd::ui::DrawContext& /*context*/,
                      const foundation::reflect::TypeInfo& /*type*/,
                      const void* /*value*/)
    {}

    void DummyDrawerB(const dd::ui::DrawContext& /*context*/,
                      const foundation::reflect::TypeInfo& /*type*/,
                      const void* /*value*/)
    {}

    void DummyDrawerC(const dd::ui::DrawContext& /*context*/,
                      const foundation::reflect::TypeInfo& /*type*/,
                      const void* /*value*/)
    {}

    std::optional<dd::ui::FieldDecoration> DelegatedDecoratorCallback(std::string_view field_path,
                                                                      const void* user_data)
    {
        const auto* state = static_cast<const CustomDrawerState*>(user_data);
        state->DelegatedPaths.emplace_back(field_path);
        return dd::ui::FieldDecoration{ .SecondaryText = "Delegated", .HintText = "" };
    }

    void DelegatingCustomStructDrawer(const dd::ui::DrawContext& context,
                                      const foundation::reflect::TypeInfo& type,
                                      const void* value)
    {
        if (g_customDrawerState != nullptr)
        {
            ++g_customDrawerState->CallCount;
        }

        const dd::ui::FieldDecorator innerDecorator{ &DelegatedDecoratorCallback, g_customDrawerState };
        context.DrawDefaultFields(type, value, innerDecorator);
    }
}
