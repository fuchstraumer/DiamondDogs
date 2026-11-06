#include <gtest/gtest.h>

#include "content/Encode.hpp"
#include "content/TextSink.hpp"
#include "reflect/TypeInfo.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace typeinfotest
{
    enum class Shape : uint8_t
    {
        Invalid = 0,
        Round,
        Square,
    };

    enum class Signed : int16_t
    {
        Invalid = 0,
        Below = -5,
        Above = 7,
    };

    enum class Wide : uint64_t
    {
        Invalid = 0,
        Top = 0xFFFFFFFFFFFFFFFFull,
    };

    struct Leaf
    {
        float Value{ 0.0f };
    };

    struct OtherLeaf
    {
        float Value{ 0.0f };
    };

    struct Everything
    {
        bool Flag{ false };
        int16_t Small{ 0 };
        uint64_t Big{ 0u };
        float Single{ 0.0f };
        double Double{ 0.0 };
        std::string Text;
        std::string_view View;
        Shape Form{ Shape::Invalid };
        Signed Offset{ Signed::Invalid };
        Leaf Nested;
        std::vector<int32_t> List;
        std::array<float, 3> Fixed{ 0.0f, 0.0f, 0.0f };
        std::span<const uint32_t> DynamicSpan;
        std::span<const uint32_t, 2> FixedSpan{ k_FixedSpanSource };
        std::optional<Leaf> Maybe;
        std::variant<Leaf, OtherLeaf> Choice;

        static constexpr std::array<uint32_t, 2> k_FixedSpanSource{ 1u, 2u };
    };

    struct Node
    {
        int32_t Id{ 0 };
        std::vector<Node> Children;
    };
}

namespace foundation::reflect
{
    template<>
    struct Describe<typeinfotest::Shape>
    {
        static constexpr std::string_view Name = "typeinfotest.Shape";
        static constexpr std::string_view Doc = "The outline of a thing.";
        static constexpr std::array<EnumCase<typeinfotest::Shape>, 3> Cases
        {
            EnumCase<typeinfotest::Shape>{ "Invalid", typeinfotest::Shape::Invalid },
            EnumCase<typeinfotest::Shape>{ "Round", typeinfotest::Shape::Round, "No corners." },
            EnumCase<typeinfotest::Shape>{ "Square", typeinfotest::Shape::Square },
        };
    };

    template<>
    struct Describe<typeinfotest::Signed>
    {
        static constexpr std::string_view Name = "typeinfotest.Signed";
        static constexpr std::array<EnumCase<typeinfotest::Signed>, 3> Cases
        {
            EnumCase<typeinfotest::Signed>{ "Invalid", typeinfotest::Signed::Invalid },
            EnumCase<typeinfotest::Signed>{ "Below", typeinfotest::Signed::Below },
            EnumCase<typeinfotest::Signed>{ "Above", typeinfotest::Signed::Above },
        };
    };

    template<>
    struct Describe<typeinfotest::Wide>
    {
        static constexpr std::string_view Name = "typeinfotest.Wide";
        static constexpr std::array<EnumCase<typeinfotest::Wide>, 2> Cases
        {
            EnumCase<typeinfotest::Wide>{ "Invalid", typeinfotest::Wide::Invalid },
            EnumCase<typeinfotest::Wide>{ "Top", typeinfotest::Wide::Top },
        };
    };

    template<>
    struct Describe<typeinfotest::Leaf>
    {
        using T = typeinfotest::Leaf;

        static constexpr std::string_view Name = "typeinfotest.Leaf";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<typeinfotest::OtherLeaf>
    {
        using T = typeinfotest::OtherLeaf;

        static constexpr std::string_view Name = "typeinfotest.OtherLeaf";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<typeinfotest::Everything>
    {
        using T = typeinfotest::Everything;

        static constexpr std::string_view Name = "typeinfotest.Everything";
        static constexpr std::string_view Doc = "One field of every kind.";
        static constexpr std::tuple Fields
        {
            Field<T, bool>{ "Flag", &T::Flag },
            Field<T, int16_t>{ "Small", &T::Small },
            Field<T, uint64_t>{ "Big", &T::Big },
            Field<T, float>
            {
                "Single",
                &T::Single,
                {
                    .Unit = "m",
                    .Range = FieldRange{ 0.0, 8.0 },
                    .Doc = "A single-precision length.",
                    .ReadOnly = true,
                },
            },
            Field<T, double>{ "Double", &T::Double, { .Hidden = true } },
            Field<T, std::string>{ "Text", &T::Text },
            Field<T, std::string_view>{ "View", &T::View },
            Field<T, typeinfotest::Shape>{ "Form", &T::Form },
            Field<T, typeinfotest::Signed>{ "Offset", &T::Offset },
            Field<T, typeinfotest::Leaf>{ "Nested", &T::Nested },
            Field<T, std::vector<int32_t>>{ "List", &T::List },
            Field<T, std::array<float, 3>>{ "Fixed", &T::Fixed },
            Field<T, std::span<const uint32_t>>{ "DynamicSpan", &T::DynamicSpan },
            Field<T, std::span<const uint32_t, 2>>{ "FixedSpan", &T::FixedSpan },
            Field<T, std::optional<typeinfotest::Leaf>>{ "Maybe", &T::Maybe },
            Field<T, std::variant<typeinfotest::Leaf, typeinfotest::OtherLeaf>>{ "Choice", &T::Choice },
        };
    };

    template<>
    struct Describe<typeinfotest::Node>
    {
        using T = typeinfotest::Node;

        static constexpr std::string_view Name = "typeinfotest.Node";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Id", &T::Id },
            Field<T, std::vector<typeinfotest::Node>>{ "Children", &T::Children },
        };
    };
}

namespace
{
    using foundation::reflect::EnumCaseInfo;
    using foundation::reflect::FieldInfo;
    using foundation::reflect::FindEnumCase;
    using foundation::reflect::FindField;
    using foundation::reflect::GetTypeInfo;
    using foundation::reflect::TypeInfo;
    using foundation::reflect::TypeKind;

    // the proof that the compiler builds every table, with no dynamic initialization
    constinit const TypeInfo* g_everythingInfo = &GetTypeInfo<typeinfotest::Everything>();
    constinit const TypeInfo* g_nodeInfo = &GetTypeInfo<typeinfotest::Node>();

    constexpr const TypeInfo& k_Everything = GetTypeInfo<typeinfotest::Everything>();

    static_assert(k_Everything.Kind == TypeKind::Struct);
    static_assert(k_Everything.Name == "typeinfotest.Everything");
    static_assert(k_Everything.Fields.size() == 16);
    static_assert(k_Everything.Fields[1].Name == "Small");
    static_assert(k_Everything.Fields[1].Type().IntegerWidthBytes == 2);
    static_assert(k_Everything.Fields[3].Metadata.Range->Max == 8.0);
    static_assert(GetTypeInfo<typeinfotest::Node>().Fields[1].Type().Element().Name == "typeinfotest.Node");

    const TypeInfo& FieldType(const TypeInfo& owner, std::string_view name)
    {
        const FieldInfo* field = FindField(owner, name);

        EXPECT_NE(field, nullptr) << name;

        return field->Type();
    }
}

TEST(ReflectTypeInfoTest, ScalarKinds)
{
    EXPECT_EQ(GetTypeInfo<bool>().Kind, TypeKind::Bool);
    EXPECT_EQ(GetTypeInfo<float>().Kind, TypeKind::Float32);
    EXPECT_EQ(GetTypeInfo<double>().Kind, TypeKind::Float64);
    EXPECT_EQ(GetTypeInfo<std::string>().Kind, TypeKind::String);
    EXPECT_EQ(GetTypeInfo<std::string_view>().Kind, TypeKind::String);
}

TEST(ReflectTypeInfoTest, IntegersKeepWidthAndSign)
{
    const TypeInfo& small = GetTypeInfo<int16_t>();

    EXPECT_EQ(small.Kind, TypeKind::Int);
    EXPECT_EQ(small.IntegerWidthBytes, 2);
    EXPECT_TRUE(small.IntegerIsSigned);

    const TypeInfo& big = GetTypeInfo<uint64_t>();

    EXPECT_EQ(big.Kind, TypeKind::Int);
    EXPECT_EQ(big.IntegerWidthBytes, 8);
    EXPECT_FALSE(big.IntegerIsSigned);
}

TEST(ReflectTypeInfoTest, FieldsKeepDeclarationOrderAndKind)
{
    const std::array<std::string_view, 16> expectedNames
    {
        "Flag", "Small", "Big", "Single", "Double", "Text", "View", "Form",
        "Offset", "Nested", "List", "Fixed", "DynamicSpan", "FixedSpan", "Maybe", "Choice",
    };
    const std::array<TypeKind, 16> expectedKinds
    {
        TypeKind::Bool, TypeKind::Int, TypeKind::Int, TypeKind::Float32,
        TypeKind::Float64, TypeKind::String, TypeKind::String, TypeKind::Enum,
        TypeKind::Enum, TypeKind::Struct, TypeKind::Sequence, TypeKind::Sequence,
        TypeKind::Sequence, TypeKind::Sequence, TypeKind::Optional, TypeKind::Variant,
    };

    ASSERT_EQ(g_everythingInfo->Fields.size(), expectedNames.size());

    for (size_t index = 0; index < expectedNames.size(); ++index)
    {
        const FieldInfo& field = g_everythingInfo->Fields[index];

        EXPECT_EQ(field.Name, expectedNames[index]);
        EXPECT_EQ(field.Type().Kind, expectedKinds[index]) << field.Name;
    }
}

TEST(ReflectTypeInfoTest, TableKeepsMetadataAndDocs)
{
    EXPECT_EQ(g_everythingInfo->Doc, "One field of every kind.");

    const FieldInfo* single = FindField(*g_everythingInfo, "Single");

    ASSERT_NE(single, nullptr);
    EXPECT_EQ(single->Metadata.Unit, "m");
    ASSERT_TRUE(single->Metadata.Range.has_value());
    EXPECT_EQ(single->Metadata.Range->Min, 0.0);
    EXPECT_EQ(single->Metadata.Range->Max, 8.0);
    EXPECT_EQ(single->Metadata.Doc, "A single-precision length.");
    EXPECT_TRUE(single->Metadata.ReadOnly);
    EXPECT_FALSE(single->Metadata.Hidden);

    const FieldInfo* wide = FindField(*g_everythingInfo, "Double");

    ASSERT_NE(wide, nullptr);
    EXPECT_FALSE(wide->Metadata.Range.has_value());
    EXPECT_FALSE(wide->Metadata.ReadOnly);
    EXPECT_TRUE(wide->Metadata.Hidden);
}

TEST(ReflectTypeInfoTest, EnumTableKeepsNamesValuesAndDocs)
{
    const TypeInfo& shape = GetTypeInfo<typeinfotest::Shape>();

    EXPECT_EQ(shape.Kind, TypeKind::Enum);
    EXPECT_EQ(shape.Name, "typeinfotest.Shape");
    EXPECT_EQ(shape.Doc, "The outline of a thing.");
    EXPECT_EQ(shape.IntegerWidthBytes, 1);
    EXPECT_FALSE(shape.IntegerIsSigned);
    ASSERT_EQ(shape.Cases.size(), 3u);
    EXPECT_EQ(shape.Cases[1].Name, "Round");
    EXPECT_EQ(shape.Cases[1].Value, 1);
    EXPECT_EQ(shape.Cases[1].Doc, "No corners.");
}

TEST(ReflectTypeInfoTest, EnumValuesExtendAsTheIntegerEncodingDoes)
{
    const TypeInfo& negative = GetTypeInfo<typeinfotest::Signed>();
    const EnumCaseInfo* below = FindEnumCase(negative, "Below");

    ASSERT_NE(below, nullptr);
    EXPECT_EQ(below->Value, -5);
    EXPECT_TRUE(negative.IntegerIsSigned);

    const TypeInfo& wide = GetTypeInfo<typeinfotest::Wide>();
    const EnumCaseInfo* top = FindEnumCase(wide, "Top");

    ASSERT_NE(top, nullptr);
    EXPECT_EQ(static_cast<uint64_t>(top->Value), 0xFFFFFFFFFFFFFFFFull);
    EXPECT_FALSE(wide.IntegerIsSigned);
}

TEST(ReflectTypeInfoTest, SequencesReportElementAndFixedCount)
{
    const TypeInfo& list = FieldType(*g_everythingInfo, "List");

    EXPECT_EQ(list.Element().Kind, TypeKind::Int);
    EXPECT_EQ(list.Element().IntegerWidthBytes, 4);
    EXPECT_EQ(list.FixedElementCount, -1);

    const TypeInfo& fixed = FieldType(*g_everythingInfo, "Fixed");

    EXPECT_EQ(fixed.Element().Kind, TypeKind::Float32);
    EXPECT_EQ(fixed.FixedElementCount, 3);

    const TypeInfo& dynamicSpan = FieldType(*g_everythingInfo, "DynamicSpan");

    EXPECT_EQ(dynamicSpan.Element().Kind, TypeKind::Int);
    EXPECT_FALSE(dynamicSpan.Element().IntegerIsSigned);
    EXPECT_EQ(dynamicSpan.FixedElementCount, -1);

    const TypeInfo& fixedSpan = FieldType(*g_everythingInfo, "FixedSpan");

    EXPECT_EQ(fixedSpan.FixedElementCount, 2);
}

TEST(ReflectTypeInfoTest, OptionalAndVariantReportTheirTypes)
{
    const TypeInfo& maybe = FieldType(*g_everythingInfo, "Maybe");

    EXPECT_EQ(maybe.Element().Name, "typeinfotest.Leaf");

    const TypeInfo& choice = FieldType(*g_everythingInfo, "Choice");

    ASSERT_EQ(choice.Alternatives.size(), 2u);
    EXPECT_EQ(choice.Alternatives[0]().Name, "typeinfotest.Leaf");
    EXPECT_EQ(choice.Alternatives[1]().Name, "typeinfotest.OtherLeaf");
}

TEST(ReflectTypeInfoTest, RecursiveTypeBuildsAFiniteTable)
{
    const TypeInfo& children = FieldType(*g_nodeInfo, "Children");

    EXPECT_EQ(children.Kind, TypeKind::Sequence);
    EXPECT_EQ(children.Element().Name, "typeinfotest.Node");
    EXPECT_EQ(children.Element().Fields.size(), 2u);
    EXPECT_EQ(children.Element().Fields[1].Type().Element().Name, "typeinfotest.Node");
}

TEST(ReflectTypeInfoTest, AddressReachesEachField)
{
    typeinfotest::Everything value{};

    const FieldInfo* flag = FindField(*g_everythingInfo, "Flag");
    const FieldInfo* single = FindField(*g_everythingInfo, "Single");
    const FieldInfo* text = FindField(*g_everythingInfo, "Text");
    const FieldInfo* choice = FindField(*g_everythingInfo, "Choice");

    ASSERT_NE(flag, nullptr);
    ASSERT_NE(single, nullptr);
    ASSERT_NE(text, nullptr);
    ASSERT_NE(choice, nullptr);

    EXPECT_EQ(flag->Address(&value), &value.Flag);
    EXPECT_EQ(single->Address(&value), &value.Single);
    EXPECT_EQ(text->Address(&value), &value.Text);
    EXPECT_EQ(choice->Address(&value), &value.Choice);

    *static_cast<float*>(single->Address(&value)) = 4.5f;

    EXPECT_EQ(value.Single, 4.5f);
}

TEST(ReflectTypeInfoTest, AddressWalksIntoNestedStruct)
{
    typeinfotest::Everything value{};

    const FieldInfo* nested = FindField(*g_everythingInfo, "Nested");

    ASSERT_NE(nested, nullptr);

    const FieldInfo* leafValue = FindField(nested->Type(), "Value");

    ASSERT_NE(leafValue, nullptr);

    void* nestedAddress = nested->Address(&value);
    *static_cast<float*>(leafValue->Address(nestedAddress)) = 2.25f;

    EXPECT_EQ(value.Nested.Value, 2.25f);
}

TEST(ReflectTypeInfoTest, FieldAddressReadsFromConstOwner)
{
    typeinfotest::Everything mutableValue{};
    mutableValue.Small = 42;

    const typeinfotest::Everything& value = mutableValue;

    const FieldInfo* small = FindField(*g_everythingInfo, "Small");

    ASSERT_NE(small, nullptr);

    const void* address = foundation::reflect::FieldAddress(*small, &value);

    EXPECT_EQ(address, &value.Small);
    EXPECT_EQ(*static_cast<const int16_t*>(address), 42);
}

TEST(ReflectTypeInfoTest, FindReturnsNullForUnknownNames)
{
    EXPECT_EQ(FindField(*g_everythingInfo, "Sngle"), nullptr);
    EXPECT_EQ(FindField(*g_everythingInfo, "single"), nullptr);
    EXPECT_EQ(FindField(GetTypeInfo<float>(), "Value"), nullptr);

    const TypeInfo& shape = GetTypeInfo<typeinfotest::Shape>();

    EXPECT_EQ(FindEnumCase(shape, std::string_view{ "Hexagon" }), nullptr);
    EXPECT_EQ(FindEnumCase(shape, int64_t{ 99 }), nullptr);
}

TEST(ReflectTypeInfoTest, FindEnumCaseByValue)
{
    const TypeInfo& negative = GetTypeInfo<typeinfotest::Signed>();
    const EnumCaseInfo* below = FindEnumCase(negative, int64_t{ -5 });

    ASSERT_NE(below, nullptr);
    EXPECT_EQ(below->Name, "Below");
}

TEST(ReflectTypeInfoTest, EncodeAcceptsEveryKindInTheTable)
{
    typeinfotest::Node root{};
    root.Id = 1;
    root.Children.push_back(typeinfotest::Node{ .Id = 2, .Children = {} });

    foundation::content::TextSink nodeSink;
    foundation::content::Encode(root, nodeSink);

    EXPECT_FALSE(nodeSink.Text().empty());

    typeinfotest::Everything value{};
    value.Form = typeinfotest::Shape::Round;
    value.Offset = typeinfotest::Signed::Below;

    foundation::content::TextSink everythingSink;
    foundation::content::Encode(value, everythingSink);

    EXPECT_FALSE(everythingSink.Text().empty());
}
