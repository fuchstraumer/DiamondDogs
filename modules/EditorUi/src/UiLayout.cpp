#include "UiLayout.hpp"
#include "ImGuiModule.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cassert>
#include <memory>

namespace
{
    [[nodiscard]] bool HasActiveImGuiWindow() noexcept;
    [[nodiscard]] foundation::SmallVector<dd::ui::Rect, 16>& ScopeStack() noexcept;
    [[nodiscard]] foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& ActiveCardSplitters() noexcept;
    [[nodiscard]] foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& FreeCardSplitters() noexcept;
    [[nodiscard]] dd::ui::Rect CurrentAvailableRect() noexcept;
    void PushScopeRect(const dd::ui::Rect& rect) noexcept;
    void PopScopeRect() noexcept;
    void DrawBoxDecoration(const dd::ui::Rect& rect, const dd::ui::BoxStyle& style) noexcept;
    void SetCursorAndDummy(float x, float y) noexcept;
    void BeginCardSplitter(ImDrawList* draw_list) noexcept;
    void EndCardSplitter(ImDrawList* draw_list,
                         const dd::ui::Rect& rect,
                         uint32_t background_color,
                         uint32_t border_color,
                         float rounding) noexcept;
    [[nodiscard]] float ResolveSlotHeight(const dd::ui::SizeSpec& height,
                                          const dd::ui::Rect& inner,
                                          float cursor_y,
                                          bool has_explicit_bounds) noexcept;
}

namespace dd::ui
{
    float Rect::Width() const noexcept
    {
        return std::max(MaxX - MinX, 0.0f);
    }

    float Rect::Height() const noexcept
    {
        return std::max(MaxY - MinY, 0.0f);
    }

    bool Rect::IsEmpty() const noexcept
    {
        return MaxX <= MinX || MaxY <= MinY;
    }

    Rect Rect::Inset(float horizontal, float vertical) const noexcept
    {
        const float minX = MinX + horizontal;
        const float minY = MinY + vertical;
        const float maxX = std::max(minX, MaxX - horizontal);
        const float maxY = std::max(minY, MaxY - vertical);
        return Rect{ minX, minY, maxX, maxY };
    }

    void SolveTrackSizes(float available_extent,
                         float gap,
                         std::span<const SizeSpec> specs,
                         std::span<float> out_sizes) noexcept
    {
        const int32_t count = static_cast<int32_t>(std::min(std::ssize(specs), std::ssize(out_sizes)));

        if (count <= 0)
        {
            return;
        }

        const float clampedGap = std::max(gap, 0.0f);
        const float totalGap = static_cast<float>(count - 1) * clampedGap;
        const float usable = std::max(std::max(available_extent, 0.0f) - totalGap, 0.0f);

        float fixedUsed = 0.0f;
        float totalFillWeight = 0.0f;

        for (int32_t index = 0; index < count; ++index)
        {
            const SizeSpec& spec = specs[static_cast<size_t>(index)];

            switch (spec.Kind)
            {
            case SizeKind::Fixed:
                {
                    const float size = std::max(spec.Value, 0.0f);
                    out_sizes[static_cast<size_t>(index)] = size;
                    fixedUsed += size;
                }
                break;
            case SizeKind::Fraction:
                {
                    const float size = std::max(spec.Value, 0.0f) * usable;
                    out_sizes[static_cast<size_t>(index)] = size;
                    fixedUsed += size;
                }
                break;
            case SizeKind::Fill:
                out_sizes[static_cast<size_t>(index)] = 0.0f;
                totalFillWeight += std::max(spec.Value, 0.0f);
                break;
            case SizeKind::Fit:
                if (spec.Value > 0.0f)
                {
                    out_sizes[static_cast<size_t>(index)] = spec.Value;
                    fixedUsed += spec.Value;
                }
                else
                {
                    out_sizes[static_cast<size_t>(index)] = 0.0f;
                    totalFillWeight += 1.0f;
                }
                break;
            case SizeKind::Invalid:
                out_sizes[static_cast<size_t>(index)] = 0.0f;
                break;
            }
        }

        const float remaining = std::max(usable - fixedUsed, 0.0f);

        if (totalFillWeight > 0.0f && remaining > 0.0f)
        {
            for (int32_t index = 0; index < count; ++index)
            {
                const SizeSpec& spec = specs[static_cast<size_t>(index)];

                if (spec.Kind == SizeKind::Fill)
                {
                    const float weight = std::max(spec.Value, 0.0f);
                    out_sizes[static_cast<size_t>(index)] = remaining * (weight / totalFillWeight);
                }
                else if (spec.Kind == SizeKind::Fit && spec.Value <= 0.0f)
                {
                    out_sizes[static_cast<size_t>(index)] = remaining * (1.0f / totalFillWeight);
                }
            }
        }
    }

    void SolveHorizontalRects(const Rect& bounds,
                              std::span<const SizeSpec> columns,
                              const BoxStyle& style,
                              std::span<Rect> out_rects) noexcept
    {
        const int32_t count = static_cast<int32_t>(std::min(std::ssize(columns), std::ssize(out_rects)));

        if (count <= 0)
        {
            return;
        }

        const Rect inner = bounds.Inset(style.PaddingX, style.PaddingY);
        foundation::SmallVector<float, 8> widths;
        static_assert(decltype(widths)::k_UseFixedInlineCopy);
        widths.resize(static_cast<size_t>(count));

        const float clampedGap = std::max(style.Gap, 0.0f);
        SolveTrackSizes(inner.Width(), clampedGap, columns.subspan(0, static_cast<size_t>(count)), widths);

        float totalWidth = static_cast<float>(count - 1) * clampedGap;

        for (int32_t index = 0; index < count; ++index)
        {
            totalWidth += widths[static_cast<size_t>(index)];
        }

        const float slack = std::max(inner.Width() - totalWidth, 0.0f);
        float cursorX = inner.MinX;

        switch (style.Align)
        {
        case CrossAlign::Center:
            cursorX += 0.5f * slack;
            break;
        case CrossAlign::End:
            cursorX += slack;
            break;
        case CrossAlign::Start:
        case CrossAlign::Stretch:
        case CrossAlign::Invalid:
            break;
        }

        for (int32_t index = 0; index < count; ++index)
        {
            const float columnWidth = widths[static_cast<size_t>(index)];
            out_rects[static_cast<size_t>(index)] = Rect{ cursorX, inner.MinY, cursorX + columnWidth, inner.MaxY };
            cursorX += columnWidth + clampedGap;
        }
    }

    Rect SolveGridCellRect(const Rect& origin_bounds,
                           int32_t columns,
                           float row_height,
                           float gap_x,
                           float gap_y,
                           int32_t cell_index) noexcept
    {
        const int32_t clampedColumns = std::max(columns, 1);
        const int32_t clampedIndex = std::max(cell_index, 0);
        const float clampedGapX = std::max(gap_x, 0.0f);
        const float clampedGapY = std::max(gap_y, 0.0f);
        const float clampedRowHeight = std::max(row_height, 0.0f);

        const int32_t row = clampedIndex / clampedColumns;
        const int32_t col = clampedIndex % clampedColumns;
        const float totalGapX = clampedGapX * static_cast<float>(clampedColumns - 1);
        const float cellWidth =
            std::max((origin_bounds.Width() - totalGapX) / static_cast<float>(clampedColumns), 0.0f);
        const float minX = origin_bounds.MinX + static_cast<float>(col) * (cellWidth + clampedGapX);
        const float minY = origin_bounds.MinY + static_cast<float>(row) * (clampedRowHeight + clampedGapY);
        return Rect{ minX, minY, minX + cellWidth, minY + clampedRowHeight };
    }

    VBox::VBox(const BoxStyle& _style) :
        hasExplicitBounds{ false }
    {
        initFromBounds(CurrentAvailableRect(), _style);
    }

    VBox::VBox(const Rect& _bounds, const BoxStyle& _style) :
        hasExplicitBounds{ true }
    {
        initFromBounds(_bounds, _style);
    }

    VBox::~VBox()
    {
        releasePushedSlot();

        if (HasActiveImGuiWindow())
        {
            const float bottomY = maxReachedY + style.PaddingY;
            const float finalY = hasExplicitBounds ? std::max(bounds.MaxY, bottomY) : bottomY;
            SetCursorAndDummy(bounds.MinX, finalY);
        }
    }

    Rect VBox::Slot(SizeSpec height)
    {
        releasePushedSlot();

        const Rect inner = bounds.Inset(style.PaddingX, style.PaddingY);
        const float clampedGap = std::max(style.Gap, 0.0f);

        if (slotCount > 0)
        {
            cursorY += clampedGap;
        }

        const float slotHeight = ResolveSlotHeight(height, inner, cursorY, hasExplicitBounds);
        const Rect slotRect{ inner.MinX, cursorY, inner.MaxX, cursorY + slotHeight };
        cursorY = slotRect.MaxY;
        maxReachedY = std::max(maxReachedY, cursorY);
        ++slotCount;

        if (!hasExplicitBounds)
        {
            bounds.MaxY = std::max(bounds.MaxY, maxReachedY + style.PaddingY);
        }

        PushScopeRect(slotRect);
        hasPushedSlotScope = true;

        if (HasActiveImGuiWindow())
        {
            ImGui::SetCursorScreenPos(ImVec2{ slotRect.MinX, slotRect.MinY });
            ImGui::PushItemWidth(std::max(slotRect.Width(), 1.0f));
            hasPushedItemWidth = true;
        }

        return slotRect;
    }

    void VBox::Spacer(float pixels)
    {
        releasePushedSlot();

        const float delta = std::max(pixels, 0.0f);
        cursorY += delta;
        maxReachedY = std::max(maxReachedY, cursorY);

        if (!hasExplicitBounds)
        {
            bounds.MaxY = std::max(bounds.MaxY, maxReachedY + style.PaddingY);
        }

        if (HasActiveImGuiWindow())
        {
            const Rect inner = bounds.Inset(style.PaddingX, style.PaddingY);
            SetCursorAndDummy(inner.MinX, cursorY);
        }
    }

    void VBox::Divider()
    {
        releasePushedSlot();

        constexpr float k_MinDividerHeight = 4.0f;
        const Rect inner = bounds.Inset(style.PaddingX, style.PaddingY);
        const float dividerHeight = std::max(style.Gap, k_MinDividerHeight);
        const float lineY = cursorY + 0.5f * dividerHeight;

        if (HasActiveImGuiWindow())
        {
            const uint32_t color =
                style.BorderColor != 0u ? style.BorderColor : ImGui::GetColorU32(ImGuiCol_Separator);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2{ inner.MinX, lineY },
                ImVec2{ inner.MaxX, lineY },
                color,
                1.0f);
        }

        cursorY += dividerHeight;
        maxReachedY = std::max(maxReachedY, cursorY);

        if (!hasExplicitBounds)
        {
            bounds.MaxY = std::max(bounds.MaxY, maxReachedY + style.PaddingY);
        }

        if (HasActiveImGuiWindow())
        {
            SetCursorAndDummy(inner.MinX, cursorY);
        }
    }

    const Rect& VBox::Bounds() const noexcept
    {
        return bounds;
    }

    float VBox::CursorY() const noexcept
    {
        return cursorY;
    }

    void VBox::initFromBounds(const Rect& _bounds, const BoxStyle& _style)
    {
        bounds = _bounds;
        style = _style;
        const Rect inner = bounds.Inset(style.PaddingX, style.PaddingY);
        cursorY = inner.MinY;
        maxReachedY = inner.MinY;
        slotCount = 0;
        hasPushedItemWidth = false;
        hasPushedSlotScope = false;

        if (hasExplicitBounds)
        {
            DrawBoxDecoration(bounds, style);
        }
    }

    void VBox::releasePushedSlot() noexcept
    {
        if (hasPushedItemWidth)
        {
            if (HasActiveImGuiWindow())
            {
                ImGui::PopItemWidth();
            }

            hasPushedItemWidth = false;
        }

        if (hasPushedSlotScope)
        {
            PopScopeRect();
            hasPushedSlotScope = false;
        }
    }

    HBox::HBox(float _height, std::span<const SizeSpec> _columns, const BoxStyle& _style)
    {
        Rect available = CurrentAvailableRect();

        if (_height > 0.0f)
        {
            available.MaxY = available.MinY + _height;
        }

        initFromBounds(available, _columns, _style);
    }

    HBox::HBox(const Rect& _bounds, std::span<const SizeSpec> _columns, const BoxStyle& _style)
    {
        initFromBounds(_bounds, _columns, _style);
    }

    HBox::~HBox()
    {
        releasePushedColumn();

        if (HasActiveImGuiWindow())
        {
            SetCursorAndDummy(bounds.MinX, bounds.MaxY);
        }
    }

    Rect HBox::Next()
    {
        releasePushedColumn();

        if (nextIndex >= static_cast<int32_t>(std::ssize(columnRects)))
        {
            ++nextIndex;
            return Rect{};
        }

        const Rect columnRect = columnRects[static_cast<size_t>(nextIndex)];
        ++nextIndex;

        PushScopeRect(columnRect);
        hasPushedSlotScope = true;

        if (HasActiveImGuiWindow())
        {
            ImGui::SetCursorScreenPos(ImVec2{ columnRect.MinX, columnRect.MinY });
            ImGui::PushItemWidth(std::max(columnRect.Width(), 1.0f));
            hasPushedItemWidth = true;
        }

        return columnRect;
    }

    int32_t HBox::ColumnCount() const noexcept
    {
        return static_cast<int32_t>(std::ssize(columnRects));
    }

    void HBox::initFromBounds(const Rect& _bounds,
                              std::span<const SizeSpec> _columns,
                              const BoxStyle& _style)
    {
        static_assert(decltype(columnRects)::k_UseFixedInlineCopy);

        bounds = _bounds;
        nextIndex = 0;
        hasPushedItemWidth = false;
        hasPushedSlotScope = false;
        columnRects.resize(_columns.size());
        SolveHorizontalRects(bounds, _columns, _style, columnRects);
        DrawBoxDecoration(bounds, _style);
    }

    void HBox::releasePushedColumn() noexcept
    {
        if (hasPushedItemWidth)
        {
            if (HasActiveImGuiWindow())
            {
                ImGui::PopItemWidth();
            }

            hasPushedItemWidth = false;
        }

        if (hasPushedSlotScope)
        {
            PopScopeRect();
            hasPushedSlotScope = false;
        }
    }

    Grid::Grid(int32_t _columns, float _row_height, float _gap_x, float _gap_y) :
        columns{ std::max(_columns, 1) },
        rowHeight{ std::max(_row_height, 0.0f) },
        gapX{ std::max(_gap_x, 0.0f) },
        gapY{ std::max(_gap_y, 0.0f) }
    {
        initFromBounds(CurrentAvailableRect());
    }

    Grid::Grid(const Rect& _bounds, int32_t _columns, float _row_height, float _gap_x, float _gap_y) :
        columns{ std::max(_columns, 1) },
        rowHeight{ std::max(_row_height, 0.0f) },
        gapX{ std::max(_gap_x, 0.0f) },
        gapY{ std::max(_gap_y, 0.0f) }
    {
        initFromBounds(_bounds);
    }

    Grid::~Grid()
    {
        releasePushedCell();

        if (HasActiveImGuiWindow())
        {
            const int32_t rows = cellCount > 0 ? ((cellCount + columns - 1) / columns) : 0;
            const float totalHeight = rows > 0
                ? (static_cast<float>(rows) * rowHeight + static_cast<float>(rows - 1) * gapY)
                : 0.0f;
            SetCursorAndDummy(bounds.MinX, bounds.MinY + totalHeight);
        }
    }

    Rect Grid::NextCell()
    {
        releasePushedCell();

        const Rect cellRect = SolveGridCellRect(bounds, columns, rowHeight, gapX, gapY, cellCount);
        ++cellCount;

        PushScopeRect(cellRect);
        hasPushedSlotScope = true;

        if (HasActiveImGuiWindow())
        {
            ImGui::SetCursorScreenPos(ImVec2{ cellRect.MinX, cellRect.MinY });
            ImGui::PushItemWidth(std::max(cellRect.Width(), 1.0f));
            hasPushedItemWidth = true;
        }

        return cellRect;
    }

    void Grid::initFromBounds(const Rect& _bounds)
    {
        bounds = _bounds;
        cellCount = 0;
        hasPushedItemWidth = false;
        hasPushedSlotScope = false;
    }

    void Grid::releasePushedCell() noexcept
    {
        if (hasPushedItemWidth)
        {
            if (HasActiveImGuiWindow())
            {
                ImGui::PopItemWidth();
            }

            hasPushedItemWidth = false;
        }

        if (hasPushedSlotScope)
        {
            PopScopeRect();
            hasPushedSlotScope = false;
        }
    }

    Card::Card(std::string_view _title, const UiTheme& _theme, AccentTone _tone) :
        outerBounds{ CurrentAvailableRect() },
        borderColor{ _theme.ToneColor(_tone) },
        backgroundColor{ _theme.Colors.SurfaceTop },
        rounding{ _theme.Spacing.CardRounding },
        paddingY{ _theme.Spacing.Space3 }
    {
        const float paddingX = _theme.Spacing.Space3;
        float headerHeight = 0.0f;

        if (HasActiveImGuiWindow())
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            BeginCardSplitter(drawList);

            if (!_title.empty())
            {
                const float lineHeight = ImGui::GetTextLineHeight();
                const uint32_t titleColor =
                    _tone == AccentTone::Neutral ? _theme.Colors.TextLabel : borderColor;
                const ImVec2 titlePos{ outerBounds.MinX + paddingX, outerBounds.MinY + paddingY };
                drawList->AddText(
                    ImGui::GetFont(),
                    ImGui::GetFontSize(),
                    titlePos,
                    titleColor,
                    _title.data(),
                    _title.data() + _title.size());
                headerHeight = lineHeight + _theme.Spacing.Space2;
            }
        }

        innerBounds = Rect
        {
            outerBounds.MinX + paddingX,
            outerBounds.MinY + paddingY + headerHeight,
            std::max(outerBounds.MinX + paddingX, outerBounds.MaxX - paddingX),
            std::max(outerBounds.MinY + paddingY + headerHeight, outerBounds.MaxY - paddingY),
        };

        PushScopeRect(innerBounds);
        pushedScope = true;

        if (HasActiveImGuiWindow())
        {
            ImGui::SetCursorScreenPos(ImVec2{ innerBounds.MinX, innerBounds.MinY });
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2{ 0.0f, 0.0f });
        }
    }

    Card::~Card()
    {
        if (pushedScope)
        {
            PopScopeRect();
            pushedScope = false;
        }

        if (HasActiveImGuiWindow())
        {
            ImGui::EndGroup();
            const float contentBottom = std::max(innerBounds.MinY, ImGui::GetItemRectMax().y);
            const Rect finalOuter{ outerBounds.MinX, outerBounds.MinY, outerBounds.MaxX, contentBottom + paddingY };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            EndCardSplitter(drawList, finalOuter, backgroundColor, borderColor, rounding);
            SetCursorAndDummy(finalOuter.MinX, finalOuter.MaxY);
        }
    }

    PropertyRow::PropertyRow(std::string_view _label, const UiTheme& _theme, float _height)
    {
        initRow(_label, _theme, nullptr, _height);
    }

    PropertyRow::PropertyRow(std::string_view _label,
                             const UiTheme& _theme,
                             const ImGuiFontSet& _fonts,
                             float _height)
    {
        initRow(_label, _theme, &_fonts, _height);
    }

    PropertyRow::~PropertyRow()
    {
        if (pushedItemWidth)
        {
            if (HasActiveImGuiWindow())
            {
                ImGui::PopItemWidth();
            }

            pushedItemWidth = false;
        }

        if (pushedScope)
        {
            PopScopeRect();
            pushedScope = false;
        }

        if (HasActiveImGuiWindow())
        {
            SetCursorAndDummy(rowBounds.MinX, rowBounds.MaxY);
        }
    }

    const Rect& PropertyRow::ValueRect() const noexcept
    {
        return valueRect;
    }

    void PropertyRow::initRow(std::string_view _label,
                              const UiTheme& _theme,
                              const ImGuiFontSet* _fonts,
                              float _height)
    {
        rowBounds = CurrentAvailableRect();
        const float defaultHeight =
            HasActiveImGuiWindow() ? ImGui::GetFrameHeight() : _theme.Spacing.OptionHeight;
        const float resolvedHeight = _height > 0.0f ? _height : defaultHeight;
        rowBounds.MaxY = rowBounds.MinY + resolvedHeight;

        foundation::SmallVector<SizeSpec, 2> columns;
        static_assert(decltype(columns)::k_UseFixedInlineCopy);
        columns.push_back(SizeSpec::Fixed(_theme.Spacing.LabelColumnWidth));
        columns.push_back(SizeSpec::FillEqual());

        foundation::SmallVector<Rect, 2> rects;
        static_assert(decltype(rects)::k_UseFixedInlineCopy);
        rects.resize(2);

        BoxStyle rowStyle{};
        rowStyle.Gap = _theme.Spacing.Space2;
        SolveHorizontalRects(rowBounds, columns, rowStyle, rects);

        const Rect labelRect = rects[0];
        valueRect = rects[1];

        if (HasActiveImGuiWindow() && !_label.empty())
        {
            bool pushedFont = false;

            if (_fonts != nullptr)
            {
                const ResolvedFont resolved = ResolveFont(*_fonts, _theme, TextRole::Body);
                ImGui::PushFont(resolved.Font, resolved.Size);
                pushedFont = true;
            }

            const float textHeight = ImGui::GetTextLineHeight();
            const float textY = labelRect.MinY + 0.5f * (labelRect.Height() - textHeight);
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetFont(),
                ImGui::GetFontSize(),
                ImVec2{ labelRect.MinX, textY },
                _theme.Colors.TextLabel,
                _label.data(),
                _label.data() + _label.size());

            if (pushedFont)
            {
                ImGui::PopFont();
            }
        }

        PushScopeRect(valueRect);
        pushedScope = true;

        if (HasActiveImGuiWindow())
        {
            ImGui::SetCursorScreenPos(ImVec2{ valueRect.MinX, valueRect.MinY });
            ImGui::PushItemWidth(std::max(valueRect.Width(), 1.0f));
            pushedItemWidth = true;
        }
    }
}

namespace
{
    bool HasActiveImGuiWindow() noexcept
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetCurrentWindowRead() != nullptr;
    }

    foundation::SmallVector<dd::ui::Rect, 16>& ScopeStack() noexcept
    {
        static foundation::SmallVector<dd::ui::Rect, 16> s_stack;
        return s_stack;
    }

    foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& ActiveCardSplitters() noexcept
    {
        static foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8> s_active;
        return s_active;
    }

    foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& FreeCardSplitters() noexcept
    {
        static foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8> s_free;
        return s_free;
    }

    dd::ui::Rect CurrentAvailableRect() noexcept
    {
        foundation::SmallVector<dd::ui::Rect, 16>& stack = ScopeStack();

        if (!stack.empty())
        {
            return stack.back();
        }

        if (!HasActiveImGuiWindow())
        {
            return dd::ui::Rect{};
        }

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        return dd::ui::Rect
        {
            pos.x,
            pos.y,
            pos.x + std::max(avail.x, 0.0f),
            pos.y + std::max(avail.y, 0.0f),
        };
    }

    void PushScopeRect(const dd::ui::Rect& rect) noexcept
    {
        foundation::SmallVector<dd::ui::Rect, 16>& stack = ScopeStack();
        stack.push_back(rect);
        assert(stack.is_inline());
    }

    void PopScopeRect() noexcept
    {
        foundation::SmallVector<dd::ui::Rect, 16>& stack = ScopeStack();
        assert(!stack.empty());
        stack.pop_back();
    }

    void DrawBoxDecoration(const dd::ui::Rect& rect, const dd::ui::BoxStyle& style) noexcept
    {
        if (!HasActiveImGuiWindow())
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 minPos{ rect.MinX, rect.MinY };
        const ImVec2 maxPos{ rect.MaxX, rect.MaxY };

        if (style.BackgroundColor != 0u)
        {
            drawList->AddRectFilled(minPos, maxPos, style.BackgroundColor, style.Rounding);
        }

        if (style.BorderColor != 0u)
        {
            drawList->AddRect(minPos, maxPos, style.BorderColor, style.Rounding, 0, 1.0f);
        }
    }

    void SetCursorAndDummy(float x, float y) noexcept
    {
        ImGui::SetCursorScreenPos(ImVec2{ x, y });
        ImGui::Dummy(ImVec2{ 0.0f, 0.0f });
    }

    void BeginCardSplitter(ImDrawList* draw_list) noexcept
    {
        foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& freePool = FreeCardSplitters();
        std::unique_ptr<ImDrawListSplitter> splitter;

        if (!freePool.empty())
        {
            splitter = std::move(freePool.back());
            freePool.pop_back();
        }
        else
        {
            splitter = std::make_unique<ImDrawListSplitter>();
        }

        splitter->Split(draw_list, 2);
        splitter->SetCurrentChannel(draw_list, 1);

        foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& active = ActiveCardSplitters();
        active.push_back(std::move(splitter));
        assert(active.is_inline());
    }

    void EndCardSplitter(ImDrawList* draw_list,
                         const dd::ui::Rect& rect,
                         uint32_t background_color,
                         uint32_t border_color,
                         float rounding) noexcept
    {
        foundation::SmallVector<std::unique_ptr<ImDrawListSplitter>, 8>& active = ActiveCardSplitters();
        assert(!active.empty());
        std::unique_ptr<ImDrawListSplitter> splitter = std::move(active.back());
        active.pop_back();

        splitter->SetCurrentChannel(draw_list, 0);
        const ImVec2 minPos{ rect.MinX, rect.MinY };
        const ImVec2 maxPos{ rect.MaxX, rect.MaxY };

        if (background_color != 0u)
        {
            draw_list->AddRectFilled(minPos, maxPos, background_color, rounding);
        }

        if (border_color != 0u)
        {
            draw_list->AddRect(minPos, maxPos, border_color, rounding, 0, 1.0f);
        }

        splitter->Merge(draw_list);
        FreeCardSplitters().push_back(std::move(splitter));
    }

    float ResolveSlotHeight(const dd::ui::SizeSpec& height,
                            const dd::ui::Rect& inner,
                            float cursor_y,
                            bool has_explicit_bounds) noexcept
    {
        const float defaultHeight = HasActiveImGuiWindow() ? ImGui::GetFrameHeight() : 0.0f;

        switch (height.Kind)
        {
        case dd::ui::SizeKind::Fixed:
            return std::max(height.Value, 0.0f);
        case dd::ui::SizeKind::Fraction:
            return std::max(height.Value, 0.0f) * inner.Height();
        case dd::ui::SizeKind::Fit:
            return height.Value > 0.0f ? height.Value : defaultHeight;
        case dd::ui::SizeKind::Fill:
            if (inner.MaxY > cursor_y)
            {
                return inner.MaxY - cursor_y;
            }
            return has_explicit_bounds ? 0.0f : defaultHeight;
        case dd::ui::SizeKind::Invalid:
            break;
        }

        return defaultHeight;
    }
}
