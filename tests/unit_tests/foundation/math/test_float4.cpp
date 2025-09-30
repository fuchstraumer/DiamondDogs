#include <gtest/gtest.h>
#include <Math.hpp>

namespace math
{

class Float4Test : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-6f;
    
    static void ExpectNear(const Float4& actual, const Float4& expected, float epsilon = EPSILON)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon) << "X component mismatch";
        EXPECT_NEAR(actual.y, expected.y, epsilon) << "Y component mismatch";
        EXPECT_NEAR(actual.z, expected.z, epsilon) << "Z component mismatch";
        EXPECT_NEAR(actual.w, expected.w, epsilon) << "W component mismatch";
    }
};

// Construction Tests
TEST_F(Float4Test, DefaultConstruction)
{
    Float4 v;
    ExpectNear(v, Float4(0.0f, 0.0f, 0.0f, 0.0f));
}

TEST_F(Float4Test, ComponentConstruction)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(v, Float4(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_F(Float4Test, ScalarBroadcast)
{
    Float4 v(9.0f);
    ExpectNear(v, Float4(9.0f, 9.0f, 9.0f, 9.0f));
}

// Arithmetic Operations
TEST_F(Float4Test, VectorAddition)
{
    Float4 v1(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 v2(5.0f, 6.0f, 7.0f, 8.0f);
    Float4 result = v1 + v2;
    ExpectNear(result, Float4(6.0f, 8.0f, 10.0f, 12.0f));
}

TEST_F(Float4Test, VectorSubtraction)
{
    Float4 v1(10.0f, 9.0f, 8.0f, 7.0f);
    Float4 v2(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 result = v1 - v2;
    ExpectNear(result, Float4(9.0f, 7.0f, 5.0f, 3.0f));
}

TEST_F(Float4Test, ComponentWiseMultiplication)
{
    Float4 v1(2.0f, 3.0f, 4.0f, 5.0f);
    Float4 v2(6.0f, 7.0f, 8.0f, 9.0f);
    Float4 result = v1 * v2;
    ExpectNear(result, Float4(12.0f, 21.0f, 32.0f, 45.0f));
}

TEST_F(Float4Test, ScalarMultiplication)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 result = v * 5.0f;
    ExpectNear(result, Float4(5.0f, 10.0f, 15.0f, 20.0f));
}

TEST_F(Float4Test, ScalarMultiplicationCommutative)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(v * 5.0f, 5.0f * v);
}

TEST_F(Float4Test, UnaryNegation)
{
    Float4 v(1.0f, -2.0f, 3.0f, -4.0f);
    Float4 result = -v;
    ExpectNear(result, Float4(-1.0f, 2.0f, -3.0f, 4.0f));
}

// Compound Assignment Tests
TEST_F(Float4Test, CompoundAddition)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    v += Float4(5.0f, 6.0f, 7.0f, 8.0f);
    ExpectNear(v, Float4(6.0f, 8.0f, 10.0f, 12.0f));
}

TEST_F(Float4Test, CompoundScalarMultiplication)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    v *= 2.0f;
    ExpectNear(v, Float4(2.0f, 4.0f, 6.0f, 8.0f));
}

// Comparison Tests
TEST_F(Float4Test, Equality)
{
    Float4 v1(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 v2(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 v3(1.0f, 2.0f, 3.0f, 5.0f);
    
    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(Float4Test, Inequality)
{
    Float4 v1(1.0f, 2.0f, 3.0f, 4.0f);
    Float4 v2(1.0f, 2.0f, 3.0f, 5.0f);
    
    EXPECT_TRUE(v1 != v2);
}

// Accessor Tests
TEST_F(Float4Test, ArrayAccessor)
{
    Float4 v(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_FLOAT_EQ(v[0], 5.0f);
    EXPECT_FLOAT_EQ(v[1], 6.0f);
    EXPECT_FLOAT_EQ(v[2], 7.0f);
    EXPECT_FLOAT_EQ(v[3], 8.0f);
}

TEST_F(Float4Test, ComponentAccessors)
{
    Float4 v(11.0f, 12.0f, 13.0f, 14.0f);
    EXPECT_FLOAT_EQ(v.x, 11.0f);
    EXPECT_FLOAT_EQ(v.y, 12.0f);
    EXPECT_FLOAT_EQ(v.z, 13.0f);
    EXPECT_FLOAT_EQ(v.w, 14.0f);
}

TEST_F(Float4Test, RGBAAccessors)
{
    Float4 v(0.1f, 0.2f, 0.3f, 0.4f);
    EXPECT_FLOAT_EQ(v.r, 0.1f);
    EXPECT_FLOAT_EQ(v.g, 0.2f);
    EXPECT_FLOAT_EQ(v.b, 0.3f);
    EXPECT_FLOAT_EQ(v.a, 0.4f);
}

// Swizzle Tests
TEST_F(Float4Test, SwizzleXYZW)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(v.xyzw(), Float4(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_F(Float4Test, SwizzleXYWZ)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(v.xywz(), Float4(1.0f, 2.0f, 4.0f, 3.0f));
}

TEST_F(Float4Test, SwizzleXZYW)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(v.xzyw(), Float4(1.0f, 3.0f, 2.0f, 4.0f));
}

TEST_F(Float4Test, SwizzleWXYZ)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(v.wxyz(), Float4(4.0f, 1.0f, 2.0f, 3.0f));
}

TEST_F(Float4Test, SwizzleXXXX)
{
    Float4 v(5.0f, 6.0f, 7.0f, 8.0f);
    ExpectNear(v.xxxx(), Float4(5.0f, 5.0f, 5.0f, 5.0f));
}

TEST_F(Float4Test, SwizzleYYYY)
{
    Float4 v(5.0f, 6.0f, 7.0f, 8.0f);
    ExpectNear(v.yyyy(), Float4(6.0f, 6.0f, 6.0f, 6.0f));
}

TEST_F(Float4Test, SwizzleZZZZ)
{
    Float4 v(5.0f, 6.0f, 7.0f, 8.0f);
    ExpectNear(v.zzzz(), Float4(7.0f, 7.0f, 7.0f, 7.0f));
}

TEST_F(Float4Test, SwizzleWWWW)
{
    Float4 v(5.0f, 6.0f, 7.0f, 8.0f);
    ExpectNear(v.wwww(), Float4(8.0f, 8.0f, 8.0f, 8.0f));
}

TEST_F(Float4Test, Swizzle3D_XYZ)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Float3 result = v.xyz();
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST_F(Float4Test, Swizzle3D_RGB)
{
    Float4 v(0.1f, 0.2f, 0.3f, 0.4f);
    Float3 result = v.rgb();
    EXPECT_FLOAT_EQ(result.x, 0.1f);
    EXPECT_FLOAT_EQ(result.y, 0.2f);
    EXPECT_FLOAT_EQ(result.z, 0.3f);
}

TEST_F(Float4Test, Swizzle2D_XY)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Float2 result = v.xy();
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
}

TEST_F(Float4Test, Swizzle2D_ZW)
{
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Float2 result = v.zw();
    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 4.0f);
}

// Edge Cases
TEST_F(Float4Test, ZeroVector)
{
    Float4 zero(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 v(1.0f, 2.0f, 3.0f, 4.0f);
    ExpectNear(zero + v, v);
}

TEST_F(Float4Test, HomogeneousCoordinate_Point)
{
    Float4 point(1.0f, 2.0f, 3.0f, 1.0f);
    EXPECT_FLOAT_EQ(point.w, 1.0f);
}

TEST_F(Float4Test, HomogeneousCoordinate_Direction)
{
    Float4 direction(1.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(direction.w, 0.0f);
}

TEST_F(Float4Test, ColorOperations)
{
    Float4 color1(1.0f, 0.5f, 0.25f, 1.0f);
    Float4 color2(0.0f, 0.5f, 0.75f, 0.5f);
    Float4 blended = color1 * 0.5f + color2 * 0.5f;
    ExpectNear(blended, Float4(0.5f, 0.5f, 0.5f, 0.75f));
}

} // namespace math
