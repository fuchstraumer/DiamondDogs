#include "UiControls.hpp"
#include "UiLayout.hpp"
#include "UiTheme.hpp"
#include "ImGuiModule.hpp"
#include "containers/SmallVector.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <span>
#include <string_view>

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
}

TEST(UiLayoutRect, GeometryAndInsetClamping)
{
    const dd::ui::Rect rect{ 10.0f, 20.0f, 110.0f, 70.0f };
    EXPECT_FLOAT_EQ(rect.Width(), 100.0f);
    EXPECT_FLOAT_EQ(rect.Height(), 50.0f);
    EXPECT_FALSE(rect.IsEmpty());

    const dd::ui::Rect inset = rect.Inset(8.0f, 5.0f);
    EXPECT_FLOAT_EQ(inset.MinX, 18.0f);
    EXPECT_FLOAT_EQ(inset.MinY, 25.0f);
    EXPECT_FLOAT_EQ(inset.MaxX, 102.0f);
    EXPECT_FLOAT_EQ(inset.MaxY, 65.0f);
    EXPECT_FLOAT_EQ(inset.Width(), 84.0f);
    EXPECT_FLOAT_EQ(inset.Height(), 40.0f);

    const dd::ui::Rect collapsed = rect.Inset(60.0f, 30.0f);
    EXPECT_FLOAT_EQ(collapsed.Width(), 0.0f);
    EXPECT_FLOAT_EQ(collapsed.Height(), 0.0f);
    EXPECT_TRUE(collapsed.IsEmpty());

    const dd::ui::Rect inverted{ 50.0f, 40.0f, 20.0f, 10.0f };
    EXPECT_FLOAT_EQ(inverted.Width(), 0.0f);
    EXPECT_FLOAT_EQ(inverted.Height(), 0.0f);
    EXPECT_TRUE(inverted.IsEmpty());
}

TEST(UiLayoutSolveTrackSizes, FixedFractionFillFitAndGaps)
{
    foundation::SmallVector<dd::ui::SizeSpec, 4> specs;
    specs.push_back(dd::ui::SizeSpec::Fixed(40.0f));
    specs.push_back(dd::ui::SizeSpec::Fraction(0.25f));
    specs.push_back(dd::ui::SizeSpec::Fill(1.0f));
    specs.push_back(dd::ui::SizeSpec::Fill(2.0f));

    foundation::SmallVector<float, 4> sizes;
    sizes.resize(4);

    // available=230, gap=10 -> totalGap=30, usable=200
    // Fixed=40, Fraction(0.25)=50, remaining=110 split 1:2 -> 36.6667, 73.3333
    dd::ui::SolveTrackSizes(230.0f, 10.0f, specs, sizes);
    EXPECT_FLOAT_EQ(sizes[0], 40.0f);
    EXPECT_FLOAT_EQ(sizes[1], 50.0f);
    EXPECT_NEAR(sizes[2], 110.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(sizes[3], 220.0f / 3.0f, 1e-4f);

    foundation::SmallVector<dd::ui::SizeSpec, 3> fitSpecs;
    fitSpecs.push_back(dd::ui::SizeSpec::Fit());
    fitSpecs.push_back(dd::ui::SizeSpec{ dd::ui::SizeKind::Fit, 30.0f });
    fitSpecs.push_back(dd::ui::SizeSpec::FillEqual());

    foundation::SmallVector<float, 3> fitSizes;
    fitSizes.resize(3);

    // available=150, gap=10 -> totalGap=20, usable=130; explicit Fit(30) takes 30, remaining 100 split 1:1
    dd::ui::SolveTrackSizes(150.0f, 10.0f, fitSpecs, fitSizes);
    EXPECT_FLOAT_EQ(fitSizes[0], 50.0f);
    EXPECT_FLOAT_EQ(fitSizes[1], 30.0f);
    EXPECT_FLOAT_EQ(fitSizes[2], 50.0f);
}

TEST(UiLayoutSolveTrackSizes, EmptyAndUnderBudgetClamping)
{
    foundation::SmallVector<dd::ui::SizeSpec, 2> emptySpecs;
    foundation::SmallVector<float, 2> emptySizes;
    dd::ui::SolveTrackSizes(100.0f, 5.0f, emptySpecs, emptySizes);

    foundation::SmallVector<dd::ui::SizeSpec, 2> specs;
    specs.push_back(dd::ui::SizeSpec::Fixed(80.0f));
    specs.push_back(dd::ui::SizeSpec::FillEqual());

    foundation::SmallVector<float, 2> sizes;
    sizes.resize(2);

    // available=60, gap=10 -> usable=50 < fixed(80), so Fill clamps to 0
    dd::ui::SolveTrackSizes(60.0f, 10.0f, specs, sizes);
    EXPECT_FLOAT_EQ(sizes[0], 80.0f);
    EXPECT_FLOAT_EQ(sizes[1], 0.0f);

    // gap alone exceeds available_extent -> usable=0, Fill clamps to 0
    dd::ui::SolveTrackSizes(5.0f, 20.0f, specs, sizes);
    EXPECT_FLOAT_EQ(sizes[0], 80.0f);
    EXPECT_FLOAT_EQ(sizes[1], 0.0f);
}

TEST(UiLayoutSolveHorizontalRects, PaddingGapAlignAndOverflowColumns)
{
    const dd::ui::Rect bounds{ 10.0f, 20.0f, 230.0f, 80.0f };
    foundation::SmallVector<dd::ui::SizeSpec, 2> fixedCols;
    fixedCols.push_back(dd::ui::SizeSpec::Fixed(40.0f));
    fixedCols.push_back(dd::ui::SizeSpec::Fixed(60.0f));

    foundation::SmallVector<dd::ui::Rect, 2> rects;
    rects.resize(2);

    // inner width = 220 - 2*10 = 200; totalWidth = 40 + 10 + 60 = 110; slack = 90
    dd::ui::BoxStyle style{};
    style.PaddingX = 10.0f;
    style.PaddingY = 5.0f;
    style.Gap = 10.0f;

    style.Align = dd::ui::CrossAlign::Start;
    dd::ui::SolveHorizontalRects(bounds, fixedCols, style, rects);
    EXPECT_FLOAT_EQ(rects[0].MinX, 20.0f);
    EXPECT_FLOAT_EQ(rects[0].MaxX, 60.0f);
    EXPECT_FLOAT_EQ(rects[0].MinY, 25.0f);
    EXPECT_FLOAT_EQ(rects[0].MaxY, 75.0f);
    EXPECT_FLOAT_EQ(rects[1].MinX, 70.0f);
    EXPECT_FLOAT_EQ(rects[1].MaxX, 130.0f);

    style.Align = dd::ui::CrossAlign::Center;
    dd::ui::SolveHorizontalRects(bounds, fixedCols, style, rects);
    EXPECT_FLOAT_EQ(rects[0].MinX, 65.0f);
    EXPECT_FLOAT_EQ(rects[1].MaxX, 175.0f);

    style.Align = dd::ui::CrossAlign::End;
    dd::ui::SolveHorizontalRects(bounds, fixedCols, style, rects);
    EXPECT_FLOAT_EQ(rects[0].MinX, 110.0f);
    EXPECT_FLOAT_EQ(rects[1].MaxX, 220.0f);

    style.Align = dd::ui::CrossAlign::Stretch;
    dd::ui::SolveHorizontalRects(bounds, fixedCols, style, rects);
    EXPECT_FLOAT_EQ(rects[0].MinX, 20.0f);
    EXPECT_FLOAT_EQ(rects[1].MaxX, 130.0f);

    // 6 columns (> inline capacity 4 in HBox) verifies SmallVector fallback cleanly
    foundation::SmallVector<dd::ui::SizeSpec, 8> sixCols;
    for (int32_t index = 0; index < 6; ++index)
    {
        sixCols.push_back(dd::ui::SizeSpec::FillEqual());
    }

    foundation::SmallVector<dd::ui::Rect, 4> sixRects;
    sixRects.resize(6);
    EXPECT_FALSE(sixRects.is_inline());

    dd::ui::BoxStyle sixStyle{};
    sixStyle.Gap = 4.0f;
    dd::ui::SolveHorizontalRects(dd::ui::Rect{ 0.0f, 0.0f, 140.0f, 20.0f }, sixCols, sixStyle, sixRects);
    // usable = 140 - 5*4 = 120, each = 20
    for (int32_t index = 0; index < 6; ++index)
    {
        EXPECT_FLOAT_EQ(sixRects[static_cast<size_t>(index)].MinX, static_cast<float>(index) * 24.0f);
        EXPECT_FLOAT_EQ(sixRects[static_cast<size_t>(index)].Width(), 20.0f);
    }
}

TEST(UiLayoutGrid, MultiRowCellPositions)
{
    const dd::ui::Rect origin{ 10.0f, 20.0f, 230.0f, 200.0f };
    // width=220, columns=3, gapX=10 -> cellWidth = (220 - 20)/3 = 200/3
    const float expectedWidth = 200.0f / 3.0f;

    const dd::ui::Rect c0 = dd::ui::SolveGridCellRect(origin, 3, 24.0f, 10.0f, 6.0f, 0);
    EXPECT_FLOAT_EQ(c0.MinX, 10.0f);
    EXPECT_FLOAT_EQ(c0.MinY, 20.0f);
    EXPECT_NEAR(c0.Width(), expectedWidth, 1e-4f);
    EXPECT_FLOAT_EQ(c0.Height(), 24.0f);

    const dd::ui::Rect c4 = dd::ui::SolveGridCellRect(origin, 3, 24.0f, 10.0f, 6.0f, 4);
    EXPECT_NEAR(c4.MinX, 10.0f + expectedWidth + 10.0f, 1e-4f);
    EXPECT_FLOAT_EQ(c4.MinY, 50.0f);
    EXPECT_NEAR(c4.Width(), expectedWidth, 1e-4f);
    EXPECT_FLOAT_EQ(c4.Height(), 24.0f);
}

TEST(UiThemeTests, TokensTonesAlphaAndFonts)
{
    const dd::ui::UiTheme theme = dd::ui::MakeTelemetryTheme();
    EXPECT_EQ(theme.Colors.Accent, IM_COL32(72, 208, 232, 255));
    EXPECT_EQ(theme.Colors.Warning, IM_COL32(242, 112, 82, 255));
    EXPECT_EQ(theme.Colors.SurfaceTop, IM_COL32(12, 16, 22, 255));

    EXPECT_EQ(theme.ToneColor(dd::ui::AccentTone::Accent), theme.Colors.Accent);
    EXPECT_EQ(theme.ToneColor(dd::ui::AccentTone::Warning), theme.Colors.Warning);
    EXPECT_EQ(theme.ToneColor(dd::ui::AccentTone::Wind), theme.Colors.DataWind);
    EXPECT_EQ(theme.ToneColor(dd::ui::AccentTone::Swell), theme.Colors.DataSwell);
    EXPECT_EQ(theme.ToneColor(dd::ui::AccentTone::Neutral), theme.Colors.LineWeak);
    EXPECT_EQ(theme.ToneColor(dd::ui::AccentTone::Invalid), theme.Colors.LineWeak);

    const uint32_t halfAlpha = dd::ui::WithAlpha(IM_COL32(72, 208, 232, 200), 0.5f);
    EXPECT_EQ((halfAlpha >> IM_COL32_A_SHIFT) & 0xFFu, 100u);
    EXPECT_EQ(halfAlpha & ~IM_COL32_A_MASK, IM_COL32(72, 208, 232, 200) & ~IM_COL32_A_MASK);

    const uint32_t clampedAlpha = dd::ui::WithAlpha(IM_COL32(72, 208, 232, 200), 2.0f);
    EXPECT_EQ((clampedAlpha >> IM_COL32_A_SHIFT) & 0xFFu, 255u);

    const ImGuiFontSet emptyFonts{};
    const dd::ui::ResolvedFont blinkerCaps = dd::ui::ResolveFont(emptyFonts, theme, dd::ui::TextRole::Caps);
    EXPECT_FLOAT_EQ(blinkerCaps.Size, 12.5f);

    const dd::ui::ResolvedFont blinkerTitle = dd::ui::ResolveFont(emptyFonts, theme, dd::ui::TextRole::Title);
    EXPECT_FLOAT_EQ(blinkerTitle.Size, 19.0f);

    const dd::ui::ResolvedFont blinkerHero = dd::ui::ResolveFont(emptyFonts, theme, dd::ui::TextRole::Hero);
    EXPECT_FLOAT_EQ(blinkerHero.Size, 56.0f);

    const dd::ui::ResolvedFont body = dd::ui::ResolveFont(emptyFonts, theme, dd::ui::TextRole::Body);
    EXPECT_FLOAT_EQ(body.Size, 15.0f);

    const dd::ui::ResolvedFont small = dd::ui::ResolveFont(emptyFonts, theme, dd::ui::TextRole::Small);
    EXPECT_FLOAT_EQ(small.Size, 12.5f);

    const dd::ui::ResolvedFont note = dd::ui::ResolveFont(emptyFonts, theme, dd::ui::TextRole::Note);
    EXPECT_FLOAT_EQ(note.Size, 12.5f);

    dd::ui::UiTheme michromaTheme = theme;
    michromaTheme.FontMode = dd::ui::HeaderFontMode::Michroma;
    EXPECT_FLOAT_EQ(dd::ui::ResolveFont(emptyFonts, michromaTheme, dd::ui::TextRole::Caps).Size, 9.5f);
    EXPECT_FLOAT_EQ(dd::ui::ResolveFont(emptyFonts, michromaTheme, dd::ui::TextRole::Title).Size, 14.0f);
    EXPECT_FLOAT_EQ(dd::ui::ResolveFont(emptyFonts, michromaTheme, dd::ui::TextRole::Hero).Size, 40.0f);
}

TEST(UiLayoutHeadlessImGui, ContainersAndControlsSmokeAndStackBalance)
{
    HeadlessImGuiFrame frame;
    const dd::ui::UiTheme theme = dd::ui::MakeTelemetryTheme();
    dd::ui::ApplyImGuiTheme(theme);

    frame.BeginWindow("EditorUiSmoke", 640.0f, 480.0f);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const int32_t initialItemWidthDepth = window->DC.ItemWidthStack.Size;
    const ImGuiFontSet emptyFonts{};

    {
        dd::ui::BoxStyle outerStyle{};
        outerStyle.Gap = 8.0f;
        outerStyle.PaddingX = 12.0f;
        outerStyle.PaddingY = 10.0f;

        dd::ui::VBox outer(dd::ui::Rect{ 40.0f, 50.0f, 540.0f, 450.0f }, outerStyle);

        const dd::ui::Rect headerSlot = outer.Slot(dd::ui::SizeSpec::Fixed(30.0f));
        EXPECT_FLOAT_EQ(headerSlot.MinX, 52.0f);
        EXPECT_FLOAT_EQ(headerSlot.MinY, 60.0f);
        EXPECT_FLOAT_EQ(headerSlot.Width(), 476.0f);
        EXPECT_FLOAT_EQ(headerSlot.Height(), 30.0f);
        EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().x, 52.0f);
        EXPECT_FLOAT_EQ(ImGui::GetCursorScreenPos().y, 60.0f);
        dd::ui::DrawSectionHeader("SIMULATION", theme, emptyFonts);

        outer.Spacer(6.0f);
        outer.Divider();

        const dd::ui::Rect nestedSlot = outer.Slot(dd::ui::SizeSpec::Fixed(90.0f));
        {
            dd::ui::BoxStyle innerStyle{};
            innerStyle.Gap = 4.0f;
            dd::ui::VBox inner(innerStyle);
            const dd::ui::Rect innerFirst = inner.Slot(dd::ui::SizeSpec::Fixed(24.0f));
            EXPECT_FLOAT_EQ(innerFirst.MinX, nestedSlot.MinX);
            EXPECT_FLOAT_EQ(innerFirst.MinY, nestedSlot.MinY);
            EXPECT_FLOAT_EQ(innerFirst.Width(), nestedSlot.Width());
            dd::ui::DrawKeyValue("Wind Speed", "12.4", "m/s", theme, emptyFonts);

            const dd::ui::Rect innerSecond = inner.Slot(dd::ui::SizeSpec::Fixed(24.0f));
            EXPECT_FLOAT_EQ(innerSecond.MinY, innerFirst.MaxY + 4.0f);
            dd::ui::DrawNote("Telemetry note", theme, emptyFonts);
        }

        const dd::ui::Rect hboxSlot = outer.Slot(dd::ui::SizeSpec::Fixed(32.0f));
        {
            foundation::SmallVector<dd::ui::SizeSpec, 6> cols;
            cols.push_back(dd::ui::SizeSpec::Fixed(80.0f));
            cols.push_back(dd::ui::SizeSpec::FillEqual());
            cols.push_back(dd::ui::SizeSpec::Fill(2.0f));
            cols.push_back(dd::ui::SizeSpec::Fixed(50.0f));
            cols.push_back(dd::ui::SizeSpec::FillEqual());
            cols.push_back(dd::ui::SizeSpec::Fixed(40.0f));

            dd::ui::BoxStyle hboxStyle{};
            hboxStyle.Gap = 4.0f;
            dd::ui::HBox row(0.0f, cols, hboxStyle);
            EXPECT_EQ(row.ColumnCount(), 6);

            const dd::ui::Rect c0 = row.Next();
            EXPECT_FLOAT_EQ(c0.MinX, hboxSlot.MinX);
            EXPECT_FLOAT_EQ(c0.Width(), 80.0f);
            EXPECT_FLOAT_EQ(c0.Height(), hboxSlot.Height());

            const dd::ui::Rect c1 = row.Next();
            EXPECT_FLOAT_EQ(c1.MinX, c0.MaxX + 4.0f);

            for (int32_t colIndex = 2; colIndex < 6; ++colIndex)
            {
                const dd::ui::Rect col = row.Next();
                EXPECT_FALSE(col.IsEmpty());
            }

            const dd::ui::Rect pastEnd = row.Next();
            EXPECT_TRUE(pastEnd.IsEmpty());
        }

        const dd::ui::Rect gridSlot = outer.Slot(dd::ui::SizeSpec::Fixed(60.0f));
        {
            dd::ui::Grid grid(2, 24.0f, 8.0f, 6.0f);
            const dd::ui::Rect g0 = grid.NextCell();
            const dd::ui::Rect g1 = grid.NextCell();
            const dd::ui::Rect g2 = grid.NextCell();
            EXPECT_FLOAT_EQ(g0.MinX, gridSlot.MinX);
            EXPECT_FLOAT_EQ(g0.MinY, gridSlot.MinY);
            EXPECT_FLOAT_EQ(g1.MinX, g0.MaxX + 8.0f);
            EXPECT_FLOAT_EQ(g2.MinY, g0.MaxY + 6.0f);
        }

        const dd::ui::Rect cardSlot = outer.Slot(dd::ui::SizeSpec::Fixed(150.0f));
        {
            dd::ui::Card card("OCEAN CARD", theme, dd::ui::AccentTone::Accent);
            EXPECT_GE(ImGui::GetCursorScreenPos().x, cardSlot.MinX);

            {
                dd::ui::PropertyRow prop("Enable Swell", theme, emptyFonts, 28.0f);
                EXPECT_GT(prop.ValueRect().Width(), 0.0f);
                bool enabled = true;
                EXPECT_FALSE(dd::ui::DrawTelemetryToggle("##swell_toggle",
                                                          "",
                                                          "OFF",
                                                          "ON",
                                                          &enabled,
                                                          theme,
                                                          emptyFonts));
            }

            foundation::SmallVector<std::string_view, 3> options;
            options.push_back("LOW");
            options.push_back("MED");
            options.push_back("HIGH");
            int32_t selected = 1;
            EXPECT_FALSE(dd::ui::DrawSegmentedSelector("##quality", options, selected, 3, theme, emptyFonts));

            float windSpeed = 10.0f;
            const dd::ui::SliderParams windParams
            {
                .Id = "##wind_speed",
                .Label = "Wind",
                .Hint = "Wind speed in m/s",
                .ValueText = "10.0",
                .UnitText = "m/s",
                .Min = 0.0f,
                .Max = 30.0f,
            };
            EXPECT_FALSE(dd::ui::DrawArtistSlider(windParams, &windSpeed, theme, emptyFonts));

            float directionDeg = 45.0f;
            const dd::ui::SliderParams angleParams
            {
                .Id = "##wind_dir",
                .Label = "Direction",
                .Hint = "Degrees",
                .Min = -180.0f,
                .Max = 180.0f,
            };
            EXPECT_FALSE(dd::ui::DrawAngleSlider(angleParams, &directionDeg, theme, emptyFonts));
        }
    }

    EXPECT_EQ(window->DC.ItemWidthStack.Size, initialItemWidthDepth);
    frame.EndFrameAndRender();
}
