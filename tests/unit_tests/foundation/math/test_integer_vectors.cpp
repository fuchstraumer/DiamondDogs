#include <gtest/gtest.h>
#include <Math.hpp>

namespace math
{

// Int2 Tests
class Int2Test : public ::testing::Test {};

TEST_F(Int2Test, DefaultConstruction)
{
    Int2 v;
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
}

TEST_F(Int2Test, ComponentConstruction)
{
    Int2 v(3, 4);
    EXPECT_EQ(v.x, 3);
    EXPECT_EQ(v.y, 4);
}

TEST_F(Int2Test, IntegerArithmetic)
{
    Int2 v1(10, 20);
    Int2 v2(3, 4);
    
    Int2 sum = v1 + v2;
    EXPECT_EQ(sum.x, 13);
    EXPECT_EQ(sum.y, 24);
    
    Int2 diff = v1 - v2;
    EXPECT_EQ(diff.x, 7);
    EXPECT_EQ(diff.y, 16);
    
    Int2 prod = v1 * v2;
    EXPECT_EQ(prod.x, 30);
    EXPECT_EQ(prod.y, 80);
}

TEST_F(Int2Test, IntegerDivisionTruncation)
{
    Int2 v1(10, 21);
    Int2 v2(3, 4);
    Int2 result = v1 / v2;
    
    EXPECT_EQ(result.x, 3);  // 10/3 = 3 (truncated)
    EXPECT_EQ(result.y, 5);  // 21/4 = 5 (truncated)
}

TEST_F(Int2Test, MixedTypePromotion_Addition)
{
    Int2 vi(1, 2);
    Float2 vf(3.5f, 4.5f);
    Float2 result = vi + vf;
    
    EXPECT_FLOAT_EQ(result.x, 4.5f);
    EXPECT_FLOAT_EQ(result.y, 6.5f);
}

TEST_F(Int2Test, MixedTypePromotion_Multiplication)
{
    Int2 vi(2, 3);
    Float2 vf(2.5f, 3.5f);
    Float2 result = vi * vf;
    
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 10.5f);
}

TEST_F(Int2Test, MixedScalarMultiplication)
{
    Int2 v(2, 3);
    
    Int2 intResult = v * 5;
    EXPECT_EQ(intResult.x, 10);
    EXPECT_EQ(intResult.y, 15);
    
    Float2 floatResult = v * 2.5f;
    EXPECT_FLOAT_EQ(floatResult.x, 5.0f);
    EXPECT_FLOAT_EQ(floatResult.y, 7.5f);
}

TEST_F(Int2Test, NegativeValues)
{
    Int2 v1(-5, -10);
    Int2 v2(2, 3);
    
    Int2 sum = v1 + v2;
    EXPECT_EQ(sum.x, -3);
    EXPECT_EQ(sum.y, -7);
    
    Int2 prod = v1 * v2;
    EXPECT_EQ(prod.x, -10);
    EXPECT_EQ(prod.y, -30);
}

TEST_F(Int2Test, UnaryNegation)
{
    Int2 v(5, -10);
    Int2 result = -v;
    EXPECT_EQ(result.x, -5);
    EXPECT_EQ(result.y, 10);
}

TEST_F(Int2Test, SwizzlePatterns)
{
    Int2 v(1, 2);
    
    Int2 xy = v.xy();
    EXPECT_EQ(xy.x, 1);
    EXPECT_EQ(xy.y, 2);
    
    Int2 yx = v.yx();
    EXPECT_EQ(yx.x, 2);
    EXPECT_EQ(yx.y, 1);
    
    Int2 xx = v.xx();
    EXPECT_EQ(xx.x, 1);
    EXPECT_EQ(xx.y, 1);
}

// UInt2 Tests
class UInt2Test : public ::testing::Test {};

TEST_F(UInt2Test, DefaultConstruction)
{
    UInt2 v;
    EXPECT_EQ(v.x, 0u);
    EXPECT_EQ(v.y, 0u);
}

TEST_F(UInt2Test, ComponentConstruction)
{
    UInt2 v(5u, 10u);
    EXPECT_EQ(v.x, 5u);
    EXPECT_EQ(v.y, 10u);
}

TEST_F(UInt2Test, UnsignedArithmetic)
{
    UInt2 v1(10u, 20u);
    UInt2 v2(3u, 4u);
    
    UInt2 sum = v1 + v2;
    EXPECT_EQ(sum.x, 13u);
    EXPECT_EQ(sum.y, 24u);
    
    UInt2 diff = v1 - v2;
    EXPECT_EQ(diff.x, 7u);
    EXPECT_EQ(diff.y, 16u);
}

TEST_F(UInt2Test, UnsignedUnderflow)
{
    UInt2 v1(5u, 10u);
    UInt2 v2(10u, 20u);
    UInt2 result = v1 - v2;
    
    // Unsigned underflow wraps around
    EXPECT_GT(result.x, 1000000u);  // Should wrap to very large value
    EXPECT_GT(result.y, 1000000u);
}

TEST_F(UInt2Test, MixedTypeWithFloat)
{
    UInt2 vi(2u, 3u);
    Float2 vf(1.5f, 2.5f);
    Float2 result = vi + vf;
    
    EXPECT_FLOAT_EQ(result.x, 3.5f);
    EXPECT_FLOAT_EQ(result.y, 5.5f);
}

// Int3 Tests
class Int3Test : public ::testing::Test {};

TEST_F(Int3Test, ComponentConstruction)
{
    Int3 v(1, 2, 3);
    EXPECT_EQ(v.x, 1);
    EXPECT_EQ(v.y, 2);
    EXPECT_EQ(v.z, 3);
}

TEST_F(Int3Test, SwizzlePatterns)
{
    Int3 v(1, 2, 3);
    
    Int3 xyz = v.xyz();
    EXPECT_EQ(xyz.x, 1);
    EXPECT_EQ(xyz.y, 2);
    EXPECT_EQ(xyz.z, 3);
    
    Int3 zyx = v.zyx();
    EXPECT_EQ(zyx.x, 3);
    EXPECT_EQ(zyx.y, 2);
    EXPECT_EQ(zyx.z, 1);
    
    Int2 xy = v.xy();
    EXPECT_EQ(xy.x, 1);
    EXPECT_EQ(xy.y, 2);
}

TEST_F(Int3Test, MixedTypeOperations)
{
    Int3 vi(1, 2, 3);
    Float3 vf(0.5f, 1.5f, 2.5f);
    Float3 result = vi * vf;
    
    EXPECT_FLOAT_EQ(result.x, 0.5f);
    EXPECT_FLOAT_EQ(result.y, 3.0f);
    EXPECT_FLOAT_EQ(result.z, 7.5f);
}

// Int4 Tests
class Int4Test : public ::testing::Test {};

TEST_F(Int4Test, ComponentConstruction)
{
    Int4 v(1, 2, 3, 4);
    EXPECT_EQ(v.x, 1);
    EXPECT_EQ(v.y, 2);
    EXPECT_EQ(v.z, 3);
    EXPECT_EQ(v.w, 4);
}

TEST_F(Int4Test, SwizzlePatterns)
{
    Int4 v(1, 2, 3, 4);
    
    Int4 xyzw = v.xyzw();
    EXPECT_EQ(xyzw.x, 1);
    EXPECT_EQ(xyzw.y, 2);
    EXPECT_EQ(xyzw.z, 3);
    EXPECT_EQ(xyzw.w, 4);
    
    Int3 xyz = v.xyz();
    EXPECT_EQ(xyz.x, 1);
    EXPECT_EQ(xyz.y, 2);
    EXPECT_EQ(xyz.z, 3);
    
    Int2 zw = v.zw();
    EXPECT_EQ(zw.x, 3);
    EXPECT_EQ(zw.y, 4);
}

TEST_F(Int4Test, ComponentAccessors)
{
    Int4 color(255, 128, 64, 255);
    EXPECT_EQ(color.x, 255);
    EXPECT_EQ(color.y, 128);
    EXPECT_EQ(color.z, 64);
    EXPECT_EQ(color.w, 255);
}

TEST_F(Int4Test, MixedTypeOperations)
{
    Int4 vi(1, 2, 3, 4);
    Float4 vf(2.0f, 3.0f, 4.0f, 5.0f);
    Float4 result = vi + vf;
    
    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 7.0f);
    EXPECT_FLOAT_EQ(result.w, 9.0f);
}

// UInt3 Tests
class UInt3Test : public ::testing::Test {};

TEST_F(UInt3Test, ComponentConstruction)
{
    UInt3 v(10u, 20u, 30u);
    EXPECT_EQ(v.x, 10u);
    EXPECT_EQ(v.y, 20u);
    EXPECT_EQ(v.z, 30u);
}

TEST_F(UInt3Test, TextureCoordinates)
{
    // Common use case: texture coordinates/dimensions
    UInt3 texSize(1024u, 1024u, 64u);
    UInt3 coord(512u, 256u, 32u);
    
    // Check coordinate is within bounds
    EXPECT_LT(coord.x, texSize.x);
    EXPECT_LT(coord.y, texSize.y);
    EXPECT_LT(coord.z, texSize.z);
}

// UInt4 Tests
class UInt4Test : public ::testing::Test {};

TEST_F(UInt4Test, ComponentConstruction)
{
    UInt4 v(10u, 20u, 30u, 40u);
    EXPECT_EQ(v.x, 10u);
    EXPECT_EQ(v.y, 20u);
    EXPECT_EQ(v.z, 30u);
    EXPECT_EQ(v.w, 40u);
}

TEST_F(UInt4Test, ColorPacking)
{
    // Common use case: RGBA8 color
    UInt4 color(255u, 128u, 64u, 255u);
    EXPECT_EQ(color.x, 255u);
    EXPECT_EQ(color.y, 128u);
    EXPECT_EQ(color.z, 64u);
    EXPECT_EQ(color.w, 255u);
}

}
