#include <gtest/gtest.h>

#include "content/BytesSink.hpp"
#include "content/Encode.hpp"
#include "reflect/ValueAccess.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace accesstest
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
    };

    struct Leaf
    {
        float Value{ 0.0f };
        float Locked{ 1.0f };
    };

    struct OtherLeaf
    {
        int32_t Count{ 0 };
    };

    inline constexpr std::array<uint32_t, 2> k_ConstSource{ 1u, 2u };

    struct Root
    {
        bool Flag{ false };
        int8_t Tiny{ 0 };
        uint8_t TinyUnsigned{ 0u };
        int32_t Ranged{ 5 };
        int64_t Big{ 0 };
        uint64_t BigUnsigned{ 0u };
        float Single{ 0.0f };
        float Tenth{ 0.1f };
        float NoUpper{ 1.0f };
        double Precise{ 0.0 };
        std::string Text;
        std::string_view View;
        Shape Form{ Shape::Invalid };
        Signed Offset{ Signed::Invalid };
        Leaf Nested;
        Leaf ReadOnlyLeaf;
        std::vector<Leaf> Leaves;
        std::array<float, 3> Fixed{ 0.0f, 0.0f, 0.0f };
        std::vector<float> RangedList;
        std::span<const uint32_t> ConstSpan{ k_ConstSource };
        std::optional<Leaf> MaybeLeaf;
        std::optional<float> MaybeNumber;
        std::variant<Leaf, OtherLeaf> Choice;
        std::array<std::array<int32_t, 2>, 2> Grid{};
        std::optional<std::vector<int32_t>> MaybeList;
    };
}

namespace foundation::reflect
{
    template<>
    struct Describe<accesstest::Shape>
    {
        static constexpr std::string_view Name = "accesstest.Shape";
        static constexpr std::array<EnumCase<accesstest::Shape>, 3> Cases
        {
            EnumCase<accesstest::Shape>{ "Invalid", accesstest::Shape::Invalid },
            EnumCase<accesstest::Shape>{ "Round", accesstest::Shape::Round },
            EnumCase<accesstest::Shape>{ "Square", accesstest::Shape::Square },
        };
    };

    template<>
    struct Describe<accesstest::Signed>
    {
        static constexpr std::string_view Name = "accesstest.Signed";
        static constexpr std::array<EnumCase<accesstest::Signed>, 2> Cases
        {
            EnumCase<accesstest::Signed>{ "Invalid", accesstest::Signed::Invalid },
            EnumCase<accesstest::Signed>{ "Below", accesstest::Signed::Below },
        };
    };

    template<>
    struct Describe<accesstest::Leaf>
    {
        using T = accesstest::Leaf;

        static constexpr std::string_view Name = "accesstest.Leaf";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value, { .Range = FieldRange{ 0.0, 10.0 } } },
            Field<T, float>{ "Locked", &T::Locked, { .ReadOnly = true } },
        };
    };

    template<>
    struct Describe<accesstest::OtherLeaf>
    {
        using T = accesstest::OtherLeaf;

        static constexpr std::string_view Name = "accesstest.OtherLeaf";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Count", &T::Count },
        };
    };

    template<>
    struct Describe<accesstest::Root>
    {
        using T = accesstest::Root;

        static constexpr std::string_view Name = "accesstest.Root";
        static constexpr std::tuple Fields
        {
            Field<T, bool>{ "Flag", &T::Flag },
            Field<T, int8_t>{ "Tiny", &T::Tiny },
            Field<T, uint8_t>{ "TinyUnsigned", &T::TinyUnsigned },
            Field<T, int32_t>{ "Ranged", &T::Ranged, { .Range = FieldRange{ 0.0, 10.0 } } },
            Field<T, int64_t>{ "Big", &T::Big },
            Field<T, uint64_t>{ "BigUnsigned", &T::BigUnsigned },
            Field<T, float>{ "Single", &T::Single, { .Hidden = true } },
            Field<T, float>{ "Tenth", &T::Tenth, { .Range = FieldRange{ 0.0, 0.1 } } },
            Field<T, float>{ "NoUpper", &T::NoUpper, { .Range = FieldRange{ 0.0, k_NoLimit } } },
            Field<T, double>{ "Precise", &T::Precise },
            Field<T, std::string>{ "Text", &T::Text },
            Field<T, std::string_view>{ "View", &T::View },
            Field<T, accesstest::Shape>{ "Form", &T::Form },
            Field<T, accesstest::Signed>{ "Offset", &T::Offset },
            Field<T, accesstest::Leaf>{ "Nested", &T::Nested },
            Field<T, accesstest::Leaf>{ "ReadOnlyLeaf", &T::ReadOnlyLeaf, { .ReadOnly = true } },
            Field<T, std::vector<accesstest::Leaf>>{ "Leaves", &T::Leaves },
            Field<T, std::array<float, 3>>{ "Fixed", &T::Fixed },
            Field<T, std::vector<float>>{ "RangedList", &T::RangedList, { .Range = FieldRange{ 0.0, 1.0 } } },
            Field<T, std::span<const uint32_t>>{ "ConstSpan", &T::ConstSpan },
            Field<T, std::optional<accesstest::Leaf>>{ "MaybeLeaf", &T::MaybeLeaf },
            Field<T, std::optional<float>>{ "MaybeNumber", &T::MaybeNumber },
            Field<T, std::variant<accesstest::Leaf, accesstest::OtherLeaf>>{ "Choice", &T::Choice },
            Field<T, std::array<std::array<int32_t, 2>, 2>>{ "Grid", &T::Grid },
            Field<T, std::optional<std::vector<int32_t>>>{ "MaybeList", &T::MaybeList },
        };
    };
}

namespace
{
    using accesstest::Root;
    using foundation::reflect::GetTypeInfo;
    using foundation::reflect::PathError;
    using foundation::reflect::ResolvedValue;
    using foundation::reflect::ResolvePath;
    using foundation::reflect::ScalarValue;
    using foundation::reflect::SetValue;
    using foundation::reflect::TypeInfo;
    using foundation::reflect::TypeKind;
    using foundation::reflect::ValueError;

    const TypeInfo& RootInfo()
    {
        return GetTypeInfo<Root>();
    }

    std::vector<std::byte> EncodedBytes(const Root& value)
    {
        foundation::content::BytesSink sink;
        foundation::content::Encode(value, sink);

        return sink.TakeBytes();
    }

    void ExpectWritten(Root& root, std::string_view path, const ScalarValue& value)
    {
        const std::expected<void, PathError> result = SetValue(RootInfo(), &root, path, value);

        ASSERT_TRUE(result.has_value()) << path << ": "
                                        << foundation::reflect::ValueErrorText(result.error().Code);
    }

    // the property that the agent tools rely on: a refused write changes no byte of the object
    void ExpectRefused(Root& root, std::string_view path, const ScalarValue& value, ValueError expected)
    {
        const std::vector<std::byte> before = EncodedBytes(root);
        const std::expected<void, PathError> result = SetValue(RootInfo(), &root, path, value);

        ASSERT_FALSE(result.has_value()) << path;
        EXPECT_EQ(result.error().Code, expected) << path;
        EXPECT_EQ(EncodedBytes(root), before) << path;
    }

    void ExpectPathError(const Root& root, std::string_view path, ValueError expected, int32_t offset)
    {
        const std::expected<ResolvedValue, PathError> result = ResolvePath(RootInfo(), &root, path);

        ASSERT_FALSE(result.has_value()) << path;
        EXPECT_EQ(result.error().Code, expected) << path;
        EXPECT_EQ(result.error().Offset, offset) << path;
    }
}

TEST(ReflectValueAccessTest, EmptyPathReturnsTheRoot)
{
    const Root root{};
    const std::expected<ResolvedValue, PathError> resolved = ResolvePath(RootInfo(), &root, "");

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->Address, &root);
    EXPECT_EQ(resolved->Type->Kind, TypeKind::Struct);
    EXPECT_EQ(resolved->Metadata, nullptr);
    EXPECT_TRUE(resolved->Writable);
}

TEST(ReflectValueAccessTest, PathsReachNestedElementsAndKeepTheAddress)
{
    Root root{};
    root.Leaves.resize(3);
    root.MaybeLeaf = accesstest::Leaf{};
    root.Choice = accesstest::OtherLeaf{};
    root.MaybeList = std::vector<int32_t>{ 1, 2 };

    const auto addressOf = [&root](std::string_view path) -> const void*
    {
        const std::expected<ResolvedValue, PathError> resolved = ResolvePath(RootInfo(), &root, path);

        return resolved.has_value() ? resolved->Address : nullptr;
    };

    EXPECT_EQ(addressOf("Nested.Value"), &root.Nested.Value);
    EXPECT_EQ(addressOf("Leaves[2].Value"), &root.Leaves[2].Value);
    EXPECT_EQ(addressOf("Fixed[1]"), &root.Fixed[1]);
    EXPECT_EQ(addressOf("Grid[1][0]"), &root.Grid[1][0]);
    EXPECT_EQ(addressOf("MaybeLeaf.Value"), &root.MaybeLeaf->Value);
    EXPECT_EQ(addressOf("MaybeList[1]"), &(*root.MaybeList)[1]);
    EXPECT_EQ(addressOf("Choice.Count"), &std::get<accesstest::OtherLeaf>(root.Choice).Count);
    EXPECT_EQ(addressOf("ConstSpan[1]"), &accesstest::k_ConstSource[1]);
}

TEST(ReflectValueAccessTest, MalformedPathsReportTheirOffset)
{
    const Root root{};

    ExpectPathError(root, ".Flag", ValueError::MalformedPath, 0);
    ExpectPathError(root, "Nested.", ValueError::MalformedPath, 6);
    ExpectPathError(root, "Nested..Value", ValueError::MalformedPath, 6);
    ExpectPathError(root, "Fixed[", ValueError::MalformedPath, 5);
    ExpectPathError(root, "Fixed[]", ValueError::MalformedPath, 5);
    ExpectPathError(root, "Fixed[-1]", ValueError::MalformedPath, 5);
    ExpectPathError(root, "Fixed[1", ValueError::MalformedPath, 5);
    ExpectPathError(root, "Fixed[x]", ValueError::MalformedPath, 5);
    ExpectPathError(root, "Fixed[99999999999999999999]", ValueError::MalformedPath, 5);
    ExpectPathError(root, "Fixed[1]Value", ValueError::MalformedPath, 8);
}

TEST(ReflectValueAccessTest, WalkErrorsNameTheFailedStep)
{
    const Root root{};

    ExpectPathError(root, "Nope", ValueError::UnknownField, 0);
    ExpectPathError(root, "flag", ValueError::UnknownField, 0);
    ExpectPathError(root, "Nested.Nope", ValueError::UnknownField, 6);
    ExpectPathError(root, "Flag.Value", ValueError::NotAStruct, 4);
    ExpectPathError(root, "Flag[0]", ValueError::NotASequence, 4);
    ExpectPathError(root, "Fixed[3]", ValueError::IndexOutOfRange, 5);
    ExpectPathError(root, "Leaves[0]", ValueError::IndexOutOfRange, 6);
    ExpectPathError(root, "MaybeLeaf.Value", ValueError::NoActiveValue, 9);
    ExpectPathError(root, "MaybeList[0]", ValueError::NoActiveValue, 9);
    ExpectPathError(root, "Choice.Count", ValueError::UnknownField, 6);
}

TEST(ReflectValueAccessTest, ReadOnlyAppliesToTheWholeSubtree)
{
    Root root{};

    const std::expected<ResolvedValue, PathError> inside =
        ResolvePath(RootInfo(), &root, "ReadOnlyLeaf.Value");

    ASSERT_TRUE(inside.has_value());
    EXPECT_FALSE(inside->Writable);

    ExpectRefused(root, "ReadOnlyLeaf.Value", 1.0, ValueError::ReadOnly);
    ExpectRefused(root, "Nested.Locked", 1.0, ValueError::ReadOnly);
    ExpectWritten(root, "Nested.Value", 1.0);
    EXPECT_EQ(root.Nested.Value, 1.0f);
}

TEST(ReflectValueAccessTest, HiddenFieldsRemainAddressable)
{
    Root root{};

    ExpectWritten(root, "Single", 2.5);
    EXPECT_EQ(root.Single, 2.5f);
}

TEST(ReflectValueAccessTest, ConstSpanElementsAndViewsAreReadOnly)
{
    Root root{};

    ExpectRefused(root, "ConstSpan[0]", int64_t{ 9 }, ValueError::ReadOnly);
    ExpectRefused(root, "View", std::string_view{ "text" }, ValueError::ReadOnly);
    EXPECT_EQ(accesstest::k_ConstSource[0], 1u);
}

TEST(ReflectValueAccessTest, ScalarWritesReachTheirFields)
{
    Root root{};
    root.Leaves.resize(2);
    root.RangedList.resize(1);

    ExpectWritten(root, "Flag", true);
    ExpectWritten(root, "Tiny", int64_t{ -128 });
    ExpectWritten(root, "TinyUnsigned", uint64_t{ 255u });
    ExpectWritten(root, "Big", std::numeric_limits<int64_t>::min());
    ExpectWritten(root, "BigUnsigned", std::numeric_limits<uint64_t>::max());
    ExpectWritten(root, "Precise", 0.125);
    ExpectWritten(root, "Text", std::string_view{ "written" });
    ExpectWritten(root, "Form", std::string_view{ "Square" });
    ExpectWritten(root, "Offset", std::string_view{ "Below" });
    ExpectWritten(root, "Leaves[1].Value", 7.0);
    ExpectWritten(root, "Fixed[2]", 3.0);
    ExpectWritten(root, "Grid[1][1]", int64_t{ 4 });
    ExpectWritten(root, "Choice.Value", 2.0);

    EXPECT_TRUE(root.Flag);
    EXPECT_EQ(root.Tiny, -128);
    EXPECT_EQ(root.TinyUnsigned, 255u);
    EXPECT_EQ(root.Big, std::numeric_limits<int64_t>::min());
    EXPECT_EQ(root.BigUnsigned, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(root.Precise, 0.125);
    EXPECT_EQ(root.Text, "written");
    EXPECT_EQ(root.Form, accesstest::Shape::Square);
    EXPECT_EQ(root.Offset, accesstest::Signed::Below);
    EXPECT_EQ(root.Leaves[0].Value, 0.0f);
    EXPECT_EQ(root.Leaves[1].Value, 7.0f);
    EXPECT_EQ(root.Fixed[2], 3.0f);
    EXPECT_EQ(root.Grid[1][1], 4);
    EXPECT_EQ(root.Grid[0][1], 0);
    EXPECT_EQ(std::get<accesstest::Leaf>(root.Choice).Value, 2.0f);
}

TEST(ReflectValueAccessTest, WritesNeverTouchTheNeighboringBytes)
{
    Root root{};
    root.Tiny = 1;
    root.TinyUnsigned = 2u;

    ExpectWritten(root, "Tiny", int64_t{ -1 });

    EXPECT_EQ(root.Tiny, -1);
    EXPECT_EQ(root.TinyUnsigned, 2u);
    EXPECT_EQ(root.Flag, false);
}

TEST(ReflectValueAccessTest, FloatFieldsAcceptIntegers)
{
    Root root{};

    ExpectWritten(root, "Single", int64_t{ 3 });
    EXPECT_EQ(root.Single, 3.0f);

    ExpectWritten(root, "Precise", uint64_t{ 4u });
    EXPECT_EQ(root.Precise, 4.0);
}

TEST(ReflectValueAccessTest, WrongValueTypesAreRefused)
{
    Root root{};

    ExpectRefused(root, "Flag", int64_t{ 1 }, ValueError::WrongValueType);
    ExpectRefused(root, "Tiny", 1.0, ValueError::WrongValueType);
    ExpectRefused(root, "Tiny", true, ValueError::WrongValueType);
    ExpectRefused(root, "Single", true, ValueError::WrongValueType);
    ExpectRefused(root, "Single", std::string_view{ "1.0" }, ValueError::WrongValueType);
    ExpectRefused(root, "Text", 1.0, ValueError::WrongValueType);
    ExpectRefused(root, "Form", int64_t{ 1 }, ValueError::WrongValueType);
}

TEST(ReflectValueAccessTest, ContainersAreNotScalars)
{
    Root root{};
    root.MaybeLeaf = accesstest::Leaf{};

    ExpectRefused(root, "", 1.0, ValueError::NotAScalar);
    ExpectRefused(root, "Nested", 1.0, ValueError::NotAScalar);
    ExpectRefused(root, "Fixed", 1.0, ValueError::NotAScalar);
    ExpectRefused(root, "MaybeLeaf", 1.0, ValueError::NotAScalar);
    ExpectRefused(root, "Choice", 1.0, ValueError::NotAScalar);
}

TEST(ReflectValueAccessTest, OptionalScalarsWriteOnlyWhenFilled)
{
    Root root{};

    ExpectRefused(root, "MaybeNumber", 1.0, ValueError::NoActiveValue);
    EXPECT_FALSE(root.MaybeNumber.has_value());

    root.MaybeNumber = 0.0f;

    ExpectWritten(root, "MaybeNumber", 1.0);
    EXPECT_EQ(root.MaybeNumber, 1.0f);
}

TEST(ReflectValueAccessTest, IntegersMustFitWidthAndSign)
{
    Root root{};

    ExpectRefused(root, "Tiny", int64_t{ 128 }, ValueError::OutOfRange);
    ExpectRefused(root, "Tiny", int64_t{ -129 }, ValueError::OutOfRange);
    ExpectRefused(root, "Tiny", uint64_t{ 128u }, ValueError::OutOfRange);
    ExpectRefused(root, "TinyUnsigned", int64_t{ -1 }, ValueError::OutOfRange);
    ExpectRefused(root, "TinyUnsigned", int64_t{ 256 }, ValueError::OutOfRange);
    ExpectRefused(root, "Big", std::numeric_limits<uint64_t>::max(), ValueError::OutOfRange);
    ExpectRefused(root, "BigUnsigned", int64_t{ -1 }, ValueError::OutOfRange);

    ExpectWritten(root, "Tiny", int64_t{ 127 });
    EXPECT_EQ(root.Tiny, 127);

    ExpectWritten(root, "Tiny", uint64_t{ 127u });
    ExpectWritten(root, "TinyUnsigned", int64_t{ 255 });
    EXPECT_EQ(root.TinyUnsigned, 255u);

    ExpectWritten(root, "BigUnsigned", int64_t{ 7 });
    EXPECT_EQ(root.BigUnsigned, 7u);
}

TEST(ReflectValueAccessTest, RangesAreInclusive)
{
    Root root{};

    ExpectRefused(root, "Ranged", int64_t{ -1 }, ValueError::OutOfRange);
    ExpectRefused(root, "Ranged", int64_t{ 11 }, ValueError::OutOfRange);
    ExpectWritten(root, "Ranged", int64_t{ 0 });
    ExpectWritten(root, "Ranged", int64_t{ 10 });
    EXPECT_EQ(root.Ranged, 10);

    ExpectRefused(root, "Nested.Value", -0.001, ValueError::OutOfRange);
    ExpectRefused(root, "Nested.Value", 10.001, ValueError::OutOfRange);
    ExpectWritten(root, "Nested.Value", 0.0);
    ExpectWritten(root, "Nested.Value", 10.0);
    EXPECT_EQ(root.Nested.Value, 10.0f);
}

TEST(ReflectValueAccessTest, RangeOfAFieldAppliesToItsElements)
{
    Root root{};
    root.RangedList.resize(2);

    ExpectRefused(root, "RangedList[1]", 1.5, ValueError::OutOfRange);
    ExpectWritten(root, "RangedList[1]", 0.5);
    EXPECT_EQ(root.RangedList[1], 0.5f);
}

TEST(ReflectValueAccessTest, Float32ValueReadBackIsNeverRefusedByItsOwnLimit)
{
    Root root{};

    // 0.1f is 0.100000001 as a double, which is above a limit of 0.1
    ASSERT_GT(static_cast<double>(root.Tenth), 0.1);

    ExpectWritten(root, "Tenth", static_cast<double>(root.Tenth));
    EXPECT_EQ(root.Tenth, 0.1f);

    ExpectRefused(root, "Tenth", 0.2, ValueError::OutOfRange);
}

TEST(ReflectValueAccessTest, LimitWithoutAnEndAcceptsTheLargestValues)
{
    Root root{};

    const double largestFloat = static_cast<double>(std::numeric_limits<float>::max());

    ExpectWritten(root, "NoUpper", largestFloat);
    EXPECT_EQ(root.NoUpper, std::numeric_limits<float>::max());

    ExpectRefused(root, "NoUpper", -1.0, ValueError::OutOfRange);
}

TEST(ReflectValueAccessTest, NonFiniteValuesAreRefused)
{
    Root root{};

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();

    ExpectRefused(root, "Single", nan, ValueError::NotFinite);
    ExpectRefused(root, "Single", infinity, ValueError::NotFinite);
    ExpectRefused(root, "Single", -infinity, ValueError::NotFinite);
    ExpectRefused(root, "Precise", nan, ValueError::NotFinite);
    ExpectRefused(root, "Precise", infinity, ValueError::NotFinite);
    ExpectRefused(root, "NoUpper", infinity, ValueError::NotFinite);
}

TEST(ReflectValueAccessTest, DoubleTooLargeForFloat32IsRefused)
{
    Root root{};

    ExpectRefused(root, "Single", 1.0e300, ValueError::OutOfRange);
    ExpectRefused(root, "Single", -1.0e300, ValueError::OutOfRange);

    ExpectWritten(root, "Precise", 1.0e300);
    EXPECT_EQ(root.Precise, 1.0e300);
}

TEST(ReflectValueAccessTest, UnknownEnumCasesAreRefused)
{
    Root root{};

    ExpectRefused(root, "Form", std::string_view{ "Hexagon" }, ValueError::UnknownEnumCase);
    ExpectRefused(root, "Form", std::string_view{ "round" }, ValueError::UnknownEnumCase);
}

TEST(ReflectValueAccessTest, PathErrorsAlsoRefuseTheWrite)
{
    Root root{};

    ExpectRefused(root, "Nope", 1.0, ValueError::UnknownField);
    ExpectRefused(root, "Fixed[9]", 1.0, ValueError::IndexOutOfRange);
    ExpectRefused(root, "Fixed[", 1.0, ValueError::MalformedPath);
}

TEST(ReflectValueAccessTest, ReadIntegerBitsExtendsAsTheEncodingDoes)
{
    const int8_t negative = -1;
    const uint8_t positive = 255u;
    const uint64_t top = std::numeric_limits<uint64_t>::max();
    const accesstest::Signed below = accesstest::Signed::Below;

    using foundation::reflect::ReadIntegerBits;

    EXPECT_EQ(ReadIntegerBits(GetTypeInfo<int8_t>(), &negative), -1);
    EXPECT_EQ(ReadIntegerBits(GetTypeInfo<uint8_t>(), &positive), 255);
    EXPECT_EQ(static_cast<uint64_t>(ReadIntegerBits(GetTypeInfo<uint64_t>(), &top)), top);
    EXPECT_EQ(ReadIntegerBits(GetTypeInfo<accesstest::Signed>(), &below), -5);
}

TEST(ReflectValueAccessTest, EveryErrorHasText)
{
    const std::array<ValueError, 12> errors
    {
        ValueError::MalformedPath,
        ValueError::UnknownField,
        ValueError::NotAStruct,
        ValueError::NotASequence,
        ValueError::IndexOutOfRange,
        ValueError::NoActiveValue,
        ValueError::ReadOnly,
        ValueError::NotAScalar,
        ValueError::WrongValueType,
        ValueError::UnknownEnumCase,
        ValueError::NotFinite,
        ValueError::OutOfRange,
    };

    for (const ValueError error : errors)
    {
        const std::string_view text = foundation::reflect::ValueErrorText(error);

        EXPECT_FALSE(text.empty());
        EXPECT_NE(text, "no error");
        EXPECT_NE(text, "unknown error");
    }
}
