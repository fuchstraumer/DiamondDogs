#include "OceanUi.hpp"
#include "GpuTimers.hpp"
#include "ImGuiModule.hpp"
#include "OceanSimulation.hpp"
#include "SeaState.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ocean
{
    namespace
    {
        // Role tokens. A color belongs to a role, never to a widget. The surfaces are white at a low alpha, so that
        // every step shows on the final image whatever the scene is under the scrim.
        constexpr ImU32 k_SurfaceTop = IM_COL32(12, 16, 22, 255);
        constexpr ImU32 k_SurfaceBottom = IM_COL32(7, 10, 14, 255);
        constexpr ImU32 k_SurfacePopup = IM_COL32(10, 14, 20, 252);
        constexpr ImU32 k_FieldRest = IM_COL32(255, 255, 255, 14);
        constexpr ImU32 k_FieldHover = IM_COL32(255, 255, 255, 26);
        constexpr ImU32 k_FieldActive = IM_COL32(255, 255, 255, 38);

        constexpr ImU32 k_TextValue = IM_COL32(236, 241, 247, 255);
        constexpr ImU32 k_TextLabel = IM_COL32(152, 163, 178, 255);
        constexpr ImU32 k_TextHint = IM_COL32(108, 120, 136, 255);

        constexpr ImU32 k_LineWeak = IM_COL32(255, 255, 255, 28);
        constexpr ImU32 k_LineStrong = IM_COL32(255, 255, 255, 70);

        constexpr ImU32 k_Accent = IM_COL32(72, 208, 232, 255);
        constexpr ImU32 k_Warning = IM_COL32(242, 112, 82, 255);

        constexpr ImU32 k_DataWind = IM_COL32(142, 176, 255, 255);
        constexpr ImU32 k_DataSwell = IM_COL32(236, 186, 112, 255);
        constexpr ImU32 k_DataGraphicsQueue = IM_COL32(104, 198, 168, 255);
        constexpr ImU32 k_DataAsyncQueue = IM_COL32(196, 150, 232, 255);

        // Type scale, in pixels at a DPI factor of 1.
        constexpr float k_SizeCaption = 12.5f;
        constexpr float k_SizeCaptionMichroma = 9.5f;
        constexpr float k_SizeBody = 15.0f;
        constexpr float k_SizeTitle = 19.0f;
        constexpr float k_SizeTitleMichroma = 14.0f;
        constexpr float k_SizeHero = 56.0f;
        constexpr float k_SizeHeroMichroma = 40.0f;

        // Spacing scale, in pixels at a DPI factor of 1.
        constexpr float k_Space1 = 4.0f;
        constexpr float k_Space2 = 8.0f;
        constexpr float k_Space3 = 12.0f;
        constexpr float k_Space4 = 16.0f;

        constexpr float k_PanelMargin = 16.0f;
        constexpr float k_LabelColumnWidth = 150.0f;
        constexpr float k_SliderRowHeight = 34.0f;
        constexpr float k_SliderTrackOffset = 26.0f;
        constexpr float k_SliderGrabSize = 12.0f;
        constexpr float k_HandleRadius = 4.5f;
        constexpr float k_HandleRadiusHot = 6.0f;
        constexpr float k_OptionHeight = 28.0f;
        constexpr float k_TabHeight = 32.0f;
        constexpr float k_OptionPadding = 20.0f;
        constexpr float k_KeyValueHeight = 22.0f;
        constexpr float k_CompassRadius = 54.0f;
        constexpr int32_t k_MaxOptions = 8;
        constexpr float k_AngleSnapDegrees = 1.5f;
        constexpr float k_Pi = std::numbers::pi_v<float>;
        constexpr float k_DegreesToRadians = k_Pi / 180.0f;

        enum class Face : uint8_t
        {
            Invalid = 0,
            Caps,
            Title,
            Hero,
            Body,
            Small,
            Note,
        };

        enum class CellKind : uint8_t
        {
            Invalid = 0,
            Option,
            Tab,
        };

        struct FaceSpec
        {
            ImFont* Font{ nullptr };
            float Size{ 0.0f };
        };

        // The fonts and the header typeface of the panel that is open. The UI thread alone reads and writes this,
        // between BeginTelemetryPanel and EndTelemetryPanel, so that no widget needs the fonts as a parameter.
        struct FrameContext
        {
            const ImGuiFontSet* Fonts{ nullptr };
            HeaderFontMode Mode{ HeaderFontMode::Blinker };
        };

        struct OptionRowSpec
        {
            const char* Id{ nullptr };
            std::string_view Label{};
            std::span<const std::string_view> Options{};
            std::span<const bool> On{};
            int32_t Columns{ 1 };
            ImU32 SelectedColor{ 0u };
            CellKind Kind{ CellKind::Option };
        };

        struct SliderRowSpec
        {
            const char* Id{ nullptr };
            std::string_view Label{};
            std::string_view Hint{};
            std::string_view ValueText{};
            std::string_view UnitText{};
            float Min{ 0.0f };
            float Max{ 1.0f };
            bool Logarithmic{ false };
            bool Centered{ false };
        };

        struct KeyValueSpec
        {
            std::string_view Label{};
            std::string_view Value{};
            std::string_view Unit{};
            ImU32 DotColor{ 0u };
        };

        FrameContext activeFrame;

        [[nodiscard]] float Scale() noexcept;
        [[nodiscard]] float Px(float pixels) noexcept;
        [[nodiscard]] ImU32 WithAlpha(ImU32 color, float alpha_scale) noexcept;
        [[nodiscard]] FaceSpec ResolveFace(Face face) noexcept;
        [[nodiscard]] float TextWidth(Face face, std::string_view text);
        [[nodiscard]] float LineHeight(Face face);
        void PutText(ImDrawList* draw_list, Face face, const ImVec2& position, ImU32 color, std::string_view text);
        void PutTextRight(ImDrawList* draw_list, Face face, const ImVec2& right_position, ImU32 color,
                          std::string_view text);
        void TextItem(Face face, ImU32 color, std::string_view text);
        void TextItemRight(Face face, ImU32 color, std::string_view text);
        void ApplyTelemetryStyle() noexcept;
        void DrawPanelSurface(const ImVec2& min_pos, const ImVec2& max_pos);
        void DrawCloseCross(ImDrawList* draw_list, const ImVec2& min_pos, float size, ImU32 color);
        void DrawCompass(ImDrawList* draw_list, const ImVec2& center, float radius, const SeaState& state);
        void DrawCompassArrow(ImDrawList* draw_list, const ImVec2& center, float angle_radians, float length,
                              ImU32 color);
        void DrawKeyValueAt(ImDrawList* draw_list, const ImVec2& position, float width, const KeyValueSpec& spec);
        void DrawSectionLabelWithDot(std::string_view label, ImU32 dot_color);
        [[nodiscard]] int32_t DrawOptionRow(const OptionRowSpec& spec);
        void FillSelection(std::span<bool> on, int32_t selected) noexcept;
        [[nodiscard]] bool DrawSliderRow(const SliderRowSpec& spec, float* value);
        void DrawSliderTooltip(std::string_view label, std::string_view hint);
        [[nodiscard]] bool MatchesPreset(const SeaState& current, const SeaState& preset) noexcept;
        void DrawTimingTable(const char* table_id, const rhi::GpuTimers& gpu_timers,
                             const std::unordered_map<uint64_t, double>& timing_averages, ImU32 queue_color);
    }

    bool BeginTelemetryPanel(const char* name, const ImGuiFontSet& fonts, HeaderFontMode font_mode, bool* p_open)
    {
        if (p_open != nullptr && !(*p_open))
        {
            return false;
        }

        activeFrame.Fonts = &fonts;
        activeFrame.Mode = font_mode;
        ApplyTelemetryStyle();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float margin = Px(k_PanelMargin);
        const float maxHeight = std::max(viewport->WorkSize.y - 2.0f * margin, 320.0f);
        const float minWidth = Px(440.0f);
        const float maxWidth = std::max(viewport->WorkSize.x - 2.0f * margin, Px(850.0f));

        ImGui::SetNextWindowPos(ImVec2{ viewport->WorkPos.x + margin, viewport->WorkPos.y + margin },
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2{ minWidth, 300.0f }, ImVec2{ maxWidth, maxHeight });
        ImGui::SetNextWindowSize(ImVec2{ Px(480.0f), maxHeight }, ImGuiCond_FirstUseEver);

        constexpr ImGuiWindowFlags k_Flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground;

        const bool open = ImGui::Begin(name, p_open, k_Flags);

        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowMax{ windowMin.x + ImGui::GetWindowWidth(), windowMin.y + ImGui::GetWindowHeight() };
        DrawPanelSurface(windowMin, windowMax);

        return open;
    }

    void EndTelemetryPanel()
    {
        ImGui::End();
    }

    bool DrawRestoreMenuButton(const ImGuiFontSet& fonts, bool* p_open)
    {
        if (p_open != nullptr && *p_open)
        {
            return false;
        }

        activeFrame.Fonts = &fonts;
        ApplyTelemetryStyle();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float margin = Px(k_PanelMargin);
        ImGui::SetNextWindowPos(ImVec2{ viewport->WorkPos.x + margin, viewport->WorkPos.y + margin }, ImGuiCond_Always);

        constexpr ImGuiWindowFlags k_Flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBackground;

        bool clicked = false;

        if (ImGui::Begin("##RestoreTelemetryPanel", nullptr, k_Flags))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const ImVec2 buttonSize{ Px(88.0f), Px(28.0f) };

            if (ImGui::InvisibleButton("##restore_menu_btn", buttonSize))
            {
                if (p_open != nullptr)
                {
                    *p_open = true;
                }

                clicked = true;
            }

            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 maxPosition{ position.x + buttonSize.x, position.y + buttonSize.y };
            drawList->AddRectFilled(position, maxPosition, hovered ? k_SurfacePopup : k_SurfaceTop);
            drawList->AddRect(position, maxPosition, hovered ? k_LineStrong : k_LineWeak);

            const float lineLeft = position.x + Px(k_Space3);
            const float lineRight = lineLeft + Px(12.0f);
            const float lineCenter = position.y + 0.5f * buttonSize.y;
            const ImU32 iconColor = hovered ? k_TextValue : k_TextLabel;

            for (int32_t line = -1; line <= 1; ++line)
            {
                const float lineY = lineCenter + static_cast<float>(line) * Px(4.0f);
                drawList->AddLine(ImVec2{ lineLeft, lineY }, ImVec2{ lineRight, lineY }, iconColor, 1.0f);
            }

            const float textY = position.y + 0.5f * (buttonSize.y - LineHeight(Face::Caps));
            PutText(drawList, Face::Caps, ImVec2{ lineRight + Px(k_Space2), textY }, iconColor, "MENU");
        }

        ImGui::End();
        return clicked;
    }

    void DrawTelemetryBanner(const OceanSimulationSettings& settings, bool* p_open)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 startPosition = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float titleHeight = LineHeight(Face::Title);

        PutText(drawList, Face::Title, startPosition, k_TextValue, "OCEAN SIMULATION");

        float rightEdge = startPosition.x + availWidth;

        if (p_open != nullptr)
        {
            const float closeSize = Px(16.0f);
            const ImVec2 closeMin{ rightEdge - closeSize, startPosition.y + 0.5f * (titleHeight - closeSize) };
            ImGui::SetCursorScreenPos(closeMin);

            if (ImGui::InvisibleButton("##close_panel_banner_btn", ImVec2{ closeSize, closeSize }))
            {
                *p_open = false;
            }

            DrawCloseCross(drawList, closeMin, closeSize, ImGui::IsItemHovered() ? k_TextValue : k_TextHint);
            rightEdge = closeMin.x - Px(k_Space3);
        }

        const float framesPerSecond = ImGui::GetIO().Framerate;
        const float frameMs = framesPerSecond > 0.0f ? 1000.0f / framesPerSecond : 0.0f;
        const std::string performanceText = std::format("{:.2f} ms  {:.0f} FPS", frameMs, framesPerSecond);
        const float performanceY = startPosition.y + 0.5f * (titleHeight - LineHeight(Face::Small));
        PutTextRight(drawList, Face::Small, ImVec2{ rightEdge, performanceY }, k_TextHint, performanceText);

        const std::string waveTag = settings.WaveSize == 0u ?
            std::string{ "no wave ops" } :
            std::format("wave {}", settings.WaveSize);
        const std::string specLine = std::format("IFFT radix-4  /  {}x{}  /  {} cascades  /  {}",
            settings.Size, settings.Size, k_CascadeCount, waveTag);

        ImGui::SetCursorScreenPos(ImVec2{ startPosition.x, startPosition.y + titleHeight + Px(k_Space1) });
        TextItem(Face::Small, k_TextHint, specLine);
        ImGui::Dummy(ImVec2{ 0.0f, Px(k_Space2) });
    }

    void DrawSeaReadout(const SeaState& state, const SeaStateReadout& readout)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;

        const float captionHeight = LineHeight(Face::Caps);
        const float heroHeight = LineHeight(Face::Hero);
        const float titleHeight = LineHeight(Face::Title);
        const float bodyHeight = LineHeight(Face::Body);

        float cursorY = origin.y;
        PutText(drawList, Face::Caps, ImVec2{ origin.x, cursorY }, k_TextHint, "SIGNIFICANT WAVE HEIGHT");
        cursorY += captionHeight + Px(k_Space1);

        const std::string heightText = std::format("{:.2f}", readout.SignificantHeight);
        PutText(drawList, Face::Hero, ImVec2{ origin.x, cursorY }, k_TextValue, heightText);

        const float heightWidth = TextWidth(Face::Hero, heightText);
        const float unitY = cursorY + heroHeight - titleHeight - Px(10.0f);
        PutText(drawList, Face::Title, ImVec2{ origin.x + heightWidth + Px(k_Space2), unitY }, k_TextHint, "m");
        cursorY += heroHeight;

        const std::string forceText = std::format("Bft {:.1f}  /  {}  /  {:.1f} m/s",
            state.WindForce, readout.BeaufortName, readout.WindSpeed);
        PutText(drawList, Face::Body, ImVec2{ origin.x, cursorY }, k_TextLabel, forceText);
        cursorY += bodyHeight;

        const float compassRadius = Px(k_CompassRadius);
        const ImVec2 compassCenter{ origin.x + availWidth - compassRadius - Px(k_Space1), origin.y + compassRadius + Px(k_Space1) };
        DrawCompass(drawList, compassCenter, compassRadius, state);

        const float blockBottom = std::max(cursorY, compassCenter.y + compassRadius + Px(k_Space1));
        const float columnWidth = 0.5f * (availWidth - Px(k_Space4));
        const float rowStep = Px(k_KeyValueHeight);
        float rowY = blockBottom + Px(k_Space3);

        const std::string windSea = std::format("{:.2f}", readout.WindSeaHeight);
        const std::string swell = std::format("{:.2f}", readout.SwellHeight);
        const std::string peakPeriod = std::format("{:.1f}", readout.PeakPeriod);
        const std::string peakLength = std::format("{:.0f}", readout.PeakWavelength);

        const KeyValueSpec windSeaRow{ "Wind sea", windSea, "m", k_DataWind };
        const KeyValueSpec swellRow{ "Swell", swell, "m", k_DataSwell };
        const KeyValueSpec periodRow{ "Peak period", peakPeriod, "s", 0u };
        const KeyValueSpec lengthRow{ "Peak wavelength", peakLength, "m", 0u };

        DrawKeyValueAt(drawList, ImVec2{ origin.x, rowY }, columnWidth, windSeaRow);
        DrawKeyValueAt(drawList, ImVec2{ origin.x + columnWidth + Px(k_Space4), rowY }, columnWidth, swellRow);
        rowY += rowStep;
        DrawKeyValueAt(drawList, ImVec2{ origin.x, rowY }, columnWidth, periodRow);
        DrawKeyValueAt(drawList, ImVec2{ origin.x + columnWidth + Px(k_Space4), rowY }, columnWidth, lengthRow);
        rowY += rowStep;

        ImGui::SetCursorScreenPos(ImVec2{ origin.x, rowY });
        ImGui::Dummy(ImVec2{ availWidth, Px(k_Space2) });
    }

    bool DrawSimulationTransportBar(bool* paused, float* time_scale, bool* snapshot_requested, bool snapshot_saved)
    {
        bool changed = false;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 barMin = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float barHeight = Px(28.0f);
        const float centerY = barMin.y + 0.5f * barHeight;
        const float captionHeight = LineHeight(Face::Caps);

        const bool isPaused = paused != nullptr && *paused;
        const float toggleWidth = Px(110.0f);

        if (ImGui::InvisibleButton("##sim_transport_toggle", ImVec2{ toggleWidth, barHeight }))
        {
            if (paused != nullptr)
            {
                *paused = !(*paused);
                changed = true;
            }
        }

        // a ring with the action's glyph: pause bars while running, a play triangle while paused
        const bool toggleHovered = ImGui::IsItemHovered();
        const ImU32 stateColor = isPaused ? k_Warning : k_Accent;
        const ImU32 ringColor = toggleHovered ? stateColor : k_TextLabel;
        const float ringRadius = Px(11.0f);
        const ImVec2 ringCenter{ barMin.x + ringRadius + Px(1.0f), centerY };
        drawList->AddCircle(ringCenter, ringRadius, ringColor, 32, Px(1.25f));

        if (isPaused)
        {
            const std::array<ImVec2, 3> play
            {
                ImVec2{ ringCenter.x - Px(3.0f), ringCenter.y - Px(5.0f) },
                ImVec2{ ringCenter.x + Px(5.0f), ringCenter.y },
                ImVec2{ ringCenter.x - Px(3.0f), ringCenter.y + Px(5.0f) },
            };
            drawList->AddConvexPolyFilled(play.data(), static_cast<int>(play.size()), stateColor);
        }
        else
        {
            const float barHalfHeight = Px(5.0f);
            const float barWidth = Px(2.5f);
            const float barGap = Px(2.0f);
            drawList->AddRectFilled(
                ImVec2{ ringCenter.x - barGap - barWidth, ringCenter.y - barHalfHeight },
                ImVec2{ ringCenter.x - barGap, ringCenter.y + barHalfHeight },
                stateColor);
            drawList->AddRectFilled(
                ImVec2{ ringCenter.x + barGap, ringCenter.y - barHalfHeight },
                ImVec2{ ringCenter.x + barGap + barWidth, ringCenter.y + barHalfHeight },
                stateColor);
        }

        const char* statusText = isPaused ? "PAUSED" : "RUNNING";
        const ImU32 statusColor = isPaused ? k_Warning : (toggleHovered ? k_TextValue : k_TextLabel);
        const float statusX = ringCenter.x + ringRadius + Px(k_Space2);
        PutText(drawList, Face::Caps, ImVec2{ statusX, centerY - 0.5f * captionHeight }, statusColor, statusText);

        // the snapshot control: a ring with an aperture, the same shape language as the run and pause ring
        if (snapshot_requested != nullptr)
        {
            const float statusWidth = std::max(TextWidth(Face::Caps, "RUNNING"), TextWidth(Face::Caps, "PAUSED"));
            const float snapLeft = statusX + statusWidth + Px(k_Space4);
            const float snapLabelWidth = TextWidth(Face::Caps, "SNAPSHOT");
            const float snapWidth = 2.0f * ringRadius + Px(k_Space2) + snapLabelWidth;
            ImGui::SetCursorScreenPos(ImVec2{ snapLeft, barMin.y });

            if (ImGui::InvisibleButton("##snapshot", ImVec2{ snapWidth, barHeight }))
            {
                *snapshot_requested = true;
            }

            const bool snapHovered = ImGui::IsItemHovered();
            const ImU32 snapColor = (snapHovered || snapshot_saved) ? k_Accent : k_TextLabel;
            const ImVec2 snapCenter{ snapLeft + ringRadius, centerY };
            drawList->AddCircle(snapCenter, ringRadius, snapColor, 32, Px(1.25f));
            drawList->AddCircle(snapCenter, Px(4.0f), snapColor, 16, Px(1.25f));

            const float snapLabelX = snapCenter.x + ringRadius + Px(k_Space2);
            const ImU32 snapLabelColor = snapshot_saved ? k_Accent : (snapHovered ? k_TextValue : k_TextLabel);
            const char* snapLabel = snapshot_saved ? "SAVED" : "SNAPSHOT";
            PutText(drawList, Face::Caps, ImVec2{ snapLabelX, centerY - 0.5f * captionHeight }, snapLabelColor, snapLabel);
        }

        if (time_scale != nullptr)
        {
            float rightEdge = barMin.x + availWidth;
            const bool deviates = std::abs(*time_scale - 1.0f) > 0.01f;

            if (deviates)
            {
                const float resetWidth = TextWidth(Face::Caps, "RESET");
                ImGui::SetCursorScreenPos(ImVec2{ rightEdge - resetWidth - Px(k_Space2), barMin.y });

                if (ImGui::InvisibleButton("##time_scale_reset_1x", ImVec2{ resetWidth + Px(k_Space2), barHeight }))
                {
                    *time_scale = 1.0f;
                    changed = true;
                }

                const ImU32 resetColor = ImGui::IsItemHovered() ? k_Accent : k_TextHint;
                PutTextRight(drawList, Face::Caps, ImVec2{ rightEdge, centerY - 0.5f * captionHeight }, resetColor, "RESET");
                rightEdge -= resetWidth + Px(k_Space4);
            }

            const std::string rateText = std::format("{:.2f}x", *time_scale);
            PutTextRight(drawList, Face::Body, ImVec2{ rightEdge, centerY - 0.5f * LineHeight(Face::Body) }, k_TextValue, rateText);

            const float rateWidth = TextWidth(Face::Body, rateText);
            const float speedX = rightEdge - rateWidth - Px(k_Space2);
            PutTextRight(drawList, Face::Caps, ImVec2{ speedX, centerY - 0.5f * captionHeight }, k_TextHint, "SPEED");
        }

        ImGui::SetCursorScreenPos(ImVec2{ barMin.x, barMin.y + barHeight });
        ImGui::Dummy(ImVec2{ availWidth, Px(k_Space1) });
        return changed;
    }

    bool DrawTelemetryTabs(TelemetryTab& active_tab)
    {
        constexpr std::array<std::string_view, 7> k_TabNames
        {
            "SEA STATE", "SURFACE", "FOAM", "PHYSICS", "TIMINGS", "SCENE", "DEBUG",
        };

        std::array<bool, 7> on{};
        const int32_t activeIndex = static_cast<int32_t>(active_tab);
        FillSelection(on, activeIndex);

        const OptionRowSpec spec
        {
            .Id = "##tabs",
            .Options = k_TabNames,
            .On = on,
            .Columns = static_cast<int32_t>(k_TabNames.size()),
            .SelectedColor = k_Accent,
            .Kind = CellKind::Tab,
        };

        const int32_t clicked = DrawOptionRow(spec);

        if (clicked < 0 || clicked == activeIndex)
        {
            return false;
        }

        active_tab = static_cast<TelemetryTab>(clicked);
        return true;
    }

    void DrawSectionLabel(std::string_view label)
    {
        DrawSectionLabelWithDot(label, 0u);
    }

    void DrawKeyValue(std::string_view label, std::string_view value, std::string_view unit)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;

        const KeyValueSpec spec{ label, value, unit, 0u };
        DrawKeyValueAt(drawList, position, availWidth, spec);
        ImGui::Dummy(ImVec2{ availWidth, Px(k_KeyValueHeight) - ImGui::GetStyle().ItemSpacing.y });
    }

    void BeginFieldRow(std::string_view label)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float labelColumn = Px(k_LabelColumnWidth);
        const float labelY = position.y + 0.5f * (ImGui::GetFrameHeight() - LineHeight(Face::Body));

        PutText(drawList, Face::Body, ImVec2{ position.x, labelY }, k_TextLabel, label);
        ImGui::SetCursorScreenPos(ImVec2{ position.x + labelColumn, position.y });
        ImGui::SetNextItemWidth(availWidth - labelColumn);
    }

    bool DrawSelector(const char* id, std::span<const std::string_view> options, int32_t& selected, int32_t columns)
    {
        std::array<bool, k_MaxOptions> on{};
        FillSelection(on, selected);

        const OptionRowSpec spec
        {
            .Id = id,
            .Options = options,
            .On = on,
            .Columns = columns,
            .SelectedColor = k_Accent,
        };

        const int32_t clicked = DrawOptionRow(spec);

        if (clicked < 0)
        {
            return false;
        }

        selected = clicked;
        return true;
    }

    bool DrawSelectorRow(const char* id, std::string_view label, std::span<const std::string_view> options,
                         int32_t& selected, int32_t columns)
    {
        std::array<bool, k_MaxOptions> on{};
        FillSelection(on, selected);

        const OptionRowSpec spec
        {
            .Id = id,
            .Label = label,
            .Options = options,
            .On = on,
            .Columns = columns,
            .SelectedColor = k_Accent,
        };

        const int32_t clicked = DrawOptionRow(spec);

        if (clicked < 0)
        {
            return false;
        }

        selected = clicked;
        return true;
    }

    bool DrawToggle(const char* id, std::string_view label, std::string_view state_off, std::string_view state_on,
                    bool* value)
    {
        const std::array<std::string_view, 2> options{ state_off, state_on };
        const std::array<bool, 2> on{ !(*value), *value };

        const OptionRowSpec spec
        {
            .Id = id,
            .Label = label,
            .Options = options,
            .On = on,
            .Columns = 2,
            .SelectedColor = *value ? k_Accent : k_TextValue,
        };

        const int32_t clicked = DrawOptionRow(spec);

        if (clicked < 0)
        {
            return false;
        }

        const bool newValue = clicked == 1;
        const bool changed = newValue != *value;
        *value = newValue;
        return changed;
    }

    bool DrawPresetSelector(SeaState& state)
    {
        const std::array<SeaStatePreset, 6>& presets = SeaStatePresets();
        std::array<std::string_view, 6> names{};
        int32_t selected = -1;

        for (int32_t index = 0; index < static_cast<int32_t>(presets.size()); ++index)
        {
            const SeaStatePreset& preset = presets[static_cast<size_t>(index)];
            names[static_cast<size_t>(index)] = preset.Name;

            if (MatchesPreset(state, preset.State))
            {
                selected = index;
            }
        }

        if (!DrawSelector("##sea_presets", names, selected, 6))
        {
            return false;
        }

        state = presets[static_cast<size_t>(selected)].State;
        return true;
    }

    bool DrawSkyPresetSelector(OceanSkyPreset& active_preset)
    {
        constexpr std::array<OceanSkyPreset, 6> k_Presets
        {
            OceanSkyPreset::AnalyticPreetham,
            OceanSkyPreset::BrightDay,
            OceanSkyPreset::Twilight,
            OceanSkyPreset::OvercastEvening,
            OceanSkyPreset::StarryNight,
            OceanSkyPreset::AuroralNight,
        };

        constexpr std::array<std::string_view, 6> k_Names
        {
            "Preetham", "Bright Day", "Twilight", "Overcast", "Starry Night", "Auroral",
        };

        int32_t selected = -1;

        for (int32_t index = 0; index < static_cast<int32_t>(k_Presets.size()); ++index)
        {
            if (k_Presets[static_cast<size_t>(index)] == active_preset)
            {
                selected = index;
            }
        }

        if (!DrawSelector("##sky_presets", k_Names, selected, 3))
        {
            return false;
        }

        active_preset = k_Presets[static_cast<size_t>(selected)];
        return true;
    }

    bool DrawTonemapperSelector(color::TonemapperKind& active_tonemapper)
    {
        constexpr std::array<color::TonemapperKind, 5> k_Kinds
        {
            color::TonemapperKind::AgX,
            color::TonemapperKind::AgXPunchy,
            color::TonemapperKind::KhronosNeutral,
            color::TonemapperKind::Passthrough,
            color::TonemapperKind::FalseColorEv,
        };

        constexpr std::array<std::string_view, 5> k_Names{ "AgX", "Punchy", "Khronos", "None", "False Color" };
        int32_t selected = -1;

        for (int32_t index = 0; index < static_cast<int32_t>(k_Kinds.size()); ++index)
        {
            if (k_Kinds[static_cast<size_t>(index)] == active_tonemapper)
            {
                selected = index;
            }
        }

        if (!DrawSelector("##tonemappers", k_Names, selected, static_cast<int32_t>(k_Names.size())))
        {
            return false;
        }

        active_tonemapper = k_Kinds[static_cast<size_t>(selected)];
        return true;
    }

    bool DrawSlider(const char* id, std::string_view label, std::string_view hint, float* value, float min_value,
                    float max_value, std::string_view value_text, std::string_view unit_text, bool logarithmic)
    {
        const SliderRowSpec spec
        {
            .Id = id,
            .Label = label,
            .Hint = hint,
            .ValueText = value_text,
            .UnitText = unit_text,
            .Min = min_value,
            .Max = max_value,
            .Logarithmic = logarithmic,
        };

        return DrawSliderRow(spec, value);
    }

    bool DrawAngleSlider(const char* id, std::string_view label, std::string_view hint, float* degrees,
                         float min_degrees, float max_degrees)
    {
        const std::string valueText = std::format("{:+.0f}", *degrees);

        const SliderRowSpec spec
        {
            .Id = id,
            .Label = label,
            .Hint = hint,
            .ValueText = valueText,
            .UnitText = "deg",
            .Min = min_degrees,
            .Max = max_degrees,
            .Centered = true,
        };

        const bool changed = DrawSliderRow(spec, degrees);

        if (changed && std::abs(*degrees) < k_AngleSnapDegrees && ImGui::IsItemActive())
        {
            *degrees = 0.0f;
        }

        return changed;
    }

    void DrawCascadeBandStrip(const std::array<float, k_CascadeCount>& length_scales,
                              std::array<bool, k_CascadeCount>& cascade_enabled)
    {
        std::array<std::string, k_CascadeCount> labelStorage;
        std::array<std::string_view, k_CascadeCount> labels{};

        for (size_t cascade = 0; cascade < k_CascadeCount; ++cascade)
        {
            const float scale = length_scales[cascade];
            labelStorage[cascade] = scale < 1.0f ?
                std::format("{:.2f} m", scale) :
                (scale < 10.0f ? std::format("{:.1f} m", scale) : std::format("{:.0f} m", scale));
            labels[cascade] = labelStorage[cascade];
        }

        const OptionRowSpec spec
        {
            .Id = "##cascade_bands",
            .Label = "Active cascades",
            .Options = labels,
            .On = cascade_enabled,
            .Columns = static_cast<int32_t>(k_CascadeCount),
            .SelectedColor = k_Accent,
        };

        const int32_t clicked = DrawOptionRow(spec);

        if (clicked >= 0)
        {
            cascade_enabled[static_cast<size_t>(clicked)] = !cascade_enabled[static_cast<size_t>(clicked)];
        }
    }

    bool DrawSpectrumPhysicsInspector(SpectrumParameters& spectrum)
    {
        bool changed = false;

        constexpr std::array<std::string_view, 3> k_EnergyModels{ "JONSWAP", "ELFOUHAILY", "BRINGER" };
        int32_t energyModel = static_cast<int32_t>(spectrum.EnergySpectrumModel);

        if (DrawSelectorRow("##energy_model", "Energy spectrum", k_EnergyModels, energyModel, 3))
        {
            spectrum.EnergySpectrumModel = static_cast<uint32_t>(energyModel);
            changed = true;
        }

        constexpr std::array<std::string_view, 6> k_SpreadingModels
        {
            "MITSUYASU", "HASSELMANN", "DONELAN", "ELFOUHAILY", "HYBRID", "EWANS",
        };

        int32_t spreadingModel = static_cast<int32_t>(spectrum.DirectionalSpreadingMode);

        if (DrawSelectorRow("##spreading_model", "Spreading model", k_SpreadingModels, spreadingModel, 3))
        {
            spectrum.DirectionalSpreadingMode = static_cast<uint32_t>(spreadingModel);
            changed = true;
        }

        BeginFieldRow("Spectrum seed");
        int32_t seedValue = static_cast<int32_t>(spectrum.NoiseSeed);

        if (ImGui::InputInt("##spectrum_seed", &seedValue, 1, 10))
        {
            spectrum.NoiseSeed = static_cast<uint32_t>(std::max(seedValue, 0));
            changed = true;
        }

        DrawSectionLabel("WIND SEA");
        DrawKeyValue("Wind speed", std::format("{:.1f}", spectrum.WindSpeed), "m/s");
        DrawKeyValue("Fetch", std::format("{:.0f}", spectrum.Fetch), "km");
        DrawKeyValue("Peaking factor", std::format("{:.1f}", spectrum.PeakingFactor), "gamma");
        DrawKeyValue("Fade length", std::format("{:.3f}", spectrum.FadeLength), "m");

        DrawSectionLabel("SPREADING");
        DrawKeyValue("Alignment", std::format("{:.2f}", spectrum.Alignment), "");
        DrawKeyValue("Narrowness", std::format("{:.2f}", spectrum.SwellAmount), "");

        DrawSectionLabel("SWELL JONSWAP");
        DrawKeyValue("Wind speed", std::format("{:.1f}", spectrum.SwellWindSpeed), "m/s");
        DrawKeyValue("Fetch", std::format("{:.0f}", spectrum.SwellFetch), "km");
        DrawKeyValue("Peaking factor", std::format("{:.1f}", spectrum.SwellPeakingFactor), "gamma");
        DrawKeyValue("Energy scale", std::format("{:.3f}", spectrum.SwellScale), "x");

        return changed;
    }

    void DrawGpuTimingsSection(const rhi::GpuTimers& gpu_timers,
                               const std::unordered_map<uint64_t, double>& timing_averages,
                               const rhi::GpuTimers* async_timers,
                               const std::unordered_map<uint64_t, double>* async_timing_averages)
    {
        const bool showAsync = async_timers != nullptr && async_timers->Active() && async_timing_averages != nullptr;

        if (!showAsync)
        {
            DrawSectionLabel("GPU TIMINGS");
            DrawTimingTable("GpuTimingsTelemetry", gpu_timers, timing_averages, k_DataGraphicsQueue);
            return;
        }

        DrawSectionLabelWithDot("GRAPHICS QUEUE", k_DataGraphicsQueue);
        DrawTimingTable("GpuTimingsGraphics", gpu_timers, timing_averages, k_DataGraphicsQueue);
        DrawSectionLabelWithDot("ASYNC COMPUTE QUEUE", k_DataAsyncQueue);
        DrawTimingTable("GpuTimingsAsyncCompute", *async_timers, *async_timing_averages, k_DataAsyncQueue);
    }

    void DrawNote(std::string_view text)
    {
        ImGui::Dummy(ImVec2{ 0.0f, Px(k_Space1) });
        TextItem(Face::Note, k_TextHint, text);
    }

    namespace
    {
        float Scale() noexcept
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            return style.FontScaleMain * style.FontScaleDpi;
        }

        float Px(float pixels) noexcept
        {
            return pixels * Scale();
        }

        ImU32 WithAlpha(ImU32 color, float alpha_scale) noexcept
        {
            const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) * alpha_scale;
            const uint32_t alphaByte = static_cast<uint32_t>(std::clamp(alpha, 0.0f, 255.0f));
            return (color & ~IM_COL32_A_MASK) | (alphaByte << IM_COL32_A_SHIFT);
        }

        FaceSpec ResolveFace(Face face) noexcept
        {
            const ImGuiFontSet& fonts = *activeFrame.Fonts;
            const bool michroma = activeFrame.Mode == HeaderFontMode::Michroma;
            const float scale = Scale();

            switch (face)
            {
            case Face::Caps:
                return FaceSpec{ michroma ? fonts.SectionMichroma : fonts.SectionBlinker,
                                 (michroma ? k_SizeCaptionMichroma : k_SizeCaption) * scale };
            case Face::Title:
                return FaceSpec{ michroma ? fonts.HeaderMichroma : fonts.HeaderBlinker,
                                 (michroma ? k_SizeTitleMichroma : k_SizeTitle) * scale };
            case Face::Hero:
                return FaceSpec{ michroma ? fonts.HeaderMichroma : fonts.HeaderBlinker,
                                 (michroma ? k_SizeHeroMichroma : k_SizeHero) * scale };
            case Face::Body:
                return FaceSpec{ fonts.Body, k_SizeBody * scale };
            case Face::Small:
                return FaceSpec{ fonts.Body, k_SizeCaption * scale };
            case Face::Note:
                return FaceSpec{ fonts.BodyItalic, k_SizeCaption * scale };
            case Face::Invalid:
                break;
            }

            return FaceSpec{ fonts.Body, k_SizeBody * scale };
        }

        float TextWidth(Face face, std::string_view text)
        {
            const FaceSpec spec = ResolveFace(face);
            ImGui::PushFont(spec.Font, spec.Size);
            const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
            ImGui::PopFont();
            return size.x;
        }

        float LineHeight(Face face)
        {
            const FaceSpec spec = ResolveFace(face);
            ImGui::PushFont(spec.Font, spec.Size);
            const float height = ImGui::GetTextLineHeight();
            ImGui::PopFont();
            return height;
        }

        void PutText(ImDrawList* draw_list, Face face, const ImVec2& position, ImU32 color, std::string_view text)
        {
            const FaceSpec spec = ResolveFace(face);
            ImGui::PushFont(spec.Font, spec.Size);
            draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, color, text.data(),
                               text.data() + text.size());
            ImGui::PopFont();
        }

        void PutTextRight(ImDrawList* draw_list, Face face, const ImVec2& right_position, ImU32 color,
                          std::string_view text)
        {
            const float width = TextWidth(face, text);
            PutText(draw_list, face, ImVec2{ right_position.x - width, right_position.y }, color, text);
        }

        void TextItem(Face face, ImU32 color, std::string_view text)
        {
            const FaceSpec spec = ResolveFace(face);
            ImGui::PushFont(spec.Font, spec.Size);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        void TextItemRight(Face face, ImU32 color, std::string_view text)
        {
            const float width = TextWidth(face, text);
            const float freeWidth = ImGui::GetContentRegionAvail().x - width;

            if (freeWidth > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + freeWidth);
            }

            TextItem(face, color, text);
        }

        void ApplyTelemetryStyle() noexcept
        {
            ImGuiStyle& style = ImGui::GetStyle();

            style.WindowPadding = ImVec2{ Px(k_Space4), Px(k_Space4) };
            style.FramePadding = ImVec2{ Px(k_Space2), Px(5.0f) };
            style.ItemSpacing = ImVec2{ Px(k_Space2), Px(k_Space1) };
            style.ItemInnerSpacing = ImVec2{ Px(k_Space2), Px(k_Space1) };
            style.CellPadding = ImVec2{ Px(k_Space2), Px(3.0f) };
            style.ScrollbarSize = Px(4.0f);
            style.GrabMinSize = Px(k_SliderGrabSize);

            style.WindowRounding = 0.0f;
            style.ChildRounding = 0.0f;
            style.FrameRounding = Px(2.0f);
            style.PopupRounding = Px(2.0f);
            style.ScrollbarRounding = Px(2.0f);
            style.GrabRounding = Px(2.0f);
            style.WindowBorderSize = 0.0f;
            style.FrameBorderSize = 0.0f;
            style.PopupBorderSize = 1.0f;

            ImVec4* colors = style.Colors;
            colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(k_TextValue);
            colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(k_TextHint);
            colors[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4(WithAlpha(k_Accent, 0.3f));
            colors[ImGuiCol_WindowBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            colors[ImGuiCol_ChildBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(k_SurfacePopup);
            colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(k_LineWeak);
            colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(k_FieldRest);
            colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(k_FieldHover);
            colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(k_FieldActive);
            colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(k_FieldRest);
            colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(k_FieldHover);
            colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(k_FieldActive);
            colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(k_FieldRest);
            colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(k_FieldHover);
            colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(k_FieldActive);
            colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(k_Accent);
            colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(k_TextValue);
            colors[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(k_Accent);
            colors[ImGuiCol_ScrollbarBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(k_LineStrong);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(k_TextLabel);
            colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(k_Accent);
            colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(k_LineWeak);
            colors[ImGuiCol_TableHeaderBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            colors[ImGuiCol_TableBorderStrong] = ImGui::ColorConvertU32ToFloat4(k_LineStrong);
            colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(k_LineWeak);
            colors[ImGuiCol_TableRowBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            colors[ImGuiCol_TableRowBgAlt] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            colors[ImGuiCol_ResizeGrip] = ImGui::ColorConvertU32ToFloat4(k_LineWeak);
            colors[ImGuiCol_ResizeGripHovered] = ImGui::ColorConvertU32ToFloat4(k_LineStrong);
            colors[ImGuiCol_ResizeGripActive] = ImGui::ColorConvertU32ToFloat4(k_Accent);
            colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(k_Accent);
        }

        void DrawPanelSurface(const ImVec2& min_pos, const ImVec2& max_pos)
        {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            drawList->AddRectFilledMultiColor(min_pos, max_pos, k_SurfaceTop, k_SurfaceTop, k_SurfaceBottom, k_SurfaceBottom);
            drawList->AddRect(min_pos, max_pos, k_LineWeak, 0.0f, 0, 1.0f);
        }

        void DrawCloseCross(ImDrawList* draw_list, const ImVec2& min_pos, float size, ImU32 color)
        {
            const float inset = 0.25f * size;
            const ImVec2 topLeft{ min_pos.x + inset, min_pos.y + inset };
            const ImVec2 bottomRight{ min_pos.x + size - inset, min_pos.y + size - inset };
            const ImVec2 topRight{ bottomRight.x, topLeft.y };
            const ImVec2 bottomLeft{ topLeft.x, bottomRight.y };

            draw_list->AddLine(topLeft, bottomRight, color, 1.0f);
            draw_list->AddLine(topRight, bottomLeft, color, 1.0f);
        }

        void DrawCompass(ImDrawList* draw_list, const ImVec2& center, float radius, const SeaState& state)
        {
            draw_list->AddCircle(center, radius, k_LineStrong, 64, 1.0f);
            draw_list->AddCircle(center, radius * 0.66f, k_LineWeak, 48, 1.0f);
            draw_list->AddCircle(center, radius * 0.33f, k_LineWeak, 32, 1.0f);

            const float tickLength = Px(5.0f);

            for (int32_t index = 0; index < 4; ++index)
            {
                const float angle = static_cast<float>(index) * 0.5f * k_Pi;
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                draw_list->AddLine(
                    ImVec2{ center.x + cosine * (radius - tickLength), center.y + sine * (radius - tickLength) },
                    ImVec2{ center.x + cosine * radius, center.y + sine * radius },
                    k_LineStrong,
                    1.0f);
            }

            const float arrowReach = radius - Px(6.0f);

            // zero degrees points up in the compass
            const float swellFraction = std::clamp(state.SwellHeight / 6.0f, 0.0f, 1.0f);

            if (swellFraction > 0.02f)
            {
                const float swellAngle = (state.SwellDirection - 90.0f) * k_DegreesToRadians;
                const float swellLength = (0.30f + 0.70f * swellFraction) * arrowReach;
                DrawCompassArrow(draw_list, center, swellAngle, swellLength, k_DataSwell);
            }

            const float windFraction = std::clamp(state.WindForce / 12.0f, 0.15f, 1.0f);
            const float windAngle = (state.WindDirection - 90.0f) * k_DegreesToRadians;
            const float windLength = (0.30f + 0.70f * windFraction) * arrowReach;
            DrawCompassArrow(draw_list, center, windAngle, windLength, k_DataWind);

            draw_list->AddCircleFilled(center, Px(2.0f), k_TextValue, 12);
        }

        void DrawCompassArrow(ImDrawList* draw_list, const ImVec2& center, float angle_radians, float length,
                              ImU32 color)
        {
            const float directionX = std::cos(angle_radians);
            const float directionY = std::sin(angle_radians);
            const ImVec2 tip{ center.x + directionX * length, center.y + directionY * length };
            draw_list->AddLine(center, tip, color, 1.5f);

            const float headLength = Px(7.0f);
            const float headSpread = 0.42f;
            const ImVec2 leftWing
            {
                tip.x - headLength * std::cos(angle_radians - headSpread),
                tip.y - headLength * std::sin(angle_radians - headSpread),
            };
            const ImVec2 rightWing
            {
                tip.x - headLength * std::cos(angle_radians + headSpread),
                tip.y - headLength * std::sin(angle_radians + headSpread),
            };

            draw_list->AddTriangleFilled(tip, leftWing, rightWing, color);
        }

        void DrawKeyValueAt(ImDrawList* draw_list, const ImVec2& position, float width, const KeyValueSpec& spec)
        {
            const float rowHeight = Px(k_KeyValueHeight);
            const float centerY = position.y + 0.5f * rowHeight;
            float labelX = position.x;

            if (spec.DotColor != 0u)
            {
                const float radius = Px(3.0f);
                draw_list->AddCircleFilled(ImVec2{ position.x + radius, centerY }, radius, spec.DotColor);
                labelX += 2.0f * radius + Px(k_Space2);
            }

            const float bodyHeight = LineHeight(Face::Body);
            PutText(draw_list, Face::Body, ImVec2{ labelX, centerY - 0.5f * bodyHeight }, k_TextLabel, spec.Label);

            float rightEdge = position.x + width;

            if (!spec.Unit.empty())
            {
                const float smallHeight = LineHeight(Face::Small);
                const ImVec2 unitPosition{ rightEdge, centerY - 0.5f * smallHeight };
                PutTextRight(draw_list, Face::Small, unitPosition, k_TextHint, spec.Unit);
                rightEdge -= TextWidth(Face::Small, spec.Unit) + Px(k_Space1);
            }

            PutTextRight(draw_list, Face::Body, ImVec2{ rightEdge, centerY - 0.5f * bodyHeight }, k_TextValue, spec.Value);
        }

        void DrawSectionLabelWithDot(std::string_view label, ImU32 dot_color)
        {
            ImGui::Dummy(ImVec2{ 0.0f, Px(k_Space2) });

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float labelHeight = LineHeight(Face::Caps);
            float textX = position.x;

            if (dot_color != 0u)
            {
                const float radius = Px(3.0f);
                drawList->AddCircleFilled(ImVec2{ position.x + radius, position.y + 0.5f * labelHeight }, radius, dot_color);
                textX += 2.0f * radius + Px(k_Space2);
            }

            PutText(drawList, Face::Caps, ImVec2{ textX, position.y }, k_TextLabel, label);

            const float lineStart = textX + TextWidth(Face::Caps, label) + Px(k_Space2);
            const float lineY = position.y + 0.5f * labelHeight;
            const float lineEnd = position.x + availWidth;

            if (lineStart < lineEnd)
            {
                drawList->AddLine(ImVec2{ lineStart, lineY }, ImVec2{ lineEnd, lineY }, k_LineWeak, 1.0f);
            }

            ImGui::Dummy(ImVec2{ availWidth, labelHeight + Px(k_Space1) });
        }

        int32_t DrawOptionRow(const OptionRowSpec& spec)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const int32_t count = static_cast<int32_t>(spec.Options.size());
            const int32_t columns = std::min(spec.Columns, count);
            const int32_t rows = (count + columns - 1) / columns;
            const bool isTab = spec.Kind == CellKind::Tab;
            const float cellHeight = Px(isTab ? k_TabHeight : k_OptionHeight);

            std::array<float, k_MaxOptions> textWidths{};
            float textTotal = 0.0f;

            for (int32_t index = 0; index < count; ++index)
            {
                const float width = TextWidth(Face::Caps, spec.Options[static_cast<size_t>(index)]);
                textWidths[static_cast<size_t>(index)] = width;
                textTotal += width;
            }

            float left = start.x;
            float width = availWidth;
            float cellsTop = start.y;

            if (!spec.Label.empty())
            {
                const float labelWidth = TextWidth(Face::Body, spec.Label);
                const float besideOffset = std::max(Px(k_LabelColumnWidth), labelWidth + Px(k_Space3));
                const float besideWidth = availWidth - besideOffset;
                const float neededWidth = textTotal + static_cast<float>(count) * Px(k_OptionPadding);
                const bool stacked = rows == 1 && neededWidth > besideWidth;
                const float bodyHeight = LineHeight(Face::Body);

                if (stacked)
                {
                    PutText(drawList, Face::Body, start, k_TextLabel, spec.Label);
                    cellsTop = start.y + bodyHeight + Px(k_Space1);
                }
                else
                {
                    const float labelY = start.y + 0.5f * (cellHeight - bodyHeight);
                    PutText(drawList, Face::Body, ImVec2{ start.x, labelY }, k_TextLabel, spec.Label);
                    left = start.x + besideOffset;
                    width = besideWidth;
                }
            }

            const float rowsBottom = cellsTop + static_cast<float>(rows) * cellHeight;

            if (isTab)
            {
                drawList->AddLine(ImVec2{ left, rowsBottom - 0.5f }, ImVec2{ left + width, rowsBottom - 0.5f }, k_LineWeak, 1.0f);
            }

            const float captionHeight = LineHeight(Face::Caps);
            int32_t clicked = -1;

            ImGui::PushID(spec.Id);

            for (int32_t row = 0; row < rows; ++row)
            {
                const int32_t first = row * columns;
                const int32_t last = std::min(first + columns, count);
                const int32_t cellCount = last - first;
                float rowTextTotal = 0.0f;

                for (int32_t index = first; index < last; ++index)
                {
                    rowTextTotal += textWidths[static_cast<size_t>(index)];
                }

                const float extraPerCell = std::max((width - rowTextTotal) / static_cast<float>(cellCount), 0.0f);
                const float equalWidth = width / static_cast<float>(columns);
                float cellLeft = left;

                for (int32_t index = first; index < last; ++index)
                {
                    const float textWidth = textWidths[static_cast<size_t>(index)];
                    const float cellWidth = rows == 1 ? textWidth + extraPerCell : equalWidth;
                    const float cellTop = cellsTop + static_cast<float>(row) * cellHeight;

                    ImGui::SetCursorScreenPos(ImVec2{ cellLeft, cellTop });
                    ImGui::PushID(index);

                    if (ImGui::InvisibleButton("##option", ImVec2{ cellWidth, cellHeight }))
                    {
                        clicked = index;
                    }

                    ImGui::PopID();

                    const bool hovered = ImGui::IsItemHovered();
                    const bool selected = spec.On[static_cast<size_t>(index)];
                    ImU32 textColor = hovered ? k_TextValue : k_TextHint;

                    if (selected)
                    {
                        textColor = spec.SelectedColor;
                    }
                    else if (!isTab)
                    {
                        textColor = hovered ? k_TextValue : k_TextLabel;
                    }

                    const float textX = cellLeft + 0.5f * (cellWidth - textWidth);
                    const float textY = cellTop + 0.5f * (cellHeight - captionHeight) - (isTab ? Px(1.0f) : 0.0f);
                    PutText(drawList, Face::Caps, ImVec2{ textX, textY }, textColor, spec.Options[static_cast<size_t>(index)]);

                    if (selected)
                    {
                        const float underlineY = cellTop + cellHeight - Px(1.0f);
                        const float underlineLeft = isTab ? cellLeft + Px(2.0f) : textX;
                        const float underlineRight = isTab ? cellLeft + cellWidth - Px(2.0f) : textX + textWidth;
                        const float underlineThickness = isTab ? 2.0f : 1.0f;
                        drawList->AddLine(ImVec2{ underlineLeft, underlineY }, ImVec2{ underlineRight, underlineY },
                                          spec.SelectedColor, underlineThickness);
                    }

                    cellLeft += cellWidth;
                }
            }

            ImGui::PopID();

            const bool needsTrailingGap = isTab || spec.Label.empty();
            ImGui::SetCursorScreenPos(ImVec2{ start.x, rowsBottom });
            ImGui::Dummy(ImVec2{ 1.0f, needsTrailingGap ? Px(k_Space2) : 0.0f });
            return clicked;
        }

        void FillSelection(std::span<bool> on, int32_t selected) noexcept
        {
            for (size_t index = 0; index < on.size(); ++index)
            {
                on[index] = static_cast<int32_t>(index) == selected;
            }
        }

        bool DrawSliderRow(const SliderRowSpec& spec, float* value)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();

            if (window->SkipItems)
            {
                return false;
            }

            ImGuiContext& context = *GImGui;
            const ImGuiID id = window->GetID(spec.Id);
            const ImVec2 origin = window->DC.CursorPos;
            const float width = ImGui::GetContentRegionAvail().x;
            const float rowHeight = Px(k_SliderRowHeight);
            const ImRect rowBounds{ origin, ImVec2{ origin.x + width, origin.y + rowHeight } };

            ImGui::ItemSize(rowBounds);

            if (!ImGui::ItemAdd(rowBounds, id, &rowBounds, ImGuiItemFlags_Inputable))
            {
                return false;
            }

            const bool hovered = ImGui::ItemHoverable(rowBounds, id, context.LastItemData.ItemFlags);
            bool typing = ImGui::TempInputIsActive(id);

            if (!typing)
            {
                const bool clicked = hovered && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);
                const bool navActivated = context.NavActivateId == id;
                const bool makeActive = clicked || navActivated;

                if (makeActive && clicked)
                {
                    ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
                }

                const bool navWantsInput = navActivated && (context.NavActivateFlags & ImGuiActivateFlags_PreferInput) != 0;

                if (makeActive && ((clicked && context.IO.KeyCtrl) || navWantsInput))
                {
                    typing = true;
                }

                if (makeActive && !typing)
                {
                    ImGui::SetActiveID(id, window);
                    ImGui::SetFocusID(id, window);
                    ImGui::FocusWindow(window);
                    context.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
                }
            }

            const bool active = context.ActiveId == id;
            ImDrawList* drawList = window->DrawList;
            const ImU32 labelColor = (hovered || active || typing) ? k_TextValue : k_TextLabel;
            PutText(drawList, Face::Body, origin, labelColor, spec.Label);

            float minValue = spec.Min;
            float maxValue = spec.Max;

            if (typing)
            {
                const float entryWidth = Px(110.0f);
                const ImVec2 entryMax{ origin.x + width, origin.y + Px(24.0f) };
                const ImRect entryBounds{ ImVec2{ entryMax.x - entryWidth, origin.y - Px(2.0f) }, entryMax };
                return ImGui::TempInputScalar(entryBounds, id, spec.Id, ImGuiDataType_Float, value, "%.4g",
                                              &minValue, &maxValue);
            }

            constexpr ImGuiSliderFlags k_BaseFlags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat;
            const ImGuiSliderFlags flags = k_BaseFlags | (spec.Logarithmic ? ImGuiSliderFlags_Logarithmic : 0);

            ImRect grabBounds;
            const bool changed = ImGui::SliderBehavior(rowBounds, id, ImGuiDataType_Float, value, &minValue, &maxValue,
                                                       "%.4g", flags, &grabBounds);

            if (changed)
            {
                ImGui::MarkItemEdited(id);
            }

            float unitWidth = 0.0f;
            float valueRight = origin.x + width;

            if (!spec.UnitText.empty())
            {
                unitWidth = TextWidth(Face::Small, spec.UnitText);
                const float unitY = origin.y + 0.5f * (LineHeight(Face::Body) - LineHeight(Face::Small));
                PutTextRight(drawList, Face::Small, ImVec2{ valueRight, unitY }, k_TextHint, spec.UnitText);
                valueRight -= unitWidth + Px(k_Space1);
            }

            PutTextRight(drawList, Face::Body, ImVec2{ valueRight, origin.y }, k_TextValue, spec.ValueText);

            const float trackY = origin.y + Px(k_SliderTrackOffset);
            const float handleX = 0.5f * (grabBounds.Min.x + grabBounds.Max.x);
            const float grabSize = ImGui::GetStyle().GrabMinSize;
            const float usableWidth = std::max(width - grabSize, 1.0f);
            const float zeroFraction = std::clamp((0.0f - spec.Min) / (spec.Max - spec.Min), 0.0f, 1.0f);
            const float zeroX = origin.x + 0.5f * grabSize + zeroFraction * usableWidth;
            const float fillStartX = spec.Centered ? zeroX : origin.x;

            drawList->AddLine(ImVec2{ origin.x, trackY }, ImVec2{ origin.x + width, trackY }, k_LineStrong, 1.0f);
            drawList->AddLine(ImVec2{ fillStartX, trackY }, ImVec2{ handleX, trackY }, k_Accent, 2.0f);

            if (spec.Centered)
            {
                drawList->AddLine(ImVec2{ zeroX, trackY - Px(4.0f) }, ImVec2{ zeroX, trackY + Px(4.0f) }, k_LineStrong, 1.0f);
            }

            const bool hot = hovered || active;
            const float handleRadius = Px(hot ? k_HandleRadiusHot : k_HandleRadius);
            drawList->AddCircleFilled(ImVec2{ handleX, trackY }, handleRadius, active ? k_Accent : k_TextValue, 20);

            if (hovered && !active && !spec.Hint.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                DrawSliderTooltip(spec.Label, spec.Hint);
            }

            return changed;
        }

        void DrawSliderTooltip(std::string_view label, std::string_view hint)
        {
            ImGui::BeginTooltip();
            TextItem(Face::Caps, k_TextValue, label);
            TextItem(Face::Small, k_TextLabel, hint);
            ImGui::EndTooltip();
        }

        bool MatchesPreset(const SeaState& current, const SeaState& preset) noexcept
        {
            constexpr float k_Epsilon = 1e-3f;
            return std::abs(current.WindForce - preset.WindForce) < k_Epsilon &&
                   std::abs(current.Openness - preset.Openness) < k_Epsilon &&
                   std::abs(current.WindDirection - preset.WindDirection) < k_Epsilon &&
                   std::abs(current.Spread - preset.Spread) < k_Epsilon &&
                   std::abs(current.Regularity - preset.Regularity) < k_Epsilon &&
                   std::abs(current.RippleDetail - preset.RippleDetail) < k_Epsilon &&
                   std::abs(current.Depth - preset.Depth) < k_Epsilon &&
                   std::abs(current.SwellHeight - preset.SwellHeight) < k_Epsilon &&
                   std::abs(current.SwellPeriod - preset.SwellPeriod) < k_Epsilon &&
                   std::abs(current.SwellDirection - preset.SwellDirection) < k_Epsilon &&
                   std::abs(current.SwellOrder - preset.SwellOrder) < k_Epsilon &&
                   std::abs(current.HeightScale - preset.HeightScale) < k_Epsilon;
        }

        void DrawTimingTable(const char* table_id, const rhi::GpuTimers& gpu_timers,
                             const std::unordered_map<uint64_t, double>& timing_averages, ImU32 queue_color)
        {
            if (!gpu_timers.Active())
            {
                TextItem(Face::Note, k_TextHint, gpu_timers.InactiveReason());
                return;
            }

            const std::optional<rhi::GpuTimingBlock> latest = gpu_timers.Latest();

            if (!latest.has_value())
            {
                TextItem(Face::Note, k_TextHint, "Waiting for first completed frame...");
                return;
            }

            const double topLevelMs = std::max(latest->TopLevelNanoseconds * 1e-6, 0.001);
            constexpr ImGuiTableFlags k_TableFlags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH;

            if (ImGui::BeginTable(table_id, 3, k_TableFlags))
            {
                ImGui::TableSetupColumn("SCOPE", ImGuiTableColumnFlags_WidthStretch, 2.2f);
                ImGui::TableSetupColumn("AVG ms", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("LAST ms", ImGuiTableColumnFlags_WidthStretch, 0.9f);

                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableSetColumnIndex(0);
                TextItem(Face::Caps, k_TextHint, "SCOPE");
                ImGui::TableSetColumnIndex(1);
                TextItemRight(Face::Caps, k_TextHint, "AVG ms");
                ImGui::TableSetColumnIndex(2);
                TextItemRight(Face::Caps, k_TextHint, "LAST ms");

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float rowBarHeight = LineHeight(Face::Body) + 2.0f * ImGui::GetStyle().CellPadding.y;

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
                    const bool isRoot = timing.Depth == 0u;

                    if (barWidth > 1.0f)
                    {
                        const float barTop = cellMin.y - ImGui::GetStyle().CellPadding.y;
                        const ImU32 barColor = WithAlpha(queue_color, isRoot ? 0.22f : 0.12f);
                        drawList->AddRectFilled(ImVec2{ cellMin.x, barTop }, ImVec2{ cellMin.x + barWidth, barTop + rowBarHeight }, barColor);
                    }

                    const float indentPixels = static_cast<float>(timing.Depth) * Px(14.0f);

                    if (timing.Depth > 0u)
                    {
                        const float tickX = cellMin.x + indentPixels - Px(9.0f);
                        const float tickY = cellMin.y + 0.5f * LineHeight(Face::Body);
                        drawList->AddLine(ImVec2{ tickX, tickY - Px(5.0f) }, ImVec2{ tickX, tickY }, k_LineStrong, 1.0f);
                        drawList->AddLine(ImVec2{ tickX, tickY }, ImVec2{ tickX + Px(5.0f), tickY }, k_LineStrong, 1.0f);
                    }

                    ImGui::SetCursorScreenPos(ImVec2{ cellMin.x + indentPixels, cellMin.y });
                    TextItem(Face::Body, isRoot ? k_TextValue : k_TextLabel, name);

                    ImGui::TableNextColumn();
                    TextItemRight(Face::Body, k_TextValue, std::format("{:.3f}", avgMs));

                    ImGui::TableNextColumn();
                    TextItemRight(Face::Body, k_TextLabel, std::format("{:.3f}", lastMs));
                }

                ImGui::EndTable();
            }

            const std::string footer = std::format(
                "Scoped GPU total {:.3f} ms  /  dropped {} scopes, {} frames  /  lost {}",
                topLevelMs,
                latest->DroppedScopes,
                static_cast<unsigned long long>(gpu_timers.DroppedBlocks()),
                static_cast<unsigned long long>(gpu_timers.LostBlocks()));
            TextItem(Face::Note, k_TextHint, footer);
        }
    }
}
