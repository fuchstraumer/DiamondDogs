#include "Inspector.hpp"
#include "ImGuiModule.hpp"
#include "UiControls.hpp"
#include "UiLayout.hpp"
#include "containers/SmallVector.hpp"
#include "imgui.h"
#include "reflect/Describe.hpp"
#include "reflect/ValueAccess.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <string>

namespace
{
    [[nodiscard]] bool HasActiveImGuiWindow() noexcept;
    [[nodiscard]] bool IsContextReadOnly(const dd::ui::DrawContext& context) noexcept;
    void PushPathId(std::string_view path) noexcept;
    void CopyNullTerminated(std::string_view source, foundation::SmallVector<char, 128>& out) noexcept;
    void CopySmallFormat(std::string_view source,
                         const char* fallback,
                         foundation::SmallVector<char, 64>& out) noexcept;
    [[nodiscard]] std::string_view LeafLabel(const dd::ui::DrawContext& context) noexcept;
    [[nodiscard]] std::string BuildChildPath(std::string_view prefix, std::string_view field_name);
    [[nodiscard]] std::string BuildIndexPath(std::string_view prefix, int64_t index);
    void EmitPathScalarProposal(const dd::ui::DrawContext& context,
                                std::string_view path,
                                dd::ui::OwnedScalarValue value,
                                dd::ui::CommitKind commit);

    struct NumericBounds
    {
        bool HasFiniteBounds{ false };
        double Min{ 0.0 };
        double Max{ 0.0 };
        bool Logarithmic{ false };
    };

    [[nodiscard]] NumericBounds ResolveNumericBounds(
        const foundation::reflect::FieldMetadata* metadata) noexcept;
    void FormatFloatValue(double value,
                          std::string_view ui_format,
                          foundation::SmallVector<char, 64>& out);
    [[nodiscard]] ImGuiDataType IntegerImGuiType(const foundation::reflect::TypeInfo& type) noexcept;
    [[nodiscard]] int64_t ReadInt64FromStorage(const foundation::reflect::TypeInfo& type,
                                               uint64_t storage) noexcept;
    [[nodiscard]] uint64_t WriteBitsToStorage(const foundation::reflect::TypeInfo& type,
                                              int64_t bits) noexcept;
    [[nodiscard]] dd::ui::OwnedScalarValue MakeOwnedInteger(const foundation::reflect::TypeInfo& type,
                                                            uint64_t storage) noexcept;
    void DrawFieldDecoration(const dd::ui::DrawContext& context, std::string_view path);

    template<typename Integer>
    [[nodiscard]] int64_t ReadTypedFromStorage(uint64_t storage) noexcept
    {
        Integer val = 0;
        std::memcpy(&val, &storage, sizeof(val));
        return static_cast<int64_t>(val);
    }

    template<typename Integer>
    [[nodiscard]] uint64_t WriteTypedToStorage(int64_t bits) noexcept
    {
        uint64_t storage = 0u;
        const Integer val = static_cast<Integer>(bits);
        std::memcpy(&storage, &val, sizeof(val));
        return storage;
    }

    template<typename EntryList>
    void UpsertRegistryEntry(EntryList& entries, std::string_view key, dd::ui::ErasedDrawerFn drawer)
    {
        if (key.empty())
        {
            return;
        }

        for (auto& entry : entries)
        {
            if (entry.Key == key)
            {
                entry.Drawer = drawer;
                return;
            }
        }

        entries.push_back(typename EntryList::value_type{ std::string{ key }, drawer });
    }

    template<typename EntryList>
    [[nodiscard]] dd::ui::ErasedDrawerFn FindRegistryEntry(const EntryList& entries,
                                                           std::string_view key) noexcept
    {
        if (key.empty())
        {
            return nullptr;
        }

        for (const auto& entry : entries)
        {
            if (entry.Key == key)
            {
                return entry.Drawer;
            }
        }

        return nullptr;
    }

    void DrawBool(const dd::ui::DrawContext& context,
                  const foundation::reflect::TypeInfo& type,
                  const void* value);
    void DrawInt(const dd::ui::DrawContext& context,
                 const foundation::reflect::TypeInfo& type,
                 const void* value);
    void DrawFloat(const dd::ui::DrawContext& context,
                   const foundation::reflect::TypeInfo& type,
                   const void* value);
    void DrawString(const dd::ui::DrawContext& context,
                    const foundation::reflect::TypeInfo& type,
                    const void* value);
    void DrawEnum(const dd::ui::DrawContext& context,
                  const foundation::reflect::TypeInfo& type,
                  const void* value);
    void DrawStruct(const dd::ui::DrawContext& context,
                    const foundation::reflect::TypeInfo& type,
                    const void* value);
    void DrawStructFields(const dd::ui::DrawContext& context,
                          const foundation::reflect::TypeInfo& type,
                          const void* value);
    void DrawCompactSequenceScalar(const dd::ui::DrawContext& context,
                                   const foundation::reflect::TypeInfo& elem_type,
                                   const void* elem_value,
                                   int64_t index,
                                   bool read_only);
    void DrawSequence(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value);
    void DrawOptional(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value);
    void DrawVariant(const dd::ui::DrawContext& context,
                     const foundation::reflect::TypeInfo& type,
                     const void* value);

    void DrawAngleSliderTag(const dd::ui::DrawContext& context,
                            const foundation::reflect::TypeInfo& type,
                            const void* value);
    void DrawPercentTag(const dd::ui::DrawContext& context,
                        const foundation::reflect::TypeInfo& type,
                        const void* value);
    void DrawColorTag(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value,
                      bool has_alpha);
    void DrawColorRgbTag(const dd::ui::DrawContext& context,
                         const foundation::reflect::TypeInfo& type,
                         const void* value);
    void DrawColorRgbaTag(const dd::ui::DrawContext& context,
                          const foundation::reflect::TypeInfo& type,
                          const void* value);
}

namespace dd::ui
{
    void DrawContext::DrawDefaultFields(const foundation::reflect::TypeInfo& type,
                                        const void* object,
                                        FieldDecorator decorator) const
    {
        if (object == nullptr || type.Kind != foundation::reflect::TypeKind::Struct)
        {
            return;
        }

        const DrawContext childContext
        {
            .Owner = Owner,
            .Theme = Theme,
            .Fonts = Fonts,
            .Proposals = Proposals,
            .TargetId = TargetId,
            .FieldPath = FieldPath,
            .Metadata = Metadata,
            .ReadOnly = ReadOnly,
            .Decorator = decorator,
        };
        DrawStructFields(childContext, type, object);
    }

    void DrawContext::EmitScalarProposal(OwnedScalarValue value, CommitKind commit) const
    {
        if (IsContextReadOnly(*this))
        {
            return;
        }

        Proposals.Push(EditProposal
        {
            .TargetId = TargetId,
            .Path = std::string{ FieldPath },
            .Op = ProposalOp::SetScalar,
            .Commit = commit,
            .Value = std::move(value),
            .IntArgument = 0,
        });
    }

    void DrawContext::EmitOpProposal(ProposalOp op, int64_t int_argument, CommitKind commit) const
    {
        if (IsContextReadOnly(*this))
        {
            return;
        }

        Proposals.Push(EditProposal
        {
            .TargetId = TargetId,
            .Path = std::string{ FieldPath },
            .Op = op,
            .Commit = commit,
            .Value = {},
            .IntArgument = int_argument,
        });
    }

    WidgetRegistry::WidgetRegistry() = default;
    WidgetRegistry::~WidgetRegistry() = default;

    void WidgetRegistry::RegisterTag(std::string_view tag, ErasedDrawerFn drawer)
    {
        UpsertRegistryEntry(tagDrawers, tag, drawer);
    }

    void WidgetRegistry::RegisterTypeName(std::string_view type_name, ErasedDrawerFn drawer)
    {
        UpsertRegistryEntry(typeNameDrawers, type_name, drawer);
    }

    void WidgetRegistry::RegisterBuiltins()
    {
        RegisterTag("AngleSlider", &DrawAngleSliderTag);
        RegisterTag("ColorRgb", &DrawColorRgbTag);
        RegisterTag("ColorRgba", &DrawColorRgbaTag);
        RegisterTag("Percent", &DrawPercentTag);
    }

    ErasedDrawerFn WidgetRegistry::Resolve(const foundation::reflect::TypeInfo& type,
                                           const foundation::reflect::FieldMetadata* metadata) const noexcept
    {
        if (metadata != nullptr && !metadata->Ui.Widget.empty())
        {
            const ErasedDrawerFn tagDrawer = FindRegistryEntry(tagDrawers, metadata->Ui.Widget);

            if (tagDrawer != nullptr)
            {
                return tagDrawer;
            }
        }

        if (!type.UiWidget.empty())
        {
            const ErasedDrawerFn typeTagDrawer = FindRegistryEntry(tagDrawers, type.UiWidget);

            if (typeTagDrawer != nullptr)
            {
                return typeTagDrawer;
            }
        }

        if (!type.Name.empty())
        {
            const ErasedDrawerFn namedDrawer = FindRegistryEntry(typeNameDrawers, type.Name);

            if (namedDrawer != nullptr)
            {
                return namedDrawer;
            }
        }

        return ResolveKindFallback(type.Kind);
    }

    ErasedDrawerFn WidgetRegistry::ResolveKindFallback(foundation::reflect::TypeKind kind) const noexcept
    {
        switch (kind)
        {
        case foundation::reflect::TypeKind::Bool:
            return &DrawBool;
        case foundation::reflect::TypeKind::Int:
            return &DrawInt;
        case foundation::reflect::TypeKind::Float32:
        case foundation::reflect::TypeKind::Float64:
            return &DrawFloat;
        case foundation::reflect::TypeKind::String:
            return &DrawString;
        case foundation::reflect::TypeKind::Enum:
            return &DrawEnum;
        case foundation::reflect::TypeKind::Struct:
            return &DrawStruct;
        case foundation::reflect::TypeKind::Sequence:
            return &DrawSequence;
        case foundation::reflect::TypeKind::Optional:
            return &DrawOptional;
        case foundation::reflect::TypeKind::Variant:
            return &DrawVariant;
        case foundation::reflect::TypeKind::Invalid:
            return nullptr;
        }

        return nullptr;
    }

    Inspector::Inspector(const WidgetRegistry& _registry,
                         const UiTheme& _theme,
                         const ImGuiFontSet& _fonts) :
        registry{ _registry },
        theme{ _theme },
        fonts{ _fonts }
    {}

    const WidgetRegistry& Inspector::Registry() const noexcept
    {
        return registry;
    }

    void Inspector::DrawErased(uint64_t target_id,
                               std::string_view root_label,
                               const foundation::reflect::TypeInfo& type,
                               const void* object,
                               EditProposalSink& proposals,
                               FieldDecorator decorator) const
    {
        if (object == nullptr || type.Kind == foundation::reflect::TypeKind::Invalid
            || !HasActiveImGuiWindow())
        {
            return;
        }

        if (!root_label.empty())
        {
            DrawSectionHeader(root_label, theme, fonts);
        }

        const int rootIdSeed = static_cast<int>(target_id ^ (target_id >> 32));
        ImGui::PushID(rootIdSeed);

        if (!root_label.empty())
        {
            PushPathId(root_label);
        }

        const DrawContext rootContext
        {
            .Owner = *this,
            .Theme = theme,
            .Fonts = fonts,
            .Proposals = proposals,
            .TargetId = target_id,
            .FieldPath = std::string_view{},
            .Metadata = nullptr,
            .ReadOnly = false,
            .Decorator = decorator,
        };

        const ErasedDrawerFn drawer = registry.Resolve(type, nullptr);

        if (drawer != nullptr)
        {
            drawer(rootContext, type, object);
        }

        if (!root_label.empty())
        {
            ImGui::PopID();
        }

        ImGui::PopID();
    }
}

namespace
{
    bool HasActiveImGuiWindow() noexcept
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetCurrentWindowRead() != nullptr;
    }

    bool IsContextReadOnly(const dd::ui::DrawContext& context) noexcept
    {
        return context.ReadOnly || (context.Metadata != nullptr && context.Metadata->ReadOnly);
    }

    void PushPathId(std::string_view path) noexcept
    {
        if (path.empty())
        {
            ImGui::PushID("##root");
            return;
        }

        ImGui::PushID(path.data(), path.data() + path.size());
    }

    void CopyNullTerminated(std::string_view source, foundation::SmallVector<char, 128>& out) noexcept
    {
        out.clear();
        const size_t maxChars = 127;
        const size_t count = std::min(source.size(), maxChars);
        out.resize(count + 1, '\0');

        if (count > 0)
        {
            std::memcpy(out.data(), source.data(), count);
        }

        out[count] = '\0';
    }

    void CopySmallFormat(std::string_view source,
                         const char* fallback,
                         foundation::SmallVector<char, 64>& out) noexcept
    {
        const std::string_view chosen = source.empty() ? std::string_view{ fallback } : source;
        out.clear();
        const size_t maxChars = 63;
        const size_t count = std::min(chosen.size(), maxChars);
        out.resize(count + 1, '\0');

        if (count > 0)
        {
            std::memcpy(out.data(), chosen.data(), count);
        }

        out[count] = '\0';
    }

    std::string_view LeafLabel(const dd::ui::DrawContext& context) noexcept
    {
        if (!context.FieldPath.empty() && context.FieldPath.back() == ']')
        {
            const size_t openBracket = context.FieldPath.rfind('[');

            if (openBracket != std::string_view::npos)
            {
                return context.FieldPath.substr(openBracket);
            }
        }

        if (context.Metadata != nullptr && !context.Metadata->Ui.Label.empty())
        {
            return context.Metadata->Ui.Label;
        }

        const size_t dot = context.FieldPath.rfind('.');

        if (dot != std::string_view::npos)
        {
            return context.FieldPath.substr(dot + 1);
        }

        return context.FieldPath;
    }

    std::string BuildChildPath(std::string_view prefix, std::string_view field_name)
    {
        if (prefix.empty())
        {
            return std::string{ field_name };
        }

        return std::format("{}.{}", prefix, field_name);
    }

    std::string BuildIndexPath(std::string_view prefix, int64_t index)
    {
        return std::format("{}[{}]", prefix, index);
    }

    void EmitPathScalarProposal(const dd::ui::DrawContext& context,
                                std::string_view path,
                                dd::ui::OwnedScalarValue value,
                                dd::ui::CommitKind commit)
    {
        if (IsContextReadOnly(context))
        {
            return;
        }

        context.Proposals.Push(dd::ui::EditProposal
        {
            .TargetId = context.TargetId,
            .Path = std::string{ path },
            .Op = dd::ui::ProposalOp::SetScalar,
            .Commit = commit,
            .Value = std::move(value),
            .IntArgument = 0,
        });
    }

    NumericBounds ResolveNumericBounds(const foundation::reflect::FieldMetadata* metadata) noexcept
    {
        if (metadata == nullptr)
        {
            return NumericBounds{};
        }

        const bool isLogarithmic = metadata->Ui.Scale == foundation::reflect::UiScale::Logarithmic;

        if (metadata->Ui.Slider.has_value())
        {
            const double minVal = metadata->Ui.Slider->Min;
            const double maxVal = metadata->Ui.Slider->Max;

            if (std::isfinite(minVal) && std::isfinite(maxVal) && minVal < maxVal)
            {
                return NumericBounds
                {
                    .HasFiniteBounds = true,
                    .Min = minVal,
                    .Max = maxVal,
                    .Logarithmic = isLogarithmic,
                };
            }
        }

        if (metadata->Range.has_value())
        {
            const double minVal = metadata->Range->Min;
            const double maxVal = metadata->Range->Max;

            if (std::isfinite(minVal) && std::isfinite(maxVal) && minVal < maxVal)
            {
                return NumericBounds
                {
                    .HasFiniteBounds = true,
                    .Min = minVal,
                    .Max = maxVal,
                    .Logarithmic = isLogarithmic,
                };
            }
        }

        return NumericBounds{};
    }

    void FormatFloatValue(double value,
                          std::string_view ui_format,
                          foundation::SmallVector<char, 64>& out)
    {
        out.resize(64, '\0');

        // Use literal format strings with snprintf so Clang's -Wformat-security stays clean.
        if (ui_format.size() == 4 && ui_format[0] == '%' && ui_format[1] == '.'
            && ui_format[2] >= '0' && ui_format[2] <= '6' && ui_format[3] == 'f')
        {
            const int precision = ui_format[2] - '0';
            std::snprintf(out.data(), out.size(), "%.*f", precision, value);
            return;
        }

        std::snprintf(out.data(), out.size(), "%.4g", value);
    }

    ImGuiDataType IntegerImGuiType(const foundation::reflect::TypeInfo& type) noexcept
    {
        switch (type.IntegerWidthBytes)
        {
        case 1:
            return type.IntegerIsSigned ? ImGuiDataType_S8 : ImGuiDataType_U8;
        case 2:
            return type.IntegerIsSigned ? ImGuiDataType_S16 : ImGuiDataType_U16;
        case 4:
            return type.IntegerIsSigned ? ImGuiDataType_S32 : ImGuiDataType_U32;
        default:
            return type.IntegerIsSigned ? ImGuiDataType_S64 : ImGuiDataType_U64;
        }
    }

    int64_t ReadInt64FromStorage(const foundation::reflect::TypeInfo& type, uint64_t storage) noexcept
    {
        switch (type.IntegerWidthBytes)
        {
        case 1:
            return type.IntegerIsSigned ? ReadTypedFromStorage<int8_t>(storage)
                                        : ReadTypedFromStorage<uint8_t>(storage);
        case 2:
            return type.IntegerIsSigned ? ReadTypedFromStorage<int16_t>(storage)
                                        : ReadTypedFromStorage<uint16_t>(storage);
        case 4:
            return type.IntegerIsSigned ? ReadTypedFromStorage<int32_t>(storage)
                                        : ReadTypedFromStorage<uint32_t>(storage);
        default:
            return ReadTypedFromStorage<int64_t>(storage);
        }
    }

    uint64_t WriteBitsToStorage(const foundation::reflect::TypeInfo& type, int64_t bits) noexcept
    {
        switch (type.IntegerWidthBytes)
        {
        case 1:
            return WriteTypedToStorage<uint8_t>(bits);
        case 2:
            return WriteTypedToStorage<uint16_t>(bits);
        case 4:
            return WriteTypedToStorage<uint32_t>(bits);
        default:
            return WriteTypedToStorage<uint64_t>(bits);
        }
    }

    dd::ui::OwnedScalarValue MakeOwnedInteger(const foundation::reflect::TypeInfo& type,
                                              uint64_t storage) noexcept
    {
        if (type.IntegerIsSigned)
        {
            return ReadInt64FromStorage(type, storage);
        }

        if (type.IntegerWidthBytes == 8)
        {
            return storage;
        }

        return static_cast<uint64_t>(ReadInt64FromStorage(type, storage));
    }

    void DrawFieldDecoration(const dd::ui::DrawContext& context, std::string_view path)
    {
        if (context.Decorator.Callback == nullptr)
        {
            return;
        }

        const std::optional<dd::ui::FieldDecoration> decoration =
            context.Decorator.Callback(path, context.Decorator.UserData);

        if (!decoration.has_value())
        {
            return;
        }

        if (!decoration->SecondaryText.empty())
        {
            dd::ui::DrawNote(decoration->SecondaryText, context.Theme, context.Fonts);
        }

        if (!decoration->HintText.empty())
        {
            dd::ui::DrawNote(decoration->HintText, context.Theme, context.Fonts);
        }
    }

    void DrawBool(const dd::ui::DrawContext& context,
                  const foundation::reflect::TypeInfo& /*type*/,
                  const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        bool editedValue = *static_cast<const bool*>(value);
        const std::string_view label = LeafLabel(context);
        bool changed = false;

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, context.Theme.Spacing.OptionHeight);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);
            changed = dd::ui::DrawTelemetryToggle("##bool",
                                                  "",
                                                  "OFF",
                                                  "ON",
                                                  &editedValue,
                                                  context.Theme,
                                                  context.Fonts);
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if (changed && !readOnly)
        {
            context.EmitScalarProposal(editedValue, dd::ui::CommitKind::Commit);
        }
    }

    void DrawInt(const dd::ui::DrawContext& context,
                 const foundation::reflect::TypeInfo& type,
                 const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const int64_t bits = foundation::reflect::ReadIntegerBits(type, value);
        uint64_t storage = WriteBitsToStorage(type, bits);
        const ImGuiDataType dataType = IntegerImGuiType(type);
        const NumericBounds bounds = ResolveNumericBounds(context.Metadata);
        const std::string_view label = LeafLabel(context);

        foundation::SmallVector<char, 64> formatBuf;
        const std::string_view uiFormat =
            context.Metadata != nullptr ? context.Metadata->Ui.Format : std::string_view{};
        CopySmallFormat(uiFormat, type.IntegerIsSigned ? "%lld" : "%llu", formatBuf);
        const char* fmtPtr = uiFormat.empty() ? nullptr : formatBuf.data();

        bool changed = false;
        bool deactivated = false;

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);

            if (bounds.HasFiniteBounds)
            {
                const int64_t minInt = static_cast<int64_t>(std::llround(bounds.Min));
                const int64_t maxInt = static_cast<int64_t>(std::llround(bounds.Max));
                const uint64_t minStorage = WriteBitsToStorage(type, minInt);
                const uint64_t maxStorage = WriteBitsToStorage(type, maxInt);
                const ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp
                    | (bounds.Logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
                changed = ImGui::DragScalar("##int",
                                            dataType,
                                            &storage,
                                            1.0f,
                                            &minStorage,
                                            &maxStorage,
                                            fmtPtr,
                                            flags);
            }
            else
            {
                const uint64_t stepOne = WriteBitsToStorage(type, 1);
                const uint64_t stepTen = WriteBitsToStorage(type, 10);
                changed = ImGui::InputScalar("##int",
                                             dataType,
                                             &storage,
                                             &stepOne,
                                             &stepTen,
                                             fmtPtr,
                                             ImGuiInputTextFlags_None);
            }

            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if ((changed || deactivated) && !readOnly)
        {
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
            context.EmitScalarProposal(MakeOwnedInteger(type, storage), commit);
        }
    }

    void DrawFloat(const dd::ui::DrawContext& context,
                   const foundation::reflect::TypeInfo& type,
                   const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const bool isFloat32 = type.Kind == foundation::reflect::TypeKind::Float32;
        const double current = isFloat32 ? static_cast<double>(*static_cast<const float*>(value))
                                         : *static_cast<const double*>(value);
        const NumericBounds bounds = ResolveNumericBounds(context.Metadata);
        const std::string_view label = LeafLabel(context);
        const std::string_view uiFormat =
            context.Metadata != nullptr ? context.Metadata->Ui.Format : std::string_view{};

        if (!label.empty() && bounds.HasFiniteBounds)
        {
            float sliderValue = static_cast<float>(current);
            foundation::SmallVector<char, 64> valueTextBuf;
            FormatFloatValue(current, uiFormat, valueTextBuf);

            const dd::ui::SliderParams params
            {
                .Id = "##float_slider",
                .Label = label,
                .Hint = context.Metadata != nullptr ? context.Metadata->Doc : std::string_view{},
                .ValueText = std::string_view{ valueTextBuf.data() },
                .UnitText = context.Metadata != nullptr ? context.Metadata->Unit : std::string_view{},
                .Min = static_cast<float>(bounds.Min),
                .Max = static_cast<float>(bounds.Max),
                .Logarithmic = bounds.Logarithmic,
            };

            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);
            const bool changed = dd::ui::DrawArtistSlider(params, &sliderValue, context.Theme, context.Fonts);
            const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImGui::PopID();

            if ((changed || deactivated) && !readOnly)
            {
                const dd::ui::CommitKind commit =
                    deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
                context.EmitScalarProposal(static_cast<double>(sliderValue), commit);
            }

            return;
        }

        foundation::SmallVector<char, 64> formatBuf;
        CopySmallFormat(uiFormat, "%.4g", formatBuf);
        const float stepSpeed =
            (context.Metadata != nullptr && context.Metadata->Ui.Step > 0.0)
                ? static_cast<float>(context.Metadata->Ui.Step)
                : 0.05f;

        float editFloat = static_cast<float>(current);
        double editDouble = current;
        bool changed = false;
        bool deactivated = false;

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);

            if (isFloat32)
            {
                const float minFloat = static_cast<float>(bounds.Min);
                const float maxFloat = static_cast<float>(bounds.Max);
                const float* minPtr = bounds.HasFiniteBounds ? &minFloat : nullptr;
                const float* maxPtr = bounds.HasFiniteBounds ? &maxFloat : nullptr;
                const ImGuiSliderFlags flags = bounds.HasFiniteBounds ? ImGuiSliderFlags_AlwaysClamp : 0;
                changed = ImGui::DragScalar("##float",
                                            ImGuiDataType_Float,
                                            &editFloat,
                                            stepSpeed,
                                            minPtr,
                                            maxPtr,
                                            formatBuf.data(),
                                            flags);
            }
            else
            {
                const double* minPtr = bounds.HasFiniteBounds ? &bounds.Min : nullptr;
                const double* maxPtr = bounds.HasFiniteBounds ? &bounds.Max : nullptr;
                const ImGuiSliderFlags flags = bounds.HasFiniteBounds ? ImGuiSliderFlags_AlwaysClamp : 0;
                changed = ImGui::DragScalar("##double",
                                            ImGuiDataType_Double,
                                            &editDouble,
                                            stepSpeed,
                                            minPtr,
                                            maxPtr,
                                            formatBuf.data(),
                                            flags);
            }

            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if ((changed || deactivated) && !readOnly)
        {
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
            const double emitted = isFloat32 ? static_cast<double>(editFloat) : editDouble;
            context.EmitScalarProposal(emitted, commit);
        }
    }

    void DrawString(const dd::ui::DrawContext& context,
                    const foundation::reflect::TypeInfo& type,
                    const void* value)
    {
        if (value == nullptr || type.ReadString == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context) || type.WriteString == nullptr;
        const std::string_view current = type.ReadString(value);

        foundation::SmallVector<char, 256> textBuffer;
        textBuffer.resize(256, '\0');
        const size_t copyLen = std::min(current.size(), textBuffer.size() - 1);

        if (copyLen > 0)
        {
            std::memcpy(textBuffer.data(), current.data(), copyLen);
        }

        textBuffer[copyLen] = '\0';

        const std::string_view label = LeafLabel(context);
        bool changed = false;
        bool deactivated = false;

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);
            const ImGuiInputTextFlags flags =
                readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;
            changed = ImGui::InputText("##string",
                                       textBuffer.data(),
                                       textBuffer.size(),
                                       flags,
                                       nullptr,
                                       nullptr);
            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if ((changed || deactivated) && !readOnly)
        {
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
            context.EmitScalarProposal(std::string{ textBuffer.data() }, commit);
        }
    }

    void DrawEnum(const dd::ui::DrawContext& context,
                  const foundation::reflect::TypeInfo& type,
                  const void* value)
    {
        if (value == nullptr || type.Cases.empty())
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const int64_t bits = foundation::reflect::ReadIntegerBits(type, value);
        const int32_t caseCount = static_cast<int32_t>(std::ssize(type.Cases));
        int32_t selectedIndex = -1;

        for (int32_t index = 0; index < caseCount; ++index)
        {
            if (type.Cases[static_cast<size_t>(index)].Value == bits)
            {
                selectedIndex = index;
                break;
            }
        }

        const std::string_view label = LeafLabel(context);

        if (caseCount <= 4)
        {
            foundation::SmallVector<std::string_view, 4> options;

            for (int32_t index = 0; index < caseCount; ++index)
            {
                options.push_back(type.Cases[static_cast<size_t>(index)].Name);
            }

            int32_t editedIndex = selectedIndex;
            bool changed = false;

            {
                dd::ui::PropertyRow row(label,
                                        context.Theme,
                                        context.Fonts,
                                        context.Theme.Spacing.OptionHeight);
                PushPathId(context.FieldPath);
                ImGui::BeginDisabled(readOnly);
                changed = dd::ui::DrawSegmentedSelector("##enum_seg",
                                                        options,
                                                        editedIndex,
                                                        caseCount,
                                                        context.Theme,
                                                        context.Fonts);
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            if (changed && !readOnly && editedIndex >= 0 && editedIndex < caseCount)
            {
                context.EmitScalarProposal(std::string{ type.Cases[static_cast<size_t>(editedIndex)].Name },
                                           dd::ui::CommitKind::Commit);
            }

            return;
        }

        const std::string_view preview =
            (selectedIndex >= 0 && selectedIndex < caseCount)
                ? type.Cases[static_cast<size_t>(selectedIndex)].Name
                : std::string_view{ "<invalid>" };
        foundation::SmallVector<char, 128> previewBuf;
        CopyNullTerminated(preview, previewBuf);

        int32_t chosenIndex = -1;

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);

            if (ImGui::BeginCombo("##enum_combo", previewBuf.data(), 0))
            {
                foundation::SmallVector<char, 128> itemBuf;

                for (int32_t index = 0; index < caseCount; ++index)
                {
                    CopyNullTerminated(type.Cases[static_cast<size_t>(index)].Name, itemBuf);
                    const bool isSelected = index == selectedIndex;

                    if (ImGui::Selectable(itemBuf.data(), isSelected))
                    {
                        chosenIndex = index;
                    }

                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if (chosenIndex >= 0 && !readOnly)
        {
            context.EmitScalarProposal(std::string{ type.Cases[static_cast<size_t>(chosenIndex)].Name },
                                       dd::ui::CommitKind::Commit);
        }
    }

    void DrawStruct(const dd::ui::DrawContext& context,
                    const foundation::reflect::TypeInfo& type,
                    const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        const std::string_view label = LeafLabel(context);

        if (label.empty())
        {
            DrawStructFields(context, type, value);
            return;
        }

        foundation::SmallVector<char, 128> headerBuf;
        CopyNullTerminated(label, headerBuf);
        PushPathId(context.FieldPath);
        const bool open = ImGui::CollapsingHeader(headerBuf.data(), ImGuiTreeNodeFlags_DefaultOpen);

        if (open)
        {
            ImGui::Indent();
            DrawStructFields(context, type, value);
            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    void DrawStructFields(const dd::ui::DrawContext& context,
                          const foundation::reflect::TypeInfo& type,
                          const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        std::string_view activeGroup{};
        bool insideGroup = false;
        bool groupOpen = true;
        foundation::SmallVector<char, 128> groupLabelBuf;

        for (const foundation::reflect::FieldInfo& field : type.Fields)
        {
            if (field.Metadata.Hidden)
            {
                continue;
            }

            const std::string_view fieldGroup = field.Metadata.Ui.Group;

            if (fieldGroup != activeGroup)
            {
                if (insideGroup)
                {
                    if (groupOpen)
                    {
                        ImGui::Unindent();
                    }

                    ImGui::PopID();
                    insideGroup = false;
                    groupOpen = true;
                }

                activeGroup = fieldGroup;

                if (!activeGroup.empty())
                {
                    ImGui::PushID(activeGroup.data(), activeGroup.data() + activeGroup.size());
                    CopyNullTerminated(activeGroup, groupLabelBuf);
                    groupOpen = ImGui::CollapsingHeader(groupLabelBuf.data(), ImGuiTreeNodeFlags_DefaultOpen);
                    insideGroup = true;

                    if (groupOpen)
                    {
                        ImGui::Indent();
                    }
                }
            }

            if (insideGroup && !groupOpen)
            {
                continue;
            }

            const std::string childPath = BuildChildPath(context.FieldPath, field.Name);
            const foundation::reflect::TypeInfo& fieldType = field.Type();
            const void* fieldPtr = foundation::reflect::FieldAddress(field, value);
            const dd::ui::DrawContext fieldContext
            {
                .Owner = context.Owner,
                .Theme = context.Theme,
                .Fonts = context.Fonts,
                .Proposals = context.Proposals,
                .TargetId = context.TargetId,
                .FieldPath = childPath,
                .Metadata = &field.Metadata,
                .ReadOnly = context.ReadOnly || field.Metadata.ReadOnly,
                .Decorator = context.Decorator,
            };

            const dd::ui::ErasedDrawerFn drawer =
                context.Owner.Registry().Resolve(fieldType, &field.Metadata);

            if (drawer != nullptr)
            {
                drawer(fieldContext, fieldType, fieldPtr);
            }

            DrawFieldDecoration(context, childPath);
        }

        if (insideGroup)
        {
            if (groupOpen)
            {
                ImGui::Unindent();
            }

            ImGui::PopID();
        }
    }

    void DrawCompactSequenceScalar(const dd::ui::DrawContext& context,
                                   const foundation::reflect::TypeInfo& elem_type,
                                   const void* elem_value,
                                   int64_t index,
                                   bool read_only)
    {
        if (elem_value == nullptr)
        {
            return;
        }

        const NumericBounds bounds = ResolveNumericBounds(context.Metadata);
        bool changed = false;
        bool deactivated = false;
        dd::ui::OwnedScalarValue emittedValue{};

        if (elem_type.Kind == foundation::reflect::TypeKind::Int)
        {
            const int64_t bits = foundation::reflect::ReadIntegerBits(elem_type, elem_value);
            uint64_t storage = WriteBitsToStorage(elem_type, bits);
            const ImGuiDataType dataType = IntegerImGuiType(elem_type);
            const int64_t minInt = static_cast<int64_t>(std::llround(bounds.Min));
            const int64_t maxInt = static_cast<int64_t>(std::llround(bounds.Max));
            const uint64_t minStorage = WriteBitsToStorage(elem_type, minInt);
            const uint64_t maxStorage = WriteBitsToStorage(elem_type, maxInt);
            const void* minPtr = bounds.HasFiniteBounds ? &minStorage : nullptr;
            const void* maxPtr = bounds.HasFiniteBounds ? &maxStorage : nullptr;
            const ImGuiSliderFlags flags = bounds.HasFiniteBounds ? ImGuiSliderFlags_AlwaysClamp : 0;
            changed = ImGui::DragScalar("##elem",
                                        dataType,
                                        &storage,
                                        1.0f,
                                        minPtr,
                                        maxPtr,
                                        nullptr,
                                        flags);
            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            emittedValue = MakeOwnedInteger(elem_type, storage);
        }
        else if (elem_type.Kind == foundation::reflect::TypeKind::Float32)
        {
            float editVal = *static_cast<const float*>(elem_value);
            const float minVal = static_cast<float>(bounds.Min);
            const float maxVal = static_cast<float>(bounds.Max);
            const float* minPtr = bounds.HasFiniteBounds ? &minVal : nullptr;
            const float* maxPtr = bounds.HasFiniteBounds ? &maxVal : nullptr;
            const ImGuiSliderFlags flags = bounds.HasFiniteBounds ? ImGuiSliderFlags_AlwaysClamp : 0;
            changed = ImGui::DragScalar("##elem",
                                        ImGuiDataType_Float,
                                        &editVal,
                                        0.05f,
                                        minPtr,
                                        maxPtr,
                                        "%.3g",
                                        flags);
            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            emittedValue = static_cast<double>(editVal);
        }
        else
        {
            double editVal = *static_cast<const double*>(elem_value);
            const double* minPtr = bounds.HasFiniteBounds ? &bounds.Min : nullptr;
            const double* maxPtr = bounds.HasFiniteBounds ? &bounds.Max : nullptr;
            const ImGuiSliderFlags flags = bounds.HasFiniteBounds ? ImGuiSliderFlags_AlwaysClamp : 0;
            changed = ImGui::DragScalar("##elem",
                                        ImGuiDataType_Double,
                                        &editVal,
                                        0.05f,
                                        minPtr,
                                        maxPtr,
                                        "%.3g",
                                        flags);
            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            emittedValue = editVal;
        }

        if ((changed || deactivated) && !read_only)
        {
            const std::string elemPath = BuildIndexPath(context.FieldPath, index);
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
            EmitPathScalarProposal(context, elemPath, std::move(emittedValue), commit);
        }
    }

    void DrawSequence(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value)
    {
        if (value == nullptr || type.ElementCount == nullptr || type.ElementAt == nullptr
            || type.Element == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const int64_t count = type.ElementCount(value);
        const foundation::reflect::TypeInfo& elemType = type.Element();
        const std::string_view label = LeafLabel(context);

        const bool isCompactNumeric = type.FixedElementCount >= 2 && type.FixedElementCount <= 4
            && count == type.FixedElementCount
            && (elemType.Kind == foundation::reflect::TypeKind::Float32
                || elemType.Kind == foundation::reflect::TypeKind::Float64
                || elemType.Kind == foundation::reflect::TypeKind::Int);

        if (isCompactNumeric)
        {
            const bool elemReadOnly = readOnly || !type.ElementsAreWritable;
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(elemReadOnly);

            foundation::SmallVector<dd::ui::SizeSpec, 4> columns;

            for (int64_t index = 0; index < count; ++index)
            {
                columns.push_back(dd::ui::SizeSpec::FillEqual());
            }

            dd::ui::BoxStyle boxStyle{};
            boxStyle.Gap = context.Theme.Spacing.Space1;
            dd::ui::HBox hbox(row.ValueRect(), columns, boxStyle);

            for (int64_t index = 0; index < count; ++index)
            {
                const dd::ui::Rect cellRect = hbox.Next();

                if (cellRect.IsEmpty())
                {
                    continue;
                }

                ImGui::PushID(static_cast<int>(index));
                DrawCompactSequenceScalar(context,
                                          elemType,
                                          type.ElementAt(value, index),
                                          index,
                                          elemReadOnly);
                ImGui::PopID();
            }

            ImGui::EndDisabled();
            ImGui::PopID();
            return;
        }

        const std::string_view title = label.empty() ? std::string_view{ "Items" } : label;
        const std::string headerText = std::format("{} ({})###seq", title, count);
        foundation::SmallVector<char, 128> headerBuf;
        CopyNullTerminated(headerText, headerBuf);

        PushPathId(context.FieldPath);
        const bool open = ImGui::CollapsingHeader(headerBuf.data(), ImGuiTreeNodeFlags_DefaultOpen);

        if (open)
        {
            ImGui::Indent();

            if (type.ResizeSequence != nullptr && !readOnly)
            {
                ImGui::BeginDisabled(count <= 0);

                if (ImGui::SmallButton("-"))
                {
                    context.EmitOpProposal(dd::ui::ProposalOp::ResizeSequence,
                                           count - 1,
                                           dd::ui::CommitKind::Commit);
                }

                ImGui::EndDisabled();
                ImGui::SameLine();

                if (ImGui::SmallButton("+"))
                {
                    context.EmitOpProposal(dd::ui::ProposalOp::ResizeSequence,
                                           count + 1,
                                           dd::ui::CommitKind::Commit);
                }
            }

            for (int64_t index = 0; index < count; ++index)
            {
                const void* elemPtr = type.ElementAt(value, index);
                const std::string elemPath = BuildIndexPath(context.FieldPath, index);
                const dd::ui::DrawContext elemContext
                {
                    .Owner = context.Owner,
                    .Theme = context.Theme,
                    .Fonts = context.Fonts,
                    .Proposals = context.Proposals,
                    .TargetId = context.TargetId,
                    .FieldPath = elemPath,
                    .Metadata = context.Metadata,
                    .ReadOnly = readOnly || !type.ElementsAreWritable,
                    .Decorator = context.Decorator,
                };
                const dd::ui::ErasedDrawerFn elemDrawer =
                    context.Owner.Registry().Resolve(elemType, context.Metadata);

                if (elemDrawer != nullptr)
                {
                    elemDrawer(elemContext, elemType, elemPtr);
                }

                DrawFieldDecoration(context, elemPath);
            }

            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    void DrawOptional(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value)
    {
        if (value == nullptr || type.OptionalValue == nullptr || type.Element == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const void* contained = type.OptionalValue(value);
        bool present = contained != nullptr;
        const std::string_view label = LeafLabel(context);
        bool toggled = false;

        PushPathId(context.FieldPath);

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            ImGui::BeginDisabled(readOnly);
            toggled = ImGui::Checkbox("##present", &present);
            ImGui::EndDisabled();
        }

        if (toggled && !readOnly)
        {
            const dd::ui::ProposalOp op =
                present ? dd::ui::ProposalOp::EmplaceOptional : dd::ui::ProposalOp::ResetOptional;
            context.EmitOpProposal(op, 0, dd::ui::CommitKind::Commit);
        }

        if (contained != nullptr)
        {
            const foundation::reflect::TypeInfo& elemType = type.Element();
            const dd::ui::ErasedDrawerFn elemDrawer =
                context.Owner.Registry().Resolve(elemType, context.Metadata);

            if (elemDrawer != nullptr)
            {
                ImGui::Indent();
                elemDrawer(context, elemType, contained);
                ImGui::Unindent();
            }
        }

        ImGui::PopID();
    }

    void DrawVariant(const dd::ui::DrawContext& context,
                     const foundation::reflect::TypeInfo& type,
                     const void* value)
    {
        if (value == nullptr || type.ActiveIndex == nullptr || type.ActiveValue == nullptr)
        {
            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const int32_t activeIndex = type.ActiveIndex(value);
        const void* activeValue = type.ActiveValue(value);
        const int32_t altCount = static_cast<int32_t>(std::ssize(type.Alternatives));
        const std::string_view label = LeafLabel(context);
        const std::string_view preview =
            (activeIndex >= 0 && activeIndex < altCount)
                ? type.Alternatives[static_cast<size_t>(activeIndex)]().Name
                : std::string_view{ "<empty>" };

        foundation::SmallVector<char, 128> previewBuf;
        CopyNullTerminated(preview, previewBuf);
        int32_t chosenIndex = -1;

        PushPathId(context.FieldPath);

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            ImGui::BeginDisabled(readOnly);

            if (ImGui::BeginCombo("##variant", previewBuf.data(), 0))
            {
                foundation::SmallVector<char, 128> altBuf;

                for (int32_t index = 0; index < altCount; ++index)
                {
                    const foundation::reflect::TypeInfo& altType =
                        type.Alternatives[static_cast<size_t>(index)]();
                    CopyNullTerminated(altType.Name, altBuf);
                    const bool isSelected = index == activeIndex;

                    if (ImGui::Selectable(altBuf.data(), isSelected) && index != activeIndex)
                    {
                        chosenIndex = index;
                    }

                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();
        }

        if (chosenIndex >= 0 && !readOnly)
        {
            context.EmitOpProposal(dd::ui::ProposalOp::SetVariantIndex,
                                   chosenIndex,
                                   dd::ui::CommitKind::Commit);
        }

        if (activeIndex >= 0 && activeIndex < altCount && activeValue != nullptr)
        {
            const foundation::reflect::TypeInfo& activeType =
                type.Alternatives[static_cast<size_t>(activeIndex)]();
            const dd::ui::ErasedDrawerFn activeDrawer = context.Owner.Registry().Resolve(activeType, nullptr);

            if (activeDrawer != nullptr)
            {
                ImGui::Indent();
                activeDrawer(context, activeType, activeValue);
                ImGui::Unindent();
            }
        }

        ImGui::PopID();
    }

    void DrawAngleSliderTag(const dd::ui::DrawContext& context,
                            const foundation::reflect::TypeInfo& type,
                            const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        if (type.Kind != foundation::reflect::TypeKind::Float32
            && type.Kind != foundation::reflect::TypeKind::Float64)
        {
            const dd::ui::ErasedDrawerFn fallback = context.Owner.Registry().ResolveKindFallback(type.Kind);

            if (fallback != nullptr)
            {
                fallback(context, type, value);
            }

            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const double current = type.Kind == foundation::reflect::TypeKind::Float32
                                   ? static_cast<double>(*static_cast<const float*>(value))
                                   : *static_cast<const double*>(value);
        const NumericBounds bounds = ResolveNumericBounds(context.Metadata);
        float degrees = static_cast<float>(current);

        const dd::ui::SliderParams params
        {
            .Id = "##angle_slider",
            .Label = LeafLabel(context),
            .Hint = context.Metadata != nullptr ? context.Metadata->Doc : std::string_view{},
            .ValueText = std::string_view{},
            .UnitText = context.Metadata != nullptr ? context.Metadata->Unit : std::string_view{},
            .Min = bounds.HasFiniteBounds ? static_cast<float>(bounds.Min) : -180.0f,
            .Max = bounds.HasFiniteBounds ? static_cast<float>(bounds.Max) : 180.0f,
            .Logarithmic = false,
        };

        PushPathId(context.FieldPath);
        ImGui::BeginDisabled(readOnly);
        const bool changed = dd::ui::DrawAngleSlider(params, &degrees, context.Theme, context.Fonts);
        const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        ImGui::PopID();

        if ((changed || deactivated) && !readOnly)
        {
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
            context.EmitScalarProposal(static_cast<double>(degrees), commit);
        }
    }

    void DrawPercentTag(const dd::ui::DrawContext& context,
                        const foundation::reflect::TypeInfo& type,
                        const void* value)
    {
        if (value == nullptr)
        {
            return;
        }

        if (type.Kind != foundation::reflect::TypeKind::Float32
            && type.Kind != foundation::reflect::TypeKind::Float64)
        {
            const dd::ui::ErasedDrawerFn fallback = context.Owner.Registry().ResolveKindFallback(type.Kind);

            if (fallback != nullptr)
            {
                fallback(context, type, value);
            }

            return;
        }

        const bool readOnly = IsContextReadOnly(context);
        const double current = type.Kind == foundation::reflect::TypeKind::Float32
                                   ? static_cast<double>(*static_cast<const float*>(value))
                                   : *static_cast<const double*>(value);
        const NumericBounds bounds = ResolveNumericBounds(context.Metadata);
        const double minNorm = bounds.HasFiniteBounds ? bounds.Min : 0.0;
        const double maxNorm = bounds.HasFiniteBounds ? bounds.Max : 1.0;
        float percent = static_cast<float>(current * 100.0);

        foundation::SmallVector<char, 64> valueBuf;
        valueBuf.resize(64, '\0');
        std::snprintf(valueBuf.data(), valueBuf.size(), "%.0f", static_cast<double>(percent));

        const dd::ui::SliderParams params
        {
            .Id = "##percent_slider",
            .Label = LeafLabel(context),
            .Hint = context.Metadata != nullptr ? context.Metadata->Doc : std::string_view{},
            .ValueText = std::string_view{ valueBuf.data() },
            .UnitText = "%",
            .Min = static_cast<float>(minNorm * 100.0),
            .Max = static_cast<float>(maxNorm * 100.0),
            .Logarithmic = false,
        };

        PushPathId(context.FieldPath);
        ImGui::BeginDisabled(readOnly);
        const bool changed = dd::ui::DrawArtistSlider(params, &percent, context.Theme, context.Fonts);
        const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        ImGui::PopID();

        if ((changed || deactivated) && !readOnly)
        {
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;
            context.EmitScalarProposal(static_cast<double>(percent) * 0.01, commit);
        }
    }

    void DrawColorTag(const dd::ui::DrawContext& context,
                      const foundation::reflect::TypeInfo& type,
                      const void* value,
                      bool has_alpha)
    {
        if (value == nullptr)
        {
            return;
        }

        const int64_t expectedCount = has_alpha ? 4 : 3;
        const bool validSequence = type.Kind == foundation::reflect::TypeKind::Sequence
            && type.Element != nullptr
            && type.Element().Kind == foundation::reflect::TypeKind::Float32
            && type.ElementCount != nullptr
            && type.ElementAt != nullptr
            && type.ElementCount(value) == expectedCount;

        if (!validSequence)
        {
            const dd::ui::ErasedDrawerFn fallback = context.Owner.Registry().ResolveKindFallback(type.Kind);

            if (fallback != nullptr)
            {
                fallback(context, type, value);
            }

            return;
        }

        const bool readOnly = IsContextReadOnly(context) || !type.ElementsAreWritable;
        foundation::SmallVector<float, 4> color;
        color.resize(static_cast<size_t>(expectedCount), 0.0f);

        for (int64_t index = 0; index < expectedCount; ++index)
        {
            const void* elemPtr = type.ElementAt(value, index);
            color[static_cast<size_t>(index)] = *static_cast<const float*>(elemPtr);
        }

        const std::string_view label = LeafLabel(context);
        bool changed = false;
        bool deactivated = false;

        {
            dd::ui::PropertyRow row(label, context.Theme, context.Fonts, 0.0f);
            PushPathId(context.FieldPath);
            ImGui::BeginDisabled(readOnly);
            changed = has_alpha ? ImGui::ColorEdit4("##color", color.data(), ImGuiColorEditFlags_None)
                                : ImGui::ColorEdit3("##color", color.data(), ImGuiColorEditFlags_None);
            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if ((changed || deactivated) && !readOnly)
        {
            const dd::ui::CommitKind commit =
                deactivated ? dd::ui::CommitKind::Commit : dd::ui::CommitKind::Preview;

            for (int64_t index = 0; index < expectedCount; ++index)
            {
                const std::string elemPath = BuildIndexPath(context.FieldPath, index);
                EmitPathScalarProposal(context,
                                       elemPath,
                                       static_cast<double>(color[static_cast<size_t>(index)]),
                                       commit);
            }
        }
    }

    void DrawColorRgbTag(const dd::ui::DrawContext& context,
                         const foundation::reflect::TypeInfo& type,
                         const void* value)
    {
        DrawColorTag(context, type, value, false);
    }

    void DrawColorRgbaTag(const dd::ui::DrawContext& context,
                          const foundation::reflect::TypeInfo& type,
                          const void* value)
    {
        DrawColorTag(context, type, value, true);
    }
}
