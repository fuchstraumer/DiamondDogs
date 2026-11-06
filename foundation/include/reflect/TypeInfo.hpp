#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_REFLECT_TYPE_INFO_HPP
#define DIAMOND_DOGS_FOUNDATION_REFLECT_TYPE_INFO_HPP
#include "reflect/Describe.hpp"
#include "reflect/ValueTraits.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace foundation::reflect
{
    enum class TypeKind : uint8_t
    {
        Invalid = 0,
        Bool,
        Int,
        Float32,
        Float64,
        String,
        Enum,
        Struct,
        Sequence,
        Optional,
        Variant,
    };

    struct TypeInfo;

    // a function, not a pointer to the table, so that a type can contain a vector of itself
    using TypeInfoGetter = const TypeInfo& (*)() noexcept;

    struct EnumCaseInfo
    {
        std::string_view Name;
        std::string_view Doc;
        // sign-extended or zero-extended to 64 bits, as the integer encoding does
        int64_t Value{ 0 };
    };

    struct FieldInfo
    {
        std::string_view Name;
        FieldMetadata Metadata;
        TypeInfoGetter Type{ nullptr };
        void* (*Address)(void* owner) noexcept { nullptr };
    };

    /** The type-erased description of one value type, built at compile time from Describe<T>. Do not compare
     *  the addresses of two tables: the linker may fold tables with the same contents. */
    struct TypeInfo
    {
        TypeKind Kind{ TypeKind::Invalid };
        // the descriptor's name and doc. Empty when the kind has no descriptor
        std::string_view Name;
        std::string_view Doc;
        // for Int, and for the underlying type of Enum
        int32_t IntegerWidthBytes{ 0 };
        bool IntegerIsSigned{ false };
        std::span<const FieldInfo> Fields;
        std::span<const EnumCaseInfo> Cases;
        // for Sequence and Optional
        TypeInfoGetter Element{ nullptr };
        // for a Sequence of fixed size. -1 otherwise
        int64_t FixedElementCount{ -1 };
        std::span<const TypeInfoGetter> Alternatives;
        // for String. WriteString is null for a string_view, because a view stores no characters of its own
        std::string_view (*ReadString)(const void* value) noexcept { nullptr };
        void (*WriteString)(void* value, std::string_view text) { nullptr };
        // for Sequence
        int64_t (*ElementCount)(const void* sequence) noexcept { nullptr };
        const void* (*ElementAt)(const void* sequence, int64_t index) noexcept { nullptr };
        // false for a span of const elements
        bool ElementsAreWritable{ false };
        // for Optional. Returns null when the optional is empty
        const void* (*OptionalValue)(const void* optional) noexcept { nullptr };
        // for Variant. A valueless variant returns -1 and null
        int32_t (*ActiveIndex)(const void* variant) noexcept { nullptr };
        const void* (*ActiveValue)(const void* variant) noexcept { nullptr };
    };

    /** @brief Returns the table of T. The compiler builds it, so it allocates nothing and any thread can read
     *  it at any time. A type that Encode rejects fails to compile here too. */
    template<typename T>
    constexpr const TypeInfo& GetTypeInfo() noexcept;

    /** @brief Returns the field whose name matches exactly, or null when no field matches. */
    [[nodiscard]] const FieldInfo* FindField(const TypeInfo& type, std::string_view name);
    /** @brief Returns the enum case whose name matches exactly, or null when no case matches. */
    [[nodiscard]] const EnumCaseInfo* FindEnumCase(const TypeInfo& type, std::string_view name);
    /** @brief Returns the first enum case with this value, as Encode does, or null when no case matches. */
    [[nodiscard]] const EnumCaseInfo* FindEnumCase(const TypeInfo& type, int64_t value);
    /** @brief Returns the address of a field for reading, from a const owner. */
    [[nodiscard]] const void* FieldAddress(const FieldInfo& field, const void* owner) noexcept;

    namespace detail
    {
        void WriteStdString(void* value, std::string_view text);

        template<typename T>
        std::string_view ReadStringLike(const void* value) noexcept
        {
            return *static_cast<const T*>(value);
        }

        template<typename T>
        int64_t SequenceElementCount(const void* sequence) noexcept
        {
            return static_cast<int64_t>(static_cast<const T*>(sequence)->size());
        }

        template<typename T>
        const void* SequenceElementAt(const void* sequence, int64_t index) noexcept
        {
            const T& typedSequence = *static_cast<const T*>(sequence);
            return &typedSequence[static_cast<size_t>(index)];
        }

        template<typename T>
        const void* OptionalValueOf(const void* optional) noexcept
        {
            const T& typedOptional = *static_cast<const T*>(optional);
            return typedOptional.has_value() ? &*typedOptional : nullptr;
        }

        template<typename T>
        int32_t VariantActiveIndex(const void* variant) noexcept
        {
            const T& typedVariant = *static_cast<const T*>(variant);
            return typedVariant.valueless_by_exception() ? -1 : static_cast<int32_t>(typedVariant.index());
        }

        template<typename T>
        const void* VariantActiveValue(const void* variant) noexcept
        {
            const T& typedVariant = *static_cast<const T*>(variant);

            if (typedVariant.valueless_by_exception())
            {
                return nullptr;
            }

            const auto addressOf = [](const auto& alternative) -> const void* { return &alternative; };

            return std::visit(addressOf, typedVariant);
        }

        template<typename Owner, size_t Index>
        void* AddressOfField(void* owner) noexcept
        {
            static constexpr const auto& field = std::get<Index>(Describe<Owner>::Fields);

            Owner& typedOwner = *static_cast<Owner*>(owner);

            return &(typedOwner.*(field.Pointer));
        }

        template<size_t Index, typename Owner, typename Member>
        constexpr FieldInfo MakeFieldInfo(const Field<Owner, Member>& field) noexcept
        {
            return FieldInfo
            {
                .Name = field.Name,
                .Metadata = field.Metadata,
                .Type = &GetTypeInfo<Member>,
                .Address = &AddressOfField<Owner, Index>,
            };
        }

        template<typename T, size_t... Indices>
        constexpr std::array<FieldInfo, sizeof...(Indices)> MakeFieldInfos(
            std::index_sequence<Indices...>) noexcept
        {
            return { MakeFieldInfo<Indices>(std::get<Indices>(Describe<T>::Fields))... };
        }

        template<typename T>
        inline constexpr std::array<FieldInfo, k_FieldCount<T>> k_FieldInfos =
            MakeFieldInfos<T>(std::make_index_sequence<k_FieldCount<T>>{});

        template<typename E, size_t... Indices>
        constexpr std::array<EnumCaseInfo, sizeof...(Indices)> MakeEnumCaseInfos(
            std::index_sequence<Indices...>) noexcept
        {
            return
            {
                EnumCaseInfo
                {
                    .Name = Describe<E>::Cases[Indices].Name,
                    .Doc = Describe<E>::Cases[Indices].Doc,
                    .Value = static_cast<int64_t>(std::to_underlying(Describe<E>::Cases[Indices].Value)),
                }...
            };
        }

        template<typename E>
        inline constexpr std::array<EnumCaseInfo, std::size(Describe<E>::Cases)> k_EnumCaseInfos =
            MakeEnumCaseInfos<E>(std::make_index_sequence<std::size(Describe<E>::Cases)>{});

        template<typename T>
        inline constexpr bool k_VariantAlternatives = false;

        template<typename T>
        inline constexpr bool k_VariantAlternativesAreDescribed = false;

        // Encode writes the name of the active alternative, so an alternative without a name has no encoding
        template<typename... Alternatives>
        inline constexpr bool k_VariantAlternativesAreDescribed<std::variant<Alternatives...>> =
            ((DescribedStruct<Alternatives> || DescribedEnum<Alternatives>) && ...);

        template<typename... Alternatives>
        inline constexpr std::array<TypeInfoGetter, sizeof...(Alternatives)>
            k_VariantAlternatives<std::variant<Alternatives...>>
        {
            &GetTypeInfo<Alternatives>...
        };

        template<typename T>
        constexpr int64_t FixedElementCountOf() noexcept
        {
            if constexpr (IsStdArray<T>::value)
            {
                return static_cast<int64_t>(std::tuple_size_v<T>);
            }
            else if constexpr (IsSpan<T>::value)
            {
                return T::extent == std::dynamic_extent ? -1 : static_cast<int64_t>(T::extent);
            }
            else
            {
                return -1;
            }
        }

        template<typename T>
        constexpr bool SequenceElementsAreWritable() noexcept
        {
            if constexpr (IsSpan<T>::value)
            {
                return !std::is_const_v<typename T::element_type>;
            }
            else
            {
                return true;
            }
        }

        template<typename Integer>
        constexpr TypeInfo MakeIntegerInfo(TypeKind kind) noexcept
        {
            return TypeInfo
            {
                .Kind = kind,
                .IntegerWidthBytes = static_cast<int32_t>(sizeof(Integer)),
                .IntegerIsSigned = std::is_signed_v<Integer>,
            };
        }

        template<typename E>
        constexpr TypeInfo MakeEnumInfo() noexcept
        {
            CheckEnumDescriptor<E>();

            TypeInfo info = MakeIntegerInfo<std::underlying_type_t<E>>(TypeKind::Enum);
            info.Name = Describe<E>::Name;
            info.Doc = DescribedDoc<E>();
            info.Cases = k_EnumCaseInfos<E>;

            return info;
        }

        template<typename T>
        constexpr TypeInfo MakeStructInfo() noexcept
        {
            CheckStructDescriptor<T>();

            return TypeInfo
            {
                .Kind = TypeKind::Struct,
                .Name = Describe<T>::Name,
                .Doc = DescribedDoc<T>(),
                .Fields = k_FieldInfos<T>,
            };
        }

        // the same order of tests as Encode
        template<typename T>
        constexpr TypeInfo MakeTypeInfo() noexcept
        {
            if constexpr (std::same_as<T, bool>)
            {
                return TypeInfo{ .Kind = TypeKind::Bool };
            }
            else if constexpr (FixedInt<T>)
            {
                return MakeIntegerInfo<T>(TypeKind::Int);
            }
            else if constexpr (std::same_as<T, float>)
            {
                return TypeInfo{ .Kind = TypeKind::Float32 };
            }
            else if constexpr (std::same_as<T, double>)
            {
                return TypeInfo{ .Kind = TypeKind::Float64 };
            }
            else if constexpr (std::same_as<T, std::string>)
            {
                return TypeInfo
                {
                    .Kind = TypeKind::String,
                    .ReadString = &ReadStringLike<T>,
                    .WriteString = &WriteStdString,
                };
            }
            else if constexpr (std::same_as<T, std::string_view>)
            {
                return TypeInfo{ .Kind = TypeKind::String, .ReadString = &ReadStringLike<T> };
            }
            else if constexpr (DescribedEnum<T>)
            {
                return MakeEnumInfo<T>();
            }
            else if constexpr (DescribedStruct<T>)
            {
                return MakeStructInfo<T>();
            }
            else if constexpr (Sequence<T>)
            {
                static_assert(!std::same_as<T, std::vector<bool>>,
                              "GetTypeInfo: vector<bool> has no element addresses. Use vector<uint8_t>.");

                return TypeInfo
                {
                    .Kind = TypeKind::Sequence,
                    .Element = &GetTypeInfo<typename T::value_type>,
                    .FixedElementCount = FixedElementCountOf<T>(),
                    .ElementCount = &SequenceElementCount<T>,
                    .ElementAt = &SequenceElementAt<T>,
                    .ElementsAreWritable = SequenceElementsAreWritable<T>(),
                };
            }
            else if constexpr (IsOptional<T>::value)
            {
                return TypeInfo
                {
                    .Kind = TypeKind::Optional,
                    .Element = &GetTypeInfo<typename T::value_type>,
                    .OptionalValue = &OptionalValueOf<T>,
                };
            }
            else if constexpr (IsVariant<T>::value)
            {
                static_assert(k_VariantAlternativesAreDescribed<T>,
                              "GetTypeInfo: a variant alternative needs a Describe<T> specialization");

                return TypeInfo
                {
                    .Kind = TypeKind::Variant,
                    .Alternatives = k_VariantAlternatives<T>,
                    .ActiveIndex = &VariantActiveIndex<T>,
                    .ActiveValue = &VariantActiveValue<T>,
                };
            }
            else
            {
                static_assert(k_AlwaysFalse<T>,
                              "GetTypeInfo: no table for this type. Use bool, a fixed-width integer, float, "
                              "double, a string, a type with a Describe<T> specialization, or a vector, "
                              "array, span, optional or variant of those.");
            }
        }

        template<typename T>
        inline constexpr TypeInfo k_TypeInfo = MakeTypeInfo<T>();
    }

    template<typename T>
    constexpr const TypeInfo& GetTypeInfo() noexcept
    {
        return detail::k_TypeInfo<T>;
    }
}

#endif // !DIAMOND_DOGS_FOUNDATION_REFLECT_TYPE_INFO_HPP
