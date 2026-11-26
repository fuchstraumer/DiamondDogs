#include "UiControls.hpp"
#include "ImGuiModule.hpp"
#include "containers/SmallVector.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <string>

namespace
{
    [[nodiscard]] bool HasActiveImGuiWindow() noexcept;
    [[nodiscard]] float Scale() noexcept;
    [[nodiscard]] float Px(float pixels) noexcept;
    [[nodiscard]] float TextWidth(const ImGuiFontSet& fonts,
                                  const dd::ui::UiTheme& theme,
                                  dd::ui::TextRole role,
                                  std::string_view text);
    [[nodiscard]] float LineHeight(const ImGuiFontSet& fonts,
                                   const dd::ui::UiTheme& theme,
                                   dd::ui::TextRole role);
    void PutText(ImDrawList* draw_list,
                 const ImGuiFontSet& fonts,
                 const dd::ui::UiTheme& theme,
                 dd::ui::TextRole role,
                 const ImVec2& position,
                 uint32_t color,
                 std::string_view text);
    void PutTextRight(ImDrawList* draw_list,
                      const ImGuiFontSet& fonts,
                      const dd::ui::UiTheme& theme,
                      dd::ui::TextRole role,
                      const ImVec2& right_position,
                      uint32_t color,
                      std::string_view text);
    void TextItem(const ImGuiFontSet& fonts,
                  const dd::ui::UiTheme& theme,
                  dd::ui::TextRole role,
                  uint32_t color,
                  std::string_view text);
    void DrawSliderTooltip(const ImGuiFontSet& fonts,
                           const dd::ui::UiTheme& theme,
                           std::string_view label,
                           std::string_view hint);
    [[nodiscard]] int32_t DrawOptionRow(const char* id,
                                        std::string_view label,
                                        std::span<const std::string_view> options,
                                        std::span<const bool> on,
                                        int32_t columns,
                                        uint32_t selected_color,
                                        const dd::ui::UiTheme& theme,
                                        const ImGuiFontSet& fonts);
    [[nodiscard]] bool DrawSliderRowImpl(const dd::ui::SliderParams& params,
                                         bool centered,
                                         float* value,
                                         const dd::ui::UiTheme& theme,
                                         const ImGuiFontSet& fonts);
}

namespace dd::ui
{
    void DrawSectionHeader(std::string_view label, const UiTheme& theme, const ImGuiFontSet& fonts)
    {
        if (!HasActiveImGuiWindow())
        {
            return;
        }

        ImGui::Dummy(ImVec2{ 0.0f, Px(theme.Spacing.Space2) });

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float availWidth = std::max(ImGui::GetContentRegionAvail().x, 0.0f);
        const float labelHeight = LineHeight(fonts, theme, TextRole::Caps);

        PutText(drawList, fonts, theme, TextRole::Caps, position, theme.Colors.TextLabel, label);

        const float lineStart = position.x + TextWidth(fonts, theme, TextRole::Caps, label) + Px(theme.Spacing.Space2);
        const float lineY = position.y + 0.5f * labelHeight;
        const float lineEnd = position.x + availWidth;

        if (lineStart < lineEnd)
        {
            drawList->AddLine(ImVec2{ lineStart, lineY }, ImVec2{ lineEnd, lineY }, theme.Colors.LineWeak, 1.0f);
        }

        ImGui::Dummy(ImVec2{ availWidth, labelHeight + Px(theme.Spacing.Space1) });
    }

    void DrawKeyValue(std::string_view label,
                      std::string_view value,
                      std::string_view unit,
                      const UiTheme& theme,
                      const ImGuiFontSet& fonts)
    {
        if (!HasActiveImGuiWindow())
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        const float availWidth = std::max(ImGui::GetContentRegionAvail().x, 0.0f);
        const float rowHeight = Px(theme.Spacing.KeyValueHeight);
        const float centerY = position.y + 0.5f * rowHeight;
        const float bodyHeight = LineHeight(fonts, theme, TextRole::Body);

        PutText(drawList,
                fonts,
                theme,
                TextRole::Body,
                ImVec2{ position.x, centerY - 0.5f * bodyHeight },
                theme.Colors.TextLabel,
                label);

        float rightEdge = position.x + availWidth;

        if (!unit.empty())
        {
            const float smallHeight = LineHeight(fonts, theme, TextRole::Small);
            const ImVec2 unitPos{ rightEdge, centerY - 0.5f * smallHeight };
            PutTextRight(drawList, fonts, theme, TextRole::Small, unitPos, theme.Colors.TextHint, unit);
            rightEdge -= TextWidth(fonts, theme, TextRole::Small, unit) + Px(theme.Spacing.Space1);
        }

        PutTextRight(drawList,
                     fonts,
                     theme,
                     TextRole::Body,
                     ImVec2{ rightEdge, centerY - 0.5f * bodyHeight },
                     theme.Colors.TextValue,
                     value);

        const float dummyHeight = std::max(rowHeight - ImGui::GetStyle().ItemSpacing.y, 0.0f);
        ImGui::Dummy(ImVec2{ availWidth, dummyHeight });
    }

    void DrawNote(std::string_view text, const UiTheme& theme, const ImGuiFontSet& fonts)
    {
        if (!HasActiveImGuiWindow())
        {
            return;
        }

        ImGui::Dummy(ImVec2{ 0.0f, Px(theme.Spacing.Space1) });
        TextItem(fonts, theme, TextRole::Note, theme.Colors.TextHint, text);
    }

    bool DrawTelemetryToggle(const char* id,
                             std::string_view label,
                             std::string_view state_off,
                             std::string_view state_on,
                             bool* value,
                             const UiTheme& theme,
                             const ImGuiFontSet& fonts)
    {
        if (value == nullptr || !HasActiveImGuiWindow())
        {
            return false;
        }

        foundation::SmallVector<std::string_view, 2> options;
        static_assert(decltype(options)::k_UseFixedInlineCopy);
        options.push_back(state_off);
        options.push_back(state_on);

        foundation::SmallVector<bool, 2> on;
        static_assert(decltype(on)::k_UseFixedInlineCopy);
        on.push_back(!(*value));
        on.push_back(*value);

        const uint32_t selectedColor = *value ? theme.Colors.Accent : theme.Colors.TextValue;
        const int32_t clicked = DrawOptionRow(id, label, options, on, 2, selectedColor, theme, fonts);

        if (clicked < 0)
        {
            return false;
        }

        const bool newValue = clicked == 1;
        const bool changed = newValue != *value;
        *value = newValue;
        return changed;
    }

    bool DrawSegmentedSelector(const char* id,
                               std::span<const std::string_view> options,
                               int32_t& selected,
                               int32_t columns,
                               const UiTheme& theme,
                               const ImGuiFontSet& fonts)
    {
        const int32_t count = static_cast<int32_t>(std::ssize(options));

        if (count <= 0 || !HasActiveImGuiWindow())
        {
            return false;
        }

        foundation::SmallVector<bool, 16> on;
        static_assert(decltype(on)::k_UseFixedInlineCopy);
        on.resize(static_cast<size_t>(count), false);

        if (selected >= 0 && selected < count)
        {
            on[static_cast<size_t>(selected)] = true;
        }

        const int32_t clicked = DrawOptionRow(id,
                                              std::string_view{},
                                              options,
                                              on,
                                              columns,
                                              theme.Colors.Accent,
                                              theme,
                                              fonts);

        if (clicked < 0)
        {
            return false;
        }

        selected = clicked;
        return true;
    }

    bool DrawArtistSlider(const SliderParams& params,
                          float* value,
                          const UiTheme& theme,
                          const ImGuiFontSet& fonts)
    {
        return DrawSliderRowImpl(params, false, value, theme, fonts);
    }

    bool DrawAngleSlider(const SliderParams& params,
                         float* degrees,
                         const UiTheme& theme,
                         const ImGuiFontSet& fonts)
    {
        if (degrees == nullptr || !HasActiveImGuiWindow())
        {
            return false;
        }

        constexpr float k_AngleSnapDegrees = 1.5f;
        std::string formattedValue;
        SliderParams resolved = params;

        if (resolved.ValueText.empty())
        {
            formattedValue = std::format("{:+.0f}", *degrees);
            resolved.ValueText = formattedValue;
        }

        if (resolved.UnitText.empty())
        {
            resolved.UnitText = "deg";
        }

        const bool changed = DrawSliderRowImpl(resolved, true, degrees, theme, fonts);

        if (changed && std::abs(*degrees) < k_AngleSnapDegrees && ImGui::IsItemActive())
        {
            *degrees = 0.0f;
        }

        return changed;
    }
}

namespace
{
    bool HasActiveImGuiWindow() noexcept
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetCurrentWindowRead() != nullptr;
    }

    float Scale() noexcept
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return 1.0f;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        return style.FontScaleMain * style.FontScaleDpi;
    }

    float Px(float pixels) noexcept
    {
        return pixels * Scale();
    }

    float TextWidth(const ImGuiFontSet& fonts,
                    const dd::ui::UiTheme& theme,
                    dd::ui::TextRole role,
                    std::string_view text)
    {
        const dd::ui::ResolvedFont resolved = dd::ui::ResolveFont(fonts, theme, role);
        ImGui::PushFont(resolved.Font, resolved.Size);
        const ImVec2 size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        ImGui::PopFont();
        return size.x;
    }

    float LineHeight(const ImGuiFontSet& fonts,
                     const dd::ui::UiTheme& theme,
                     dd::ui::TextRole role)
    {
        const dd::ui::ResolvedFont resolved = dd::ui::ResolveFont(fonts, theme, role);
        ImGui::PushFont(resolved.Font, resolved.Size);
        const float height = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        return height;
    }

    void PutText(ImDrawList* draw_list,
                 const ImGuiFontSet& fonts,
                 const dd::ui::UiTheme& theme,
                 dd::ui::TextRole role,
                 const ImVec2& position,
                 uint32_t color,
                 std::string_view text)
    {
        const dd::ui::ResolvedFont resolved = dd::ui::ResolveFont(fonts, theme, role);
        ImGui::PushFont(resolved.Font, resolved.Size);
        draw_list->AddText(ImGui::GetFont(),
                           ImGui::GetFontSize(),
                           position,
                           color,
                           text.data(),
                           text.data() + text.size());
        ImGui::PopFont();
    }

    void PutTextRight(ImDrawList* draw_list,
                      const ImGuiFontSet& fonts,
                      const dd::ui::UiTheme& theme,
                      dd::ui::TextRole role,
                      const ImVec2& right_position,
                      uint32_t color,
                      std::string_view text)
    {
        const float width = TextWidth(fonts, theme, role, text);
        PutText(draw_list,
                fonts,
                theme,
                role,
                ImVec2{ right_position.x - width, right_position.y },
                color,
                text);
    }

    void TextItem(const ImGuiFontSet& fonts,
                  const dd::ui::UiTheme& theme,
                  dd::ui::TextRole role,
                  uint32_t color,
                  std::string_view text)
    {
        const dd::ui::ResolvedFont resolved = dd::ui::ResolveFont(fonts, theme, role);
        ImGui::PushFont(resolved.Font, resolved.Size);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void DrawSliderTooltip(const ImGuiFontSet& fonts,
                           const dd::ui::UiTheme& theme,
                           std::string_view label,
                           std::string_view hint)
    {
        ImGui::BeginTooltip();
        TextItem(fonts, theme, dd::ui::TextRole::Caps, theme.Colors.TextValue, label);
        TextItem(fonts, theme, dd::ui::TextRole::Small, theme.Colors.TextLabel, hint);
        ImGui::EndTooltip();
    }

    int32_t DrawOptionRow(const char* id,
                          std::string_view label,
                          std::span<const std::string_view> options,
                          std::span<const bool> on,
                          int32_t columns,
                          uint32_t selected_color,
                          const dd::ui::UiTheme& theme,
                          const ImGuiFontSet& fonts)
    {
        constexpr float k_OptionPadding = 20.0f;
        const int32_t count = static_cast<int32_t>(std::ssize(options));

        if (count <= 0)
        {
            return -1;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float availWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
        const int32_t clampedColumns = std::clamp(columns, 1, count);
        const int32_t rows = (count + clampedColumns - 1) / clampedColumns;
        const float cellHeight = Px(theme.Spacing.OptionHeight);

        foundation::SmallVector<float, 16> textWidths;
        static_assert(decltype(textWidths)::k_UseFixedInlineCopy);
        textWidths.resize(static_cast<size_t>(count));
        float textTotal = 0.0f;

        for (int32_t index = 0; index < count; ++index)
        {
            const float width = TextWidth(fonts, theme, dd::ui::TextRole::Caps, options[static_cast<size_t>(index)]);
            textWidths[static_cast<size_t>(index)] = width;
            textTotal += width;
        }

        float left = start.x;
        float width = availWidth;
        float cellsTop = start.y;

        if (!label.empty())
        {
            const float labelWidth = TextWidth(fonts, theme, dd::ui::TextRole::Body, label);
            const float besideOffset =
                std::max(Px(theme.Spacing.LabelColumnWidth), labelWidth + Px(theme.Spacing.Space3));
            const float besideWidth = availWidth - besideOffset;
            const float neededWidth = textTotal + static_cast<float>(count) * Px(k_OptionPadding);
            const bool stacked = rows == 1 && neededWidth > besideWidth;
            const float bodyHeight = LineHeight(fonts, theme, dd::ui::TextRole::Body);

            if (stacked)
            {
                PutText(drawList, fonts, theme, dd::ui::TextRole::Body, start, theme.Colors.TextLabel, label);
                cellsTop = start.y + bodyHeight + Px(theme.Spacing.Space1);
            }
            else
            {
                const float labelY = start.y + 0.5f * (cellHeight - bodyHeight);
                PutText(drawList,
                        fonts,
                        theme,
                        dd::ui::TextRole::Body,
                        ImVec2{ start.x, labelY },
                        theme.Colors.TextLabel,
                        label);
                left = start.x + besideOffset;
                width = std::max(besideWidth, 1.0f);
            }
        }

        const float rowsBottom = cellsTop + static_cast<float>(rows) * cellHeight;
        const float captionHeight = LineHeight(fonts, theme, dd::ui::TextRole::Caps);
        int32_t clicked = -1;
        const char* safeId = (id != nullptr && id[0] != '\0') ? id : "##option_row";

        ImGui::PushID(safeId);

        for (int32_t row = 0; row < rows; ++row)
        {
            const int32_t first = row * clampedColumns;
            const int32_t last = std::min(first + clampedColumns, count);
            const int32_t cellCount = last - first;
            float rowTextTotal = 0.0f;

            for (int32_t index = first; index < last; ++index)
            {
                rowTextTotal += textWidths[static_cast<size_t>(index)];
            }

            const float extraPerCell = std::max((width - rowTextTotal) / static_cast<float>(cellCount), 0.0f);
            const float equalWidth = width / static_cast<float>(clampedColumns);
            float cellLeft = left;

            for (int32_t index = first; index < last; ++index)
            {
                const float textWidth = textWidths[static_cast<size_t>(index)];
                const float cellWidth = std::max(rows == 1 ? textWidth + extraPerCell : equalWidth, 1.0f);
                const float cellTop = cellsTop + static_cast<float>(row) * cellHeight;

                ImGui::SetCursorScreenPos(ImVec2{ cellLeft, cellTop });
                ImGui::PushID(index);

                if (ImGui::InvisibleButton("##option", ImVec2{ cellWidth, cellHeight }))
                {
                    clicked = index;
                }

                ImGui::PopID();

                const bool hovered = ImGui::IsItemHovered();
                const bool selected = index < static_cast<int32_t>(std::ssize(on)) && on[static_cast<size_t>(index)];
                uint32_t textColor = hovered ? theme.Colors.TextValue : theme.Colors.TextLabel;

                if (selected)
                {
                    textColor = selected_color;
                }

                const float textX = cellLeft + 0.5f * (cellWidth - textWidth);
                const float textY = cellTop + 0.5f * (cellHeight - captionHeight);
                PutText(drawList,
                        fonts,
                        theme,
                        dd::ui::TextRole::Caps,
                        ImVec2{ textX, textY },
                        textColor,
                        options[static_cast<size_t>(index)]);

                if (selected)
                {
                    const float underlineY = cellTop + cellHeight - Px(1.0f);
                    drawList->AddLine(ImVec2{ textX, underlineY },
                                      ImVec2{ textX + textWidth, underlineY },
                                      selected_color,
                                      1.0f);
                }

                cellLeft += cellWidth;
            }
        }

        ImGui::PopID();

        const float trailingGap = label.empty() ? Px(theme.Spacing.Space2) : 0.0f;
        ImGui::SetCursorScreenPos(ImVec2{ start.x, rowsBottom });
        ImGui::Dummy(ImVec2{ 1.0f, trailingGap });
        return clicked;
    }

    bool DrawSliderRowImpl(const dd::ui::SliderParams& params,
                           bool centered,
                           float* value,
                           const dd::ui::UiTheme& theme,
                           const ImGuiFontSet& fonts)
    {
        if (value == nullptr || !HasActiveImGuiWindow())
        {
            return false;
        }

        ImGuiWindow* window = ImGui::GetCurrentWindow();

        if (window->SkipItems)
        {
            return false;
        }

        constexpr float k_SliderTrackOffset = 26.0f;
        constexpr float k_HandleRadius = 4.5f;
        constexpr float k_HandleRadiusHot = 6.0f;
        constexpr float k_EntryWidth = 110.0f;
        constexpr float k_EntryHeight = 24.0f;
        constexpr float k_EntryOffsetY = 2.0f;
        constexpr float k_ZeroTickHalfHeight = 4.0f;
        constexpr int32_t k_HandleSegments = 20;

        ImGuiContext& context = *GImGui;
        const char* safeId = (params.Id != nullptr && params.Id[0] != '\0') ? params.Id : "##slider";
        const ImGuiID id = window->GetID(safeId);
        const ImVec2 origin = window->DC.CursorPos;
        const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
        const float rowHeight = Px(theme.Spacing.SliderRowHeight);
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
        const uint32_t labelColor = (hovered || active || typing) ? theme.Colors.TextValue : theme.Colors.TextLabel;
        PutText(drawList, fonts, theme, dd::ui::TextRole::Body, origin, labelColor, params.Label);

        float minValue = params.Min;
        float maxValue = params.Max;

        if (typing)
        {
            const float entryWidth = Px(k_EntryWidth);
            const ImVec2 entryMax{ origin.x + width, origin.y + Px(k_EntryHeight) };
            const ImRect entryBounds{ ImVec2{ entryMax.x - entryWidth, origin.y - Px(k_EntryOffsetY) }, entryMax };
            return ImGui::TempInputScalar(entryBounds,
                                          id,
                                          safeId,
                                          ImGuiDataType_Float,
                                          value,
                                          "%.4g",
                                          &minValue,
                                          &maxValue);
        }

        constexpr ImGuiSliderFlags k_BaseFlags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat;
        const ImGuiSliderFlags flags = k_BaseFlags | (params.Logarithmic ? ImGuiSliderFlags_Logarithmic : 0);

        ImRect grabBounds{ rowBounds.Min, rowBounds.Min };
        const bool changed = ImGui::SliderBehavior(rowBounds,
                                                   id,
                                                   ImGuiDataType_Float,
                                                   value,
                                                   &minValue,
                                                   &maxValue,
                                                   "%.4g",
                                                   flags,
                                                   &grabBounds);

        if (changed)
        {
            ImGui::MarkItemEdited(id);
        }

        float valueRight = origin.x + width;

        if (!params.UnitText.empty())
        {
            const float unitWidth = TextWidth(fonts, theme, dd::ui::TextRole::Small, params.UnitText);
            const float bodyHeight = LineHeight(fonts, theme, dd::ui::TextRole::Body);
            const float smallHeight = LineHeight(fonts, theme, dd::ui::TextRole::Small);
            const float unitY = origin.y + 0.5f * (bodyHeight - smallHeight);
            PutTextRight(drawList,
                         fonts,
                         theme,
                         dd::ui::TextRole::Small,
                         ImVec2{ valueRight, unitY },
                         theme.Colors.TextHint,
                         params.UnitText);
            valueRight -= unitWidth + Px(theme.Spacing.Space1);
        }

        PutTextRight(drawList,
                     fonts,
                     theme,
                     dd::ui::TextRole::Body,
                     ImVec2{ valueRight, origin.y },
                     theme.Colors.TextValue,
                     params.ValueText);

        const float trackY = origin.y + Px(k_SliderTrackOffset);
        const float handleX = 0.5f * (grabBounds.Min.x + grabBounds.Max.x);
        const float grabSize = ImGui::GetStyle().GrabMinSize;
        const float usableWidth = std::max(width - grabSize, 1.0f);
        const float rangeSpan = params.Max - params.Min;
        const float zeroFraction =
            rangeSpan > 0.0f ? std::clamp((0.0f - params.Min) / rangeSpan, 0.0f, 1.0f) : 0.0f;
        const float zeroX = origin.x + 0.5f * grabSize + zeroFraction * usableWidth;
        const float fillStartX = centered ? zeroX : origin.x;

        drawList->AddLine(ImVec2{ origin.x, trackY },
                          ImVec2{ origin.x + width, trackY },
                          theme.Colors.LineStrong,
                          1.0f);
        drawList->AddLine(ImVec2{ fillStartX, trackY },
                          ImVec2{ handleX, trackY },
                          theme.Colors.Accent,
                          2.0f);

        if (centered)
        {
            drawList->AddLine(ImVec2{ zeroX, trackY - Px(k_ZeroTickHalfHeight) },
                              ImVec2{ zeroX, trackY + Px(k_ZeroTickHalfHeight) },
                              theme.Colors.LineStrong,
                              1.0f);
        }

        const bool hot = hovered || active;
        const float handleRadius = Px(hot ? k_HandleRadiusHot : k_HandleRadius);
        drawList->AddCircleFilled(ImVec2{ handleX, trackY },
                                  handleRadius,
                                  active ? theme.Colors.Accent : theme.Colors.TextValue,
                                  k_HandleSegments);

        if (hovered && !active && !params.Hint.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            DrawSliderTooltip(fonts, theme, params.Label, params.Hint);
        }

        return changed;
    }
}
