#include <gtest/gtest.h>

#include "content/Encode.hpp"
#include "content/HashSink.hpp"
#include "content/TextSink.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>

namespace desctest
{
    struct NoMetadata
    {
        float Height{ 2.0f };
        int32_t Count{ 3 };
    };

    // the same fields and the same descriptor name as NoMetadata, with metadata on every field
    struct AllMetadata
    {
        float Height{ 2.0f };
        int32_t Count{ 3 };
    };

    struct DuplicateFieldNames
    {
        float First{ 0.0f };
        float Second{ 0.0f };
    };

    struct InvertedRange
    {
        float Value{ 0.0f };
    };

    struct EqualLimits
    {
        float Value{ 0.0f };
    };

    struct NaNLimit
    {
        float Value{ 0.0f };
    };

    enum class UniqueCases : uint8_t
    {
        Invalid = 0,
        Slow,
        Fast,
    };

    enum class DuplicateCases : uint8_t
    {
        Invalid = 0,
        Slow,
        Fast,
    };

    template<typename T>
    std::string TextOf(const T& value)
    {
        foundation::content::TextSink sink;
        foundation::content::Encode(value, sink);

        return std::string{ sink.Text() };
    }

    template<typename T>
    foundation::content::Hash128 HashOf(const T& value)
    {
        foundation::content::HashSink sink;
        foundation::content::Encode(value, sink);

        return sink.Finish();
    }
}

namespace foundation::reflect
{
    template<>
    struct Describe<desctest::NoMetadata>
    {
        using T = desctest::NoMetadata;

        static constexpr std::string_view Name = "desctest.Sample";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Height", &T::Height },
            Field<T, int32_t>{ "Count", &T::Count },
        };
    };

    template<>
    struct Describe<desctest::AllMetadata>
    {
        using T = desctest::AllMetadata;

        static constexpr std::string_view Name = "desctest.Sample";
        static constexpr std::string_view Doc = "A type with a doc string.";
        static constexpr std::tuple Fields
        {
            Field<T, float>
            {
                "Height",
                &T::Height,
                {
                    .Unit = "m",
                    .Range = FieldRange{ 0.0, 10.0 },
                    .Doc = "The height of the sample.",
                    .ReadOnly = true,
                    .Hidden = true,
                },
            },
            Field<T, int32_t>{ "Count", &T::Count, { .Range = FieldRange{ 1.0, 8.0 } } },
        };
    };

    template<>
    struct Describe<desctest::DuplicateFieldNames>
    {
        using T = desctest::DuplicateFieldNames;

        static constexpr std::string_view Name = "desctest.DuplicateFieldNames";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Same", &T::First },
            Field<T, float>{ "Same", &T::Second },
        };
    };

    template<>
    struct Describe<desctest::InvertedRange>
    {
        using T = desctest::InvertedRange;

        static constexpr std::string_view Name = "desctest.InvertedRange";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value, { .Range = FieldRange{ 2.0, 1.0 } } },
        };
    };

    template<>
    struct Describe<desctest::EqualLimits>
    {
        using T = desctest::EqualLimits;

        static constexpr std::string_view Name = "desctest.EqualLimits";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value, { .Range = FieldRange{ 1.0, 1.0 } } },
        };
    };

    template<>
    struct Describe<desctest::NaNLimit>
    {
        using T = desctest::NaNLimit;

        static constexpr std::string_view Name = "desctest.NaNLimit";
        static constexpr std::tuple Fields
        {
            Field<T, float>
            {
                "Value",
                &T::Value,
                { .Range = FieldRange{ std::numeric_limits<double>::quiet_NaN(), 1.0 } },
            },
        };
    };

    template<>
    struct Describe<desctest::UniqueCases>
    {
        static constexpr std::string_view Name = "desctest.UniqueCases";
        static constexpr std::array<EnumCase<desctest::UniqueCases>, 3> Cases
        {
            EnumCase<desctest::UniqueCases>{ "Invalid", desctest::UniqueCases::Invalid },
            EnumCase<desctest::UniqueCases>{ "Slow", desctest::UniqueCases::Slow, "Takes the long path." },
            EnumCase<desctest::UniqueCases>{ "Fast", desctest::UniqueCases::Fast },
        };
    };

    template<>
    struct Describe<desctest::DuplicateCases>
    {
        static constexpr std::string_view Name = "desctest.DuplicateCases";
        static constexpr std::array<EnumCase<desctest::DuplicateCases>, 3> Cases
        {
            EnumCase<desctest::DuplicateCases>{ "Invalid", desctest::DuplicateCases::Invalid },
            EnumCase<desctest::DuplicateCases>{ "Same", desctest::DuplicateCases::Slow },
            EnumCase<desctest::DuplicateCases>{ "Same", desctest::DuplicateCases::Fast },
        };
    };
}

namespace
{
    using foundation::reflect::Describe;

    static_assert(foundation::reflect::FieldNamesAreUnique<desctest::NoMetadata>());
    static_assert(!foundation::reflect::FieldNamesAreUnique<desctest::DuplicateFieldNames>());

    static_assert(foundation::reflect::FieldRangesAreOrdered<desctest::AllMetadata>());
    static_assert(foundation::reflect::FieldRangesAreOrdered<desctest::EqualLimits>());
    static_assert(!foundation::reflect::FieldRangesAreOrdered<desctest::InvertedRange>());
    static_assert(!foundation::reflect::FieldRangesAreOrdered<desctest::NaNLimit>());

    static_assert(foundation::reflect::EnumCaseNamesAreUnique<desctest::UniqueCases>());
    static_assert(!foundation::reflect::EnumCaseNamesAreUnique<desctest::DuplicateCases>());
}

TEST(ReflectDescribeTest, FieldWithoutMetadataHasEmptyMetadata)
{
    const auto& height = std::get<0>(Describe<desctest::NoMetadata>::Fields);

    EXPECT_TRUE(height.Metadata.Unit.empty());
    EXPECT_FALSE(height.Metadata.Range.has_value());
    EXPECT_TRUE(height.Metadata.Doc.empty());
    EXPECT_FALSE(height.Metadata.ReadOnly);
    EXPECT_FALSE(height.Metadata.Hidden);
}

TEST(ReflectDescribeTest, FieldKeepsItsMetadata)
{
    const auto& height = std::get<0>(Describe<desctest::AllMetadata>::Fields);

    EXPECT_EQ(height.Metadata.Unit, "m");
    ASSERT_TRUE(height.Metadata.Range.has_value());
    EXPECT_EQ(height.Metadata.Range->Min, 0.0);
    EXPECT_EQ(height.Metadata.Range->Max, 10.0);
    EXPECT_EQ(height.Metadata.Doc, "The height of the sample.");
    EXPECT_TRUE(height.Metadata.ReadOnly);
    EXPECT_TRUE(height.Metadata.Hidden);
}

TEST(ReflectDescribeTest, EnumCaseDocIsOptional)
{
    const auto& cases = Describe<desctest::UniqueCases>::Cases;

    EXPECT_TRUE(cases[0].Doc.empty());
    EXPECT_EQ(cases[1].Doc, "Takes the long path.");
}

TEST(ReflectDescribeTest, TypeDocIsOptional)
{
    EXPECT_TRUE(foundation::reflect::DescribedDoc<desctest::NoMetadata>().empty());
    EXPECT_EQ(foundation::reflect::DescribedDoc<desctest::AllMetadata>(), "A type with a doc string.");
}

TEST(ReflectDescribeTest, MetadataDoesNotChangeTheEncoding)
{
    const desctest::NoMetadata bare{};
    const desctest::AllMetadata annotated{};

    EXPECT_EQ(desctest::TextOf(bare), desctest::TextOf(annotated));

    const foundation::content::Hash128 bareHash = desctest::HashOf(bare);
    const foundation::content::Hash128 annotatedHash = desctest::HashOf(annotated);

    EXPECT_EQ(bareHash.Low, annotatedHash.Low);
    EXPECT_EQ(bareHash.High, annotatedHash.High);
}
