#include "OceanUi.hpp"
#include "GpuTimers.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

namespace ocean
{
    namespace
    {
        constexpr float k_PanelWidth = 444.0f;
        constexpr float k_PanelMargin = 16.0f;
        constexpr float k_PanelChamfer = 14.0f;
        constexpr float k_LabelColumnWidth = 154.0f;
        constexpr float k_Pi = std::numbers::pi_v<float>;
        constexpr float k_DegreesToRadians = k_Pi / 180.0f;

        // --- Telemetry Panel and Chassis Colors ---
        constexpr ImU32 k_ColorPanelBg = IM_COL32(10, 12, 16, 252);
        constexpr ImU32 k_ColorPanelBorder = IM_COL32(60, 70, 80, 45);
        constexpr ImU32 k_ColorPanelReticle = IM_COL32(80, 95, 110, 60);
        constexpr ImU32 k_ColorTopBarAccent = IM_COL32(110, 130, 150, 120);
        constexpr ImU32 k_ColorHeaderGradientTop = IM_COL32(20, 24, 30, 0);
        constexpr ImU32 k_ColorHeaderGradientMid = IM_COL32(20, 24, 30, 0);
        constexpr ImU32 k_ColorHeaderGradientEnd = IM_COL32(10, 12, 16, 0);

        // --- Cards and Group Enclosures ---
        constexpr ImU32 k_ColorCardBg = IM_COL32(13, 16, 21, 248);
        constexpr ImU32 k_ColorCardBorder = IM_COL32(50, 60, 70, 35);
        constexpr ImU32 k_ColorCardAccentBar = IM_COL32(90, 115, 140, 100);
        constexpr ImU32 k_ColorCardSubBg = IM_COL32(11, 14, 18, 248);
        constexpr ImU32 k_ColorCardSubBorder = IM_COL32(45, 55, 65, 30);

        // --- Dividers and Separator Hairlines ---
        constexpr ImU32 k_ColorHairline = IM_COL32(55, 65, 75, 35);
        constexpr ImU32 k_ColorHairlineSub = IM_COL32(55, 65, 75, 25);
        constexpr ImU32 k_ColorHairlineBanner = IM_COL32(60, 70, 80, 40);

        // --- Typography (ImU32 Direct DrawList Colors) ---
        constexpr ImU32 k_ColorTextHeader = IM_COL32(235, 240, 245, 255);
        constexpr ImU32 k_ColorTextHeaderHover = IM_COL32(250, 252, 255, 255);
        constexpr ImU32 k_ColorTextSection = IM_COL32(180, 190, 200, 235);
        constexpr ImU32 k_ColorTextSubHeader = IM_COL32(150, 160, 172, 225);
        constexpr ImU32 k_ColorTextLabel = IM_COL32(130, 140, 150, 215);
        constexpr ImU32 k_ColorTextLabelHover = IM_COL32(200, 210, 220, 245);
        constexpr ImU32 k_ColorTextHint = IM_COL32(95, 105, 115, 200);
        constexpr ImU32 k_ColorTextValue = IM_COL32(210, 220, 230, 240);
        constexpr ImU32 k_ColorTextValueHover = IM_COL32(245, 250, 255, 255);
        constexpr ImU32 k_ColorTextUnit = IM_COL32(100, 110, 120, 200);
        constexpr ImU32 k_ColorTextCyanAccent = IM_COL32(165, 180, 195, 225);
        constexpr ImU32 k_ColorTextBodyMuted = IM_COL32(135, 145, 155, 220);
        constexpr ImU32 k_ColorTextSubMuted = IM_COL32(105, 115, 125, 205);

        // --- Typography (ImVec4 TextColored Constants) ---
        constexpr ImVec4 k_ColorVecTitle{ 0.90f, 0.93f, 0.96f, 0.98f };
        constexpr ImVec4 k_ColorVecSubtitle{ 0.45f, 0.50f, 0.55f, 0.90f };
        constexpr ImVec4 k_ColorVecNote{ 0.45f, 0.50f, 0.55f, 0.90f };
        constexpr ImVec4 k_ColorVecMutedStatus{ 0.45f, 0.50f, 0.55f, 0.85f };
        constexpr ImVec4 k_ColorVecTimingRoot{ 0.85f, 0.88f, 0.92f, 0.95f };
        constexpr ImVec4 k_ColorVecTimingChild{ 0.55f, 0.60f, 0.65f, 0.90f };
        constexpr ImVec4 k_ColorVecTimingAvg{ 0.75f, 0.80f, 0.85f, 0.95f };
        constexpr ImVec4 k_ColorVecTimingLast{ 0.48f, 0.52f, 0.58f, 0.85f };
        constexpr ImVec4 k_ColorVecTimingFooter{ 0.38f, 0.42f, 0.48f, 0.80f };

        // --- Slider Tracks and Chunky Thumbs ---
        constexpr ImU32 k_ColorTrackBg = IM_COL32(5, 7, 10, 255);
        constexpr ImU32 k_ColorTrackTick = IM_COL32(80, 90, 100, 18);
        constexpr ImU32 k_ColorCenterTick = IM_COL32(120, 135, 150, 60);
        constexpr ImU32 k_ColorThumbBg = IM_COL32(20, 24, 30, 255);
        constexpr ImU32 k_ColorThumbBgHover = IM_COL32(32, 38, 48, 255);
        constexpr ImU32 k_ColorThumbBorder = IM_COL32(75, 90, 105, 80);
        constexpr ImU32 k_ColorThumbNotch = IM_COL32(85, 100, 115, 100);

        // --- Preset Buttons ---
        constexpr ImU32 k_ColorButtonBg = IM_COL32(14, 18, 23, 240);
        constexpr ImU32 k_ColorButtonBgHover = IM_COL32(24, 30, 38, 245);
        constexpr ImU32 k_ColorButtonBgActive = IM_COL32(32, 40, 50, 255);
        constexpr ImU32 k_ColorButtonBorderActive = IM_COL32(120, 140, 160, 150);
        constexpr ImU32 k_ColorButtonIndicator = IM_COL32(140, 165, 190, 220);
        constexpr ImU32 k_ColorButtonText = IM_COL32(135, 145, 155, 220);
        constexpr ImU32 k_ColorButtonTextActive = IM_COL32(240, 245, 250, 255);

        // --- Toggle Switches and Segmented Selectors ---
        constexpr ImU32 k_ColorToggleOffBg = IM_COL32(10, 13, 17, 240);
        constexpr ImU32 k_ColorToggleOffText = IM_COL32(105, 115, 125, 200);
        constexpr ImU32 k_ColorChevronClosed = IM_COL32(100, 115, 130, 180);

        // --- Cascade Bands ---
        constexpr ImU32 k_ColorBandOffBg = IM_COL32(10, 13, 17, 240);
        constexpr ImU32 k_ColorBandOnBg = IM_COL32(28, 36, 46, 255);
        constexpr ImU32 k_ColorBandOffBorder = IM_COL32(50, 60, 70, 35);
        constexpr ImU32 k_ColorBandOnBorder = IM_COL32(110, 130, 150, 140);
        constexpr ImU32 k_ColorBandHoverBorder = IM_COL32(85, 105, 125, 80);
        constexpr ImU32 k_ColorBandOffLed = IM_COL32(45, 55, 65, 80);
        constexpr ImU32 k_ColorBandOnLed = IM_COL32(140, 165, 190, 220);
        constexpr ImU32 k_ColorBandOffText = IM_COL32(100, 110, 120, 190);
        constexpr ImU32 k_ColorBandOnText = IM_COL32(235, 242, 250, 255);

        // --- Spectrum Model Segment Buttons ---
        constexpr ImU32 k_ColorSegmentOffBg = IM_COL32(10, 13, 17, 240);
        constexpr ImU32 k_ColorSegmentOnBg = IM_COL32(28, 36, 46, 255);
        constexpr ImU32 k_ColorSegmentOnBorder = IM_COL32(110, 130, 150, 140);
        constexpr ImU32 k_ColorSegmentOffText = IM_COL32(115, 125, 135, 195);

        // --- Compass Reticle ---
        constexpr ImU32 k_ColorCompassBg = IM_COL32(8, 10, 14, 255);
        constexpr ImU32 k_ColorCompassRingOuter = IM_COL32(70, 85, 100, 50);
        constexpr ImU32 k_ColorCompassRingInner = IM_COL32(70, 85, 100, 25);
        constexpr ImU32 k_ColorCompassCrosshair = IM_COL32(80, 95, 110, 60);
        constexpr ImU32 k_ColorCompassPivot = IM_COL32(160, 175, 190, 180);
        constexpr ImU32 k_ColorCompassWind = IM_COL32(140, 170, 200, 220);
        constexpr ImU32 k_ColorCompassSwell = IM_COL32(185, 155, 115, 210);

        // --- Physics Inspection Row ---
        constexpr ImU32 k_ColorPhysicsLabel = IM_COL32(115, 125, 135, 210);
        constexpr ImU32 k_ColorPhysicsValueA = IM_COL32(205, 215, 225, 240);
        constexpr ImU32 k_ColorPhysicsUnitA = IM_COL32(130, 145, 160, 190);
        constexpr ImU32 k_ColorPhysicsValueB = IM_COL32(135, 145, 155, 205);

        // --- Performance Badge ---
        constexpr ImU32 k_ColorPerfBadgeBg = IM_COL32(12, 15, 20, 245);
        constexpr ImU32 k_ColorPerfBadgeBorder = IM_COL32(70, 85, 100, 55);
        constexpr ImU32 k_ColorPerfBadgeText = IM_COL32(150, 165, 180, 230);

        // --- GPU Timings Scope Bars ---
        constexpr ImU32 k_ColorTimingBarRoot = IM_COL32(75, 95, 115, 35);
        constexpr ImU32 k_ColorTimingBarChild = IM_COL32(65, 80, 100, 22);
        constexpr ImU32 k_ColorTimingBarUnderline = IM_COL32(90, 110, 130, 80);
        constexpr ImU32 k_ColorTimingTreeTick = IM_COL32(70, 85, 100, 80);

        // --- Primary Accent Palettes (Slate Cyan, Muted Amber, Slate Ice) ---
        constexpr ImU32 k_ColorCyanPrimary = IM_COL32(140, 165, 190, 220);
        constexpr ImU32 k_ColorCyanFillBright = IM_COL32(65, 80, 95, 160);
        constexpr ImU32 k_ColorCyanFillMuted = IM_COL32(40, 50, 60, 120);
        constexpr ImU32 k_ColorCyanGlow = IM_COL32(140, 165, 190, 20);

        constexpr ImU32 k_ColorAmberPrimary = IM_COL32(185, 155, 115, 220);
        constexpr ImU32 k_ColorAmberFillBright = IM_COL32(85, 75, 60, 160);
        constexpr ImU32 k_ColorAmberFillMuted = IM_COL32(50, 45, 40, 120);
        constexpr ImU32 k_ColorAmberGlow = IM_COL32(185, 155, 115, 20);

        constexpr ImU32 k_ColorIcePrimary = IM_COL32(140, 165, 190, 220);
        constexpr ImU32 k_ColorIceFillBright = IM_COL32(60, 75, 90, 160);
        constexpr ImU32 k_ColorIceFillMuted = IM_COL32(38, 48, 58, 120);
        constexpr ImU32 k_ColorIceGlow = IM_COL32(140, 165, 190, 20);

        struct ToneColors
        {
            ImU32 Primary{ 0u };
            ImU32 FillBright{ 0u };
            ImU32 FillMuted{ 0u };
            ImU32 Glow{ 0u };
        };

        [[nodiscard]] ImFont* PickHeaderFont(const ImGuiFontSet& fonts, HeaderFontMode font_mode) noexcept;
        [[nodiscard]] ImFont* PickSectionFont(const ImGuiFontSet& fonts, HeaderFontMode font_mode) noexcept;
        [[nodiscard]] ToneColors ResolveTone(AccentTone tone) noexcept;
        [[nodiscard]] float SmoothHoverState(ImGuiID id, bool active_or_hovered) noexcept;
        [[nodiscard]] ImU32 DynamicBorderColor(uint8_t alpha) noexcept;
        [[nodiscard]] ImU32 DynamicTrackBorderColor(uint8_t alpha) noexcept;
        void DrawChamferedRect(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float chamfer,
                               ImU32 fill_color, ImU32 border_color, float border_thickness);
        void DrawCornerReticles(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float arm_length,
                                ImU32 color);
        void DrawCompassReticle(ImDrawList* draw_list, const ImVec2& center, float radius, const SeaState& state,
                                const ImGuiFontSet& fonts);
        void DrawCompassArrow(ImDrawList* draw_list, const ImVec2& center, float angle_radians, float length,
                              ImU32 color, float thickness);
        [[nodiscard]] bool MatchesPreset(const SeaState& current, const SeaState& preset) noexcept;
        [[nodiscard]] float NormalizeSliderValue(float value, float min_value, float max_value, bool logarithmic) noexcept;
        [[nodiscard]] float DenormalizeSliderValue(float normalized, float min_value, float max_value, bool logarithmic) noexcept;
        void DrawChunkyThumb(ImDrawList* draw_list, const ImVec2& center, float half_width, float half_height,
                             const ToneColors& tone, float highlight);
        void DrawPhysicsRow(const ImGuiFontSet& fonts, std::string_view label, std::string_view value_a,
                            std::string_view unit_a, std::string_view value_b, std::string_view unit_b);
    }

    bool BeginTelemetryPanel(const char* name, const ImGuiFontSet& fonts, HeaderFontMode font_mode)
    {
        (void)fonts;
        (void)font_mode;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float maxHeight = std::max(viewport->WorkSize.y - 2.0f * k_PanelMargin, 320.0f);
        ImGui::SetNextWindowPos(ImVec2{ viewport->WorkPos.x + k_PanelMargin, viewport->WorkPos.y + k_PanelMargin }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2{ k_PanelWidth, 300.0f }, ImVec2{ k_PanelWidth, maxHeight });
        ImGui::SetNextWindowSize(ImVec2{ k_PanelWidth, maxHeight }, ImGuiCond_FirstUseEver);

        constexpr ImGuiWindowFlags k_Flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground;

        const bool open = ImGui::Begin(name, nullptr, k_Flags);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 winMin = ImGui::GetWindowPos();
        const ImVec2 winMax = ImVec2{ winMin.x + ImGui::GetWindowWidth(), winMin.y + ImGui::GetWindowHeight() };

        // deep abyssal slate chassis with a 45-degree cut bottom-right corner
        DrawChamferedRect(
            drawList,
            winMin,
            winMax,
            k_PanelChamfer,
            k_ColorPanelBg,
            k_ColorPanelBorder,
            1.0f);

        // subtle top atmospheric header band
        drawList->AddRectFilledMultiColor(
            winMin,
            ImVec2{ winMax.x, winMin.y + 54.0f },
            k_ColorHeaderGradientTop,
            k_ColorHeaderGradientMid,
            k_ColorHeaderGradientEnd,
            k_ColorHeaderGradientEnd);

        // top-left cyan structural bar
        drawList->AddLine(
            ImVec2{ winMin.x, winMin.y },
            ImVec2{ winMin.x + 88.0f, winMin.y },
            k_ColorTopBarAccent,
            2.0f);

        DrawCornerReticles(
            drawList,
            ImVec2{ winMin.x + 3.0f, winMin.y + 3.0f },
            ImVec2{ winMax.x - 3.0f, winMax.y - 3.0f },
            6.0f,
            k_ColorPanelReticle);

        return open;
    }

    void EndTelemetryPanel()
    {
        ImGui::End();
    }

    void DrawTelemetryBanner(const ImGuiFontSet& fonts, HeaderFontMode font_mode, const OceanSimulationSettings& settings)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 startPos = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;

        ImGui::PushFont(PickHeaderFont(fonts, font_mode));
        ImGui::TextColored(k_ColorVecTitle, "OCEAN SIMULATION");
        ImGui::PopFont();

        const float fps = ImGui::GetIO().Framerate;
        const float frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;
        const std::string perfText = std::format("{:.2f} ms  //  {:.0f} FPS", frameMs, fps);

        ImGui::PushFont(fonts.SectionBlinker);
        const ImVec2 perfSize = ImGui::CalcTextSize(perfText.c_str());
        const ImVec2 badgeMin{ startPos.x + availWidth - perfSize.x - 14.0f, startPos.y + 2.0f };
        const ImVec2 badgeMax{ startPos.x + availWidth, startPos.y + perfSize.y + 8.0f };
        DrawChamferedRect(
            drawList,
            badgeMin,
            badgeMax,
            5.0f,
            k_ColorPerfBadgeBg,
            k_ColorPerfBadgeBorder,
            1.0f);
        drawList->AddText(
            ImVec2{ badgeMin.x + 7.0f, badgeMin.y + 3.0f },
            k_ColorPerfBadgeText,
            perfText.c_str());

        const std::string kernelTag = settings.Kernel == IfftKernel::Radix2 ? "RADIX-2" : "RADIX-4";
        const std::string waveTag = settings.WaveSize == 0u ?
            std::string{ "NO WAVE OPS" } :
            std::format("WAVE {}", settings.WaveSize);
        const std::string specLine = std::format("IFFT {}  //  {}x{}  //  {} CASCADES  //  {}",
            kernelTag, settings.Size, settings.Size, k_CascadeCount, waveTag);

        ImGui::TextColored(k_ColorVecSubtitle, "%s", specLine.c_str());
        ImGui::PopFont();

        const ImVec2 linePos = ImGui::GetCursorScreenPos();
        drawList->AddLine(
            ImVec2{ linePos.x, linePos.y + 2.0f },
            ImVec2{ linePos.x + availWidth, linePos.y + 2.0f },
            k_ColorHairlineBanner,
            1.0f);
        ImGui::Dummy(ImVec2{ 0.0f, 6.0f });
    }

    void DrawSeaReadoutCard(const ImGuiFontSet& fonts, const SeaState& state, const SeaStateReadout& readout)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr float k_CardHeight = 82.0f;
        const ImVec2 cardMax{ cardMin.x + availWidth, cardMin.y + k_CardHeight };

        DrawChamferedRect(
            drawList,
            cardMin,
            cardMax,
            8.0f,
            k_ColorCardBg,
            k_ColorCardBorder,
            1.0f);

        // left vertical cyan telemetry bar
        drawList->AddRectFilled(
            cardMin,
            ImVec2{ cardMin.x + 2.0f, cardMax.y },
            k_ColorCardAccentBar);

        // left side: measured significant height and wave metrics
        const float textLeft = cardMin.x + 12.0f;
        drawList->AddText(
            fonts.SectionBlinker,
            13.0f,
            ImVec2{ textLeft, cardMin.y + 7.0f },
            k_ColorTextSubMuted,
            "MEASURED SEA STATE  (4 sqrt(m0))");

        const std::string hsValue = std::format("Hs {:.2f} m", readout.SignificantHeight);
        drawList->AddText(
            fonts.HeaderBlinker,
            22.0f,
            ImVec2{ textLeft, cardMin.y + 23.0f },
            k_ColorTextHeader,
            hsValue.c_str());

        const std::string beaufortTag = std::format("Bft {:.1f}  //  {} ({:.1f} m/s)",
            state.WindForce, readout.BeaufortName, readout.WindSpeed);
        drawList->AddText(
            fonts.BodyItalic,
            14.0f,
            ImVec2{ textLeft + 104.0f, cardMin.y + 29.0f },
            k_ColorTextCyanAccent,
            beaufortTag.c_str());

        const std::string detailLine = std::format("Wind sea {:.2f} m   |   Swell {:.2f} m   |   Peak {:.1f} s ({:.0f} m)",
            readout.WindSeaHeight, readout.SwellHeight, readout.PeakPeriod, readout.PeakWavelength);
        drawList->AddText(
            fonts.Body,
            15.0f,
            ImVec2{ textLeft, cardMin.y + 53.0f },
            k_ColorTextBodyMuted,
            detailLine.c_str());

        // right side: circular wind vs. swell vector compass reticle
        constexpr float k_CompassRadius = 30.0f;
        const ImVec2 compassCenter{ cardMax.x - k_CompassRadius - 12.0f, cardMin.y + 0.5f * k_CardHeight };
        DrawCompassReticle(drawList, compassCenter, k_CompassRadius, state, fonts);

        ImGui::Dummy(ImVec2{ availWidth, k_CardHeight + 4.0f });
    }

    bool DrawSectionHeader(const char* label, const ImGuiFontSet& fonts, HeaderFontMode font_mode,
                           AccentTone accent_tone, bool default_open)
    {
        const ImGuiID id = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const int32_t defaultState = default_open ? 1 : 0;
        bool isOpen = storage->GetInt(id, defaultState) != 0;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr float k_HeaderHeight = 26.0f;

        ImGui::InvisibleButton(label, ImVec2{ availWidth, k_HeaderHeight });
        if (ImGui::IsItemClicked())
        {
            isOpen = !isOpen;
            storage->SetInt(id, isOpen ? 1 : 0);
        }

        const bool hovered = ImGui::IsItemHovered();
        const float hoverT = SmoothHoverState(id + 0x9E3779B9u, hovered);
        const ToneColors tone = ResolveTone(accent_tone);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (hoverT > 0.01f)
        {
            const uint8_t bgAlpha = static_cast<uint8_t>(std::lround(14.0f * hoverT));
            drawList->AddRectFilledMultiColor(
                pos,
                ImVec2{ pos.x + availWidth, pos.y + k_HeaderHeight },
                IM_COL32(0, 155, 195, bgAlpha),
                IM_COL32(0, 155, 195, 0),
                IM_COL32(0, 155, 195, 0),
                IM_COL32(0, 155, 195, bgAlpha));
        }

        ImFont* sectionFont = PickSectionFont(fonts, font_mode);
        ImGui::PushFont(sectionFont);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const float textY = pos.y + 0.5f * (k_HeaderHeight - textSize.y) - 1.0f;
        const ImU32 textColor = hovered ? k_ColorTextHeaderHover : k_ColorTextSection;
        drawList->AddText(ImVec2{ pos.x + 10.0f, textY }, textColor, label);
        ImGui::PopFont();

        // small geometric indicator triangle on the right
        const ImVec2 chevronCenter{ pos.x + availWidth - 10.0f, pos.y + 0.5f * k_HeaderHeight - 1.0f };
        if (isOpen)
        {
            const std::array<ImVec2, 3> tri
            {
                ImVec2{ chevronCenter.x - 4.5f, chevronCenter.y - 2.0f },
                ImVec2{ chevronCenter.x + 4.5f, chevronCenter.y - 2.0f },
                ImVec2{ chevronCenter.x, chevronCenter.y + 3.5f },
            };
            drawList->AddConvexPolyFilled(tri.data(), 3, tone.Primary);
        }
        else
        {
            const std::array<ImVec2, 3> tri
            {
                ImVec2{ chevronCenter.x - 2.0f, chevronCenter.y - 4.5f },
                ImVec2{ chevronCenter.x + 3.5f, chevronCenter.y },
                ImVec2{ chevronCenter.x - 2.0f, chevronCenter.y + 4.5f },
            };
            drawList->AddConvexPolyFilled(tri.data(), 3, k_ColorChevronClosed);
        }

        // full-width 1px hairline + subtle left accent bar
        const float lineY = pos.y + k_HeaderHeight - 1.0f;
        drawList->AddLine(
            ImVec2{ pos.x, lineY },
            ImVec2{ pos.x + availWidth, lineY },
            k_ColorHairline,
            1.0f);

        const float accentWidth = isOpen ? (32.0f + 20.0f * hoverT) : (14.0f + 12.0f * hoverT);
        drawList->AddLine(
            ImVec2{ pos.x, lineY },
            ImVec2{ pos.x + accentWidth, lineY },
            tone.Primary,
            1.5f);

        ImGui::Dummy(ImVec2{ 0.0f, 2.0f });
        return isOpen;
    }

    void DrawSubSectionDivider(const char* label, const ImGuiFontSet& fonts, HeaderFontMode font_mode, AccentTone accent_tone)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const ToneColors tone = ResolveTone(accent_tone);

        ImFont* sectionFont = PickSectionFont(fonts, font_mode);
        ImGui::PushFont(sectionFont);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        drawList->AddRectFilled(
            ImVec2{ pos.x, pos.y + 3.0f },
            ImVec2{ pos.x + 2.0f, pos.y + 3.0f + textSize.y },
            tone.Primary);
        drawList->AddText(ImVec2{ pos.x + 9.0f, pos.y + 2.0f }, k_ColorTextSubHeader, label);
        ImGui::PopFont();

        const float lineX = pos.x + textSize.x + 18.0f;
        const float lineY = pos.y + 3.0f + 0.5f * textSize.y;
        if (lineX < pos.x + availWidth)
        {
            drawList->AddLine(
                ImVec2{ lineX, lineY },
                ImVec2{ pos.x + availWidth, lineY },
                k_ColorHairlineSub,
                1.0f);
        }

        ImGui::Dummy(ImVec2{ availWidth, textSize.y + 6.0f });
    }

    bool DrawPresetGrid(const ImGuiFontSet& fonts, SeaState& state)
    {
        bool changed = false;
        const std::array<SeaStatePreset, 6>& presets = SeaStatePresets();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr int32_t k_Columns = 6;
        constexpr float k_Gap = 5.0f;
        constexpr float k_ButtonHeight = 24.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGui::PushFont(fonts.SectionBlinker);

        std::array<float, k_Columns> textWidths{};
        float totalTextWidth = 0.0f;
        for (int32_t i = 0; i < k_Columns; ++i)
        {
            const std::string_view name = presets[static_cast<size_t>(i)].Name;
            textWidths[static_cast<size_t>(i)] = ImGui::CalcTextSize(name.data(), name.data() + name.size()).x;
            totalTextWidth += textWidths[static_cast<size_t>(i)];
        }

        const float totalGap = k_Gap * static_cast<float>(k_Columns - 1);
        const float extraPerButton = std::max((availWidth - totalGap - totalTextWidth) / static_cast<float>(k_Columns), 8.0f);

        for (int32_t i = 0; i < k_Columns; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, k_Gap);
            }

            const SeaStatePreset& preset = presets[static_cast<size_t>(i)];
            const float buttonWidth = textWidths[static_cast<size_t>(i)] + extraPerButton;
            const bool active = MatchesPreset(state, preset.State);
            const std::string buttonId = std::format("##preset_{}", i);
            const ImVec2 minPos = ImGui::GetCursorScreenPos();
            const ImVec2 maxPos{ minPos.x + buttonWidth, minPos.y + k_ButtonHeight };

            if (ImGui::InvisibleButton(buttonId.c_str(), ImVec2{ buttonWidth, k_ButtonHeight }))
            {
                state = preset.State;
                changed = true;
            }

            const bool hovered = ImGui::IsItemHovered();
            const float hoverT = SmoothHoverState(ImGui::GetItemID(), hovered || active);

            const ImU32 fillCol = active ?
                k_ColorButtonBgActive :
                (hovered ? k_ColorButtonBgHover : k_ColorButtonBg);
            const ImU32 borderCol = active ?
                k_ColorButtonBorderActive :
                DynamicBorderColor(static_cast<uint8_t>(18 + 40 * hoverT));

            DrawChamferedRect(drawList, minPos, maxPos, 5.0f, fillCol, borderCol, 1.0f);

            if (active)
            {
                drawList->AddLine(
                    ImVec2{ minPos.x + 1.0f, maxPos.y - 1.0f },
                    ImVec2{ maxPos.x - 5.0f, maxPos.y - 1.0f },
                    k_ColorButtonIndicator,
                    1.5f);
            }

            const std::string labelStr{ preset.Name };
            const ImVec2 textSize = ImGui::CalcTextSize(labelStr.c_str());
            const ImVec2 textPos
            {
                minPos.x + 0.5f * (buttonWidth - textSize.x),
                minPos.y + 0.5f * (k_ButtonHeight - textSize.y) - 0.5f
            };
            const ImU32 textCol = active ? k_ColorButtonTextActive : k_ColorButtonText;
            drawList->AddText(textPos, textCol, labelStr.c_str());
        }

        ImGui::PopFont();
        ImGui::Dummy(ImVec2{ 0.0f, 2.0f });
        return changed;
    }

    bool DrawArtistSlider(const char* id, std::string_view label, std::string_view hint, float* value,
                          float min_value, float max_value, std::string_view value_text,
                          std::string_view unit_text, const ImGuiFontSet& fonts, AccentTone accent_tone,
                          bool logarithmic)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const bool hasHint = !hint.empty();
        const float rowHeight = hasHint ? 38.0f : 30.0f;

        ImGui::InvisibleButton(id, ImVec2{ availWidth, rowHeight });
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float highlight = SmoothHoverState(ImGui::GetItemID(), hovered || active);
        const ToneColors tone = ResolveTone(accent_tone);

        const float trackLeft = rowMin.x + k_LabelColumnWidth;
        const float trackRight = rowMin.x + availWidth - 8.0f;
        const float trackWidth = std::max(trackRight - trackLeft, 40.0f);

        bool changed = false;
        if (active)
        {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const float normalized = std::clamp((mouseX - trackLeft) / trackWidth, 0.0f, 1.0f);
            const float newValue = DenormalizeSliderValue(normalized, min_value, max_value, logarithmic);
            if (std::fabs(newValue - *value) > 1e-6f)
            {
                *value = newValue;
                changed = true;
            }
        }

        // left column: humanist parameter label + optional semantic transform hint
        const ImU32 labelColor = (hovered || active) ? k_ColorTextLabelHover : k_ColorTextLabel;
        drawList->AddText(
            fonts.Body,
            16.0f,
            ImVec2{ rowMin.x + 2.0f, rowMin.y + 1.0f },
            labelColor,
            label.data(),
            label.data() + label.size());

        if (hasHint)
        {
            drawList->AddText(
                fonts.BodyItalic,
                13.5f,
                ImVec2{ rowMin.x + 2.0f, rowMin.y + 17.0f },
                k_ColorTextHint,
                hint.data(),
                hint.data() + hint.size());
        }

        // right column top: bold numeric value + italic physical unit
        ImGui::PushFont(fonts.BodyItalic);
        const ImVec2 unitSize = unit_text.empty() ?
            ImVec2{ 0.0f, 0.0f } :
            ImGui::CalcTextSize(unit_text.data(), unit_text.data() + unit_text.size());
        ImGui::PopFont();

        ImGui::PushFont(fonts.BodyBold);
        const ImVec2 valSize = ImGui::CalcTextSize(value_text.data(), value_text.data() + value_text.size());
        ImGui::PopFont();

        const float unitX = trackRight - unitSize.x;
        const float valX = unit_text.empty() ? (trackRight - valSize.x) : (unitX - valSize.x - 4.0f);

        drawList->AddText(
            fonts.BodyBold,
            15.0f,
            ImVec2{ valX, rowMin.y + 0.0f },
            (hovered || active) ? k_ColorTextValueHover : k_ColorTextValue,
            value_text.data(),
            value_text.data() + value_text.size());

        if (!unit_text.empty())
        {
            drawList->AddText(
                fonts.BodyItalic,
                13.5f,
                ImVec2{ unitX, rowMin.y + 1.0f },
                k_ColorTextUnit,
                unit_text.data(),
                unit_text.data() + unit_text.size());
        }

        // recessed slider channel
        const float channelCenterY = rowMin.y + (hasHint ? 27.0f : 22.0f);
        constexpr float k_ChannelHalfHeight = 4.0f;
        const ImVec2 chanMin{ trackLeft, channelCenterY - k_ChannelHalfHeight };
        const ImVec2 chanMax{ trackRight, channelCenterY + k_ChannelHalfHeight };

        DrawChamferedRect(
            drawList,
            chanMin,
            chanMax,
            3.0f,
            k_ColorTrackBg,
            DynamicTrackBorderColor(static_cast<uint8_t>(16 + 24 * highlight)),
            1.0f);

        // subtle quarter-interval graduation ticks inside the channel
        for (int32_t tick = 1; tick < 4; ++tick)
        {
            const float tickX = trackLeft + trackWidth * (static_cast<float>(tick) * 0.25f);
            drawList->AddLine(
                ImVec2{ tickX, chanMin.y + 1.0f },
                ImVec2{ tickX, chanMax.y - 1.0f },
                k_ColorTrackTick,
                1.0f);
        }

        const float norm = NormalizeSliderValue(*value, min_value, max_value, logarithmic);
        const float thumbX = trackLeft + norm * trackWidth;

        if (thumbX > chanMin.x + 2.0f)
        {
            drawList->AddRectFilledMultiColor(
                ImVec2{ chanMin.x + 1.0f, chanMin.y + 1.0f },
                ImVec2{ thumbX, chanMax.y - 1.0f },
                tone.FillMuted,
                tone.FillBright,
                tone.FillBright,
                tone.FillMuted);
        }

        // chunky chamfered slider block
        DrawChunkyThumb(drawList, ImVec2{ thumbX, channelCenterY }, 8.0f, 6.5f, tone, highlight);
        return changed;
    }

    bool DrawCenteredAngleSlider(const char* id, std::string_view label, std::string_view hint,
                                 float* degrees, float min_degrees, float max_degrees,
                                 const ImGuiFontSet& fonts, AccentTone accent_tone)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const bool hasHint = !hint.empty();
        const float rowHeight = hasHint ? 38.0f : 30.0f;

        ImGui::InvisibleButton(id, ImVec2{ availWidth, rowHeight });
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float highlight = SmoothHoverState(ImGui::GetItemID(), hovered || active);
        const ToneColors tone = ResolveTone(accent_tone);

        const float trackLeft = rowMin.x + k_LabelColumnWidth;
        const float trackRight = rowMin.x + availWidth - 8.0f;
        const float trackWidth = std::max(trackRight - trackLeft, 40.0f);

        bool changed = false;
        if (active)
        {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const float normalized = std::clamp((mouseX - trackLeft) / trackWidth, 0.0f, 1.0f);
            float newDegrees = std::lerp(min_degrees, max_degrees, normalized);
            // gentle snap to 0 degrees near the center mark
            if (std::fabs(newDegrees) < 1.5f)
            {
                newDegrees = 0.0f;
            }
            if (std::fabs(newDegrees - *degrees) > 1e-4f)
            {
                *degrees = newDegrees;
                changed = true;
            }
        }

        const ImU32 labelColor = (hovered || active) ? k_ColorTextLabelHover : k_ColorTextLabel;
        drawList->AddText(
            fonts.Body,
            16.0f,
            ImVec2{ rowMin.x + 2.0f, rowMin.y + 1.0f },
            labelColor,
            label.data(),
            label.data() + label.size());

        if (hasHint)
        {
            drawList->AddText(
                fonts.BodyItalic,
                13.5f,
                ImVec2{ rowMin.x + 2.0f, rowMin.y + 17.0f },
                k_ColorTextHint,
                hint.data(),
                hint.data() + hint.size());
        }

        const std::string valueText = std::format("{:+.0f}", *degrees);
        constexpr std::string_view k_UnitText{ "deg" };

        ImGui::PushFont(fonts.BodyItalic);
        const ImVec2 unitSize = ImGui::CalcTextSize(k_UnitText.data(), k_UnitText.data() + k_UnitText.size());
        ImGui::PopFont();

        ImGui::PushFont(fonts.BodyBold);
        const ImVec2 valSize = ImGui::CalcTextSize(valueText.c_str());
        ImGui::PopFont();

        const float unitX = trackRight - unitSize.x;
        const float valX = unitX - valSize.x - 4.0f;
        drawList->AddText(fonts.BodyBold, 15.0f, ImVec2{ valX, rowMin.y }, k_ColorTextValue, valueText.c_str());
        drawList->AddText(
            fonts.BodyItalic,
            13.5f,
            ImVec2{ unitX, rowMin.y + 1.0f },
            k_ColorTextUnit,
            k_UnitText.data(),
            k_UnitText.data() + k_UnitText.size());

        const float channelCenterY = rowMin.y + (hasHint ? 27.0f : 22.0f);
        constexpr float k_ChannelHalfHeight = 4.0f;
        const ImVec2 chanMin{ trackLeft, channelCenterY - k_ChannelHalfHeight };
        const ImVec2 chanMax{ trackRight, channelCenterY + k_ChannelHalfHeight };

        DrawChamferedRect(
            drawList,
            chanMin,
            chanMax,
            3.0f,
            k_ColorTrackBg,
            DynamicTrackBorderColor(static_cast<uint8_t>(16 + 24 * highlight)),
            1.0f);

        const float zeroNorm = NormalizeSliderValue(0.0f, min_degrees, max_degrees, false);
        const float zeroX = trackLeft + zeroNorm * trackWidth;
        const float norm = NormalizeSliderValue(*degrees, min_degrees, max_degrees, false);
        const float thumbX = trackLeft + norm * trackWidth;

        // fill from the 0-degree center mark out to the thumb
        const float fillMinX = std::min(zeroX, thumbX);
        const float fillMaxX = std::max(zeroX, thumbX);
        if (fillMaxX - fillMinX > 1.0f)
        {
            drawList->AddRectFilled(
                ImVec2{ fillMinX, chanMin.y + 1.0f },
                ImVec2{ fillMaxX, chanMax.y - 1.0f },
                tone.FillBright);
        }

        // center 0-degree datum tick
        drawList->AddLine(
            ImVec2{ zeroX, chanMin.y - 2.0f },
            ImVec2{ zeroX, chanMax.y + 2.0f },
            k_ColorCenterTick,
            1.0f);

        DrawChunkyThumb(drawList, ImVec2{ thumbX, channelCenterY }, 8.0f, 6.5f, tone, highlight);
        return changed;
    }

    bool DrawTelemetryToggle(const char* id, std::string_view label, std::string_view state_off,
                             std::string_view state_on, bool* value, const ImGuiFontSet& fonts,
                             AccentTone accent_tone)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr float k_RowHeight = 24.0f;

        const bool clicked = ImGui::InvisibleButton(id, ImVec2{ availWidth, k_RowHeight });
        if (clicked)
        {
            *value = !(*value);
        }

        const bool hovered = ImGui::IsItemHovered();
        const float highlight = SmoothHoverState(ImGui::GetItemID(), hovered || *value);
        const ToneColors tone = ResolveTone(accent_tone);

        drawList->AddText(
            fonts.Body,
            16.0f,
            ImVec2{ rowMin.x + 2.0f, rowMin.y + 3.0f },
            hovered ? k_ColorTextLabelHover : k_ColorTextLabel,
            label.data(),
            label.data() + label.size());

        const float switchLeft = rowMin.x + k_LabelColumnWidth;
        const float switchRight = rowMin.x + availWidth - 8.0f;
        const float halfSwitch = 0.5f * (switchRight - switchLeft - 4.0f);

        ImGui::PushFont(fonts.SectionBlinker);
        for (int32_t side = 0; side < 2; ++side)
        {
            const bool sideActive = (side == 0) ? !(*value) : *value;
            const std::string_view sideText = (side == 0) ? state_off : state_on;
            const float boxMinX = switchLeft + static_cast<float>(side) * (halfSwitch + 4.0f);
            const ImVec2 boxMin{ boxMinX, rowMin.y + 1.0f };
            const ImVec2 boxMax{ boxMinX + halfSwitch, rowMin.y + k_RowHeight - 1.0f };

            const ImU32 fillCol = sideActive ?
                tone.FillMuted :
                k_ColorToggleOffBg;
            const ImU32 borderCol = sideActive ?
                tone.Primary :
                DynamicBorderColor(static_cast<uint8_t>(18 + 32 * highlight));

            DrawChamferedRect(drawList, boxMin, boxMax, 4.0f, fillCol, borderCol, 1.0f);

            const ImVec2 textSize = ImGui::CalcTextSize(sideText.data(), sideText.data() + sideText.size());
            const ImVec2 textPos
            {
                boxMin.x + 0.5f * (halfSwitch - textSize.x),
                boxMin.y + 0.5f * (k_RowHeight - 2.0f - textSize.y) - 0.5f
            };
            drawList->AddText(
                textPos,
                sideActive ? k_ColorButtonTextActive : k_ColorToggleOffText,
                sideText.data(),
                sideText.data() + sideText.size());
        }
        ImGui::PopFont();

        return clicked;
    }

    void DrawCascadeBandStrip(const ImGuiFontSet& fonts, const std::array<float, k_CascadeCount>& length_scales,
                              std::array<bool, k_CascadeCount>& cascade_enabled)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 startPos = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;

        drawList->AddText(
            fonts.Body,
            16.0f,
            ImVec2{ startPos.x + 2.0f, startPos.y + 5.0f },
            k_ColorTextLabel,
            "Active cascades");

        const float stripLeft = startPos.x + k_LabelColumnWidth;
        const float stripWidth = availWidth - k_LabelColumnWidth - 8.0f;
        constexpr float k_Gap = 5.0f;
        const float cellWidth = (stripWidth - k_Gap * static_cast<float>(k_CascadeCount - 1u)) / static_cast<float>(k_CascadeCount);
        constexpr float k_CellHeight = 28.0f;

        ImGui::SetCursorScreenPos(ImVec2{ stripLeft, startPos.y });
        ImGui::PushFont(fonts.SectionBlinker);

        for (int32_t cascade = 0; cascade < static_cast<int32_t>(k_CascadeCount); ++cascade)
        {
            if (cascade > 0)
            {
                ImGui::SameLine(0.0f, k_Gap);
            }

            const std::string btnId = std::format("##cascade_band_{}", cascade);
            const ImVec2 cellMin = ImGui::GetCursorScreenPos();
            const ImVec2 cellMax{ cellMin.x + cellWidth, cellMin.y + k_CellHeight };

            if (ImGui::InvisibleButton(btnId.c_str(), ImVec2{ cellWidth, k_CellHeight }))
            {
                cascade_enabled[static_cast<size_t>(cascade)] = !cascade_enabled[static_cast<size_t>(cascade)];
            }

            const bool enabled = cascade_enabled[static_cast<size_t>(cascade)];
            const bool hovered = ImGui::IsItemHovered();

            const ImU32 fillCol = enabled ?
                k_ColorBandOnBg :
                k_ColorBandOffBg;
            const ImU32 borderCol = enabled ?
                k_ColorBandOnBorder :
                (hovered ? k_ColorBandHoverBorder : k_ColorBandOffBorder);

            DrawChamferedRect(drawList, cellMin, cellMax, 5.0f, fillCol, borderCol, 1.0f);

            // top LED status bar
            const ImU32 ledCol = enabled ? k_ColorBandOnLed : k_ColorBandOffLed;
            drawList->AddLine(
                ImVec2{ cellMin.x + 4.0f, cellMin.y + 2.0f },
                ImVec2{ cellMax.x - 4.0f, cellMin.y + 2.0f },
                ledCol,
                2.0f);

            const std::string bandLabel = std::format("{:.0f} m", length_scales[static_cast<size_t>(cascade)]);
            const ImVec2 textSize = ImGui::CalcTextSize(bandLabel.c_str());
            drawList->AddText(
                ImVec2{ cellMin.x + 0.5f * (cellWidth - textSize.x), cellMin.y + 8.0f },
                enabled ? k_ColorBandOnText : k_ColorBandOffText,
                bandLabel.c_str());
        }

        ImGui::PopFont();
        ImGui::Dummy(ImVec2{ 0.0f, 2.0f });
    }

    bool DrawSpectrumPhysicsInspector(const ImGuiFontSet& fonts, SpectrumParameters& spectrum)
    {
        bool changed = false;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // directional spreading model 3-way selector
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        drawList->AddText(
            fonts.Body,
            16.0f,
            ImVec2{ rowMin.x + 2.0f, rowMin.y + 4.0f },
            k_ColorTextLabel,
            "Spreading model");

        constexpr std::array<std::string_view, 3> k_Models{ "MITSUYASU", "HASSELMANN", "DONELAN" };
        const float segLeft = rowMin.x + k_LabelColumnWidth;
        const float segWidth = availWidth - k_LabelColumnWidth - 8.0f;
        constexpr float k_Gap = 4.0f;
        const float cellW = (segWidth - 2.0f * k_Gap) / 3.0f;
        constexpr float k_CellH = 24.0f;

        ImGui::SetCursorScreenPos(ImVec2{ segLeft, rowMin.y });
        ImGui::PushFont(fonts.SectionBlinker);
        for (int32_t m = 0; m < 3; ++m)
        {
            if (m > 0)
            {
                ImGui::SameLine(0.0f, k_Gap);
            }

            const std::string btnId = std::format("##spread_mode_{}", m);
            const ImVec2 cMin = ImGui::GetCursorScreenPos();
            const ImVec2 cMax{ cMin.x + cellW, cMin.y + k_CellH };
            if (ImGui::InvisibleButton(btnId.c_str(), ImVec2{ cellW, k_CellH }))
            {
                spectrum.DirectionalSpreadingMode = static_cast<uint32_t>(m);
                changed = true;
            }

            const bool selected = spectrum.DirectionalSpreadingMode == static_cast<uint32_t>(m);
            const bool hovered = ImGui::IsItemHovered();
            DrawChamferedRect(
                drawList,
                cMin,
                cMax,
                4.0f,
                selected ? k_ColorSegmentOnBg : k_ColorSegmentOffBg,
                selected ? k_ColorSegmentOnBorder : (hovered ? k_ColorBandHoverBorder : k_ColorBandOffBorder),
                1.0f);

            const std::string_view modelName = k_Models[static_cast<size_t>(m)];
            const ImVec2 tSize = ImGui::CalcTextSize(modelName.data(), modelName.data() + modelName.size());
            drawList->AddText(
                ImVec2{ cMin.x + 0.5f * (cellW - tSize.x), cMin.y + 0.5f * (k_CellH - tSize.y) - 0.5f },
                selected ? k_ColorButtonTextActive : k_ColorSegmentOffText,
                modelName.data(),
                modelName.data() + modelName.size());
        }
        ImGui::PopFont();

        // seed input row
        const ImVec2 seedMin = ImGui::GetCursorScreenPos();
        drawList->AddText(
            fonts.Body,
            16.0f,
            ImVec2{ seedMin.x + 2.0f, seedMin.y + 3.0f },
            k_ColorTextLabel,
            "Spectrum seed");

        ImGui::SetCursorScreenPos(ImVec2{ seedMin.x + k_LabelColumnWidth, seedMin.y });
        ImGui::SetNextItemWidth(availWidth - k_LabelColumnWidth - 8.0f);
        int32_t seedValue = static_cast<int32_t>(spectrum.NoiseSeed);
        if (ImGui::InputInt("##spectrum_seed", &seedValue, 1, 10))
        {
            spectrum.NoiseSeed = static_cast<uint32_t>(std::max(seedValue, 0));
            changed = true;
        }

        ImGui::Dummy(ImVec2{ 0.0f, 3.0f });

        // read-only resolved physical parameters card
        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        constexpr float k_CardHeight = 88.0f;
        const ImVec2 cardMax{ cardMin.x + availWidth - 4.0f, cardMin.y + k_CardHeight };

        DrawChamferedRect(
            drawList,
            cardMin,
            cardMax,
            6.0f,
            k_ColorCardSubBg,
            k_ColorCardSubBorder,
            1.0f);

        ImGui::SetCursorScreenPos(ImVec2{ cardMin.x + 10.0f, cardMin.y + 6.0f });
        ImGui::PushFont(fonts.BodyItalic);
        ImGui::TextColored(k_ColorVecNote, "Resolved physical spectrum (SeaState -> SpectrumParameters)");
        ImGui::PopFont();

        DrawPhysicsRow(
            fonts,
            "Wind sea",
            std::format("{:.1f}", spectrum.WindSpeed), "m/s",
            std::format("fetch {:.0f} km   |   gamma {:.1f}   |   fade {:.3f} m",
                spectrum.Fetch, spectrum.PeakingFactor, spectrum.FadeLength), "");

        DrawPhysicsRow(
            fonts,
            "Spreading",
            std::format("{:.2f}", spectrum.Alignment), "align",
            std::format("narrowness {:.2f}", spectrum.SwellAmount), "");

        DrawPhysicsRow(
            fonts,
            "Swell JONSWAP",
            std::format("{:.1f}", spectrum.SwellWindSpeed), "m/s",
            std::format("fetch {:.0f} km   |   gamma {:.1f}   |   energy x{:.3f}",
                spectrum.SwellFetch, spectrum.SwellPeakingFactor, spectrum.SwellScale), "");

        ImGui::SetCursorScreenPos(ImVec2{ cardMin.x, cardMax.y + 4.0f });
        return changed;
    }

    void DrawGpuTimingsSection(const ImGuiFontSet& fonts, HeaderFontMode font_mode, const rhi::GpuTimers& gpu_timers,
                               const std::unordered_map<uint64_t, double>& timing_averages)
    {
        if (!DrawSectionHeader("GPU TIMINGS", fonts, font_mode, AccentTone::Ice, true))
        {
            return;
        }

        if (!gpu_timers.Active())
        {
            ImGui::PushFont(fonts.BodyItalic);
            ImGui::TextColored(k_ColorVecMutedStatus, "%s", std::string{ gpu_timers.InactiveReason() }.c_str());
            ImGui::PopFont();
            return;
        }

        const std::optional<rhi::GpuTimingBlock> latest = gpu_timers.Latest();
        if (!latest.has_value())
        {
            ImGui::PushFont(fonts.BodyItalic);
            ImGui::TextColored(k_ColorVecMutedStatus, "Waiting for first completed frame...");
            ImGui::PopFont();
            return;
        }

        const double topLevelMs = std::max(latest->TopLevelNanoseconds * 1e-6, 0.001);
        constexpr ImGuiTableFlags k_TableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;

        if (ImGui::BeginTable("GpuTimingsTelemetry", 3, k_TableFlags))
        {
            ImGui::TableSetupColumn("SCOPE", ImGuiTableColumnFlags_WidthStretch, 2.2f);
            ImGui::TableSetupColumn("AVG (ms)", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("LAST (ms)", ImGuiTableColumnFlags_WidthStretch, 0.9f);

            ImGui::PushFont(fonts.SectionBlinker);
            ImGui::TableHeadersRow();
            ImGui::PopFont();

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            for (const rhi::GpuTiming& timing : latest->Timings)
            {
                const std::string_view name = gpu_timers.Name(timing.Hash);
                const auto averageIter = timing_averages.find(timing.Hash);
                const double avgMs = averageIter != timing_averages.end() ? averageIter->second * 1e-6 : 0.0;
                const double lastMs = timing.Nanoseconds * 1e-6;
                const float ratio = static_cast<float>(std::clamp(avgMs / topLevelMs, 0.0, 1.0));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                const ImVec2 cellMin = ImGui::GetCursorScreenPos();
                const float rowWidth = ImGui::GetContentRegionAvail().x;
                const float barWidth = rowWidth * ratio;

                if (barWidth > 1.0f)
                {
                    const ImU32 barCol = timing.Depth == 0u ?
                        k_ColorTimingBarRoot :
                        k_ColorTimingBarChild;
                    drawList->AddRectFilled(
                        ImVec2{ cellMin.x, cellMin.y + 1.0f },
                        ImVec2{ cellMin.x + barWidth, cellMin.y + 19.0f },
                        barCol);
                    drawList->AddLine(
                        ImVec2{ cellMin.x, cellMin.y + 19.0f },
                        ImVec2{ cellMin.x + barWidth, cellMin.y + 19.0f },
                        k_ColorTimingBarUnderline,
                        1.0f);
                }

                const float indentPx = static_cast<float>(timing.Depth) * 14.0f;
                if (timing.Depth > 0u)
                {
                    const float tickX = cellMin.x + indentPx - 9.0f;
                    const float tickY = cellMin.y + 9.0f;
                    drawList->AddLine(
                        ImVec2{ tickX, tickY - 5.0f },
                        ImVec2{ tickX, tickY },
                        k_ColorTimingTreeTick,
                        1.0f);
                    drawList->AddLine(
                        ImVec2{ tickX, tickY },
                        ImVec2{ tickX + 5.0f, tickY },
                        k_ColorTimingTreeTick,
                        1.0f);
                }

                ImGui::SetCursorScreenPos(ImVec2{ cellMin.x + indentPx, cellMin.y });
                const ImVec4 scopeColor = timing.Depth == 0u ?
                    k_ColorVecTimingRoot :
                    k_ColorVecTimingChild;
                ImGui::TextColored(scopeColor, "%.*s", static_cast<int>(name.size()), name.data());

                ImGui::TableNextColumn();
                ImGui::PushFont(fonts.BodyBold);
                ImGui::TextColored(k_ColorVecTimingAvg, "%.3f", avgMs);
                ImGui::PopFont();

                ImGui::TableNextColumn();
                ImGui::TextColored(k_ColorVecTimingLast, "%.3f", lastMs);
            }

            ImGui::EndTable();
        }

        ImGui::PushFont(fonts.BodyItalic);
        ImGui::TextColored(
            k_ColorVecTimingFooter,
            "Scoped GPU total: %.3f ms   |   Dropped: %u scopes, %llu frames   |   Lost: %llu",
            topLevelMs,
            latest->DroppedScopes,
            static_cast<unsigned long long>(gpu_timers.DroppedBlocks()),
            static_cast<unsigned long long>(gpu_timers.LostBlocks()));
        ImGui::PopFont();
    }

    namespace
    {
        ImFont* PickHeaderFont(const ImGuiFontSet& fonts, HeaderFontMode font_mode) noexcept
        {
            if (font_mode == HeaderFontMode::Michroma && fonts.HeaderMichroma != nullptr)
            {
                return fonts.HeaderMichroma;
            }

            return fonts.HeaderBlinker;
        }

        ImFont* PickSectionFont(const ImGuiFontSet& fonts, HeaderFontMode font_mode) noexcept
        {
            if (font_mode == HeaderFontMode::Michroma && fonts.SectionMichroma != nullptr)
            {
                return fonts.SectionMichroma;
            }

            return fonts.SectionBlinker;
        }

        ToneColors ResolveTone(AccentTone tone) noexcept
        {
            switch (tone)
            {
            case AccentTone::Amber:
                return ToneColors
                {
                    k_ColorAmberPrimary,
                    k_ColorAmberFillBright,
                    k_ColorAmberFillMuted,
                    k_ColorAmberGlow,
                };
            case AccentTone::Ice:
                return ToneColors
                {
                    k_ColorIcePrimary,
                    k_ColorIceFillBright,
                    k_ColorIceFillMuted,
                    k_ColorIceGlow,
                };
            case AccentTone::Cyan:
            case AccentTone::Invalid:
            default:
                return ToneColors
                {
                    k_ColorCyanPrimary,
                    k_ColorCyanFillBright,
                    k_ColorCyanFillMuted,
                    k_ColorCyanGlow,
                };
            }
        }

        float SmoothHoverState(ImGuiID id, bool active_or_hovered) noexcept
        {
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const float current = storage->GetFloat(id, 0.0f);
            const float target = active_or_hovered ? 1.0f : 0.0f;
            const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
            const float step = 1.0f - std::exp(-20.0f * dt);
            const float next = current + (target - current) * step;
            storage->SetFloat(id, next);
            return next;
        }

        ImU32 DynamicBorderColor(uint8_t alpha) noexcept
        {
            return IM_COL32(75, 88, 100, alpha);
        }

        ImU32 DynamicTrackBorderColor(uint8_t alpha) noexcept
        {
            return IM_COL32(75, 90, 105, alpha);
        }

        void DrawChamferedRect(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float chamfer,
                               ImU32 fill_color, ImU32 border_color, float border_thickness)
        {
            const float cut = std::min({ chamfer, 0.45f * (max_pos.x - min_pos.x), 0.45f * (max_pos.y - min_pos.y) });
            const std::array<ImVec2, 5> points
            {
                min_pos,
                ImVec2{ max_pos.x, min_pos.y },
                ImVec2{ max_pos.x, max_pos.y - cut },
                ImVec2{ max_pos.x - cut, max_pos.y },
                ImVec2{ min_pos.x, max_pos.y },
            };

            if ((fill_color & IM_COL32_A_MASK) != 0u)
            {
                draw_list->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), fill_color);
            }

            if ((border_color & IM_COL32_A_MASK) != 0u)
            {
                draw_list->AddPolyline(points.data(), static_cast<int>(points.size()), border_color, ImDrawFlags_Closed, border_thickness);
            }
        }

        void DrawCornerReticles(ImDrawList* draw_list, const ImVec2& min_pos, const ImVec2& max_pos, float arm_length,
                                ImU32 color)
        {
            // top-left L-bracket
            draw_list->AddLine(min_pos, ImVec2{ min_pos.x + arm_length, min_pos.y }, color, 1.0f);
            draw_list->AddLine(min_pos, ImVec2{ min_pos.x, min_pos.y + arm_length }, color, 1.0f);

            // bottom-left L-bracket
            draw_list->AddLine(ImVec2{ min_pos.x, max_pos.y }, ImVec2{ min_pos.x + arm_length, max_pos.y }, color, 1.0f);
            draw_list->AddLine(ImVec2{ min_pos.x, max_pos.y - arm_length }, ImVec2{ min_pos.x, max_pos.y }, color, 1.0f);

            // top-right L-bracket
            draw_list->AddLine(ImVec2{ max_pos.x - arm_length, min_pos.y }, ImVec2{ max_pos.x, min_pos.y }, color, 1.0f);
            draw_list->AddLine(ImVec2{ max_pos.x, min_pos.y }, ImVec2{ max_pos.x, min_pos.y + arm_length }, color, 1.0f);
        }

        void DrawCompassReticle(ImDrawList* draw_list, const ImVec2& center, float radius, const SeaState& state,
                                const ImGuiFontSet& fonts)
        {
            draw_list->AddCircleFilled(center, radius, k_ColorCompassBg, 32);
            draw_list->AddCircle(center, radius, k_ColorCompassRingOuter, 32, 1.0f);
            draw_list->AddCircle(center, radius * 0.5f, k_ColorCompassRingInner, 24, 1.0f);

            // cardinal crosshair ticks
            for (int32_t i = 0; i < 4; ++i)
            {
                const float angle = static_cast<float>(i) * 0.5f * k_Pi;
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                draw_list->AddLine(
                    ImVec2{ center.x + c * (radius - 4.0f), center.y + s * (radius - 4.0f) },
                    ImVec2{ center.x + c * radius, center.y + s * radius },
                    k_ColorCompassCrosshair,
                    1.0f);
            }

            // swell vector in amber (0 deg points up in the reticle)
            const float swellFraction = std::clamp(state.SwellHeight / 6.0f, 0.0f, 1.0f);
            if (swellFraction > 0.02f)
            {
                const float swellAngle = (state.SwellDirection - 90.0f) * k_DegreesToRadians;
                const float swellLen = (0.30f + 0.62f * swellFraction) * (radius - 4.0f);
                DrawCompassArrow(draw_list, center, swellAngle, swellLen, k_ColorCompassSwell, 2.0f);
            }

            // wind vector in cyan
            const float windFraction = std::clamp(state.WindForce / 12.0f, 0.15f, 1.0f);
            const float windAngle = (state.WindDirection - 90.0f) * k_DegreesToRadians;
            const float windLen = (0.30f + 0.62f * windFraction) * (radius - 4.0f);
            DrawCompassArrow(draw_list, center, windAngle, windLen, k_ColorCompassWind, 2.0f);

            draw_list->AddCircleFilled(center, 2.0f, k_ColorCompassPivot, 8);
            (void)fonts;
        }

        void DrawCompassArrow(ImDrawList* draw_list, const ImVec2& center, float angle_radians, float length,
                              ImU32 color, float thickness)
        {
            const float dirX = std::cos(angle_radians);
            const float dirY = std::sin(angle_radians);
            const ImVec2 tip{ center.x + dirX * length, center.y + dirY * length };
            draw_list->AddLine(center, tip, color, thickness);

            const float headLen = 5.5f;
            const float headSpread = 0.48f;
            const ImVec2 leftWing
            {
                tip.x - headLen * std::cos(angle_radians - headSpread),
                tip.y - headLen * std::sin(angle_radians - headSpread)
            };
            const ImVec2 rightWing
            {
                tip.x - headLen * std::cos(angle_radians + headSpread),
                tip.y - headLen * std::sin(angle_radians + headSpread)
            };
            const std::array<ImVec2, 3> head{ tip, leftWing, rightWing };
            draw_list->AddConvexPolyFilled(head.data(), 3, color);
        }

        bool MatchesPreset(const SeaState& current, const SeaState& preset) noexcept
        {
            constexpr float k_Eps = 1e-3f;
            return std::fabs(current.WindForce - preset.WindForce) < k_Eps &&
                   std::fabs(current.Openness - preset.Openness) < k_Eps &&
                   std::fabs(current.WindDirection - preset.WindDirection) < k_Eps &&
                   std::fabs(current.Spread - preset.Spread) < k_Eps &&
                   std::fabs(current.Regularity - preset.Regularity) < k_Eps &&
                   std::fabs(current.RippleDetail - preset.RippleDetail) < k_Eps &&
                   std::fabs(current.Depth - preset.Depth) < k_Eps &&
                   std::fabs(current.SwellHeight - preset.SwellHeight) < k_Eps &&
                   std::fabs(current.SwellPeriod - preset.SwellPeriod) < k_Eps &&
                   std::fabs(current.SwellDirection - preset.SwellDirection) < k_Eps &&
                   std::fabs(current.SwellOrder - preset.SwellOrder) < k_Eps &&
                   std::fabs(current.HeightScale - preset.HeightScale) < k_Eps;
        }

        float NormalizeSliderValue(float value, float min_value, float max_value, bool logarithmic) noexcept
        {
            const float clamped = std::clamp(value, min_value, max_value);
            if (logarithmic && min_value > 0.0f && max_value > min_value)
            {
                const float logMin = std::log(min_value);
                const float logMax = std::log(max_value);
                return std::clamp((std::log(clamped) - logMin) / (logMax - logMin), 0.0f, 1.0f);
            }

            return std::clamp((clamped - min_value) / (max_value - min_value), 0.0f, 1.0f);
        }

        float DenormalizeSliderValue(float normalized, float min_value, float max_value, bool logarithmic) noexcept
        {
            const float t = std::clamp(normalized, 0.0f, 1.0f);
            if (logarithmic && min_value > 0.0f && max_value > min_value)
            {
                const float logMin = std::log(min_value);
                const float logMax = std::log(max_value);
                return std::exp(std::lerp(logMin, logMax, t));
            }

            return std::lerp(min_value, max_value, t);
        }

        void DrawChunkyThumb(ImDrawList* draw_list, const ImVec2& center, float half_width, float half_height,
                             const ToneColors& tone, float highlight)
        {
            const float expand = 1.0f * highlight;
            const ImVec2 minPos{ center.x - half_width - expand, center.y - half_height - expand };
            const ImVec2 maxPos{ center.x + half_width + expand, center.y + half_height + expand };

            if (highlight > 0.05f)
            {
                DrawChamferedRect(
                    draw_list,
                    ImVec2{ minPos.x - 2.0f, minPos.y - 2.0f },
                    ImVec2{ maxPos.x + 2.0f, maxPos.y + 2.0f },
                    3.5f,
                    tone.Glow,
                    0u,
                    0.0f);
            }

            const ImU32 bodyFill = highlight > 0.3f ?
                k_ColorThumbBgHover :
                k_ColorThumbBg;

            const ImU32 thumbBorder = highlight > 0.2f ? tone.Primary : k_ColorThumbBorder;
            DrawChamferedRect(draw_list, minPos, maxPos, 3.0f, bodyFill, thumbBorder, 1.0f);

            // subtle vertical grip notch inside the chunky slider thumb
            const ImU32 notchColor = highlight > 0.2f ? tone.Primary : k_ColorThumbNotch;
            draw_list->AddLine(
                ImVec2{ center.x, minPos.y + 3.0f },
                ImVec2{ center.x, maxPos.y - 3.0f },
                notchColor,
                1.0f);
        }

        void DrawPhysicsRow(const ImGuiFontSet& fonts, std::string_view label, std::string_view value_a,
                            std::string_view unit_a, std::string_view value_b, std::string_view unit_b)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            drawList->AddText(
                fonts.Body,
                15.0f,
                ImVec2{ rowPos.x + 10.0f, rowPos.y },
                k_ColorPhysicsLabel,
                label.data(),
                label.data() + label.size());

            drawList->AddText(
                fonts.BodyBold,
                14.5f,
                ImVec2{ rowPos.x + 108.0f, rowPos.y },
                k_ColorPhysicsValueA,
                value_a.data(),
                value_a.data() + value_a.size());

            ImGui::PushFont(fonts.BodyBold);
            const ImVec2 valASize = ImGui::CalcTextSize(value_a.data(), value_a.data() + value_a.size());
            ImGui::PopFont();

            const float unitAX = rowPos.x + 108.0f + valASize.x + 4.0f;
            drawList->AddText(
                fonts.BodyItalic,
                13.5f,
                ImVec2{ unitAX, rowPos.y + 1.0f },
                k_ColorPhysicsUnitA,
                unit_a.data(),
                unit_a.data() + unit_a.size());

            drawList->AddText(
                fonts.Body,
                14.0f,
                ImVec2{ rowPos.x + 178.0f, rowPos.y + 0.5f },
                k_ColorPhysicsValueB,
                value_b.data(),
                value_b.data() + value_b.size());

            (void)unit_b;
            ImGui::Dummy(ImVec2{ 0.0f, 18.0f });
        }
    }
}
