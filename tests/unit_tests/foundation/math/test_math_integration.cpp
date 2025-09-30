#include <gtest/gtest.h>
#include <Math.hpp>
#include <numbers>

namespace math
{

class MathIntegrationTest : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-4f;
};

// Storage to SIMD Roundtrip Tests
TEST_F(MathIntegrationTest, Float2_VectorRoundtrip)
{
    Float2 original(3.14f, 2.71f);
    Vector v = ToVector(original);
    Float2 roundtrip = FromVector<Float2>(v);
    
    EXPECT_FLOAT_EQ(roundtrip.x, original.x);
    EXPECT_FLOAT_EQ(roundtrip.y, original.y);
}

TEST_F(MathIntegrationTest, Float3_VectorRoundtrip)
{
    Float3 original(1.0f, 2.0f, 3.0f);
    Vector v = ToVector(original);
    Float3 roundtrip = FromVector<Float3>(v);
    
    EXPECT_FLOAT_EQ(roundtrip.x, original.x);
    EXPECT_FLOAT_EQ(roundtrip.y, original.y);
    EXPECT_FLOAT_EQ(roundtrip.z, original.z);
}

TEST_F(MathIntegrationTest, Float4_VectorRoundtrip)
{
    Float4 original(1.0f, 2.0f, 3.0f, 4.0f);
    Vector v = ToVector(original);
    Float4 roundtrip = FromVector<Float4>(v);
    
    EXPECT_FLOAT_EQ(roundtrip.x, original.x);
    EXPECT_FLOAT_EQ(roundtrip.y, original.y);
    EXPECT_FLOAT_EQ(roundtrip.z, original.z);
    EXPECT_FLOAT_EQ(roundtrip.w, original.w);
}

TEST_F(MathIntegrationTest, Float4x4_MatrixRoundtrip)
{
    Float4x4 original(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    
    Matrix m = ToMatrix(original);
    Float4x4 roundtrip = FromMatrix<Float4x4>(m);
    
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(roundtrip(i, j), original(i, j), EPSILON);
        }
    }
}

// Chained Transformation Tests
TEST_F(MathIntegrationTest, ChainedMatrixMultiplication)
{
    Matrix m1 = Matrix::Scale(2.0f);
    Matrix m2 = Matrix::Translation(5.0f, 0.0f, 0.0f);
    Matrix m3 = Matrix::RotationZ(std::numbers::pi_v<float> / 4.0f);
    
    Matrix combined = m3 * m2 * m1;
    Vector point = ToVector(Float4(1.0f, 0.0f, 0.0f, 1.0f));
    
    // Apply transformations in order: scale, translate, rotate
    Vector result = combined * point;
    
    // Verify result is reasonable
    EXPECT_NE(result.x(), 0.0f);
    EXPECT_NE(result.y(), 0.0f);
}

TEST_F(MathIntegrationTest, TransformPoint_SingleMatrix_vs_ChainedMatrices)
{
    Matrix scale = Matrix::Scale(2.0f);
    Matrix translation = Matrix::Translation(10.0f, 20.0f, 30.0f);
    Matrix combined = translation * scale;
    
    Vector point = ToVector(Float4(1.0f, 2.0f, 3.0f, 1.0f));
    
    // Method 1: Combined matrix
    Vector result1 = combined * point;
    
    // Method 2: Sequential application
    Vector temp = scale * point;
    Vector result2 = translation * temp;
    
    EXPECT_NEAR(result1.x(), result2.x(), EPSILON);
    EXPECT_NEAR(result1.y(), result2.y(), EPSILON);
    EXPECT_NEAR(result1.z(), result2.z(), EPSILON);
    EXPECT_NEAR(result1.w(), result2.w(), EPSILON);
}

// Model-View-Projection Pipeline
TEST_F(MathIntegrationTest, ModelViewProjection_Pipeline)
{
    // Model transformation  
    Matrix model = Matrix::TRS(
        ToVector(Float3(0.0f, 0.0f, -5.0f)),  // Translation
        ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f)),  // Identity quaternion
        ToVector(Float3(1.0f, 1.0f, 1.0f))  // Scale
    );
    
    // View transformation
    Vector eye = ToVector(Float3(0.0f, 0.0f, 10.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Matrix view = Matrix::LookAt(eye, target, up);
    
    // Projection transformation
    float fov = std::numbers::pi_v<float> / 4.0f;
    Matrix projection = Matrix::PerspectiveRH(fov, 16.0f/9.0f, 0.1f, 1000.0f);
    
    // Combined MVP
    Matrix mvp = projection * view * model;
    
    // Transform a model-space vertex
    Vector vertex = ToVector(Float4(0.0f, 1.0f, 0.0f, 1.0f));
    Vector clipSpace = mvp * vertex;
    
    // Perspective divide to NDC
    Float3 ndc(
        clipSpace.x() / clipSpace.w(),
        clipSpace.y() / clipSpace.w(),
        clipSpace.z() / clipSpace.w()
    );
    
    // Should be within NDC cube
    EXPECT_GE(ndc.x, -1.0f);
    EXPECT_LE(ndc.x, 1.0f);
    EXPECT_GE(ndc.y, -1.0f);
    EXPECT_LE(ndc.y, 1.0f);
    EXPECT_GE(ndc.z, -1.0f);
    EXPECT_LE(ndc.z, 1.0f);
}

// Normal Matrix Computation
TEST_F(MathIntegrationTest, NormalMatrix_InverseTranspose)
{
    // Non-uniform scale that would affect normals
    Matrix model = Matrix::Scale(2.0f, 1.0f, 1.0f);
    
    // Normal matrix = transpose(inverse(model))
    Matrix normalMatrix = model.Inverse().Transpose();
    
    // Normal pointing up
    Vector normal = ToVector(Float4(0.0f, 1.0f, 0.0f, 0.0f));
    Vector transformedNormal = normalMatrix * normal;
    
    // Normalize
    transformedNormal = transformedNormal.Normalize<3>();
    
    // Normal should still point generally upward
    EXPECT_GT(transformedNormal.y(), 0.5f);
}

TEST_F(MathIntegrationTest, NormalMatrix_UniformScale)
{
    // Uniform scale can use regular model matrix for normals
    Matrix model = Matrix::Scale(2.0f);
    Matrix normalMatrix = model.Inverse().Transpose();
    
    Vector normal = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Vector transformedNormal1 = model * normal;
    Vector transformedNormal2 = normalMatrix * normal;
    
    // With uniform scale, both should point same direction
    transformedNormal1 = transformedNormal1.Normalize<3>();
    transformedNormal2 = transformedNormal2.Normalize<3>();
    
    EXPECT_NEAR(transformedNormal1.x(), transformedNormal2.x(), EPSILON);
    EXPECT_NEAR(transformedNormal1.y(), transformedNormal2.y(), EPSILON);
    EXPECT_NEAR(transformedNormal1.z(), transformedNormal2.z(), EPSILON);
}

// World to Screen Space Conversion
TEST_F(MathIntegrationTest, WorldToScreen_FullPipeline)
{
    // Setup
    int screenWidth = 1920;
    int screenHeight = 1080;
    
    // Projection
    float fov = std::numbers::pi_v<float> / 3.0f;
    float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
    Matrix projection = Matrix::PerspectiveRH(fov, aspect, 0.1f, 1000.0f);
    
    // View
    Vector eye = ToVector(Float3(0.0f, 5.0f, 10.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Matrix view = Matrix::LookAt(eye, target, up);
    
    Matrix vp = projection * view;
    
    // World space point
    Vector worldPoint = ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f));
    
    // To clip space
    Vector clipSpace = vp * worldPoint;
    
    // To NDC
    Float3 ndc(
        clipSpace.x() / clipSpace.w(),
        clipSpace.y() / clipSpace.w(),
        clipSpace.z() / clipSpace.w()
    );
    
    // To screen space (viewport transform)
    float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
    float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;  // Flip Y for screen coords
    
    // Center of screen should be roughly at origin
    EXPECT_GT(screenX, screenWidth * 0.3f);
    EXPECT_LT(screenX, screenWidth * 0.7f);
    EXPECT_GT(screenY, screenHeight * 0.3f);
    EXPECT_LT(screenY, screenHeight * 0.7f);
}

// Vector and Matrix Interaction
TEST_F(MathIntegrationTest, VectorMatrixInteraction_TransformNormal)
{
    Matrix rotation = Matrix::RotationY(std::numbers::pi_v<float> / 2.0f);
    Vector normal = ToVector(Float3(1.0f, 0.0f, 0.0f));
    
    // TransformNormal should ignore translation
    Vector transformed = TransformNormal(normal, rotation);
    
    // Should rotate normal 90 degrees around Y
    EXPECT_NEAR(transformed.x(), 0.0f, EPSILON);
    EXPECT_NEAR(transformed.z(), -1.0f, EPSILON);
}

TEST_F(MathIntegrationTest, VectorMatrixInteraction_Transform3D)
{
    Matrix transform = Matrix::Translation(5.0f, 10.0f, 15.0f);
    Vector point = ToVector(Float3(1.0f, 2.0f, 3.0f));
    
    Vector transformed = Transform<3>(point, transform);
    
    EXPECT_NEAR(transformed.x(), 6.0f, EPSILON);
    EXPECT_NEAR(transformed.y(), 12.0f, EPSILON);
    EXPECT_NEAR(transformed.z(), 18.0f, EPSILON);
}

// Lighting Calculations
TEST_F(MathIntegrationTest, LightingCalculation_DiffuseReflection)
{
    // Surface normal
    Vector normal = ToVector(Float3(0.0f, 1.0f, 0.0f));
    normal = normal.Normalize<3>();
    
    // Light direction (pointing toward surface)
    Vector lightDir = ToVector(Float3(0.0f, -1.0f, 0.0f));
    lightDir = lightDir.Normalize<3>();
    
    // Diffuse term: max(dot(N, -L), 0)
    float diffuse = std::max(0.0f, normal.Dot<3>(-lightDir));
    
    EXPECT_NEAR(diffuse, 1.0f, EPSILON);
}

TEST_F(MathIntegrationTest, LightingCalculation_SpecularReflection)
{
    // Surface normal
    Vector normal = ToVector(Float3(0.0f, 1.0f, 0.0f));
    normal = normal.Normalize<3>();
    
    // Light direction
    Vector lightDir = ToVector(Float3(0.0f, -1.0f, 0.0f));
    lightDir = lightDir.Normalize<3>();
    
    // View direction
    Vector viewDir = ToVector(Float3(0.0f, 1.0f, 0.0f));
    viewDir = viewDir.Normalize<3>();
    
    // Reflection
    Vector reflection = lightDir.Reflect<3>(normal);
    
    // Specular term
    float specular = std::max(0.0f, reflection.Dot<3>(viewDir));
    
    EXPECT_GT(specular, 0.9f);
}

// Quaternion-like Rotation (using Vector)
TEST_F(MathIntegrationTest, QuaternionRotation_IdentityQuaternion)
{
    // Identity quaternion (0, 0, 0, 1)
    Vector quat = ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f));
    Matrix rotation = Matrix::RotationQuaternion(quat);
    
    // Should be identity matrix
    EXPECT_TRUE(rotation.IsNearlyEqual(Matrix::Identity(), EPSILON));
}

// Accumulated Floating Point Error Test
TEST_F(MathIntegrationTest, AccumulatedError_RepeatedTransformations)
{
    Matrix rotation = Matrix::RotationZ(0.01f);  // Small rotation
    Vector point = ToVector(Float3(1.0f, 0.0f, 0.0f));
    
    // Apply rotation 628 times (approximately 2π radians)
    Vector result = point;
    for (int i = 0; i < 628; ++i)
    {
        result = rotation * result;
    }
    
    // Should be back near original position
    // Allow larger epsilon due to accumulated error
    EXPECT_NEAR(result.x(), point.x(), 0.01f);
    EXPECT_NEAR(result.y(), point.y(), 0.01f);
    EXPECT_NEAR(result.z(), point.z(), 0.01f);
}

TEST_F(MathIntegrationTest, AccumulatedError_LengthPreservation)
{
    Matrix rotation = Matrix::RotationZ(0.1f);
    Vector point = ToVector(Float3(3.0f, 4.0f, 0.0f));
    float originalLength = point.Length<3>();
    
    // Apply many rotations
    Vector result = point;
    for (int i = 0; i < 100; ++i)
    {
        result = rotation * result;
    }
    
    float finalLength = result.Length<3>();
    
    // Length should be preserved
    EXPECT_NEAR(finalLength, originalLength, 0.001f);
}

// Cross Product and Normal Computation
TEST_F(MathIntegrationTest, TriangleNormal_CrossProduct)
{
    // Triangle vertices
    Vector v0 = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector v1 = ToVector(Float3(1.0f, 0.0f, 0.0f));
    Vector v2 = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    // Edge vectors
    Vector edge1 = v1 - v0;
    Vector edge2 = v2 - v0;
    
    // Normal via cross product
    Vector normal = edge1.Cross(edge2);
    normal = normal.Normalize<3>();
    
    // Triangle in XY plane should have normal pointing along +Z
    EXPECT_NEAR(normal.x(), 0.0f, EPSILON);
    EXPECT_NEAR(normal.y(), 0.0f, EPSILON);
    EXPECT_NEAR(normal.z(), 1.0f, EPSILON);
}

// Interpolation Test
TEST_F(MathIntegrationTest, VectorInterpolation_SmoothPath)
{
    Vector start = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector end = ToVector(Float3(10.0f, 10.0f, 10.0f));
    
    Vector mid1 = start.Lerp(end, 0.25f);
    Vector mid2 = start.Lerp(end, 0.5f);
    Vector mid3 = start.Lerp(end, 0.75f);
    
    // Check monotonic progression
    EXPECT_LT(mid1.x(), mid2.x());
    EXPECT_LT(mid2.x(), mid3.x());
    EXPECT_LT(mid3.x(), end.x());
    
    // Check equal spacing
    float dist1 = (mid2 - mid1).Length<3>();
    float dist2 = (mid3 - mid2).Length<3>();
    EXPECT_NEAR(dist1, dist2, EPSILON);
}

} // namespace math
