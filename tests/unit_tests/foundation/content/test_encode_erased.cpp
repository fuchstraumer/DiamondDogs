#include <gtest/gtest.h>

#include "content/BytesSink.hpp"
#include "content/Encode.hpp"
#include "content/EncodeErased.hpp"
#include "content/TextSink.hpp"
#include "reflect/TypeInfo.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace erasedtest
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
        int32_t Count{ 0 };
    };

    inline constexpr std::array<uint32_t, 2> k_FixedSource{ 11u, 22u };

    struct Everything
    {
        bool Flag{ false };
        int8_t Tiny{ 0 };
        uint8_t TinyUnsigned{ 0u };
        int16_t Small{ 0 };
        uint16_t SmallUnsigned{ 0u };
        int32_t Medium{ 0 };
        uint32_t MediumUnsigned{ 0u };
        int64_t Big{ 0 };
        uint64_t BigUnsigned{ 0u };
        float Single{ 0.0f };
        double Precise{ 0.0 };
        std::string Text;
        std::string_view View;
        Shape Form{ Shape::Invalid };
        Signed Offset{ Signed::Invalid };
        Wide Extent{ Wide::Invalid };
        Leaf Nested;
        std::vector<int32_t> List;
        std::array<float, 3> Fixed{ 0.0f, 0.0f, 0.0f };
        std::span<const uint32_t> DynamicSpan;
        std::span<const uint32_t, 2> FixedSpan{ k_FixedSource };
        std::vector<Leaf> Leaves;
        std::vector<std::string> Strings;
        std::optional<Leaf> MaybeLeaf;
        std::optional<float> MaybeNumber;
        std::variant<Leaf, OtherLeaf> Choice;
        std::variant<Shape, Leaf> EnumChoice;
        std::vector<std::optional<std::vector<Leaf>>> Deep;
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
    struct Describe<erasedtest::Shape>
    {
        static constexpr std::string_view Name = "erasedtest.Shape";
        static constexpr std::array<EnumCase<erasedtest::Shape>, 3> Cases
        {
            EnumCase<erasedtest::Shape>{ "Invalid", erasedtest::Shape::Invalid },
            EnumCase<erasedtest::Shape>{ "Round", erasedtest::Shape::Round },
            EnumCase<erasedtest::Shape>{ "Square", erasedtest::Shape::Square },
        };
    };

    template<>
    struct Describe<erasedtest::Signed>
    {
        static constexpr std::string_view Name = "erasedtest.Signed";
        static constexpr std::array<EnumCase<erasedtest::Signed>, 3> Cases
        {
            EnumCase<erasedtest::Signed>{ "Invalid", erasedtest::Signed::Invalid },
            EnumCase<erasedtest::Signed>{ "Below", erasedtest::Signed::Below },
            EnumCase<erasedtest::Signed>{ "Above", erasedtest::Signed::Above },
        };
    };

    template<>
    struct Describe<erasedtest::Wide>
    {
        static constexpr std::string_view Name = "erasedtest.Wide";
        static constexpr std::array<EnumCase<erasedtest::Wide>, 2> Cases
        {
            EnumCase<erasedtest::Wide>{ "Invalid", erasedtest::Wide::Invalid },
            EnumCase<erasedtest::Wide>{ "Top", erasedtest::Wide::Top },
        };
    };

    template<>
    struct Describe<erasedtest::Leaf>
    {
        using T = erasedtest::Leaf;

        static constexpr std::string_view Name = "erasedtest.Leaf";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<erasedtest::OtherLeaf>
    {
        using T = erasedtest::OtherLeaf;

        static constexpr std::string_view Name = "erasedtest.OtherLeaf";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Count", &T::Count },
        };
    };

    template<>
    struct Describe<erasedtest::Everything>
    {
        using T = erasedtest::Everything;

        static constexpr std::string_view Name = "erasedtest.Everything";
        static constexpr std::tuple Fields
        {
            Field<T, bool>{ "Flag", &T::Flag },
            Field<T, int8_t>{ "Tiny", &T::Tiny },
            Field<T, uint8_t>{ "TinyUnsigned", &T::TinyUnsigned },
            Field<T, int16_t>{ "Small", &T::Small },
            Field<T, uint16_t>{ "SmallUnsigned", &T::SmallUnsigned },
            Field<T, int32_t>{ "Medium", &T::Medium },
            Field<T, uint32_t>{ "MediumUnsigned", &T::MediumUnsigned },
            Field<T, int64_t>{ "Big", &T::Big },
            Field<T, uint64_t>{ "BigUnsigned", &T::BigUnsigned },
            Field<T, float>{ "Single", &T::Single },
            Field<T, double>{ "Precise", &T::Precise },
            Field<T, std::string>{ "Text", &T::Text },
            Field<T, std::string_view>{ "View", &T::View },
            Field<T, erasedtest::Shape>{ "Form", &T::Form },
            Field<T, erasedtest::Signed>{ "Offset", &T::Offset },
            Field<T, erasedtest::Wide>{ "Extent", &T::Extent },
            Field<T, erasedtest::Leaf>{ "Nested", &T::Nested },
            Field<T, std::vector<int32_t>>{ "List", &T::List },
            Field<T, std::array<float, 3>>{ "Fixed", &T::Fixed },
            Field<T, std::span<const uint32_t>>{ "DynamicSpan", &T::DynamicSpan },
            Field<T, std::span<const uint32_t, 2>>{ "FixedSpan", &T::FixedSpan },
            Field<T, std::vector<erasedtest::Leaf>>{ "Leaves", &T::Leaves },
            Field<T, std::vector<std::string>>{ "Strings", &T::Strings },
            Field<T, std::optional<erasedtest::Leaf>>{ "MaybeLeaf", &T::MaybeLeaf },
            Field<T, std::optional<float>>{ "MaybeNumber", &T::MaybeNumber },
            Field<T, std::variant<erasedtest::Leaf, erasedtest::OtherLeaf>>{ "Choice", &T::Choice },
            Field<T, std::variant<erasedtest::Shape, erasedtest::Leaf>>{ "EnumChoice", &T::EnumChoice },
            Field<T, std::vector<std::optional<std::vector<erasedtest::Leaf>>>>{ "Deep", &T::Deep },
        };
    };

    template<>
    struct Describe<erasedtest::Node>
    {
        using T = erasedtest::Node;

        static constexpr std::string_view Name = "erasedtest.Node";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Id", &T::Id },
            Field<T, std::vector<erasedtest::Node>>{ "Children", &T::Children },
        };
    };
}

namespace
{
    using foundation::content::BytesSink;
    using foundation::content::Encode;
    using foundation::content::EncodeErased;
    using foundation::content::TextSink;
    using foundation::reflect::GetTypeInfo;

    template<typename T>
    void ExpectSameBytesAsEncode(const T& value)
    {
        BytesSink typedSink;
        Encode(value, typedSink);

        BytesSink erasedSink;
        EncodeErased(GetTypeInfo<T>(), &value, erasedSink);

        const std::span<const std::byte> typedBytes = typedSink.Bytes();
        const std::span<const std::byte> erasedBytes = erasedSink.Bytes();

        ASSERT_FALSE(typedBytes.empty());
        ASSERT_EQ(typedBytes.size(), erasedBytes.size());
        EXPECT_TRUE(std::ranges::equal(typedBytes, erasedBytes));
    }

    erasedtest::Everything MakeFullValue()
    {
        static constexpr std::array<uint32_t, 3> k_DynamicSource{ 3u, 4u, 5u };

        erasedtest::Everything value{};
        value.Flag = true;
        value.Tiny = -100;
        value.TinyUnsigned = 200u;
        value.Small = -30000;
        value.SmallUnsigned = 60000u;
        value.Medium = -2000000000;
        value.MediumUnsigned = 4000000000u;
        value.Big = std::numeric_limits<int64_t>::min();
        value.BigUnsigned = std::numeric_limits<uint64_t>::max();
        value.Single = std::numbers::pi_v<float>;
        value.Precise = -std::numbers::e_v<double>;
        value.Text = "owned text";
        value.View = "viewed text";
        value.Form = erasedtest::Shape::Square;
        value.Offset = erasedtest::Signed::Below;
        value.Extent = erasedtest::Wide::Top;
        value.Nested.Value = 1.5f;
        value.List = { -1, 0, 1 };
        value.Fixed = { 0.25f, 0.5f, 0.75f };
        value.DynamicSpan = k_DynamicSource;
        value.Leaves = { erasedtest::Leaf{ 2.0f }, erasedtest::Leaf{ 3.0f } };
        value.Strings = { "first", "", "third" };
        value.MaybeLeaf = erasedtest::Leaf{ 4.0f };
        value.MaybeNumber = 5.0f;
        value.Choice = erasedtest::OtherLeaf{ 6 };
        value.EnumChoice = erasedtest::Shape::Round;
        value.Deep = { std::nullopt, std::vector<erasedtest::Leaf>{ erasedtest::Leaf{ 7.0f } } };

        return value;
    }
}

TEST(ContentEncodeErasedTest, DefaultValueMatchesEncode)
{
    ExpectSameBytesAsEncode(erasedtest::Everything{});
}

TEST(ContentEncodeErasedTest, FullValueMatchesEncode)
{
    ExpectSameBytesAsEncode(MakeFullValue());
}

TEST(ContentEncodeErasedTest, EachScalarAloneMatchesEncode)
{
    ExpectSameBytesAsEncode(true);
    ExpectSameBytesAsEncode(int8_t{ -1 });
    ExpectSameBytesAsEncode(uint8_t{ 255u });
    ExpectSameBytesAsEncode(int16_t{ -1 });
    ExpectSameBytesAsEncode(uint16_t{ 65535u });
    ExpectSameBytesAsEncode(int32_t{ -1 });
    ExpectSameBytesAsEncode(uint32_t{ 4294967295u });
    ExpectSameBytesAsEncode(int64_t{ -1 });
    ExpectSameBytesAsEncode(uint64_t{ 18446744073709551615ull });
    ExpectSameBytesAsEncode(-0.0f);
    ExpectSameBytesAsEncode(-0.0);
    ExpectSameBytesAsEncode(std::string{ "text" });
    ExpectSameBytesAsEncode(std::string_view{ "view" });
    ExpectSameBytesAsEncode(erasedtest::Signed::Above);
}

TEST(ContentEncodeErasedTest, NonFiniteFloatsMatchEncode)
{
    ExpectSameBytesAsEncode(std::numeric_limits<float>::quiet_NaN());
    ExpectSameBytesAsEncode(std::numeric_limits<double>::infinity());
}

TEST(ContentEncodeErasedTest, EachVariantAlternativeMatchesEncode)
{
    using Choice = std::variant<erasedtest::Leaf, erasedtest::OtherLeaf>;

    ExpectSameBytesAsEncode(Choice{ erasedtest::Leaf{ 1.0f } });
    ExpectSameBytesAsEncode(Choice{ erasedtest::OtherLeaf{ 2 } });
}

TEST(ContentEncodeErasedTest, EmptyAndFilledOptionalsMatchEncode)
{
    ExpectSameBytesAsEncode(std::optional<erasedtest::Leaf>{});
    ExpectSameBytesAsEncode(std::optional<erasedtest::Leaf>{ erasedtest::Leaf{ 1.0f } });
}

TEST(ContentEncodeErasedTest, RecursiveValueMatchesEncode)
{
    erasedtest::Node grandchild{ .Id = 3, .Children = {} };
    erasedtest::Node child{ .Id = 2, .Children = { grandchild } };
    erasedtest::Node root{ .Id = 1, .Children = { child, grandchild } };

    ExpectSameBytesAsEncode(root);
}

TEST(ContentEncodeErasedTest, TextSinkOutputMatchesEncode)
{
    const erasedtest::Everything value = MakeFullValue();

    TextSink typedSink;
    Encode(value, typedSink);

    TextSink erasedSink;
    EncodeErased(GetTypeInfo<erasedtest::Everything>(), &value, erasedSink);

    EXPECT_EQ(typedSink.Text(), erasedSink.Text());
}

TEST(ContentEncodeErasedTest, EnumValueWithNoCaseWritesThePlaceholderWithoutAssert)
{
    const erasedtest::Shape unknown = static_cast<erasedtest::Shape>(99);

    TextSink sink;
    EncodeErased(GetTypeInfo<erasedtest::Shape>(), &unknown, sink);

    EXPECT_NE(sink.Text().find("<no case>"), std::string::npos);
}
