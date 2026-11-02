#include <gtest/gtest.h>

#include "content/Encode.hpp"
#include "content/HashSink.hpp"
#include "content/TextSink.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace enctest
{
    enum class Mode : uint8_t
    {
        Invalid = 0,
        Slow,
        Fast,
    };

    struct Scalars
    {
        bool Flag{ false };
        int8_t Small{ 0 };
        uint8_t SmallUnsigned{ 0 };
        int64_t Big{ 0 };
        uint64_t BigUnsigned{ 0 };
        double Wide{ 0.0 };
        Mode Chosen{ Mode::Invalid };
    };

    struct TwoStrings
    {
        std::string First;
        std::string Second;
    };

    struct MaybeCount
    {
        std::optional<uint32_t> Count;
    };

    struct ListOfCounts
    {
        std::vector<uint32_t> Counts;
    };

    struct FixedCounts
    {
        std::array<uint32_t, 2> Counts{ 0, 0 };
    };

    struct SpanOfCounts
    {
        std::span<const uint32_t> Counts;
    };

    struct Leaf
    {
        float Value{ 0.0f };
    };

    // the same field as Leaf under another descriptor name
    struct OtherLeaf
    {
        float Value{ 0.0f };
    };

    struct VariantHolder
    {
        std::variant<Leaf, OtherLeaf> Choice;
    };

    struct LeafHolder
    {
        Leaf Direct;
    };

    struct OptionalLeafHolder
    {
        std::optional<Leaf> Maybe;
    };

    struct LeafList
    {
        std::vector<Leaf> Leaves;
    };

    struct DoubleArray
    {
        std::array<double, 3> Values{ 0.0, 0.0, 0.0 };
    };

    foundation::content::Hash128 HashOf(const auto& value);
    std::string TextOf(const auto& value);
}

namespace foundation::reflect
{
    template<>
    struct Describe<enctest::Mode>
    {
        static constexpr std::string_view Name = "Mode";
        static constexpr std::array<EnumCase<enctest::Mode>, 3> Cases
        {
            EnumCase<enctest::Mode>{ "Invalid", enctest::Mode::Invalid },
            EnumCase<enctest::Mode>{ "Slow", enctest::Mode::Slow },
            EnumCase<enctest::Mode>{ "Fast", enctest::Mode::Fast },
        };
    };

    template<>
    struct Describe<enctest::Scalars>
    {
        using T = enctest::Scalars;

        static constexpr std::string_view Name = "Scalars";
        static constexpr std::tuple Fields
        {
            Field<T, bool>{ "Flag", &T::Flag },
            Field<T, int8_t>{ "Small", &T::Small },
            Field<T, uint8_t>{ "SmallUnsigned", &T::SmallUnsigned },
            Field<T, int64_t>{ "Big", &T::Big },
            Field<T, uint64_t>{ "BigUnsigned", &T::BigUnsigned },
            Field<T, double>{ "Wide", &T::Wide },
            Field<T, enctest::Mode>{ "Chosen", &T::Chosen },
        };
    };

    template<>
    struct Describe<enctest::TwoStrings>
    {
        using T = enctest::TwoStrings;

        static constexpr std::string_view Name = "TwoStrings";
        static constexpr std::tuple Fields
        {
            Field<T, std::string>{ "First", &T::First },
            Field<T, std::string>{ "Second", &T::Second },
        };
    };

    template<>
    struct Describe<enctest::MaybeCount>
    {
        using T = enctest::MaybeCount;

        static constexpr std::string_view Name = "MaybeCount";
        static constexpr std::tuple Fields
        {
            Field<T, std::optional<uint32_t>>{ "Count", &T::Count },
        };
    };

    template<>
    struct Describe<enctest::ListOfCounts>
    {
        using T = enctest::ListOfCounts;

        static constexpr std::string_view Name = "Counts";
        static constexpr std::tuple Fields
        {
            Field<T, std::vector<uint32_t>>{ "Counts", &T::Counts },
        };
    };

    template<>
    struct Describe<enctest::FixedCounts>
    {
        using T = enctest::FixedCounts;

        static constexpr std::string_view Name = "Counts";
        static constexpr std::tuple Fields
        {
            Field<T, std::array<uint32_t, 2>>{ "Counts", &T::Counts },
        };
    };

    template<>
    struct Describe<enctest::SpanOfCounts>
    {
        using T = enctest::SpanOfCounts;

        static constexpr std::string_view Name = "Counts";
        static constexpr std::tuple Fields
        {
            Field<T, std::span<const uint32_t>>{ "Counts", &T::Counts },
        };
    };

    template<>
    struct Describe<enctest::Leaf>
    {
        using T = enctest::Leaf;

        static constexpr std::string_view Name = "Leaf";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<enctest::OtherLeaf>
    {
        using T = enctest::OtherLeaf;

        static constexpr std::string_view Name = "OtherLeaf";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<enctest::VariantHolder>
    {
        using T = enctest::VariantHolder;

        static constexpr std::string_view Name = "VariantHolder";
        static constexpr std::tuple Fields
        {
            Field<T, std::variant<enctest::Leaf, enctest::OtherLeaf>>{ "Choice", &T::Choice },
        };
    };

    template<>
    struct Describe<enctest::LeafHolder>
    {
        using T = enctest::LeafHolder;

        static constexpr std::string_view Name = "LeafHolder";
        static constexpr std::tuple Fields
        {
            Field<T, enctest::Leaf>{ "Direct", &T::Direct },
        };
    };

    template<>
    struct Describe<enctest::OptionalLeafHolder>
    {
        using T = enctest::OptionalLeafHolder;

        static constexpr std::string_view Name = "OptionalLeafHolder";
        static constexpr std::tuple Fields
        {
            Field<T, std::optional<enctest::Leaf>>{ "Maybe", &T::Maybe },
        };
    };

    template<>
    struct Describe<enctest::LeafList>
    {
        using T = enctest::LeafList;

        static constexpr std::string_view Name = "LeafList";
        static constexpr std::tuple Fields
        {
            Field<T, std::vector<enctest::Leaf>>{ "Leaves", &T::Leaves },
        };
    };

    template<>
    struct Describe<enctest::DoubleArray>
    {
        using T = enctest::DoubleArray;

        static constexpr std::string_view Name = "DoubleArray";
        static constexpr std::tuple Fields
        {
            Field<T, std::array<double, 3>>{ "Values", &T::Values },
        };
    };
}

using namespace foundation::content;

TEST(ContentEncodeTest, ScalarsPrintTheirWidthSignAndValue)
{
    enctest::Scalars scalars;
    scalars.Flag = true;
    scalars.Small = -1;
    scalars.SmallUnsigned = 255;
    scalars.Big = std::numeric_limits<int64_t>::min();
    scalars.BigUnsigned = std::numeric_limits<uint64_t>::max();
    scalars.Wide = 0.5;
    scalars.Chosen = enctest::Mode::Fast;

    const std::string expected =
        "Scalars {\n"
        "  Flag: bool true\n"
        "  Small: i8 -1\n"
        "  SmallUnsigned: u8 255\n"
        "  Big: i64 -9223372036854775808\n"
        "  BigUnsigned: u64 18446744073709551615\n"
        "  Wide: f64 0.5 (0x3fe0000000000000)\n"
        "  Chosen: enum Mode::Fast\n"
        "}\n";

    EXPECT_EQ(enctest::TextOf(scalars), expected);
}

TEST(ContentEncodeTest, StringBoundariesMatter)
{
    const enctest::TwoStrings first{ "ab", "c" };
    const enctest::TwoStrings second{ "a", "bc" };

    const Hash128 first_hash = enctest::HashOf(first);
    const Hash128 second_hash = enctest::HashOf(second);

    EXPECT_TRUE(first_hash.Low != second_hash.Low || first_hash.High != second_hash.High);
}

TEST(ContentEncodeTest, AbsentOptionalDiffersFromPresentOptionalHoldingZero)
{
    const enctest::MaybeCount absent{ std::nullopt };
    const enctest::MaybeCount present_zero{ 0u };

    const Hash128 absent_hash = enctest::HashOf(absent);
    const Hash128 present_hash = enctest::HashOf(present_zero);

    EXPECT_TRUE(absent_hash.Low != present_hash.Low || absent_hash.High != present_hash.High);
}

TEST(ContentEncodeTest, VectorArrayAndSpanOfTheSameElementsEncodeTheSame)
{
    const std::vector<uint32_t> storage{ 4, 5 };

    const enctest::ListOfCounts list{ storage };
    const enctest::FixedCounts fixed{ { 4, 5 } };
    const enctest::SpanOfCounts view{ std::span<const uint32_t>{ storage } };

    const Hash128 list_hash = enctest::HashOf(list);
    const Hash128 fixed_hash = enctest::HashOf(fixed);
    const Hash128 view_hash = enctest::HashOf(view);

    EXPECT_EQ(list_hash.Low, fixed_hash.Low);
    EXPECT_EQ(list_hash.High, fixed_hash.High);
    EXPECT_EQ(list_hash.Low, view_hash.Low);
    EXPECT_EQ(list_hash.High, view_hash.High);
}

TEST(ContentEncodeTest, ElementCountChangesTheHash)
{
    const enctest::ListOfCounts empty{};
    const enctest::ListOfCounts one_zero{ { 0 } };

    const Hash128 empty_hash = enctest::HashOf(empty);
    const Hash128 one_hash = enctest::HashOf(one_zero);

    EXPECT_TRUE(empty_hash.Low != one_hash.Low || empty_hash.High != one_hash.High);
}

TEST(ContentEncodeTest, VariantAlternativeNameChangesTheHashAndThePrintedText)
{
    enctest::VariantHolder first;
    first.Choice = enctest::Leaf{ 2.0f };

    enctest::VariantHolder second;
    second.Choice = enctest::OtherLeaf{ 2.0f };

    const Hash128 first_hash = enctest::HashOf(first);
    const Hash128 second_hash = enctest::HashOf(second);

    EXPECT_TRUE(first_hash.Low != second_hash.Low || first_hash.High != second_hash.High);

    const std::string expected =
        "VariantHolder {\n"
        "  Choice: variant(OtherLeaf) OtherLeaf {\n"
        "    Value: f32 2 (0x40000000)\n"
        "  }\n"
        "}\n";

    EXPECT_EQ(enctest::TextOf(second), expected);
}

TEST(ContentEncodeTest, AVariantWrapsTheValueSoItDiffersFromTheValueAlone)
{
    enctest::VariantHolder variant_holder;
    variant_holder.Choice = enctest::Leaf{ 2.0f };

    const enctest::LeafHolder direct{ enctest::Leaf{ 2.0f } };

    const Hash128 variant_hash = enctest::HashOf(variant_holder);
    const Hash128 direct_hash = enctest::HashOf(direct);

    EXPECT_TRUE(variant_hash.Low != direct_hash.Low || variant_hash.High != direct_hash.High);
}

TEST(ContentEncodeTest, OptionalStructAndStructListPrintNested)
{
    const enctest::OptionalLeafHolder absent{};
    const enctest::OptionalLeafHolder present{ enctest::Leaf{ 1.0f } };
    const enctest::LeafList list{ { enctest::Leaf{ 1.0f }, enctest::Leaf{ -0.0f } } };

    EXPECT_EQ(enctest::TextOf(absent), "OptionalLeafHolder {\n  Maybe: none\n}\n");

    const std::string present_expected =
        "OptionalLeafHolder {\n"
        "  Maybe: some Leaf {\n"
        "    Value: f32 1 (0x3f800000)\n"
        "  }\n"
        "}\n";

    EXPECT_EQ(enctest::TextOf(present), present_expected);

    const std::string list_expected =
        "LeafList {\n"
        "  Leaves: array(2) [\n"
        "    [0]: Leaf {\n"
        "      Value: f32 1 (0x3f800000)\n"
        "    }\n"
        "    [1]: Leaf {\n"
        "      Value: f32 -0 (0x80000000)\n"
        "    }\n"
        "  ]\n"
        "}\n";

    EXPECT_EQ(enctest::TextOf(list), list_expected);
}

TEST(ContentEncodeTest, FindNaNReturnsNothingForFiniteValuesAndInfinity)
{
    enctest::DoubleArray values;
    values.Values = { 1.0, std::numeric_limits<double>::infinity(), -2.0 };

    EXPECT_FALSE(FindNaN(values).has_value());
    EXPECT_FALSE(FindNaN(enctest::Scalars{}).has_value());
}

TEST(ContentEncodeTest, FindNaNPathsCoverArraysOptionalsAndVariants)
{
    enctest::DoubleArray values;
    values.Values = { 1.0, 2.0, std::numeric_limits<double>::quiet_NaN() };

    const std::optional<std::string> array_path = FindNaN(values);
    ASSERT_TRUE(array_path.has_value());
    EXPECT_EQ(*array_path, "Values[2]");

    const enctest::OptionalLeafHolder optional_holder{ enctest::Leaf{ std::numeric_limits<float>::quiet_NaN() } };
    const std::optional<std::string> optional_path = FindNaN(optional_holder);
    ASSERT_TRUE(optional_path.has_value());
    EXPECT_EQ(*optional_path, "Maybe.Value");

    enctest::VariantHolder variant_holder;
    variant_holder.Choice = enctest::OtherLeaf{ std::numeric_limits<float>::quiet_NaN() };
    const std::optional<std::string> variant_path = FindNaN(variant_holder);
    ASSERT_TRUE(variant_path.has_value());
    EXPECT_EQ(*variant_path, "Choice.Value");

    const enctest::LeafList list{ { enctest::Leaf{ 1.0f }, enctest::Leaf{ std::numeric_limits<float>::quiet_NaN() } } };
    const std::optional<std::string> list_path = FindNaN(list);
    ASSERT_TRUE(list_path.has_value());
    EXPECT_EQ(*list_path, "Leaves[1].Value");
}

namespace enctest
{
    foundation::content::Hash128 HashOf(const auto& value)
    {
        foundation::content::HashSink sink;
        foundation::content::Encode(value, sink);

        return sink.Finish();
    }

    std::string TextOf(const auto& value)
    {
        foundation::content::TextSink sink;
        foundation::content::Encode(value, sink);

        return sink.Text();
    }
}
