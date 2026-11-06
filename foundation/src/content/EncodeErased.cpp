#include "content/EncodeErased.hpp"
#include "content/CanonicalSink.hpp"
#include "reflect/TypeInfo.hpp"
#include "reflect/ValueAccess.hpp"
#include <cstdint>

namespace foundation::content
{
    namespace
    {
        void EncodeInteger(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
        {
            const int64_t bits = reflect::ReadIntegerBits(type, value);

            sink.WriteInt(bits, type.IntegerWidthBytes, type.IntegerIsSigned);
        }

        void EncodeEnumCase(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
        {
            const int64_t bits = reflect::ReadIntegerBits(type, value);
            const reflect::EnumCaseInfo* enumCase = reflect::FindEnumCase(type, bits);

            sink.WriteEnum(type.Name, enumCase != nullptr ? enumCase->Name : "<no case>");
        }

        void EncodeStructFields(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
        {
            sink.BeginStruct(type.Name);

            for (const reflect::FieldInfo& field : type.Fields)
            {
                sink.BeginField(field.Name);
                EncodeErased(field.Type(), reflect::FieldAddress(field, value), sink);
            }

            sink.EndStruct();
        }

        void EncodeSequence(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
        {
            const int64_t count = type.ElementCount(value);
            const reflect::TypeInfo& element = type.Element();

            sink.BeginArray(static_cast<uint64_t>(count));

            for (int64_t index = 0; index < count; ++index)
            {
                EncodeErased(element, type.ElementAt(value, index), sink);
            }

            sink.EndArray();
        }

        void EncodeOptional(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
        {
            const void* contained = type.OptionalValue(value);

            sink.WriteOptional(contained != nullptr);

            if (contained != nullptr)
            {
                EncodeErased(type.Element(), contained, sink);
            }
        }

        void EncodeVariant(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
        {
            const int32_t activeIndex = type.ActiveIndex(value);

            if (activeIndex < 0)
            {
                sink.WriteVariant("<valueless>");
                return;
            }

            const reflect::TypeInfo& alternative = type.Alternatives[static_cast<size_t>(activeIndex)]();

            sink.WriteVariant(alternative.Name);
            EncodeErased(alternative, type.ActiveValue(value), sink);
        }
    }

    void EncodeErased(const reflect::TypeInfo& type, const void* value, CanonicalSink& sink)
    {
        switch (type.Kind)
        {
        case reflect::TypeKind::Bool:
            sink.WriteBool(*static_cast<const bool*>(value));
            break;
        case reflect::TypeKind::Int:
            EncodeInteger(type, value, sink);
            break;
        case reflect::TypeKind::Float32:
            sink.WriteFloat32(*static_cast<const float*>(value));
            break;
        case reflect::TypeKind::Float64:
            sink.WriteFloat64(*static_cast<const double*>(value));
            break;
        case reflect::TypeKind::String:
            sink.WriteString(type.ReadString(value));
            break;
        case reflect::TypeKind::Enum:
            EncodeEnumCase(type, value, sink);
            break;
        case reflect::TypeKind::Struct:
            EncodeStructFields(type, value, sink);
            break;
        case reflect::TypeKind::Sequence:
            EncodeSequence(type, value, sink);
            break;
        case reflect::TypeKind::Optional:
            EncodeOptional(type, value, sink);
            break;
        case reflect::TypeKind::Variant:
            EncodeVariant(type, value, sink);
            break;
        case reflect::TypeKind::Invalid:
            break;
        }
    }
}
