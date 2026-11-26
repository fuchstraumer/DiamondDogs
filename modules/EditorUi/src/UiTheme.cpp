#include "UiTheme.hpp"
#include "ImGuiModule.hpp"
#include "imgui.h"
#include <algorithm>

namespace
{
    [[nodiscard]] float Scale() noexcept;
    [[nodiscard]] float Px(float pixels) noexcept;
}

namespace dd::ui
{
    uint32_t UiTheme::ToneColor(AccentTone tone) const noexcept
    {
        switch (tone)
        {
        case AccentTone::Accent:
            return Colors.Accent;
        case AccentTone::Warning:
            return Colors.Warning;
        case AccentTone::Wind:
            return Colors.DataWind;
        case AccentTone::Swell:
            return Colors.DataSwell;
        case AccentTone::Neutral:
        case AccentTone::Invalid:
            return Colors.LineWeak;
        }

        return Colors.LineWeak;
    }

    UiTheme MakeTelemetryTheme() noexcept
    {
        UiTheme theme{};
        theme.Colors = UiColors
        {
            .SurfaceTop = IM_COL32(12, 16, 22, 255),
            .SurfaceBottom = IM_COL32(7, 10, 14, 255),
            .SurfacePopup = IM_COL32(10, 14, 20, 252),
            .FieldRest = IM_COL32(255, 255, 255, 14),
            .FieldHover = IM_COL32(255, 255, 255, 26),
            .FieldActive = IM_COL32(255, 255, 255, 38),
            .TextValue = IM_COL32(236, 241, 247, 255),
            .TextLabel = IM_COL32(152, 163, 178, 255),
            .TextHint = IM_COL32(108, 120, 136, 255),
            .LineWeak = IM_COL32(255, 255, 255, 28),
            .LineStrong = IM_COL32(255, 255, 255, 70),
            .Accent = IM_COL32(72, 208, 232, 255),
            .Warning = IM_COL32(242, 112, 82, 255),
            .DataWind = IM_COL32(142, 176, 255, 255),
            .DataSwell = IM_COL32(236, 186, 112, 255),
            .DataGraphicsQueue = IM_COL32(104, 198, 168, 255),
            .DataAsyncQueue = IM_COL32(196, 150, 232, 255),
        };
        theme.Spacing = UiSpacing{};
        theme.FontMode = HeaderFontMode::Blinker;
        return theme;
    }

    uint32_t WithAlpha(uint32_t color, float alpha_scale) noexcept
    {
        const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) * alpha_scale;
        const uint32_t alphaByte = static_cast<uint32_t>(std::clamp(alpha, 0.0f, 255.0f));
        return (color & ~IM_COL32_A_MASK) | (alphaByte << IM_COL32_A_SHIFT);
    }

    void ApplyImGuiTheme(const UiTheme& theme) noexcept
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }

        constexpr float k_FramePaddingY = 5.0f;
        constexpr float k_CellPaddingY = 3.0f;
        constexpr float k_ScrollbarSize = 4.0f;
        constexpr float k_GrabMinSize = 12.0f;
        constexpr float k_PopupBorderSize = 1.0f;
        constexpr float k_SelectionAlpha = 0.3f;

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2{ Px(theme.Spacing.Space4), Px(theme.Spacing.Space4) };
        style.FramePadding = ImVec2{ Px(theme.Spacing.Space2), Px(k_FramePaddingY) };
        style.ItemSpacing = ImVec2{ Px(theme.Spacing.Space2), Px(theme.Spacing.Space1) };
        style.ItemInnerSpacing = ImVec2{ Px(theme.Spacing.Space2), Px(theme.Spacing.Space1) };
        style.CellPadding = ImVec2{ Px(theme.Spacing.Space2), Px(k_CellPaddingY) };
        style.ScrollbarSize = Px(k_ScrollbarSize);
        style.GrabMinSize = Px(k_GrabMinSize);

        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = Px(theme.Spacing.CardRounding);
        style.PopupRounding = Px(theme.Spacing.CardRounding);
        style.ScrollbarRounding = Px(theme.Spacing.CardRounding);
        style.GrabRounding = Px(theme.Spacing.CardRounding);
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = k_PopupBorderSize;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(theme.Colors.TextValue);
        colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(theme.Colors.TextHint);
        colors[ImGuiCol_TextSelectedBg] =
            ImGui::ColorConvertU32ToFloat4(WithAlpha(theme.Colors.Accent, k_SelectionAlpha));
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        colors[ImGuiCol_ChildBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(theme.Colors.SurfacePopup);
        colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineWeak);
        colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldRest);
        colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldHover);
        colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldActive);
        colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldRest);
        colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldHover);
        colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldActive);
        colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldRest);
        colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldHover);
        colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(theme.Colors.FieldActive);
        colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(theme.Colors.Accent);
        colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(theme.Colors.TextValue);
        colors[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(theme.Colors.Accent);
        colors[ImGuiCol_ScrollbarBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineStrong);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(theme.Colors.TextLabel);
        colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(theme.Colors.Accent);
        colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineWeak);
        colors[ImGuiCol_TableHeaderBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        colors[ImGuiCol_TableBorderStrong] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineStrong);
        colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineWeak);
        colors[ImGuiCol_TableRowBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        colors[ImGuiCol_TableRowBgAlt] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        colors[ImGuiCol_ResizeGrip] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineWeak);
        colors[ImGuiCol_ResizeGripHovered] = ImGui::ColorConvertU32ToFloat4(theme.Colors.LineStrong);
        colors[ImGuiCol_ResizeGripActive] = ImGui::ColorConvertU32ToFloat4(theme.Colors.Accent);
        colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(theme.Colors.Accent);
    }

    ResolvedFont ResolveFont(const ImGuiFontSet& fonts, const UiTheme& theme, TextRole role) noexcept
    {
        constexpr float k_SizeCaption = 12.5f;
        constexpr float k_SizeCaptionMichroma = 9.5f;
        constexpr float k_SizeBody = 15.0f;
        constexpr float k_SizeTitle = 19.0f;
        constexpr float k_SizeTitleMichroma = 14.0f;
        constexpr float k_SizeHero = 56.0f;
        constexpr float k_SizeHeroMichroma = 40.0f;

        const bool michroma = theme.FontMode == HeaderFontMode::Michroma;
        const float scale = Scale();

        switch (role)
        {
        case TextRole::Caps:
            return ResolvedFont{ michroma ? fonts.SectionMichroma : fonts.SectionBlinker,
                                 (michroma ? k_SizeCaptionMichroma : k_SizeCaption) * scale };
        case TextRole::Title:
            return ResolvedFont{ michroma ? fonts.HeaderMichroma : fonts.HeaderBlinker,
                                 (michroma ? k_SizeTitleMichroma : k_SizeTitle) * scale };
        case TextRole::Hero:
            return ResolvedFont{ michroma ? fonts.HeaderMichroma : fonts.HeaderBlinker,
                                 (michroma ? k_SizeHeroMichroma : k_SizeHero) * scale };
        case TextRole::Body:
            return ResolvedFont{ fonts.Body, k_SizeBody * scale };
        case TextRole::Small:
            return ResolvedFont{ fonts.Body, k_SizeCaption * scale };
        case TextRole::Note:
            return ResolvedFont{ fonts.BodyItalic, k_SizeCaption * scale };
        case TextRole::Invalid:
            break;
        }

        return ResolvedFont{ fonts.Body, k_SizeBody * scale };
    }
}

namespace
{
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
}
