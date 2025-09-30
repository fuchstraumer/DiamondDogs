#include <gtest/gtest.h>
#include <Math.hpp>

namespace math
{

// Float3x3 Storage Tests
class Float3x3Test : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-6f;
};

TEST_F(Float3x3Test, DefaultConstruction)
{
    Float3x3 m;
    
    // Default should be identity
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(1, 1), 1.0f);
    EXPECT_FLOAT_EQ(m(2, 2), 1.0f);
    EXPECT_FLOAT_EQ(m(0, 1), 0.0f);
}

TEST_F(Float3x3Test, ComponentConstruction)
{
    Float3x3 m(
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f
    );
    
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(0, 1), 2.0f);
    EXPECT_FLOAT_EQ(m(0, 2), 3.0f);
    EXPECT_FLOAT_EQ(m(1, 0), 4.0f);
    EXPECT_FLOAT_EQ(m(2, 2), 9.0f);
}

TEST_F(Float3x3Test, RowConstruction)
{
    Float3 row0(1.0f, 2.0f, 3.0f);
    Float3 row1(4.0f, 5.0f, 6.0f);
    Float3 row2(7.0f, 8.0f, 9.0f);
    
    Float3x3 m(row0, row1, row2);
    
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(1, 1), 5.0f);
    EXPECT_FLOAT_EQ(m(2, 2), 9.0f);
}

TEST_F(Float3x3Test, RowAccessors)
{
    Float3x3 m(
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f
    );
    
    Float3 row0 = m.Row(0);
    EXPECT_FLOAT_EQ(row0.x, 1.0f);
    EXPECT_FLOAT_EQ(row0.y, 2.0f);
    EXPECT_FLOAT_EQ(row0.z, 3.0f);
    
    Float3 row2 = m.Row(2);
    EXPECT_FLOAT_EQ(row2.x, 7.0f);
    EXPECT_FLOAT_EQ(row2.y, 8.0f);
    EXPECT_FLOAT_EQ(row2.z, 9.0f);
}

TEST_F(Float3x3Test, ColumnAccessors)
{
    Float3x3 m(
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f
    );
    
    Float3 col1 = m.Column(1);
    EXPECT_FLOAT_EQ(col1.x, 2.0f);
    EXPECT_FLOAT_EQ(col1.y, 5.0f);
    EXPECT_FLOAT_EQ(col1.z, 8.0f);
}

TEST_F(Float3x3Test, SetRow)
{
    Float3x3 m = Float3x3::Identity();
    m.SetRow(1, Float3(10.0f, 11.0f, 12.0f));
    
    EXPECT_FLOAT_EQ(m(1, 0), 10.0f);
    EXPECT_FLOAT_EQ(m(1, 1), 11.0f);
    EXPECT_FLOAT_EQ(m(1, 2), 12.0f);
}

TEST_F(Float3x3Test, Equality)
{
    Float3x3 m1 = Float3x3::Identity();
    Float3x3 m2 = Float3x3::Identity();
    Float3x3 m3 = Float3x3::Zero();
    
    EXPECT_TRUE(m1 == m2);
    EXPECT_FALSE(m1 == m3);
    EXPECT_TRUE(m1 != m3);
}

// Float4x3 Storage Tests
class Float4x3Test : public ::testing::Test {};

TEST_F(Float4x3Test, DefaultConstruction)
{
    Float4x3 m;
    
    // Default should be identity
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(1, 1), 1.0f);
    EXPECT_FLOAT_EQ(m(2, 2), 1.0f);
    EXPECT_FLOAT_EQ(m(3, 2), 0.0f);
}

TEST_F(Float4x3Test, RowAccessors)
{
    Float4x3 m = Float4x3::Identity();
    
    Float3 row0 = m.Row(0);
    EXPECT_FLOAT_EQ(row0.x, 1.0f);
    EXPECT_FLOAT_EQ(row0.y, 0.0f);
    EXPECT_FLOAT_EQ(row0.z, 0.0f);
}

TEST_F(Float4x3Test, ColumnAccessors)
{
    Float4x3 m = Float4x3::Identity();
    
    Float4 col2 = m.Column(2);
    EXPECT_FLOAT_EQ(col2.x, 0.0f);
    EXPECT_FLOAT_EQ(col2.y, 0.0f);
    EXPECT_FLOAT_EQ(col2.z, 1.0f);
    EXPECT_FLOAT_EQ(col2.w, 0.0f);
}

// Float4x4 Storage Tests
class Float4x4Test : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-6f;
};

TEST_F(Float4x4Test, DefaultConstruction)
{
    Float4x4 m;
    
    // Default should be identity
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(1, 1), 1.0f);
    EXPECT_FLOAT_EQ(m(2, 2), 1.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 1.0f);
    EXPECT_FLOAT_EQ(m(0, 1), 0.0f);
    EXPECT_FLOAT_EQ(m(1, 0), 0.0f);
}

TEST_F(Float4x4Test, ComponentConstruction)
{
    Float4x4 m(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(0, 3), 4.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 16.0f);
}

TEST_F(Float4x4Test, RowAccessors)
{
    Float4x4 m(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    Float4 row0 = m.Row(0);
    EXPECT_FLOAT_EQ(row0.x, 1.0f);
    EXPECT_FLOAT_EQ(row0.y, 2.0f);
    EXPECT_FLOAT_EQ(row0.z, 3.0f);
    EXPECT_FLOAT_EQ(row0.w, 4.0f);
    
    Float4 row3 = m.Row(3);
    EXPECT_FLOAT_EQ(row3.x, 13.0f);
    EXPECT_FLOAT_EQ(row3.w, 16.0f);
}

TEST_F(Float4x4Test, ColumnAccessors)
{
    Float4x4 m(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    Float4 col1 = m.Column(1);
    EXPECT_FLOAT_EQ(col1.x, 2.0f);
    EXPECT_FLOAT_EQ(col1.y, 6.0f);
    EXPECT_FLOAT_EQ(col1.z, 10.0f);
    EXPECT_FLOAT_EQ(col1.w, 14.0f);
}

TEST_F(Float4x4Test, SetRowAndColumn)
{
    Float4x4 m = Float4x4::Identity();
    
    m.SetRow(2, Float4(20.0f, 21.0f, 22.0f, 23.0f));
    EXPECT_FLOAT_EQ(m(2, 0), 20.0f);
    EXPECT_FLOAT_EQ(m(2, 3), 23.0f);
    
    m.SetColumn(1, Float4(30.0f, 31.0f, 32.0f, 33.0f));
    EXPECT_FLOAT_EQ(m(0, 1), 30.0f);
    EXPECT_FLOAT_EQ(m(3, 1), 33.0f);
}

TEST_F(Float4x4Test, IdentityMatrix)
{
    Float4x4 m = Float4x4::Identity();
    
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            if (i == j)
            {
                EXPECT_FLOAT_EQ(m(i, j), 1.0f);
            }
            else
            {
                EXPECT_FLOAT_EQ(m(i, j), 0.0f);
            }
        }
    }
}

TEST_F(Float4x4Test, ZeroMatrix)
{
    Float4x4 m = Float4x4::Zero();
    
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_FLOAT_EQ(m(i, j), 0.0f);
        }
    }
}

TEST_F(Float4x4Test, Equality)
{
    Float4x4 m1 = Float4x4::Identity();
    Float4x4 m2 = Float4x4::Identity();
    Float4x4 m3 = Float4x4::Zero();
    
    EXPECT_TRUE(m1 == m2);
    EXPECT_FALSE(m1 == m3);
    EXPECT_TRUE(m1 != m3);
}

TEST_F(Float4x4Test, ExtractionFrom3x3)
{
    Float4x4 m4(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    Float3x3 m3(m4);
    
    // Should extract upper-left 3x3
    EXPECT_FLOAT_EQ(m3(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m3(0, 2), 3.0f);
    EXPECT_FLOAT_EQ(m3(2, 2), 11.0f);
}

} // namespace math
