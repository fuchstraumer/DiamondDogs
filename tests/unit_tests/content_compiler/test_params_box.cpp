#include <gtest/gtest.h>

#include "test_kinds.hpp"
#include "ParamsBox.hpp"
#include "content/BytesSink.hpp"
#include "content/Encode.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
    std::vector<std::byte> EncodedBytes(const cc::ParamsBox& box);
    std::vector<std::byte> DirectBytes(const cctest::ConstParams& params);
}

TEST(ParamsBoxTest, AnEmptyBoxHoldsNothing)
{
    const cc::ParamsBox box;

    EXPECT_TRUE(box.IsEmpty());
    EXPECT_EQ(box.TypeIdentity(), nullptr);
    EXPECT_EQ(box.Data(), nullptr);
    EXPECT_EQ(box.As<cctest::ConstParams>(), nullptr);
}

TEST(ParamsBoxTest, MakeHoldsTheValueAndItsTypeName)
{
    const cc::ParamsBox box = cc::ParamsBox::Make(cctest::ScaleParams{ 2.5f });

    ASSERT_FALSE(box.IsEmpty());
    EXPECT_EQ(box.TypeName(), "ScaleParams");

    const cctest::ScaleParams* typed = box.As<cctest::ScaleParams>();

    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->Factor, 2.5f);
}

TEST(ParamsBoxTest, TheTypeIdentityTellsTwoTypesApart)
{
    const cc::ParamsBox scale = cc::ParamsBox::Make(cctest::ScaleParams{});
    const cc::ParamsBox blend = cc::ParamsBox::Make(cctest::BlendParams{});
    const cc::ParamsBox otherScale = cc::ParamsBox::Make(cctest::ScaleParams{ 9.0f });

    EXPECT_NE(scale.TypeIdentity(), blend.TypeIdentity());
    EXPECT_EQ(scale.TypeIdentity(), otherScale.TypeIdentity());
    EXPECT_EQ(scale.TypeIdentity(), cc::ParamsTypeIdentity<cctest::ScaleParams>());
    EXPECT_EQ(scale.As<cctest::BlendParams>(), nullptr);
}

TEST(ParamsBoxTest, ACopyIsIndependentOfTheOriginal)
{
    cc::ParamsBox original = cc::ParamsBox::Make(cctest::ScaleParams{ 1.0f });
    const cc::ParamsBox copy = original;

    original = cc::ParamsBox::Make(cctest::ScaleParams{ 7.0f });

    ASSERT_NE(copy.As<cctest::ScaleParams>(), nullptr);
    EXPECT_EQ(copy.As<cctest::ScaleParams>()->Factor, 1.0f);
    EXPECT_EQ(original.As<cctest::ScaleParams>()->Factor, 7.0f);
    EXPECT_NE(copy.Data(), original.Data());
}

TEST(ParamsBoxTest, AMoveEmptiesTheSource)
{
    cc::ParamsBox source = cc::ParamsBox::Make(cctest::ScaleParams{ 4.0f });
    const cc::ParamsBox target = std::move(source);

    EXPECT_TRUE(source.IsEmpty());
    ASSERT_FALSE(target.IsEmpty());
    EXPECT_EQ(target.As<cctest::ScaleParams>()->Factor, 4.0f);
}

TEST(ParamsBoxTest, AssigningABoxToItselfKeepsTheValue)
{
    cc::ParamsBox box = cc::ParamsBox::Make(cctest::ScaleParams{ 4.0f });
    const cc::ParamsBox& alias = box;

    box = alias;

    ASSERT_NE(box.As<cctest::ScaleParams>(), nullptr);
    EXPECT_EQ(box.As<cctest::ScaleParams>()->Factor, 4.0f);
}

TEST(ParamsBoxTest, EncodeWritesTheBytesOfADirectEncode)
{
    cctest::ConstParams params;
    params.Value = 1.25f;
    params.Seed = 77;
    params.Mode = cctest::Shape::Ridged;
    params.Nested.Weights = { 0.5f, 0.25f };

    const cc::ParamsBox box = cc::ParamsBox::Make(params);

    EXPECT_EQ(EncodedBytes(box), DirectBytes(params));
}

TEST(ParamsBoxTest, FindNaNReturnsThePathInsideTheParams)
{
    cctest::ConstParams params;
    params.Nested.Weights = { 1.0f, std::numeric_limits<float>::quiet_NaN() };

    const cc::ParamsBox box = cc::ParamsBox::Make(params);
    const std::optional<std::string> path = box.FindNaN();

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "Nested.Weights[1]");
    EXPECT_FALSE(cc::ParamsBox::Make(cctest::ConstParams{}).FindNaN().has_value());
}

namespace
{
    std::vector<std::byte> EncodedBytes(const cc::ParamsBox& box)
    {
        foundation::content::BytesSink sink;
        box.Encode(sink);

        return sink.TakeBytes();
    }

    std::vector<std::byte> DirectBytes(const cctest::ConstParams& params)
    {
        foundation::content::BytesSink sink;
        foundation::content::Encode(params, sink);

        return sink.TakeBytes();
    }
}
