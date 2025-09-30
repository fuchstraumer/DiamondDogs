#include <gtest/gtest.h>
#include <Math.hpp>
#include <numbers>
#include <cmath>

namespace math
{

class VectorTest : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-5f;
    static constexpr float EPSILON_EST = 1e-3f;  // Looser tolerance for estimated functions
    
    static void ExpectNear(const Vector& actual, const Vector& expected, float epsilon = EPSILON)
    {
        EXPECT_NEAR(actual.x(), expected.x(), epsilon) << "X component mismatch";
        EXPECT_NEAR(actual.y(), expected.y(), epsilon) << "Y component mismatch";
        EXPECT_NEAR(actual.z(), expected.z(), epsilon) << "Z component mismatch";
        EXPECT_NEAR(actual.w(), expected.w(), epsilon) << "W component mismatch";
    }

    static void ExpectNear(const Vector& actual, const Float3& expected, const float epsilon = EPSILON)
    {
        EXPECT_NEAR(actual.x(), expected.x, epsilon) << "X component mismatch";
        EXPECT_NEAR(actual.y(), expected.y, epsilon) << "Y component mismatch";
        EXPECT_NEAR(actual.z(), expected.z, epsilon) << "Z component mismatch";
    }
};

// Construction and Conversion Tests
TEST_F(VectorTest, ConstructionFromFloat2)
{
    Float2 f2(1.0f, 2.0f);
    Vector v = ToVector(f2);
    EXPECT_FLOAT_EQ(v.x(), 1.0f);
    EXPECT_FLOAT_EQ(v.y(), 2.0f);
}

TEST_F(VectorTest, ConstructionFromFloat3)
{
    Float3 f3(1.0f, 2.0f, 3.0f);
    Vector v = ToVector(f3);
    EXPECT_FLOAT_EQ(v.x(), 1.0f);
    EXPECT_FLOAT_EQ(v.y(), 2.0f);
    EXPECT_FLOAT_EQ(v.z(), 3.0f);
}

TEST_F(VectorTest, ConstructionFromFloat4)
{
    Float4 f4(1.0f, 2.0f, 3.0f, 4.0f);
    Vector v = ToVector(f4);
    EXPECT_FLOAT_EQ(v.x(), 1.0f);
    EXPECT_FLOAT_EQ(v.y(), 2.0f);
    EXPECT_FLOAT_EQ(v.z(), 3.0f);
    EXPECT_FLOAT_EQ(v.w(), 4.0f);
}

TEST_F(VectorTest, ConversionToFloat3)
{
    Vector v = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Float3 f3 = FromVector<Float3>(v);
    EXPECT_FLOAT_EQ(f3.x, 1.0f);
    EXPECT_FLOAT_EQ(f3.y, 2.0f);
    EXPECT_FLOAT_EQ(f3.z, 3.0f);
}

TEST_F(VectorTest, FactoryMethods)
{
    Vector zero = Vector::Zero();
    EXPECT_FLOAT_EQ(zero.x(), 0.0f);
    EXPECT_FLOAT_EQ(zero.y(), 0.0f);
    EXPECT_FLOAT_EQ(zero.z(), 0.0f);
    EXPECT_FLOAT_EQ(zero.w(), 0.0f);
    
    Vector replicated = Vector::Replicate(5.0f);
    EXPECT_FLOAT_EQ(replicated.x(), 5.0f);
    EXPECT_FLOAT_EQ(replicated.y(), 5.0f);
    EXPECT_FLOAT_EQ(replicated.z(), 5.0f);
    EXPECT_FLOAT_EQ(replicated.w(), 5.0f);
    
    Vector identity = Vector::Identity();
    EXPECT_FLOAT_EQ(identity.x(), 1.0f);
    EXPECT_FLOAT_EQ(identity.y(), 1.0f);
    EXPECT_FLOAT_EQ(identity.z(), 1.0f);
    EXPECT_FLOAT_EQ(identity.w(), 1.0f);
}

// Basic Arithmetic Tests
TEST_F(VectorTest, Addition)
{
    Vector v1 = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector v2 = ToVector(Float3(4.0f, 5.0f, 6.0f));
    Vector result = v1 + v2;
    
    ExpectNear(result, Float3(5.0f, 7.0f, 9.0f));
}

TEST_F(VectorTest, Subtraction)
{
    Vector v1 = ToVector(Float3(10.0f, 8.0f, 6.0f));
    Vector v2 = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector result = v1 - v2;
    
    ExpectNear(result, Float3(9.0f, 6.0f, 3.0f));
}

TEST_F(VectorTest, Multiplication)
{
    Vector v1 = ToVector(Float3(2.0f, 3.0f, 4.0f));
    Vector v2 = ToVector(Float3(5.0f, 6.0f, 7.0f));
    Vector result = v1 * v2;
    
    ExpectNear(result, Float3(10.0f, 18.0f, 28.0f));
}

TEST_F(VectorTest, Division)
{
    Vector v1 = ToVector(Float4(20.0f, 30.0f, 40.0f, 0.0f));
    Vector v2 = ToVector(Float4(4.0f, 5.0f, 8.0f, 1.0f));
    Vector result = v1 / v2;
    
    ExpectNear(result, ToVector(Float4(5.0f, 6.0f, 5.0f, 0.0f)));
}

TEST_F(VectorTest, ScalarMultiplication)
{
    Vector v = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector result = v * 4.0f;
    
    ExpectNear(result, Float3(4.0f, 8.0f, 12.0f));
}

TEST_F(VectorTest, ScalarDivision)
{
    Vector v = ToVector(Float3(10.0f, 20.0f, 30.0f));
    Vector result = v / 10.0f;
    
    ExpectNear(result, Float3(1.0f, 2.0f, 3.0f));
}

TEST_F(VectorTest, UnaryNegation)
{
    Vector v = ToVector(Float3(1.0f, -2.0f, 3.0f));
    Vector result = -v;
    
    ExpectNear(result, Float3(-1.0f, 2.0f, -3.0f));
}

TEST_F(VectorTest, MultiplyAdd)
{
    Vector v = ToVector(Float3(2.0f, 3.0f, 4.0f));
    Vector factor = ToVector(Float3(5.0f, 6.0f, 7.0f));
    Vector addend = ToVector(Float3(1.0f, 1.0f, 1.0f));
    Vector result = v.MultiplyAdd(factor, addend);
    
    // (2*5)+1=11, (3*6)+1=19, (4*7)+1=29
    ExpectNear(result, Float3(11.0f, 19.0f, 29.0f));
}

// Dot Product Tests
TEST_F(VectorTest, DotProduct2D)
{
    Vector v1 = ToVector(Float2(3.0f, 4.0f));
    Vector v2 = ToVector(Float2(2.0f, 1.0f));
    float result = v1.Dot<2>(v2);
    
    EXPECT_NEAR(result, 10.0f, EPSILON); // 3*2 + 4*1 = 10
}

TEST_F(VectorTest, DotProduct3D)
{
    Vector v1 = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector v2 = ToVector(Float3(4.0f, 5.0f, 6.0f));
    float result = v1.Dot<3>(v2);
    
    EXPECT_NEAR(result, 32.0f, EPSILON); // 1*4 + 2*5 + 3*6 = 32
}

TEST_F(VectorTest, DotProduct4D)
{
    Vector v1 = ToVector(Float4(1.0f, 2.0f, 3.0f, 4.0f));
    Vector v2 = ToVector(Float4(5.0f, 6.0f, 7.0f, 8.0f));
    float result = v1.Dot<4>(v2);
    
    EXPECT_NEAR(result, 70.0f, EPSILON); // 1*5 + 2*6 + 3*7 + 4*8 = 70
}

TEST_F(VectorTest, DotProductOrthogonal)
{
    Vector v1 = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(0.0f, 1.0f, 0.0f));
    float result = v1.Dot<3>(v2);
    
    EXPECT_NEAR(result, 0.0f, EPSILON);
}

TEST_F(VectorTest, DotProductParallel)
{
    Vector v1 = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(2.0f, 0.0f, 0.0f));
    float result = v1.Dot<3>(v2);
    
    EXPECT_NEAR(result, 2.0f, EPSILON);
}

// Cross Product Tests (3D only)
TEST_F(VectorTest, CrossProductRightHandRule)
{
    Vector v1 = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Vector result = v1.Cross(v2);
    
    ExpectNear(result, Float3(0.0f, 0.0f, 1.0f));
}

TEST_F(VectorTest, CrossProductAnticommutative)
{
    Vector v1 = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector v2 = ToVector(Float3(4.0f, 5.0f, 6.0f));
    Vector cross1 = v1.Cross(v2);
    Vector cross2 = v2.Cross(v1);
    
    ExpectNear(cross1, -cross2);
}

TEST_F(VectorTest, CrossProductParallelVectors)
{
    Vector v1 = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector v2 = ToVector(Float3(2.0f, 4.0f, 6.0f));
    Vector result = v1.Cross(v2);
    
    // Parallel vectors have zero cross product
    ExpectNear(result, Vector::Zero(), 1e-4f);
}

// Length and Normalization Tests
TEST_F(VectorTest, Length3D)
{
    Vector v = ToVector(Float3(3.0f, 4.0f, 0.0f));
    float length = v.Length<3>();
    
    EXPECT_NEAR(length, 5.0f, EPSILON);
}

TEST_F(VectorTest, LengthSquared)
{
    Vector v = ToVector(Float3(3.0f, 4.0f, 0.0f));
    float lengthSq = v.LengthSq<3>();
    
    EXPECT_NEAR(lengthSq, 25.0f, EPSILON);
}

TEST_F(VectorTest, Normalize3D)
{
    Vector v = ToVector(Float3(3.0f, 4.0f, 0.0f));
    Vector normalized = v.Normalize<3>();

    ExpectNear(normalized, Float3(0.6f, 0.8f, 0.0f));
    
    float length = normalized.Length<3>();
    EXPECT_NEAR(length, 1.0f, EPSILON);
}

TEST_F(VectorTest, NormalizeUnitVector)
{
    Vector v = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector normalized = v.Normalize<3>();
    
    ExpectNear(normalized, v);
}

TEST_F(VectorTest, NormalizeEst)
{
    Vector v = ToVector(Float3(3.0f, 4.0f, 0.0f));
    Vector normalized = v.NormalizeEst<3>();
    
    float length = normalized.Length<3>();
    EXPECT_NEAR(length, 1.0f, EPSILON_EST);
}

// Lerp Tests
TEST_F(VectorTest, LerpAtZero)
{
    Vector v1 = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(10.0f, 20.0f, 30.0f));
    Vector result = v1.Lerp(v2, 0.0f);
    
    ExpectNear(result, v1);
}

TEST_F(VectorTest, LerpAtOne)
{
    Vector v1 = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(10.0f, 20.0f, 30.0f));
    Vector result = v1.Lerp(v2, 1.0f);
    
    ExpectNear(result, v2);
}

TEST_F(VectorTest, LerpMidpoint)
{
    Vector v1 = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(10.0f, 20.0f, 30.0f));
    Vector result = v1.Lerp(v2, 0.5f);
    
    ExpectNear(result, Float3(5.0f, 10.0f, 15.0f));
}

TEST_F(VectorTest, LerpLinearity)
{
    Vector v1 = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(10.0f, 20.0f, 30.0f));
    
    Vector quarter = v1.Lerp(v2, 0.25f);
    Vector half = v1.Lerp(v2, 0.5f);
    Vector threeQuarter = v1.Lerp(v2, 0.75f);
    
    ExpectNear(quarter, Float3(2.5f, 5.0f, 7.5f));
    ExpectNear(half, Float3(5.0f, 10.0f, 15.0f));
    ExpectNear(threeQuarter, Float3(7.5f, 15.0f, 22.5f));
}

// Clamp and Saturate Tests
TEST_F(VectorTest, ClampWithinBounds)
{
    Vector v = ToVector(Float3(5.0f, 5.0f, 5.0f));
    Vector min = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector max = ToVector(Float3(10.0f, 10.0f, 10.0f));
    Vector result = v.Clamp(min, max);
    
    ExpectNear(result, v);
}

TEST_F(VectorTest, ClampBelowMin)
{
    Vector v = ToVector(Float3(-5.0f, 2.0f, 15.0f));
    Vector min = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector max = ToVector(Float3(10.0f, 10.0f, 10.0f));
    Vector result = v.Clamp(min, max);
    
    ExpectNear(result, Float3(0.0f, 2.0f, 10.0f));
}

TEST_F(VectorTest, Saturate)
{
    Vector v = ToVector(Float3(-0.5f, 0.5f, 1.5f));
    Vector result = v.Saturate();
    
    ExpectNear(result, Float3(0.0f, 0.5f, 1.0f));
}

// Min/Max/Abs Tests
TEST_F(VectorTest, Min)
{
    Vector v1 = ToVector(Float3(1.0f, 5.0f, 3.0f));
    Vector v2 = ToVector(Float3(4.0f, 2.0f, 6.0f));
    Vector result = v1.Min(v2);
    
    ExpectNear(result, Float3(1.0f, 2.0f, 3.0f));
}

TEST_F(VectorTest, Max)
{
    Vector v1 = ToVector(Float3(1.0f, 5.0f, 3.0f));
    Vector v2 = ToVector(Float3(4.0f, 2.0f, 6.0f));
    Vector result = v1.Max(v2);
    
    ExpectNear(result, Float3(4.0f, 5.0f, 6.0f));
}

TEST_F(VectorTest, Abs)
{
    Vector v = ToVector(Float3(-1.0f, 2.0f, -3.0f));
    Vector result = v.Abs();
    
    ExpectNear(result, Float3(1.0f, 2.0f, 3.0f));
}

// Reflect and Refract Tests
TEST_F(VectorTest, Reflect)
{
    // Incident ray at 45 degrees to surface normal (0,1,0)
    Vector incident = ToVector(Float3(1.0f, -1.0f, 0.0f));
    incident = incident.Normalize<3>();
    Vector normal = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Vector reflected = incident.Reflect<3>(normal);
    
    // Should reflect symmetrically
    Vector expected = ToVector(Float3(1.0f, 1.0f, 0.0f));
    expected = expected.Normalize<3>();
    ExpectNear(reflected, expected);
}

TEST_F(VectorTest, ReflectAngleEquality)
{
    // Verify angle of incidence = angle of reflection
    Vector incident = ToVector(Float3(1.0f, -1.0f, 0.0f));
    incident = incident.Normalize<3>();
    Vector normal = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Vector reflected = incident.Reflect<3>(normal);
    
    float incidentAngle = std::abs(incident.Dot<3>(normal));
    float reflectedAngle = std::abs(reflected.Dot<3>(normal));
    
    EXPECT_NEAR(incidentAngle, reflectedAngle, EPSILON);
}

TEST_F(VectorTest, Refract_AirToWater)
{
    // Air to water refraction (n1/n2 = 1.0/1.33 ≈ 0.75)
    Vector incident = ToVector(Float3(0.0f, -1.0f, 0.0f));
    Vector normal = ToVector(Float3(0.0f, 1.0f, 0.0f));
    float eta = 1.0f / 1.33f;
    
    Vector refracted = incident.Refract<3>(normal, eta);
    
    // Should still point downward but bent
    EXPECT_LT(refracted.y(), 0.0f);
    
    float length = refracted.Length<3>();
    EXPECT_NEAR(length, 1.0f, EPSILON);
}

// Reciprocal and Sqrt Tests
TEST_F(VectorTest, Reciprocal)
{
    Vector v = ToVector(Float3(2.0f, 4.0f, 8.0f));
    Vector result = v.Reciprocal();
    
    ExpectNear(result, Float3(0.5f, 0.25f, 0.125f));
}

TEST_F(VectorTest, ReciprocalEst)
{
    Vector v = ToVector(Float3(2.0f, 4.0f, 8.0f));
    Vector result = v.ReciprocalEst();
    
    ExpectNear(result, Float3(0.5f, 0.25f, 0.125f), EPSILON_EST);
}

TEST_F(VectorTest, Sqrt)
{
    Vector v = ToVector(Float3(4.0f, 9.0f, 16.0f));
    Vector result = v.Sqrt();
    
    ExpectNear(result, Float3(2.0f, 3.0f, 4.0f));
}

TEST_F(VectorTest, SqrtEst)
{
    Vector v = ToVector(Float3(4.0f, 9.0f, 16.0f));
    Vector result = v.SqrtEst();
    
    ExpectNear(result, Float3(2.0f, 3.0f, 4.0f), EPSILON_EST);
}

TEST_F(VectorTest, ReciprocalSqrt)
{
    Vector v = ToVector(Float3(4.0f, 9.0f, 16.0f));
    Vector result = v.ReciprocalSqrt();
    
    ExpectNear(result, Float3(0.5f, 1.0f/3.0f, 0.25f));
}

// Power Tests
TEST_F(VectorTest, PowScalar)
{
    Vector v = ToVector(Float3(2.0f, 3.0f, 4.0f));
    Vector result = v.Pow(2.0f);
    
    ExpectNear(result, ToVector(Float3(4.0f, 9.0f, 16.0f)));
}

TEST_F(VectorTest, PowVector)
{
    Vector base = ToVector(Float3(2.0f, 3.0f, 4.0f));
    Vector exponent = ToVector(Float3(2.0f, 2.0f, 2.0f));
    Vector result = base.Pow(exponent);
    
    ExpectNear(result, Float3(4.0f, 9.0f, 16.0f));
}

// Edge Cases
TEST_F(VectorTest, NormalizeZeroVector)
{
    Vector zero = Vector::Zero();
    Vector normalized = zero.Normalize<3>();
    
    // Normalizing zero should produce zero (or NaN), implementation defined
    // Just verify it doesn't crash
    SUCCEED();
}

TEST_F(VectorTest, CrossProductWithSelf)
{
    Vector v = ToVector(Float3(1.0f, 2.0f, 3.0f));
    Vector result = v.Cross(v);
    
    // Cross product of vector with itself is zero
    ExpectNear(result, Vector::Zero(), 1e-4f);
}

} // namespace math
