#pragma once
#ifndef DIAMOND_DOGS_EDITOR_UI_UI_LAYOUT_HPP
#define DIAMOND_DOGS_EDITOR_UI_UI_LAYOUT_HPP
#include "UiTheme.hpp"
#include "containers/SmallVector.hpp"
#include <cstdint>
#include <span>
#include <string_view>

namespace dd::ui
{
    struct Rect
    {
        float MinX{ 0.0f };
        float MinY{ 0.0f };
        float MaxX{ 0.0f };
        float MaxY{ 0.0f };

        [[nodiscard]] float Width() const noexcept;
        [[nodiscard]] float Height() const noexcept;
        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] Rect Inset(float horizontal, float vertical) const noexcept;
    };

    enum class SizeKind : uint8_t
    {
        Invalid = 0,
        Fit,
        Fixed,
        Fraction,
        Fill,
    };

    struct SizeSpec
    {
        SizeKind Kind{ SizeKind::Fit };
        float Value{ 0.0f };

        [[nodiscard]] static constexpr SizeSpec Fit() noexcept
        {
            return SizeSpec{ SizeKind::Fit, 0.0f };
        }

        [[nodiscard]] static constexpr SizeSpec Fixed(float pixels) noexcept
        {
            return SizeSpec{ SizeKind::Fixed, pixels };
        }

        [[nodiscard]] static constexpr SizeSpec Fraction(float ratio) noexcept
        {
            return SizeSpec{ SizeKind::Fraction, ratio };
        }

        [[nodiscard]] static constexpr SizeSpec Fill(float weight) noexcept
        {
            return SizeSpec{ SizeKind::Fill, weight };
        }

        [[nodiscard]] static constexpr SizeSpec FillEqual() noexcept
        {
            return SizeSpec{ SizeKind::Fill, 1.0f };
        }
    };

    enum class CrossAlign : uint8_t
    {
        Invalid = 0,
        Start,
        Center,
        End,
        Stretch,
    };

    struct BoxStyle
    {
        float Gap{ 6.0f };
        float PaddingX{ 0.0f };
        float PaddingY{ 0.0f };
        CrossAlign Align{ CrossAlign::Stretch };
        uint32_t BackgroundColor{ 0u };
        uint32_t BorderColor{ 0u };
        float Rounding{ 0.0f };
    };

    inline constexpr BoxStyle k_DefaultBoxStyle{};

    /** @brief Computes exact slot sizes along a 1D axis from SizeSpec definitions. */
    void SolveTrackSizes(float available_extent,
                         float gap,
                         std::span<const SizeSpec> specs,
                         std::span<float> out_sizes) noexcept;

    /** @brief Computes aligned column Rects inside bounds using the horizontal layout rules of HBox. */
    void SolveHorizontalRects(const Rect& bounds,
                              std::span<const SizeSpec> columns,
                              const BoxStyle& style,
                              std::span<Rect> out_rects) noexcept;

    /** @brief Computes the Rect for one 2D grid cell inside origin_bounds. */
    [[nodiscard]] Rect SolveGridCellRect(const Rect& origin_bounds,
                                         int32_t columns,
                                         float row_height,
                                         float gap_x,
                                         float gap_y,
                                         int32_t cell_index) noexcept;

    /** @brief Vertical RAII layout container that allocates ImGui cursor slots downward. */
    class VBox
    {
    public:
        explicit VBox(const BoxStyle& _style);
        VBox(const Rect& _bounds, const BoxStyle& _style);
        ~VBox();

        VBox(const VBox&) = delete;
        VBox& operator=(const VBox&) = delete;

        /** @brief Reserves vertical space, places the ImGui cursor, sets item width, and returns the Rect. */
        [[nodiscard]] Rect Slot(SizeSpec height);
        void Spacer(float pixels);
        void Divider();

        [[nodiscard]] const Rect& Bounds() const noexcept;
        [[nodiscard]] float CursorY() const noexcept;

    private:
        void initFromBounds(const Rect& _bounds, const BoxStyle& _style);
        void releasePushedSlot() noexcept;

        Rect bounds{};
        BoxStyle style{};
        float cursorY{ 0.0f };
        float maxReachedY{ 0.0f };
        int32_t slotCount{ 0 };
        bool hasExplicitBounds{ false };
        bool hasPushedItemWidth{ false };
        bool hasPushedSlotScope{ false };
    };

    /** @brief Horizontal RAII layout container with pre-solved column Rects. */
    class HBox
    {
    public:
        HBox(float _height, std::span<const SizeSpec> _columns, const BoxStyle& _style);
        HBox(const Rect& _bounds, std::span<const SizeSpec> _columns, const BoxStyle& _style);
        ~HBox();

        HBox(const HBox&) = delete;
        HBox& operator=(const HBox&) = delete;

        /** @brief Advances to the next column, places the ImGui cursor, sets item width, and returns its Rect. */
        [[nodiscard]] Rect Next();
        [[nodiscard]] int32_t ColumnCount() const noexcept;

    private:
        void initFromBounds(const Rect& _bounds,
                            std::span<const SizeSpec> _columns,
                            const BoxStyle& _style);
        void releasePushedColumn() noexcept;

        Rect bounds{};
        foundation::SmallVector<Rect, 4> columnRects;
        int32_t nextIndex{ 0 };
        bool hasPushedItemWidth{ false };
        bool hasPushedSlotScope{ false };
    };

    /** @brief Uniform multi-column grid RAII layout container. */
    class Grid
    {
    public:
        Grid(int32_t _columns, float _row_height, float _gap_x, float _gap_y);
        Grid(const Rect& _bounds, int32_t _columns, float _row_height, float _gap_x, float _gap_y);
        ~Grid();

        Grid(const Grid&) = delete;
        Grid& operator=(const Grid&) = delete;

        /** @brief Advances to the next grid cell, places the ImGui cursor, sets item width, and returns its Rect. */
        [[nodiscard]] Rect NextCell();

    private:
        void initFromBounds(const Rect& _bounds);
        void releasePushedCell() noexcept;

        Rect bounds{};
        int32_t columns{ 1 };
        float rowHeight{ 24.0f };
        float gapX{ 6.0f };
        float gapY{ 6.0f };
        int32_t cellCount{ 0 };
        bool hasPushedItemWidth{ false };
        bool hasPushedSlotScope{ false };
    };

    /** @brief Framed RAII section card with header text and padded inner layout bounds. */
    class Card
    {
    public:
        Card(std::string_view _title, const UiTheme& _theme, AccentTone _tone);
        ~Card();

        Card(const Card&) = delete;
        Card& operator=(const Card&) = delete;

    private:
        Rect outerBounds{};
        Rect innerBounds{};
        uint32_t borderColor{ 0u };
        uint32_t backgroundColor{ 0u };
        float rounding{ 0.0f };
        float paddingY{ 0.0f };
        bool pushedScope{ false };
    };

    /** @brief Two-column RAII inspector row: renders label on the left and prepares the value column on the right. */
    class PropertyRow
    {
    public:
        PropertyRow(std::string_view _label, const UiTheme& _theme, float _height);
        PropertyRow(std::string_view _label, const UiTheme& _theme, const ImGuiFontSet& _fonts, float _height);
        ~PropertyRow();

        PropertyRow(const PropertyRow&) = delete;
        PropertyRow& operator=(const PropertyRow&) = delete;

        [[nodiscard]] const Rect& ValueRect() const noexcept;

    private:
        void initRow(std::string_view _label,
                     const UiTheme& _theme,
                     const ImGuiFontSet* _fonts,
                     float _height);

        Rect rowBounds{};
        Rect valueRect{};
        bool pushedItemWidth{ false };
        bool pushedScope{ false };
    };
}

#endif // !DIAMOND_DOGS_EDITOR_UI_UI_LAYOUT_HPP
