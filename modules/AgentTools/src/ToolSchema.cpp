#include "ToolSchema.hpp"
#include "content/JsonWriter.hpp"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace
{
    using foundation::content::JsonWriter;
    using foundation::reflect::FieldInfo;
    using foundation::reflect::FieldMetadata;
    using foundation::reflect::TypeInfo;
    using foundation::reflect::TypeKind;

    // the struct names on the way down, so that a recursive type ends
    using StructStack = std::vector<std::string_view>;

    void WriteSchemaObject(JsonWriter& writer,
                           const TypeInfo& type,
                           const FieldMetadata* metadata,
                           StructStack& stack);
    void WriteSchemaMembers(JsonWriter& writer,
                            const TypeInfo& type,
                            const FieldMetadata* metadata,
                            StructStack& stack);
    void WriteDescription(JsonWriter& writer, const TypeInfo& type, const FieldMetadata* metadata);
    void WriteNumberMembers(JsonWriter& writer, std::string_view type_name, const FieldMetadata* metadata);
    void WriteEnumMembers(JsonWriter& writer, const TypeInfo& type);
    void WriteStructMembers(JsonWriter& writer, const TypeInfo& type, StructStack& stack);
    void WriteSequenceMembers(JsonWriter& writer,
                              const TypeInfo& type,
                              const FieldMetadata* metadata,
                              StructStack& stack);
    void WriteOptionalMembers(JsonWriter& writer,
                              const TypeInfo& type,
                              const FieldMetadata* metadata,
                              StructStack& stack);
    void WriteVariantMembers(JsonWriter& writer, const TypeInfo& type, StructStack& stack);
    // a limit of a field applies to each element of the field, and the description does not repeat
    [[nodiscard]] FieldMetadata LimitsOnly(const FieldMetadata* metadata) noexcept;
}

namespace agent::detail
{
    void WriteTypeSchema(JsonWriter& writer, const TypeInfo& type, const FieldMetadata* metadata)
    {
        StructStack stack;

        WriteSchemaObject(writer, type, metadata, stack);
    }
}

namespace
{
    void WriteSchemaObject(JsonWriter& writer,
                           const TypeInfo& type,
                           const FieldMetadata* metadata,
                           StructStack& stack)
    {
        writer.BeginObject();
        WriteDescription(writer, type, metadata);
        WriteSchemaMembers(writer, type, metadata, stack);
        writer.EndObject();
    }

    void WriteSchemaMembers(JsonWriter& writer,
                            const TypeInfo& type,
                            const FieldMetadata* metadata,
                            StructStack& stack)
    {
        switch (type.Kind)
        {
        case TypeKind::Bool:
            writer.KeyString("type", "boolean");
            break;
        case TypeKind::Int:
            WriteNumberMembers(writer, "integer", metadata);
            break;
        case TypeKind::Float32:
        case TypeKind::Float64:
            WriteNumberMembers(writer, "number", metadata);
            break;
        case TypeKind::String:
            writer.KeyString("type", "string");
            break;
        case TypeKind::Enum:
            WriteEnumMembers(writer, type);
            break;
        case TypeKind::Struct:
            WriteStructMembers(writer, type, stack);
            break;
        case TypeKind::Sequence:
            WriteSequenceMembers(writer, type, metadata, stack);
            break;
        case TypeKind::Optional:
            WriteOptionalMembers(writer, type, metadata, stack);
            break;
        case TypeKind::Variant:
            WriteVariantMembers(writer, type, stack);
            break;
        case TypeKind::Invalid:
            break;
        }
    }

    void WriteDescription(JsonWriter& writer, const TypeInfo& type, const FieldMetadata* metadata)
    {
        const std::string_view doc = metadata != nullptr && !metadata->Doc.empty() ? metadata->Doc : type.Doc;

        if (!doc.empty())
        {
            writer.KeyString("description", doc);
        }

        if (metadata == nullptr)
        {
            return;
        }

        if (!metadata->Unit.empty())
        {
            writer.KeyString("x-unit", metadata->Unit);
        }

        if (metadata->ReadOnly)
        {
            writer.KeyBool("readOnly", true);
        }
    }

    void WriteNumberMembers(JsonWriter& writer, std::string_view type_name, const FieldMetadata* metadata)
    {
        writer.KeyString("type", type_name);

        if (metadata == nullptr || !metadata->Range.has_value())
        {
            return;
        }

        // an open end has no limit to state, and JSON cannot write infinity
        if (std::isfinite(metadata->Range->Min))
        {
            writer.KeyDouble("minimum", metadata->Range->Min);
        }

        if (std::isfinite(metadata->Range->Max))
        {
            writer.KeyDouble("maximum", metadata->Range->Max);
        }
    }

    void WriteEnumMembers(JsonWriter& writer, const TypeInfo& type)
    {
        writer.KeyString("type", "string");
        writer.KeyString("x-type", type.Name);
        writer.Key("enum");
        writer.BeginArray();

        for (const foundation::reflect::EnumCaseInfo& enumCase : type.Cases)
        {
            writer.String(enumCase.Name);
        }

        writer.EndArray();

        const auto caseHasDoc = [](const foundation::reflect::EnumCaseInfo& enumCase)
        {
            return !enumCase.Doc.empty();
        };
        const bool hasCaseDocs = std::ranges::any_of(type.Cases, caseHasDoc);

        if (!hasCaseDocs)
        {
            return;
        }

        writer.Key("x-case-docs");
        writer.BeginObject();

        for (const foundation::reflect::EnumCaseInfo& enumCase : type.Cases)
        {
            writer.KeyString(enumCase.Name, enumCase.Doc);
        }

        writer.EndObject();
    }

    void WriteStructMembers(JsonWriter& writer, const TypeInfo& type, StructStack& stack)
    {
        writer.KeyString("type", "object");
        writer.KeyString("x-type", type.Name);

        if (std::ranges::find(stack, type.Name) != stack.end())
        {
            writer.KeyBool("x-recursive", true);
            return;
        }

        stack.push_back(type.Name);
        writer.Key("properties");
        writer.BeginObject();

        for (const FieldInfo& field : type.Fields)
        {
            if (field.Metadata.Hidden)
            {
                continue;
            }

            writer.Key(field.Name);
            WriteSchemaObject(writer, field.Type(), &field.Metadata, stack);
        }

        writer.EndObject();
        stack.pop_back();

        const auto fieldIsRequired = [](const FieldInfo& field) { return field.Metadata.Required; };
        const bool hasRequiredField = std::ranges::any_of(type.Fields, fieldIsRequired);

        if (!hasRequiredField)
        {
            return;
        }

        writer.Key("required");
        writer.BeginArray();

        for (const FieldInfo& field : type.Fields)
        {
            if (field.Metadata.Required)
            {
                writer.String(field.Name);
            }
        }

        writer.EndArray();
    }

    void WriteSequenceMembers(JsonWriter& writer,
                              const TypeInfo& type,
                              const FieldMetadata* metadata,
                              StructStack& stack)
    {
        const FieldMetadata elementLimits = LimitsOnly(metadata);

        writer.KeyString("type", "array");

        if (type.FixedElementCount >= 0)
        {
            writer.KeyInt("minItems", type.FixedElementCount);
            writer.KeyInt("maxItems", type.FixedElementCount);
        }

        writer.Key("items");
        WriteSchemaObject(writer, type.Element(), &elementLimits, stack);
    }

    void WriteOptionalMembers(JsonWriter& writer,
                              const TypeInfo& type,
                              const FieldMetadata* metadata,
                              StructStack& stack)
    {
        const FieldMetadata elementLimits = LimitsOnly(metadata);

        writer.Key("anyOf");
        writer.BeginArray();
        WriteSchemaObject(writer, type.Element(), &elementLimits, stack);
        writer.BeginObject();
        writer.KeyString("type", "null");
        writer.EndObject();
        writer.EndArray();
    }

    void WriteVariantMembers(JsonWriter& writer, const TypeInfo& type, StructStack& stack)
    {
        writer.Key("oneOf");
        writer.BeginArray();

        for (const foundation::reflect::TypeInfoGetter alternative : type.Alternatives)
        {
            WriteSchemaObject(writer, alternative(), nullptr, stack);
        }

        writer.EndArray();
    }

    FieldMetadata LimitsOnly(const FieldMetadata* metadata) noexcept
    {
        FieldMetadata limits{};

        if (metadata != nullptr)
        {
            limits.Range = metadata->Range;
        }

        return limits;
    }
}
