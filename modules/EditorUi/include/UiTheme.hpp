#pragma once
#ifndef DIAMOND_DOGS_EDITOR_UI_UI_THEME_HPP
#define DIAMOND_DOGS_EDITOR_UI_UI_THEME_HPP
#include <cstdint>

struct ImFont;
struct ImGuiFontSet;

namespace dd::ui
{
    enum class AccentTone : uint8_t
    {
        Invalid = 0,
        Neutral,
        Accent,
        Warning,
        Wind,
        Swell,
    };

    enum class HeaderFontMode : uint8_t
    {
        Invalid = 0,
        Blinker,
        Michroma,
    };

    enum class TextRole : uint8_t
    {
        Invalid = 0,
        Caps,
        Title,
        Hero,
        Body,
        Small,
        Note,
    };

    struct UiColors
    {
        uint32_t SurfaceTop{ 0u };
        uint32_t SurfaceBottom{ 0u };
        uint32_t SurfacePopup{ 0u };
        uint32_t FieldRest{ 0u };
        uint32_t FieldHover{ 0u };
        uint32_t FieldActive{ 0u };
        uint32_t TextValue{ 0u };
        uint32_t TextLabel{ 0u };
        uint32_t TextHint{ 0u };
        uint32_t LineWeak{ 0u };
        uint32_t LineStrong{ 0u };
        uint32_t Accent{ 0u };
        uint32_t Warning{ 0u };
        uint32_t DataWind{ 0u };
        uint32_t DataSwell{ 0u };
        uint32_t DataGraphicsQueue{ 0u };
        uint32_t DataAsyncQueue{ 0u };
    };

    struct UiSpacing
    {
        float Space1{ 4.0f };
        float Space2{ 8.0f };
        float Space3{ 12.0f };
        float Space4{ 16.0f };
        float PanelMargin{ 16.0f };
        float LabelColumnWidth{ 150.0f };
        float SliderRowHeight{ 34.0f };
        float OptionHeight{ 28.0f };
        float KeyValueHeight{ 22.0f };
        float CardRounding{ 2.0f };
    };

    struct UiTheme
    {
        UiColors Colors{};
        UiSpacing Spacing{};
        HeaderFontMode FontMode{ HeaderFontMode::Blinker };

        [[nodiscard]] uint32_t ToneColor(AccentTone tone) const noexcept;
    };

    struct ResolvedFont
    {
        ImFont* Font{ nullptr };
        float Size{ 0.0f };
    };

    [[nodiscard]] UiTheme MakeTelemetryTheme() noexcept;
    [[nodiscard]] uint32_t WithAlpha(uint32_t color, float alpha_scale) noexcept;
    void ApplyImGuiTheme(const UiTheme& theme) noexcept;
    [[nodiscard]] ResolvedFont ResolveFont(const ImGuiFontSet& fonts,
                                           const UiTheme& theme,
                                           TextRole role) noexcept;
}

#endif // !DIAMOND_DOGS_EDITOR_UI_UI_THEME_HPP
