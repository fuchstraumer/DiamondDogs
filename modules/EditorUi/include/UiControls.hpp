#pragma once
#ifndef DIAMOND_DOGS_EDITOR_UI_UI_CONTROLS_HPP
#define DIAMOND_DOGS_EDITOR_UI_UI_CONTROLS_HPP
#include "UiTheme.hpp"
#include <cstdint>
#include <span>
#include <string_view>

struct ImGuiFontSet;

namespace dd::ui
{
    struct SliderParams
    {
        const char* Id{ nullptr };
        std::string_view Label{};
        std::string_view Hint{};
        std::string_view ValueText{};
        std::string_view UnitText{};
        float Min{ 0.0f };
        float Max{ 1.0f };
        bool Logarithmic{ false };
    };

    void DrawSectionHeader(std::string_view label, const UiTheme& theme, const ImGuiFontSet& fonts);

    void DrawKeyValue(std::string_view label,
                      std::string_view value,
                      std::string_view unit,
                      const UiTheme& theme,
                      const ImGuiFontSet& fonts);

    void DrawNote(std::string_view text, const UiTheme& theme, const ImGuiFontSet& fonts);

    [[nodiscard]] bool DrawTelemetryToggle(const char* id,
                                           std::string_view label,
                                           std::string_view state_off,
                                           std::string_view state_on,
                                           bool* value,
                                           const UiTheme& theme,
                                           const ImGuiFontSet& fonts);

    [[nodiscard]] bool DrawSegmentedSelector(const char* id,
                                             std::span<const std::string_view> options,
                                             int32_t& selected,
                                             int32_t columns,
                                             const UiTheme& theme,
                                             const ImGuiFontSet& fonts);

    [[nodiscard]] bool DrawArtistSlider(const SliderParams& params,
                                        float* value,
                                        const UiTheme& theme,
                                        const ImGuiFontSet& fonts);

    [[nodiscard]] bool DrawAngleSlider(const SliderParams& params,
                                       float* degrees,
                                       const UiTheme& theme,
                                       const ImGuiFontSet& fonts);
}

#endif // !DIAMOND_DOGS_EDITOR_UI_UI_CONTROLS_HPP
