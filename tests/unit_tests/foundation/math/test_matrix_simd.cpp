#include <gtest/gtest.h>
#include <Math.hpp>
#include <numbers>

namespace math
{

class MatrixTest : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-4f;
    static constexpr float EPSILON_INVERSE = 1e-3f;
    
    static void ExpectNear(const Matrix& actual, const Matrix& expected, float epsilon = EPSILON)
    {
        for (size_t i = 0; i < 4; ++i)
        {
            for (size_t j = 0; j < 4; ++j)
            {
                EXPECT_NEAR(actual(i, j), expected(i, j), epsilon) 
                    << "Mismatch at (" << i << "," << j << ")";
            }
        }
    }
};

// Construction Tests
TEST_F(MatrixTest, DefaultConstruction)
{
    Matrix m;
    // Default behavior depends on implementation
    SUCCEED();
}

TEST_F(MatrixTest, ComponentConstruction)
{
    Matrix m(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 16.0f);
}

TEST_F(MatrixTest, IdentityFactory)
{
    Matrix m = Matrix::Identity();
    
    EXPECT_TRUE(m.IsIdentity());
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(1, 1), 1.0f);
    EXPECT_FLOAT_EQ(m(2, 2), 1.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 1.0f);
    EXPECT_FLOAT_EQ(m(0, 1), 0.0f);
}

TEST_F(MatrixTest, ZeroFactory)
{
    Matrix m = Matrix::Zero();
    
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_FLOAT_EQ(m(i, j), 0.0f);
        }
    }
}

// Arithmetic Operations
TEST_F(MatrixTest, MatrixAddition)
{
    Matrix m1 = Matrix::Identity();
    Matrix m2 = Matrix::Identity();
    Matrix result = m1 + m2;
    
    EXPECT_FLOAT_EQ(result(0, 0), 2.0f);
    EXPECT_FLOAT_EQ(result(1, 1), 2.0f);
    EXPECT_FLOAT_EQ(result(0, 1), 0.0f);
}

TEST_F(MatrixTest, MatrixSubtraction)
{
    Matrix m1 = Matrix::Identity();
    Matrix m2 = Matrix::Identity();
    Matrix result = m1 - m2;
    
    EXPECT_FLOAT_EQ(result(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(result(1, 1), 0.0f);
}

TEST_F(MatrixTest, ScalarMultiplication)
{
    Matrix m = Matrix::Identity();
    Matrix result = m * 3.0f;
    
    EXPECT_FLOAT_EQ(result(0, 0), 3.0f);
    EXPECT_FLOAT_EQ(result(1, 1), 3.0f);
    EXPECT_FLOAT_EQ(result(2, 2), 3.0f);
}

TEST_F(MatrixTest, MatrixMultiplicationIdentity)
{
    Matrix m = Matrix::Identity();
    Matrix result = m * m;
    
    ExpectNear(result, Matrix::Identity());
}

TEST_F(MatrixTest, MatrixMultiplicationNonCommutative)
{
    Matrix m1(
        1.0f, 2.0f, 0.0f, 0.0f,
        3.0f, 4.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    
    Matrix m2(
        5.0f, 6.0f, 0.0f, 0.0f,
        7.0f, 8.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
    
    Matrix result1 = m1 * m2;
    Matrix result2 = m2 * m1;
    
    // Matrix multiplication is not commutative
    EXPECT_NE(result1(0, 0), result2(0, 0));
}

// Matrix-Vector Operations
TEST_F(MatrixTest, MatrixVectorMultiplication)
{
    Matrix m = Matrix::Identity();
    Vector v = ToVector(Float4(1.0f, 2.0f, 3.0f, 4.0f));
    Vector result = m * v;
    
    EXPECT_FLOAT_EQ(result.x(), 1.0f);
    EXPECT_FLOAT_EQ(result.y(), 2.0f);
    EXPECT_FLOAT_EQ(result.z(), 3.0f);
    EXPECT_FLOAT_EQ(result.w(), 4.0f);
}

TEST_F(MatrixTest, TransformVector)
{
    Matrix scale = Matrix::Scale(2.0f, 3.0f, 4.0f);
    Vector v = ToVector(Float3(1.0f, 1.0f, 1.0f));
    Vector result = scale * v;
    
    EXPECT_FLOAT_EQ(result.x(), 2.0f);
    EXPECT_FLOAT_EQ(result.y(), 3.0f);
    EXPECT_FLOAT_EQ(result.z(), 4.0f);
}

// Transpose Tests
TEST_F(MatrixTest, TransposeIdentity)
{
    Matrix m = Matrix::Identity();
    Matrix transposed = m.Transpose();
    
    ExpectNear(transposed, m);
}

TEST_F(MatrixTest, TransposeDoubleInversion)
{
    Matrix m(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    Matrix transposed = m.Transpose();
    Matrix doubleTransposed = transposed.Transpose();
    
    ExpectNear(doubleTransposed, m);
}

TEST_F(MatrixTest, TransposeSwapsRowsAndColumns)
{
    Matrix m(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    Matrix transposed = m.Transpose();
    
    EXPECT_FLOAT_EQ(m(0, 1), transposed(1, 0));
    EXPECT_FLOAT_EQ(m(2, 3), transposed(3, 2));
}

// Determinant Tests
TEST_F(MatrixTest, DeterminantIdentity)
{
    Matrix m = Matrix::Identity();
    float det = m.Determinant();
    
    EXPECT_NEAR(det, 1.0f, EPSILON);
}

TEST_F(MatrixTest, DeterminantZero)
{
    Matrix m = Matrix::Zero();
    float det = m.Determinant();
    
    EXPECT_NEAR(det, 0.0f, EPSILON);
}

TEST_F(MatrixTest, DeterminantScale)
{
    Matrix m = Matrix::Scale(2.0f, 3.0f, 4.0f);
    float det = m.Determinant();
    
    // Determinant of diagonal matrix is product of diagonal elements
    EXPECT_NEAR(det, 24.0f, EPSILON);
}

// Inverse Tests
TEST_F(MatrixTest, InverseIdentity)
{
    Matrix m = Matrix::Identity();
    Matrix inverse = m.Inverse();
    
    ExpectNear(inverse, m);
}

TEST_F(MatrixTest, InverseMultiplicationIdentity)
{
    Matrix m = Matrix::Scale(2.0f, 3.0f, 4.0f);
    Matrix inverse = m.Inverse();
    Matrix result = m * inverse;
    
    ExpectNear(result, Matrix::Identity(), EPSILON_INVERSE);
}

TEST_F(MatrixTest, InverseDoubleInversion)
{
    Matrix m = Matrix::Scale(2.0f, 3.0f, 4.0f);
    Matrix inverse = m.Inverse();
    Matrix doubleInverse = inverse.Inverse();
    
    ExpectNear(doubleInverse, m, EPSILON_INVERSE);
}

TEST_F(MatrixTest, InverseScale)
{
    Matrix m = Matrix::Scale(2.0f, 4.0f, 8.0f);
    Matrix inverse = m.Inverse();
    
    EXPECT_NEAR(inverse(0, 0), 0.5f, EPSILON);
    EXPECT_NEAR(inverse(1, 1), 0.25f, EPSILON);
    EXPECT_NEAR(inverse(2, 2), 0.125f, EPSILON);
}

// Translation Tests
TEST_F(MatrixTest, TranslationFactory)
{
    Matrix m = Matrix::Translation(10.0f, 20.0f, 30.0f);
    
    EXPECT_FLOAT_EQ(m(3, 0), 10.0f);
    EXPECT_FLOAT_EQ(m(3, 1), 20.0f);
    EXPECT_FLOAT_EQ(m(3, 2), 30.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 1.0f);
}

TEST_F(MatrixTest, TranslationTransformsPoint)
{
    Matrix m = Matrix::Translation(5.0f, 10.0f, 15.0f);
    Vector point = ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f));  // Homogeneous point
    Vector result = m * point;
    
    EXPECT_NEAR(result.x(), 5.0f, EPSILON);
    EXPECT_NEAR(result.y(), 10.0f, EPSILON);
    EXPECT_NEAR(result.z(), 15.0f, EPSILON);
    EXPECT_NEAR(result.w(), 1.0f, EPSILON);
}

TEST_F(MatrixTest, TranslationDoesNotAffectDirection)
{
    Matrix m = Matrix::Translation(5.0f, 10.0f, 15.0f);
    Vector direction = ToVector(Float4(1.0f, 0.0f, 0.0f, 0.0f));  // Homogeneous direction
    Vector result = m * direction;
    
    EXPECT_NEAR(result.x(), 1.0f, EPSILON);
    EXPECT_NEAR(result.y(), 0.0f, EPSILON);
    EXPECT_NEAR(result.z(), 0.0f, EPSILON);
    EXPECT_NEAR(result.w(), 0.0f, EPSILON);
}

// Scale Tests
TEST_F(MatrixTest, UniformScale)
{
    Matrix m = Matrix::Scale(2.0f);
    Vector v = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 2.0f, EPSILON);
    EXPECT_NEAR(result.y(), 4.0f, EPSILON);
    EXPECT_NEAR(result.z(), 6.0f, EPSILON);
}

TEST_F(MatrixTest, NonUniformScale)
{
    Matrix m = Matrix::Scale(2.0f, 3.0f, 4.0f);
    Vector v = ToVector(Float3(1.0f, 1.0f, 1.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 2.0f, EPSILON);
    EXPECT_NEAR(result.y(), 3.0f, EPSILON);
    EXPECT_NEAR(result.z(), 4.0f, EPSILON);
}

TEST_F(MatrixTest, ScaleDeterminant)
{
    Matrix m = Matrix::Scale(2.0f, 3.0f, 4.0f);
    float det = m.Determinant();
    
    EXPECT_NEAR(det, 24.0f, EPSILON);
}

// Rotation Tests
TEST_F(MatrixTest, RotationX_90Degrees)
{
    Matrix m = Matrix::RotationX(std::numbers::pi_v<float> / 2.0f);
    Vector v = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 0.0f, EPSILON);
    EXPECT_NEAR(result.y(), 0.0f, EPSILON);
    EXPECT_NEAR(result.z(), 1.0f, EPSILON);
}

TEST_F(MatrixTest, RotationY_90Degrees)
{
    Matrix m = Matrix::RotationY(std::numbers::pi_v<float> / 2.0f);
    Vector v = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 0.0f, EPSILON);
    EXPECT_NEAR(result.y(), 0.0f, EPSILON);
    EXPECT_NEAR(result.z(), -1.0f, EPSILON);
}

TEST_F(MatrixTest, RotationZ_90Degrees)
{
    Matrix m = Matrix::RotationZ(std::numbers::pi_v<float> / 2.0f);
    Vector v = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 0.0f, EPSILON);
    EXPECT_NEAR(result.y(), 1.0f, EPSILON);
    EXPECT_NEAR(result.z(), 0.0f, EPSILON);
}

TEST_F(MatrixTest, Rotation360Degrees)
{
    Matrix m = Matrix::RotationZ(2.0f * std::numbers::pi_v<float>);
    Vector v = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 1.0f, EPSILON);
    EXPECT_NEAR(result.y(), 2.0f, EPSILON);
    EXPECT_NEAR(result.z(), 3.0f, EPSILON);
}

TEST_F(MatrixTest, RotationPreservesLength)
{
    Matrix m = Matrix::RotationZ(0.7f);
    Vector v = ToVector(Float3(3.0f, 4.0f, 0.0f));
    float originalLength = v.Length<3>();
    
    Vector result = m * v;
    float rotatedLength = result.Length<3>();
    
    EXPECT_NEAR(originalLength, rotatedLength, EPSILON);
}

TEST_F(MatrixTest, RotationArbitraryAxis)
{
    Vector axis = ToVector(Float3(0.0f, 0.0f, 1.0f));
    axis = axis.Normalize<3>();
    Matrix m = Matrix::RotationAxis(axis, std::numbers::pi_v<float> / 2.0f);
    
    Vector v = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector result = m * v;
    
    EXPECT_NEAR(result.x(), 0.0f, EPSILON);
    EXPECT_NEAR(result.y(), 1.0f, EPSILON);
}

// Compound Transformation Tests
TEST_F(MatrixTest, TRS_TranslationRotationScale)
{
    Vector translation = ToVector(Float3(10.0f, 20.0f, 30.0f));
    Vector rotation = ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f));  // Identity quaternion
    Vector scale = ToVector(Float3(2.0f, 2.0f, 2.0f));
    
    Matrix m = Matrix::TRS(translation, rotation, scale);
    Vector point = ToVector(Float4(1.0f, 0.0f, 0.0f, 1.0f));
    Vector result = Transform<3>(point, m);
    
    // Should scale then translate (rotation is identity)
    EXPECT_NEAR(result.x(), 12.0f, EPSILON);
    EXPECT_NEAR(result.y(), 20.0f, EPSILON);
    EXPECT_NEAR(result.z(), 30.0f, EPSILON);
}

TEST_F(MatrixTest, CombinedTransformations)
{
    Matrix translation = Matrix::Translation(5.0f, 0.0f, 0.0f);
    Matrix scale = Matrix::Scale(2.0f);
    Matrix combined = scale * translation;
    
    Vector point = ToVector(Float4(1.0f, 1.0f, 1.0f, 1.0f));
    Vector result = Transform<3>(point, combined);
    // Scale happens first, then translate
    EXPECT_NEAR(result.x(), 7.0f, EPSILON);
    EXPECT_NEAR(result.y(), 2.0f, EPSILON);
    EXPECT_NEAR(result.z(), 2.0f, EPSILON);
}

// Comparison Tests
TEST_F(MatrixTest, IsIdentity)
{
    Matrix m = Matrix::Identity();
    EXPECT_TRUE(m.IsIdentity());
    
    Matrix notIdentity = Matrix::Scale(2.0f);
    EXPECT_FALSE(notIdentity.IsIdentity());
}

TEST_F(MatrixTest, IsNearlyEqual)
{
    Matrix m1 = Matrix::Identity();
    Matrix m2 = Matrix::Identity();
    
    EXPECT_TRUE(m1.IsNearlyEqual(m2));
    
    Matrix m3 = Matrix::Scale(1.00001f);
    EXPECT_TRUE(m1.IsNearlyEqual(m3, 1e-3f));
    EXPECT_FALSE(m1.IsNearlyEqual(m3, 1e-6f));
}

} // namespace math
