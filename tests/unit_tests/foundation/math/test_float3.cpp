#include <gtest/gtest.h>
#include <Math.hpp>
#include <numbers>

namespace math
{

class Float3Test : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-6f;
    
    static void ExpectNear(const Float3& actual, const Float3& expected, float epsilon = EPSILON)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon) << "X component mismatch";
        EXPECT_NEAR(actual.y, expected.y, epsilon) << "Y component mismatch";
        EXPECT_NEAR(actual.z, expected.z, epsilon) << "Z component mismatch";
    }
};

// Construction Tests
TEST_F(Float3Test, DefaultConstruction)
{
    Float3 v;
    ExpectNear(v, Float3(0.0f, 0.0f, 0.0f));
}

TEST_F(Float3Test, ComponentConstruction)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(v, Float3(1.0f, 2.0f, 3.0f));
}

TEST_F(Float3Test, ScalarBroadcast)
{
    Float3 v(7.0f);
    ExpectNear(v, Float3(7.0f, 7.0f, 7.0f));
}

TEST_F(Float3Test, CopyConstruction)
{
    Float3 v1(4.0f, 5.0f, 6.0f);
    Float3 v2(v1);
    ExpectNear(v2, Float3(4.0f, 5.0f, 6.0f));
}

// Arithmetic Operations
TEST_F(Float3Test, VectorAddition)
{
    Float3 v1(1.0f, 2.0f, 3.0f);
    Float3 v2(4.0f, 5.0f, 6.0f);
    Float3 result = v1 + v2;
    ExpectNear(result, Float3(5.0f, 7.0f, 9.0f));
}

TEST_F(Float3Test, VectorSubtraction)
{
    Float3 v1(10.0f, 8.0f, 6.0f);
    Float3 v2(1.0f, 2.0f, 3.0f);
    Float3 result = v1 - v2;
    ExpectNear(result, Float3(9.0f, 6.0f, 3.0f));
}

TEST_F(Float3Test, ComponentWiseMultiplication)
{
    Float3 v1(2.0f, 3.0f, 4.0f);
    Float3 v2(5.0f, 6.0f, 7.0f);
    Float3 result = v1 * v2;
    ExpectNear(result, Float3(10.0f, 18.0f, 28.0f));
}

TEST_F(Float3Test, ComponentWiseDivision)
{
    Float3 v1(20.0f, 30.0f, 40.0f);
    Float3 v2(4.0f, 5.0f, 8.0f);
    Float3 result = v1 / v2;
    ExpectNear(result, Float3(5.0f, 6.0f, 5.0f));
}

TEST_F(Float3Test, ScalarMultiplication)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    Float3 result = v * 4.0f;
    ExpectNear(result, Float3(4.0f, 8.0f, 12.0f));
}

TEST_F(Float3Test, ScalarMultiplicationCommutative)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(v * 4.0f, 4.0f * v);
}

TEST_F(Float3Test, ScalarDivision)
{
    Float3 v(10.0f, 20.0f, 30.0f);
    Float3 result = v / 10.0f;
    ExpectNear(result, Float3(1.0f, 2.0f, 3.0f));
}

TEST_F(Float3Test, UnaryNegation)
{
    Float3 v(1.0f, -2.0f, 3.0f);
    Float3 result = -v;
    ExpectNear(result, Float3(-1.0f, 2.0f, -3.0f));
}

// Compound Assignment Tests
TEST_F(Float3Test, CompoundAddition)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    v += Float3(4.0f, 5.0f, 6.0f);
    ExpectNear(v, Float3(5.0f, 7.0f, 9.0f));
}

TEST_F(Float3Test, CompoundSubtraction)
{
    Float3 v(10.0f, 8.0f, 6.0f);
    v -= Float3(1.0f, 2.0f, 3.0f);
    ExpectNear(v, Float3(9.0f, 6.0f, 3.0f));
}

TEST_F(Float3Test, CompoundScalarMultiplication)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    v *= 3.0f;
    ExpectNear(v, Float3(3.0f, 6.0f, 9.0f));
}

TEST_F(Float3Test, CompoundScalarDivision)
{
    Float3 v(10.0f, 20.0f, 30.0f);
    v /= 10.0f;
    ExpectNear(v, Float3(1.0f, 2.0f, 3.0f));
}

// Comparison Tests
TEST_F(Float3Test, Equality)
{
    Float3 v1(1.0f, 2.0f, 3.0f);
    Float3 v2(1.0f, 2.0f, 3.0f);
    Float3 v3(1.0f, 2.0f, 4.0f);
    
    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(Float3Test, Inequality)
{
    Float3 v1(1.0f, 2.0f, 3.0f);
    Float3 v2(1.0f, 2.0f, 4.0f);
    Float3 v3(1.0f, 2.0f, 3.0f);
    
    EXPECT_TRUE(v1 != v2);
    EXPECT_FALSE(v1 != v3);
}

// Accessor Tests
TEST_F(Float3Test, ArrayAccessor)
{
    Float3 v(5.0f, 6.0f, 7.0f);
    EXPECT_FLOAT_EQ(v[0], 5.0f);
    EXPECT_FLOAT_EQ(v[1], 6.0f);
    EXPECT_FLOAT_EQ(v[2], 7.0f);
}

TEST_F(Float3Test, ArrayAccessorMutation)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    v[0] = 8.0f;
    v[1] = 9.0f;
    v[2] = 10.0f;
    ExpectNear(v, Float3(8.0f, 9.0f, 10.0f));
}

TEST_F(Float3Test, ComponentAccessors)
{
    Float3 v(11.0f, 12.0f, 13.0f);
    EXPECT_FLOAT_EQ(v.x, 11.0f);
    EXPECT_FLOAT_EQ(v.y, 12.0f);
    EXPECT_FLOAT_EQ(v.z, 13.0f);
}

// Swizzle Tests
TEST_F(Float3Test, SwizzleXYZ)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(v.xyz(), Float3(1.0f, 2.0f, 3.0f));
}

TEST_F(Float3Test, SwizzleXZY)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(v.xzy(), Float3(1.0f, 3.0f, 2.0f));
}

TEST_F(Float3Test, SwizzleYXZ)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(v.yxz(), Float3(2.0f, 1.0f, 3.0f));
}

TEST_F(Float3Test, SwizzleZYX)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(v.zyx(), Float3(3.0f, 2.0f, 1.0f));
}

TEST_F(Float3Test, SwizzleXXX)
{
    Float3 v(5.0f, 6.0f, 7.0f);
    ExpectNear(v.xxx(), Float3(5.0f, 5.0f, 5.0f));
}

TEST_F(Float3Test, SwizzleYYY)
{
    Float3 v(5.0f, 6.0f, 7.0f);
    ExpectNear(v.yyy(), Float3(6.0f, 6.0f, 6.0f));
}

TEST_F(Float3Test, SwizzleZZZ)
{
    Float3 v(5.0f, 6.0f, 7.0f);
    ExpectNear(v.zzz(), Float3(7.0f, 7.0f, 7.0f));
}

TEST_F(Float3Test, Swizzle2D_XY)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    Float2 result = v.xy();
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
}

TEST_F(Float3Test, Swizzle2D_XZ)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    Float2 result = v.xz();
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 3.0f);
}

TEST_F(Float3Test, Swizzle2D_YZ)
{
    Float3 v(1.0f, 2.0f, 3.0f);
    Float2 result = v.yz();
    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 3.0f);
}

// Edge Cases
TEST_F(Float3Test, ZeroVector)
{
    Float3 zero(0.0f, 0.0f, 0.0f);
    Float3 v(1.0f, 2.0f, 3.0f);
    ExpectNear(zero + v, v);
    ExpectNear(zero * 5.0f, zero);
}

TEST_F(Float3Test, NegativeComponents)
{
    Float3 v1(-1.0f, -2.0f, -3.0f);
    Float3 v2(4.0f, 5.0f, 6.0f);
    ExpectNear(v1 + v2, Float3(3.0f, 3.0f, 3.0f));
    ExpectNear(v1 * v2, Float3(-4.0f, -10.0f, -18.0f));
}

TEST_F(Float3Test, UnitVectors)
{
    Float3 unitX(1.0f, 0.0f, 0.0f);
    Float3 unitY(0.0f, 1.0f, 0.0f);
    Float3 unitZ(0.0f, 0.0f, 1.0f);
    
    ExpectNear(unitX + unitY + unitZ, Float3(1.0f, 1.0f, 1.0f));
}

}
