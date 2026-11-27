#pragma once
#ifndef DIAMOND_DOGS_EDITOR_UI_INSPECTOR_HPP
#define DIAMOND_DOGS_EDITOR_UI_INSPECTOR_HPP
#include "EditProposal.hpp"
#include "UiTheme.hpp"
#include "reflect/TypeInfo.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiFontSet;

namespace dd::ui
{
    struct FieldDecoration
    {
        std::string SecondaryText;
        std::string HintText;
    };

    using FieldDecoratorFn = std::optional<FieldDecoration> (*)(std::string_view field_path,
                                                                const void* user_data);

    struct FieldDecorator
    {
        FieldDecoratorFn Callback{ nullptr };
        const void* UserData{ nullptr };
    };

    inline constexpr FieldDecorator k_NoDecorator{};

    class Inspector;

    struct DrawContext
    {
        const Inspector& Owner;
        const UiTheme& Theme;
        const ImGuiFontSet& Fonts;
        EditProposalSink& Proposals;
        uint64_t TargetId{ 0u };
        std::string_view FieldPath;
        const foundation::reflect::FieldMetadata* Metadata{ nullptr };
        bool ReadOnly{ false };
        FieldDecorator Decorator{};

        void DrawDefaultFields(const foundation::reflect::TypeInfo& type,
                               const void* object,
                               FieldDecorator decorator) const;
        void EmitScalarProposal(OwnedScalarValue value, CommitKind commit) const;
        void EmitOpProposal(ProposalOp op, int64_t int_argument, CommitKind commit) const;
    };

    using ErasedDrawerFn = void (*)(const DrawContext& context,
                                    const foundation::reflect::TypeInfo& type,
                                    const void* value);

    /** @brief Maps UI widget tags and reflected type names to immediate-mode drawers. */
    class WidgetRegistry
    {
    public:
        WidgetRegistry();
        ~WidgetRegistry();

        void RegisterTag(std::string_view tag, ErasedDrawerFn drawer);
        void RegisterTypeName(std::string_view type_name, ErasedDrawerFn drawer);
        void RegisterBuiltins();

        [[nodiscard]] ErasedDrawerFn Resolve(const foundation::reflect::TypeInfo& type,
                                             const foundation::reflect::FieldMetadata* metadata) const noexcept;
        [[nodiscard]] ErasedDrawerFn ResolveKindFallback(
            foundation::reflect::TypeKind kind) const noexcept;

    private:
        struct Entry
        {
            std::string Key;
            ErasedDrawerFn Drawer{ nullptr };
        };

        std::vector<Entry> tagDrawers;
        std::vector<Entry> typeNameDrawers;
    };

    /** @brief Walks reflected values read-only and emits deferred EditProposal records on edits. */
    class Inspector
    {
    public:
        Inspector(const WidgetRegistry& _registry, const UiTheme& _theme, const ImGuiFontSet& _fonts);

        [[nodiscard]] const WidgetRegistry& Registry() const noexcept;

        void DrawErased(uint64_t target_id,
                        std::string_view root_label,
                        const foundation::reflect::TypeInfo& type,
                        const void* object,
                        EditProposalSink& proposals,
                        FieldDecorator decorator) const;

        template<typename T>
        void Draw(uint64_t target_id,
                  std::string_view root_label,
                  const T& object,
                  EditProposalSink& proposals,
                  FieldDecorator decorator) const
        {
            DrawErased(target_id,
                       root_label,
                       foundation::reflect::GetTypeInfo<T>(),
                       &object,
                       proposals,
                       decorator);
        }

    private:
        const WidgetRegistry& registry;
        const UiTheme& theme;
        const ImGuiFontSet& fonts;
    };
}

#endif // !DIAMOND_DOGS_EDITOR_UI_INSPECTOR_HPP
