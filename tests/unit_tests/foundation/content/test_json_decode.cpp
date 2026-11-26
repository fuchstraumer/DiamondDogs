#include "content/EncodeErased.hpp"
#include "content/JsonDecode.hpp"
#include "content/JsonSink.hpp"
#include "content/JsonWriter.hpp"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    enum class DecodeMood : uint8_t
    {
        Invalid = 0,
        Calm,
        Alert,
    };

    struct DecodeInner
    {
        int16_t Value{ -3 };
    };

    struct DecodeOther
    {
        bool Flag{ true };
    };

    struct DecodeOuter
    {
        bool Flag{ true };
        int8_t Small{ -1 };
        uint8_t Byte{ 200 };
        int64_t SignedLarge{ -9000000000000LL };
        uint64_t Large{ std::numeric_limits<uint64_t>::max() };
        float Single{ 0.1f };
        double Precise{ 0.1 };
        std::string Text{ "a\"b\n" };
        DecodeMood Mood{ DecodeMood::Calm };
        DecodeInner Inner{};
        std::vector<int32_t> Numbers{ 1, 2 };
        std::array<float, 2> Fixed{ 1.5f, 2.5f };
        std::optional<int32_t> Empty{};
        std::optional<int32_t> Present{ 4 };
        std::variant<DecodeInner, DecodeOther> Choice{ DecodeOther{} };
        std::variant<DecodeMood, DecodeOther> EnumChoice{ DecodeMood::Calm };
    };

    struct DecodeDefaults
    {
        int32_t KeepInt{ 42 };
        std::string KeepText{ "default" };
        bool KeepBool{ true };
        DecodeInner KeepInner{ .Value = 77 };
    };

    struct DecodeRanged
    {
        int32_t Count{ 5 };
        uint8_t Byte{ 10 };
        int64_t OpenSigned{ 1 };
        uint64_t OpenUnsigned{ 1u };
        float Tenth{ 0.1f };
        float Ratio{ 0.5f };
        double DoubleVal{ 1.0 };
        std::array<float, 2> FixedFloats{ 0.5f, 0.5f };
        std::optional<float> MaybeRatio{};
    };

    struct DecodeViewStruct
    {
        std::string_view View{ "static" };
    };

    struct DecodeMigrationDoc
    {
        int32_t Health{ 100 };
        std::string Title{ "unnamed" };
    };

    template<typename T>
    [[nodiscard]] std::string EncodeErasedToJson(const T& value, foundation::content::JsonStyle style);

    bool MigrateV1ToV2(std::string& json_data, std::string& error_message);
    bool MigrateV2ToV3(std::string& json_data, std::string& error_message);
    bool MigrateCycleToV1(std::string& json_data, std::string& error_message);
    bool MigrateFail(std::string& json_data, std::string& error_message);
    bool MigrateProducesBadJson(std::string& json_data, std::string& error_message);
}

template<>
struct foundation::reflect::Describe<DecodeMood>
{
    static constexpr std::string_view Name{ "decode.Mood" };
    static constexpr std::array Cases
    {
        EnumCase<DecodeMood>{ "Invalid", DecodeMood::Invalid, {} },
        EnumCase<DecodeMood>{ "Calm", DecodeMood::Calm, {} },
        EnumCase<DecodeMood>{ "Alert", DecodeMood::Alert, {} },
    };
};

template<>
struct foundation::reflect::Describe<DecodeInner>
{
    static constexpr std::string_view Name{ "decode.Inner" };
    static constexpr std::tuple Fields
    {
        Field<DecodeInner, int16_t>{ "Value", &DecodeInner::Value },
    };
};

template<>
struct foundation::reflect::Describe<DecodeOther>
{
    static constexpr std::string_view Name{ "decode.Other" };
    static constexpr std::tuple Fields
    {
        Field<DecodeOther, bool>{ "Flag", &DecodeOther::Flag },
    };
};

template<>
struct foundation::reflect::Describe<DecodeOuter>
{
    using Outer = DecodeOuter;

    static constexpr std::string_view Name{ "decode.Outer" };
    static constexpr std::tuple Fields
    {
        Field<Outer, bool>{ "Flag", &Outer::Flag },
        Field<Outer, int8_t>{ "Small", &Outer::Small },
        Field<Outer, uint8_t>{ "Byte", &Outer::Byte },
        Field<Outer, int64_t>{ "SignedLarge", &Outer::SignedLarge },
        Field<Outer, uint64_t>{ "Large", &Outer::Large },
        Field<Outer, float>{ "Single", &Outer::Single },
        Field<Outer, double>{ "Precise", &Outer::Precise },
        Field<Outer, std::string>{ "Text", &Outer::Text },
        Field<Outer, DecodeMood>{ "Mood", &Outer::Mood },
        Field<Outer, DecodeInner>{ "Inner", &Outer::Inner },
        Field<Outer, std::vector<int32_t>>{ "Numbers", &Outer::Numbers },
        Field<Outer, std::array<float, 2>>{ "Fixed", &Outer::Fixed },
        Field<Outer, std::optional<int32_t>>{ "Empty", &Outer::Empty },
        Field<Outer, std::optional<int32_t>>{ "Present", &Outer::Present },
        Field<Outer, std::variant<DecodeInner, DecodeOther>>{ "Choice", &Outer::Choice },
        Field<Outer, std::variant<DecodeMood, DecodeOther>>{ "EnumChoice", &Outer::EnumChoice },
    };
};

template<>
struct foundation::reflect::Describe<DecodeDefaults>
{
    using T = DecodeDefaults;

    static constexpr std::string_view Name{ "decode.Defaults" };
    static constexpr std::tuple Fields
    {
        Field<T, int32_t>{ "KeepInt", &T::KeepInt },
        Field<T, std::string>{ "KeepText", &T::KeepText },
        Field<T, bool>{ "KeepBool", &T::KeepBool },
        Field<T, DecodeInner>{ "KeepInner", &T::KeepInner },
    };
};

template<>
struct foundation::reflect::Describe<DecodeRanged>
{
    using T = DecodeRanged;

    static constexpr std::string_view Name{ "decode.Ranged" };
    static constexpr std::tuple Fields
    {
        Field<T, int32_t>{ "Count", &T::Count, { .Range = FieldRange{ 0.0, 10.0 } } },
        Field<T, uint8_t>{ "Byte", &T::Byte, { .Range = FieldRange{ 5.0, 200.0 } } },
        Field<T, int64_t>{ "OpenSigned", &T::OpenSigned, { .Range = FieldRange{ -k_NoLimit, k_NoLimit } } },
        Field<T, uint64_t>{ "OpenUnsigned", &T::OpenUnsigned, { .Range = FieldRange{ 0.0, k_NoLimit } } },
        Field<T, float>{ "Tenth", &T::Tenth, { .Range = FieldRange{ 0.1, 1.0 } } },
        Field<T, float>{ "Ratio", &T::Ratio, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<T, double>{ "DoubleVal", &T::DoubleVal, { .Range = FieldRange{ -2.5, 2.5 } } },
        Field<T, std::array<float, 2>>{ "FixedFloats", &T::FixedFloats, { .Range = FieldRange{ 0.0, 1.0 } } },
        Field<T, std::optional<float>>{ "MaybeRatio", &T::MaybeRatio, { .Range = FieldRange{ 0.0, 1.0 } } },
    };
};

template<>
struct foundation::reflect::Describe<DecodeViewStruct>
{
    static constexpr std::string_view Name{ "decode.ViewStruct" };
    static constexpr std::tuple Fields
    {
        Field<DecodeViewStruct, std::string_view>{ "View", &DecodeViewStruct::View },
    };
};

template<>
struct foundation::reflect::Describe<DecodeMigrationDoc>
{
    using T = DecodeMigrationDoc;

    static constexpr std::string_view Name{ "decode.MigrationDoc" };
    static constexpr std::tuple Fields
    {
        Field<T, int32_t>{ "Health", &T::Health },
        Field<T, std::string>{ "Title", &T::Title },
    };
};

TEST(JsonDecode, ToStringCoversEveryIssueKind)
{
    using foundation::content::DecodeIssueKind;

    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::Invalid), "Invalid");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::MalformedJson), "MalformedJson");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::SchemaMismatch), "SchemaMismatch");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::UnsupportedVersion), "UnsupportedVersion");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::UnknownField), "UnknownField");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::TypeMismatch), "TypeMismatch");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::OutOfRange), "OutOfRange");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::UnknownEnumCase), "UnknownEnumCase");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::InvalidVariantType), "InvalidVariantType");
    EXPECT_EQ(foundation::content::ToString(DecodeIssueKind::SequenceLengthMismatch),
              "SequenceLengthMismatch");
}

TEST(JsonDecode, RoundTripsEveryTypeKind)
{
    const DecodeOuter expected
    {
        .Flag = false,
        .Small = -42,
        .Byte = 250,
        .SignedLarge = std::numeric_limits<int64_t>::min(),
        .Large = std::numeric_limits<uint64_t>::max(),
        .Single = 0.25f,
        .Precise = 3.125,
        .Text = "hello\n\"world\"",
        .Mood = DecodeMood::Alert,
        .Inner = DecodeInner{ .Value = 321 },
        .Numbers = { 10, 20, 30 },
        .Fixed = { -1.25f, 4.75f },
        .Empty = std::nullopt,
        .Present = 99,
        .Choice = DecodeInner{ .Value = 777 },
        .EnumChoice = DecodeMood::Alert,
    };

    const std::string json = EncodeErasedToJson(expected, foundation::content::JsonStyle::Compact);
    DecodeOuter actual{};

    actual.Present = std::nullopt;
    actual.Empty = 55;
    actual.Choice = DecodeOther{ .Flag = false };
    actual.EnumChoice = DecodeOther{ .Flag = false };

    const foundation::content::DecodeResult result = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeOuter>(),
        &actual,
        json,
        foundation::content::k_StrictDecodeOptions);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Warnings.empty());
    EXPECT_TRUE(result.Errors.empty());
    EXPECT_EQ(actual.Flag, expected.Flag);
    EXPECT_EQ(actual.Small, expected.Small);
    EXPECT_EQ(actual.Byte, expected.Byte);
    EXPECT_EQ(actual.SignedLarge, expected.SignedLarge);
    EXPECT_EQ(actual.Large, expected.Large);
    EXPECT_FLOAT_EQ(actual.Single, expected.Single);
    EXPECT_DOUBLE_EQ(actual.Precise, expected.Precise);
    EXPECT_EQ(actual.Text, expected.Text);
    EXPECT_EQ(actual.Mood, expected.Mood);
    EXPECT_EQ(actual.Inner.Value, expected.Inner.Value);
    EXPECT_EQ(actual.Numbers, expected.Numbers);
    EXPECT_EQ(actual.Fixed, expected.Fixed);
    EXPECT_FALSE(actual.Empty.has_value());
    ASSERT_TRUE(actual.Present.has_value());
    EXPECT_EQ(*actual.Present, *expected.Present);
    ASSERT_TRUE(std::holds_alternative<DecodeInner>(actual.Choice));
    EXPECT_EQ(std::get<DecodeInner>(actual.Choice).Value, 777);
    ASSERT_TRUE(std::holds_alternative<DecodeMood>(actual.EnumChoice));
    EXPECT_EQ(std::get<DecodeMood>(actual.EnumChoice), DecodeMood::Alert);

    const std::string enumOtherJson = EncodeErasedToJson(
        DecodeOuter{ .EnumChoice = DecodeOther{ .Flag = false } },
        foundation::content::JsonStyle::Compact);
    DecodeOuter withOther{};
    const foundation::content::DecodeResult otherResult = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeOuter>(),
        &withOther,
        enumOtherJson,
        foundation::content::k_StrictDecodeOptions);

    EXPECT_TRUE(otherResult.Succeeded());
    ASSERT_TRUE(std::holds_alternative<DecodeOther>(withOther.EnumChoice));
    EXPECT_FALSE(std::get<DecodeOther>(withOther.EnumChoice).Flag);
}

TEST(JsonDecode, PartialJsonKeepsExistingDefaults)
{
    DecodeDefaults value{};
    const foundation::content::DecodeResult result = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeDefaults>(),
        &value,
        R"({"KeepInt":99})",
        foundation::content::k_StrictDecodeOptions);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(value.KeepInt, 99);
    EXPECT_EQ(value.KeepText, "default");
    EXPECT_TRUE(value.KeepBool);
    EXPECT_EQ(value.KeepInner.Value, 77);
}

TEST(JsonDecode, UnknownFieldsWarnWhenLenientAndErrorWhenStrict)
{
    DecodeDefaults lenientValue{};
    const foundation::content::DecodeResult lenient = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeDefaults>(),
        &lenientValue,
        R"({"KeepInt":12,"Extra":true})",
        foundation::content::k_LenientDecodeOptions);

    EXPECT_TRUE(lenient.Succeeded());
    EXPECT_EQ(lenientValue.KeepInt, 12);
    ASSERT_EQ(lenient.Warnings.size(), 1u);
    EXPECT_EQ(lenient.Warnings[0].Kind, foundation::content::DecodeIssueKind::UnknownField);
    EXPECT_EQ(lenient.Warnings[0].Path, "Extra");

    DecodeDefaults strictValue{};
    const foundation::content::DecodeResult strict = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeDefaults>(),
        &strictValue,
        R"({"KeepInt":12,"Extra":true})",
        foundation::content::k_StrictDecodeOptions);

    EXPECT_FALSE(strict.Succeeded());
    EXPECT_TRUE(strict.Warnings.empty());
    ASSERT_EQ(strict.Errors.size(), 1u);
    EXPECT_EQ(strict.Errors[0].Kind, foundation::content::DecodeIssueKind::UnknownField);
    EXPECT_EQ(strict.Errors[0].Path, "Extra");
}

TEST(JsonDecode, NumericRangesClampOrErrorAndHandleFloatPrecision)
{
    DecodeRanged precise{};
    const foundation::content::DecodeResult tenthResult = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeRanged>(),
        &precise,
        R"({"Tenth":0.1,"OpenSigned":-9223372036854775808,"OpenUnsigned":18446744073709551615})",
        foundation::content::k_StrictDecodeOptions);

    EXPECT_TRUE(tenthResult.Succeeded());
    EXPECT_FLOAT_EQ(precise.Tenth, 0.1f);
    EXPECT_EQ(precise.OpenSigned, std::numeric_limits<int64_t>::min());
    EXPECT_EQ(precise.OpenUnsigned, std::numeric_limits<uint64_t>::max());

    DecodeRanged strict{};
    const foundation::content::DecodeResult strictOut = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeRanged>(),
        &strict,
        R"({"Count":50,"Byte":300,"Ratio":-0.5,"DoubleVal":10.0,"FixedFloats":[2.0,-1.0],"MaybeRatio":5.0})",
        foundation::content::k_StrictDecodeOptions);

    EXPECT_FALSE(strictOut.Succeeded());
    EXPECT_EQ(strictOut.Errors.size(), 7u);
    EXPECT_EQ(strict.Count, 5);
    EXPECT_EQ(strict.Byte, 10);
    EXPECT_FLOAT_EQ(strict.Ratio, 0.5f);
    EXPECT_DOUBLE_EQ(strict.DoubleVal, 1.0);
    EXPECT_FLOAT_EQ(strict.FixedFloats[0], 0.5f);
    EXPECT_FLOAT_EQ(strict.FixedFloats[1], 0.5f);
    ASSERT_TRUE(strict.MaybeRatio.has_value());
    EXPECT_FLOAT_EQ(*strict.MaybeRatio, 0.0f);

    DecodeRanged clamped{};
    const foundation::content::DecodeResult clampedOut = foundation::content::DecodeJsonErased(
        foundation::reflect::GetTypeInfo<DecodeRanged>(),
        &clamped,
        R"({"Count":50,"Byte":2,"Ratio":1e40,"DoubleVal":-99.0,"FixedFloats":[2.0,-1.0],"MaybeRatio":5.0})",
        foundation::content::k_LenientDecodeOptions);

    EXPECT_TRUE(clampedOut.Succeeded());
    EXPECT_EQ(clampedOut.Warnings.size(), 7u);
    EXPECT_EQ(clamped.Count, 10);
    EXPECT_EQ(clamped.Byte, 5);
    EXPECT_FLOAT_EQ(clamped.Ratio, 1.0f);
    EXPECT_DOUBLE_EQ(clamped.DoubleVal, -2.5);
    EXPECT_FLOAT_EQ(clamped.FixedFloats[0], 1.0f);
    EXPECT_FLOAT_EQ(clamped.FixedFloats[1], 0.0f);
    ASSERT_TRUE(clamped.MaybeRatio.has_value());
    EXPECT_FLOAT_EQ(*clamped.MaybeRatio, 1.0f);
}

TEST(JsonDecode, ReportsSpecificErrorsAndLeavesTargetsUntouched)
{
    using foundation::content::DecodeIssueKind;
    const auto& outerType = foundation::reflect::GetTypeInfo<DecodeOuter>();

    {
        DecodeOuter target{};
        const auto result = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            "{not valid json",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.Errors.size(), 1u);
        EXPECT_EQ(result.Errors[0].Kind, DecodeIssueKind::MalformedJson);
    }

    {
        DecodeOuter target{};
        const auto result = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            R"({"Flag":"nope","Numbers":"not-array","Single":"bad"})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.Errors.size(), 3u);
        EXPECT_EQ(result.Errors[0].Kind, DecodeIssueKind::TypeMismatch);
        EXPECT_EQ(result.Errors[1].Kind, DecodeIssueKind::TypeMismatch);
        EXPECT_EQ(result.Errors[2].Kind, DecodeIssueKind::TypeMismatch);
        EXPECT_TRUE(target.Flag);
        EXPECT_EQ(target.Numbers, (std::vector<int32_t>{ 1, 2 }));
        EXPECT_FLOAT_EQ(target.Single, 0.1f);
    }

    {
        DecodeOuter target{};
        const auto result = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            R"({"Mood":"Furious"})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.Errors.size(), 1u);
        EXPECT_EQ(result.Errors[0].Kind, DecodeIssueKind::UnknownEnumCase);
        EXPECT_EQ(target.Mood, DecodeMood::Calm);
    }

    {
        DecodeOuter target{};
        const auto missingType = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            R"({"Choice":{"Flag":false}})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(missingType.Succeeded());
        ASSERT_EQ(missingType.Errors.size(), 1u);
        EXPECT_EQ(missingType.Errors[0].Kind, DecodeIssueKind::InvalidVariantType);

        const auto badType = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            R"({"Choice":{"$type":"decode.Unknown"}})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(badType.Succeeded());
        ASSERT_EQ(badType.Errors.size(), 1u);
        EXPECT_EQ(badType.Errors[0].Kind, DecodeIssueKind::InvalidVariantType);
    }

    {
        DecodeOuter target{};
        const auto result = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            R"({"Fixed":[1.0,2.0,3.0]})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.Errors.size(), 1u);
        EXPECT_EQ(result.Errors[0].Kind, DecodeIssueKind::SequenceLengthMismatch);
        EXPECT_FLOAT_EQ(target.Fixed[0], 1.5f);
        EXPECT_FLOAT_EQ(target.Fixed[1], 2.5f);
    }

    {
        DecodeOuter target{};
        const auto result = foundation::content::DecodeJsonErased(
            outerType,
            &target,
            R"({"Single":"NaN","Precise":"Infinity"})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.Errors.size(), 2u);
        EXPECT_EQ(result.Errors[0].Kind, DecodeIssueKind::OutOfRange);
        EXPECT_EQ(result.Errors[1].Kind, DecodeIssueKind::OutOfRange);
        EXPECT_FLOAT_EQ(target.Single, 0.1f);
        EXPECT_DOUBLE_EQ(target.Precise, 0.1);
    }

    {
        DecodeViewStruct target{};
        const auto result = foundation::content::DecodeJsonErased(
            foundation::reflect::GetTypeInfo<DecodeViewStruct>(),
            &target,
            R"({"View":"updated"})",
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(result.Succeeded());
        ASSERT_EQ(result.Errors.size(), 1u);
        EXPECT_EQ(result.Errors[0].Kind, DecodeIssueKind::TypeMismatch);
        EXPECT_EQ(target.View, "static");
    }
}

TEST(JsonDecode, DocumentRoundTripCompactAndPretty)
{
    const DecodeInner expected{ .Value = 512 };
    const auto& type = foundation::reflect::GetTypeInfo<DecodeInner>();

    for (const foundation::content::JsonStyle style :
         { foundation::content::JsonStyle::Compact, foundation::content::JsonStyle::Pretty })
    {
        const foundation::content::JsonResult<std::string> encoded =
            foundation::content::EncodeDocumentErased(type, 3, &expected, style);

        ASSERT_TRUE(encoded.has_value());

        DecodeInner decoded{};
        const foundation::content::DecodeResult result = foundation::content::DecodeDocumentErased(
            type,
            3,
            &decoded,
            *encoded,
            {},
            foundation::content::k_StrictDecodeOptions);

        EXPECT_TRUE(result.Succeeded());
        EXPECT_EQ(decoded.Value, 512);
    }
}

TEST(JsonDecode, DocumentValidationAndMultiStepMigration)
{
    using foundation::content::DecodeIssueKind;
    using foundation::content::SchemaMigration;
    const auto& type = foundation::reflect::GetTypeInfo<DecodeMigrationDoc>();

    {
        DecodeMigrationDoc target{};
        const auto mismatch = foundation::content::DecodeDocumentErased(
            type,
            1,
            &target,
            R"({"schema":"wrong.Schema","version":1,"data":{"Health":50}})",
            {},
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(mismatch.Succeeded());
        ASSERT_EQ(mismatch.Errors.size(), 1u);
        EXPECT_EQ(mismatch.Errors[0].Kind, DecodeIssueKind::SchemaMismatch);
    }

    {
        DecodeMigrationDoc target{};
        const auto unsupported = foundation::content::DecodeDocumentErased(
            type,
            2,
            &target,
            R"({"schema":"decode.MigrationDoc","version":1,"data":{"Health":50}})",
            {},
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(unsupported.Succeeded());
        ASSERT_EQ(unsupported.Errors.size(), 1u);
        EXPECT_EQ(unsupported.Errors[0].Kind, DecodeIssueKind::UnsupportedVersion);
    }

    {
        const std::array<SchemaMigration, 2> chain
        {
            SchemaMigration{ "decode.MigrationDoc", 1, 2, &MigrateV1ToV2 },
            SchemaMigration{ "decode.MigrationDoc", 2, 3, &MigrateV2ToV3 },
        };
        DecodeMigrationDoc target{};
        const auto migrated = foundation::content::DecodeDocumentErased(
            type,
            3,
            &target,
            R"({"schema":"decode.MigrationDoc","version":1,"data":{"hp":75}})",
            chain,
            foundation::content::k_StrictDecodeOptions);

        EXPECT_TRUE(migrated.Succeeded());
        EXPECT_EQ(target.Health, 75);
        EXPECT_EQ(target.Title, "migrated-v3");
    }

    {
        const std::array<SchemaMigration, 2> cycle
        {
            SchemaMigration{ "decode.MigrationDoc", 1, 2, &MigrateV1ToV2 },
            SchemaMigration{ "decode.MigrationDoc", 2, 1, &MigrateCycleToV1 },
        };
        DecodeMigrationDoc target{};
        const auto cycled = foundation::content::DecodeDocumentErased(
            type,
            3,
            &target,
            R"({"schema":"decode.MigrationDoc","version":1,"data":{"hp":75}})",
            cycle,
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(cycled.Succeeded());
        ASSERT_EQ(cycled.Errors.size(), 1u);
        EXPECT_EQ(cycled.Errors[0].Kind, DecodeIssueKind::UnsupportedVersion);
    }

    {
        const std::array<SchemaMigration, 1> failing
        {
            SchemaMigration{ "decode.MigrationDoc", 1, 2, &MigrateFail },
        };
        DecodeMigrationDoc target{};
        const auto failed = foundation::content::DecodeDocumentErased(
            type,
            2,
            &target,
            R"({"schema":"decode.MigrationDoc","version":1,"data":{}})",
            failing,
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(failed.Succeeded());
        ASSERT_EQ(failed.Errors.size(), 1u);
        EXPECT_EQ(failed.Errors[0].Kind, DecodeIssueKind::UnsupportedVersion);
    }

    {
        const std::array<SchemaMigration, 1> brokenJson
        {
            SchemaMigration{ "decode.MigrationDoc", 1, 2, &MigrateProducesBadJson },
        };
        DecodeMigrationDoc target{};
        const auto failed = foundation::content::DecodeDocumentErased(
            type,
            2,
            &target,
            R"({"schema":"decode.MigrationDoc","version":1,"data":{}})",
            brokenJson,
            foundation::content::k_StrictDecodeOptions);

        EXPECT_FALSE(failed.Succeeded());
        ASSERT_EQ(failed.Errors.size(), 1u);
        EXPECT_EQ(failed.Errors[0].Kind, DecodeIssueKind::MalformedJson);
    }
}

namespace
{
    template<typename T>
    std::string EncodeErasedToJson(const T& value, foundation::content::JsonStyle style)
    {
        foundation::content::JsonSink sink{ style };

        foundation::content::EncodeErased(foundation::reflect::GetTypeInfo<T>(), &value, sink);

        foundation::content::JsonResult<std::string> document = sink.Finish();

        return std::move(*document);
    }

    bool MigrateV1ToV2(std::string& json_data, std::string& error_message)
    {
        static_cast<void>(error_message);

        const size_t pos = json_data.find(R"("hp")");

        if (pos != std::string::npos)
        {
            json_data.replace(pos, 4, R"("Health")");
        }

        return true;
    }

    bool MigrateV2ToV3(std::string& json_data, std::string& error_message)
    {
        static_cast<void>(error_message);

        const size_t closePos = json_data.rfind('}');

        if (closePos == std::string::npos)
        {
            return false;
        }

        json_data.insert(closePos, R"(,"Title":"migrated-v3")");

        return true;
    }

    bool MigrateCycleToV1(std::string& json_data, std::string& error_message)
    {
        static_cast<void>(json_data);
        static_cast<void>(error_message);

        return true;
    }

    bool MigrateFail(std::string& json_data, std::string& error_message)
    {
        static_cast<void>(json_data);
        error_message = "cannot migrate v1";

        return false;
    }

    bool MigrateProducesBadJson(std::string& json_data, std::string& error_message)
    {
        static_cast<void>(error_message);
        json_data = "{broken";

        return true;
    }
}
