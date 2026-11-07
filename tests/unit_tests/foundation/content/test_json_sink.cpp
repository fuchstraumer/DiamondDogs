#include "content/Encode.hpp"
#include "content/EncodeErased.hpp"
#include "content/JsonSink.hpp"
#include "reflect/Describe.hpp"
#include "reflect/TypeInfo.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    enum class JsonTestMood : uint8_t
    {
        Invalid = 0,
        Calm,
    };

    struct JsonTestInner
    {
        int16_t Value{ -3 };
    };

    struct JsonTestOther
    {
        bool Flag{ true };
    };

    struct JsonTestOuter
    {
        bool Flag{ true };
        int8_t Small{ -1 };
        uint8_t Byte{ 200 };
        uint64_t Large{ std::numeric_limits<uint64_t>::max() };
        float Single{ 0.1f };
        double Precise{ 0.1 };
        std::string Text{ "a\"b\n" };
        std::string_view View{ "view" };
        JsonTestMood Mood{ JsonTestMood::Calm };
        JsonTestInner Inner{};
        std::vector<int32_t> Numbers{ 1, 2 };
        std::array<float, 2> Fixed{ 1.5f, 2.5f };
        std::optional<int32_t> Empty{};
        std::optional<int32_t> Present{ 4 };
        std::variant<JsonTestInner, JsonTestOther> Choice{ JsonTestOther{} };
        std::variant<JsonTestMood, JsonTestOther> EnumChoice{ JsonTestMood::Calm };
    };

    struct JsonTestFloats
    {
        float NotANumber{ std::numeric_limits<float>::quiet_NaN() };
        float Positive{ std::numeric_limits<float>::infinity() };
        double Negative{ -std::numeric_limits<double>::infinity() };
    };

    template<typename T>
    [[nodiscard]] std::string ToJson(const T& value);
}

template<>
struct foundation::reflect::Describe<JsonTestMood>
{
    static constexpr std::string_view Name{ "json.Mood" };
    static constexpr std::array Cases
    {
        EnumCase<JsonTestMood>{ "Invalid", JsonTestMood::Invalid, {} },
        EnumCase<JsonTestMood>{ "Calm", JsonTestMood::Calm, {} },
    };
};

template<>
struct foundation::reflect::Describe<JsonTestInner>
{
    static constexpr std::string_view Name{ "json.Inner" };
    static constexpr std::tuple Fields
    {
        Field<JsonTestInner, int16_t>{ "Value", &JsonTestInner::Value },
    };
};

template<>
struct foundation::reflect::Describe<JsonTestOther>
{
    static constexpr std::string_view Name{ "json.Other" };
    static constexpr std::tuple Fields
    {
        Field<JsonTestOther, bool>{ "Flag", &JsonTestOther::Flag },
    };
};

template<>
struct foundation::reflect::Describe<JsonTestOuter>
{
    using Outer = JsonTestOuter;

    static constexpr std::string_view Name{ "json.Outer" };
    static constexpr std::tuple Fields
    {
        Field<Outer, bool>{ "Flag", &Outer::Flag },
        Field<Outer, int8_t>{ "Small", &Outer::Small },
        Field<Outer, uint8_t>{ "Byte", &Outer::Byte },
        Field<Outer, uint64_t>{ "Large", &Outer::Large },
        Field<Outer, float>{ "Single", &Outer::Single },
        Field<Outer, double>{ "Precise", &Outer::Precise },
        Field<Outer, std::string>{ "Text", &Outer::Text },
        Field<Outer, std::string_view>{ "View", &Outer::View },
        Field<Outer, JsonTestMood>{ "Mood", &Outer::Mood },
        Field<Outer, JsonTestInner>{ "Inner", &Outer::Inner },
        Field<Outer, std::vector<int32_t>>{ "Numbers", &Outer::Numbers },
        Field<Outer, std::array<float, 2>>{ "Fixed", &Outer::Fixed },
        Field<Outer, std::optional<int32_t>>{ "Empty", &Outer::Empty },
        Field<Outer, std::optional<int32_t>>{ "Present", &Outer::Present },
        Field<Outer, std::variant<JsonTestInner, JsonTestOther>>{ "Choice", &Outer::Choice },
        Field<Outer, std::variant<JsonTestMood, JsonTestOther>>{ "EnumChoice", &Outer::EnumChoice },
    };
};

template<>
struct foundation::reflect::Describe<JsonTestFloats>
{
    static constexpr std::string_view Name{ "json.Floats" };
    static constexpr std::tuple Fields
    {
        Field<JsonTestFloats, float>{ "NotANumber", &JsonTestFloats::NotANumber },
        Field<JsonTestFloats, float>{ "Positive", &JsonTestFloats::Positive },
        Field<JsonTestFloats, double>{ "Negative", &JsonTestFloats::Negative },
    };
};

TEST(JsonSink, WritesEveryKindOfValue)
{
    constexpr std::string_view k_Expected
    {
        R"({"Flag":true,"Small":-1,"Byte":200,"Large":18446744073709551615,"Single":0.1,"Precise":0.1,)"
        R"("Text":"a\"b\n","View":"view","Mood":"Calm","Inner":{"Value":-3},"Numbers":[1,2],"Fixed":[1.5,2.5],)"
        R"("Empty":null,"Present":4,"Choice":{"$type":"json.Other","Flag":true},)"
        R"("EnumChoice":{"$type":"json.Mood","value":"Calm"}})"
    };

    EXPECT_EQ(ToJson(JsonTestOuter{}), k_Expected);
}

TEST(JsonSink, NonFiniteValuesBecomeStringsSoThatTheDocumentStaysValid)
{
    EXPECT_EQ(ToJson(JsonTestFloats{}), R"({"NotANumber":"NaN","Positive":"Infinity","Negative":"-Infinity"})");
}

TEST(JsonSink, ValuelessVariantBecomesNull)
{
    foundation::content::JsonSink sink{ foundation::content::JsonStyle::Compact };

    sink.BeginArray(2u);
    sink.WriteVariant("<valueless>");
    sink.WriteBool(true);
    sink.EndArray();

    foundation::content::JsonResult<std::string> document = sink.Finish();

    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(*document, "[null,true]");
}

TEST(JsonSink, KeyBytesBecomeHighThenLowHex)
{
    foundation::content::JsonSink sink{ foundation::content::JsonStyle::Compact };

    sink.WriteKeyBytes(0x1u, 0x2u);

    foundation::content::JsonResult<std::string> document = sink.Finish();

    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(*document, "\"00000000000000020000000000000001\"");
}

TEST(JsonSink, ErasedAndTypedWalksWriteTheSameDocument)
{
    const JsonTestOuter value{};
    foundation::content::JsonSink typed{ foundation::content::JsonStyle::Compact };
    foundation::content::JsonSink erased{ foundation::content::JsonStyle::Compact };

    foundation::content::Encode(value, typed);
    foundation::content::EncodeErased(foundation::reflect::GetTypeInfo<JsonTestOuter>(), &value, erased);

    foundation::content::JsonResult<std::string> typedDocument = typed.Finish();
    foundation::content::JsonResult<std::string> erasedDocument = erased.Finish();

    ASSERT_TRUE(typedDocument.has_value());
    ASSERT_TRUE(erasedDocument.has_value());
    EXPECT_EQ(*typedDocument, *erasedDocument);
}

namespace
{
    template<typename T>
    std::string ToJson(const T& value)
    {
        foundation::content::JsonSink sink{ foundation::content::JsonStyle::Compact };

        foundation::content::Encode(value, sink);

        foundation::content::JsonResult<std::string> document = sink.Finish();

        return std::move(*document);
    }
}
