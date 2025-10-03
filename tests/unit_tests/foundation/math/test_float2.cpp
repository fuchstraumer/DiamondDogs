#include <gtest/gtest.h>
#include <Math.hpp>
#include <numbers>

namespace math
{

class Float2Test : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-6f;
    
    static void ExpectNear(const Float2& actual, const Float2& expected, float epsilon = EPSILON)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon) << "X component mismatch";
        EXPECT_NEAR(actual.y, expected.y, epsilon) << "Y component mismatch";
    }
};

// Construction Tests
TEST_F(Float2Test, DefaultConstruction)
{
    Float2 v;
    ExpectNear(v, Float2(0.0f, 0.0f));
}

TEST_F(Float2Test, ComponentConstruction)
{
    Float2 v(3.0f, 4.0f);
    ExpectNear(v, Float2(3.0f, 4.0f));
}

TEST_F(Float2Test, ScalarBroadcast)
{
    Float2 v(5.0f);
    ExpectNear(v, Float2(5.0f, 5.0f));
}

TEST_F(Float2Test, CopyConstruction)
{
    Float2 v1(2.0f, 3.0f);
    Float2 v2(v1);
    ExpectNear(v2, Float2(2.0f, 3.0f));
}

// Arithmetic Operations Tests
TEST_F(Float2Test, VectorAddition)
{
    Float2 v1(1.0f, 2.0f);
    Float2 v2(3.0f, 4.0f);
    Float2 result = v1 + v2;
    ExpectNear(result, Float2(4.0f, 6.0f));
}

TEST_F(Float2Test, AdditionCommutativity)
{
    Float2 v1(1.5f, 2.5f);
    Float2 v2(3.5f, 4.5f);
    ExpectNear(v1 + v2, v2 + v1);
}

TEST_F(Float2Test, VectorSubtraction)
{
    Float2 v1(5.0f, 7.0f);
    Float2 v2(2.0f, 3.0f);
    Float2 result = v1 - v2;
    ExpectNear(result, Float2(3.0f, 4.0f));
}

TEST_F(Float2Test, ComponentWiseMultiplication)
{
    Float2 v1(2.0f, 3.0f);
    Float2 v2(4.0f, 5.0f);
    Float2 result = v1 * v2;
    ExpectNear(result, Float2(8.0f, 15.0f));
}

TEST_F(Float2Test, ComponentWiseDivision)
{
    Float2 v1(10.0f, 20.0f);
    Float2 v2(2.0f, 4.0f);
    Float2 result = v1 / v2;
    ExpectNear(result, Float2(5.0f, 5.0f));
}

TEST_F(Float2Test, ScalarMultiplication)
{
    Float2 v(2.0f, 3.0f);
    Float2 result = v * 3.0f;
    ExpectNear(result, Float2(6.0f, 9.0f));
}

TEST_F(Float2Test, ScalarMultiplicationCommutative)
{
    Float2 v(2.0f, 3.0f);
    ExpectNear(v * 3.0f, 3.0f * v);
}

TEST_F(Float2Test, ScalarDivision)
{
    Float2 v(10.0f, 20.0f);
    Float2 result = v / 5.0f;
    ExpectNear(result, Float2(2.0f, 4.0f));
}

TEST_F(Float2Test, UnaryNegation)
{
    Float2 v(3.0f, -4.0f);
    Float2 result = -v;
    ExpectNear(result, Float2(-3.0f, 4.0f));
}

TEST_F(Float2Test, NegativeValueOperations)
{
    Float2 v1(-2.0f, -3.0f);
    Float2 v2(1.0f, 2.0f);
    ExpectNear(v1 + v2, Float2(-1.0f, -1.0f));
    ExpectNear(v1 * v2, Float2(-2.0f, -6.0f));
}

// Compound Assignment Tests
TEST_F(Float2Test, CompoundAddition)
{
    Float2 v(1.0f, 2.0f);
    v += Float2(3.0f, 4.0f);
    ExpectNear(v, Float2(4.0f, 6.0f));
}

TEST_F(Float2Test, CompoundSubtraction)
{
    Float2 v(5.0f, 7.0f);
    v -= Float2(2.0f, 3.0f);
    ExpectNear(v, Float2(3.0f, 4.0f));
}

TEST_F(Float2Test, CompoundMultiplication)
{
    Float2 v(2.0f, 3.0f);
    v *= Float2(4.0f, 5.0f);
    ExpectNear(v, Float2(8.0f, 15.0f));
}

TEST_F(Float2Test, CompoundDivision)
{
    Float2 v(10.0f, 20.0f);
    v /= Float2(2.0f, 4.0f);
    ExpectNear(v, Float2(5.0f, 5.0f));
}

TEST_F(Float2Test, CompoundScalarMultiplication)
{
    Float2 v(2.0f, 3.0f);
    v *= 3.0f;
    ExpectNear(v, Float2(6.0f, 9.0f));
}

TEST_F(Float2Test, CompoundScalarDivision)
{
    Float2 v(10.0f, 20.0f);
    v /= 5.0f;
    ExpectNear(v, Float2(2.0f, 4.0f));
}

// Comparison Tests
TEST_F(Float2Test, Equality)
{
    Float2 v1(1.0f, 2.0f);
    Float2 v2(1.0f, 2.0f);
    Float2 v3(1.0f, 3.0f);
    
    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(Float2Test, Inequality)
{
    Float2 v1(1.0f, 2.0f);
    Float2 v2(1.0f, 3.0f);
    Float2 v3(1.0f, 2.0f);
    
    EXPECT_TRUE(v1 != v2);
    EXPECT_FALSE(v1 != v3);
}

// Accessor Tests
TEST_F(Float2Test, ArrayAccessor)
{
    Float2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_FLOAT_EQ(v[1], 4.0f);
}

TEST_F(Float2Test, ArrayAccessorMutation)
{
    Float2 v(1.0f, 2.0f);
    v[0] = 5.0f;
    v[1] = 6.0f;
    ExpectNear(v, Float2(5.0f, 6.0f));
}

TEST_F(Float2Test, ComponentAccessors)
{
    Float2 v(7.0f, 8.0f);
    EXPECT_FLOAT_EQ(v.x, 7.0f);
    EXPECT_FLOAT_EQ(v.y, 8.0f);
}

// Edge Case Tests
TEST_F(Float2Test, ZeroVector)
{
    Float2 v(0.0f, 0.0f);
    ExpectNear(v + Float2(1.0f, 2.0f), Float2(1.0f, 2.0f));
    ExpectNear(v * 5.0f, Float2(0.0f, 0.0f));
}

TEST_F(Float2Test, LargeMagnitude)
{
    Float2 v(1000000.0f, 2000000.0f);
    Float2 result = v / 1000000.0f;
    ExpectNear(result, Float2(1.0f, 2.0f), 1e-3f);
}

TEST_F(Float2Test, NearZeroDivision)
{
    Float2 v(1.0f, 2.0f);
    Float2 divisor(0.1f, 0.2f);
    Float2 result = v / divisor;
    ExpectNear(result, Float2(10.0f, 10.0f), 1e-5f);
}

} // namespace math
