#include <gtest/gtest.h>
#include <Math.hpp>
#include <numbers>

namespace math
{

class MatrixProjectionTest : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-4f;
};

// Perspective Projection Tests
TEST_F(MatrixProjectionTest, PerspectiveBasicConstruction)
{
    float fov = std::numbers::pi_v<float> / 4.0f;  // 45 degrees
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    Matrix m = Matrix::Perspective(fov, aspect, nearPlane, farPlane);
    
    // Matrix should be valid (not all zeros)
    EXPECT_NE(m(0, 0), 0.0f);
    EXPECT_NE(m(1, 1), 0.0f);
}

TEST_F(MatrixProjectionTest, PerspectiveNearPlaneMapping)
{
    float fov = std::numbers::pi_v<float> / 4.0f;
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 100.0f;
    
    Matrix m = Matrix::PerspectiveRH(fov, aspect, nearPlane, farPlane);
    
    // Point at near plane center should map to near depth
    Vector point = ToVector(Float4(0.0f, 0.0f, -nearPlane, 1.0f));
    Vector projected = m * point;
    
    // After perspective divide, z should be at near depth
    float ndcZ = projected.z() / projected.w();
    EXPECT_NEAR(ndcZ, -1.0f, EPSILON);  // Near plane at -1 in NDC
}

TEST_F(MatrixProjectionTest, PerspectiveFarPlaneMapping)
{
    float fov = std::numbers::pi_v<float> / 4.0f;
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 100.0f;
    
    Matrix m = Matrix::PerspectiveRH(fov, aspect, nearPlane, farPlane);
    
    // Point at far plane center
    Vector point = ToVector(Float4(0.0f, 0.0f, -farPlane, 1.0f));
    Vector projected = m * point;
    
    float ndcZ = projected.z() / projected.w();
    EXPECT_NEAR(ndcZ, 1.0f, EPSILON);  // Far plane at +1 in NDC
}

TEST_F(MatrixProjectionTest, PerspectiveDepthRange)
{
    // Verify depth range is [-1, 1] as per instructions
    float fov = std::numbers::pi_v<float> / 4.0f;
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 100.0f;
    
    Matrix m = Matrix::PerspectiveRH(fov, aspect, nearPlane, farPlane);
    
    // Test point between near and far
    Vector midPoint = ToVector(Float4(0.0f, 0.0f, -50.0f, 1.0f));
    Vector projected = m * midPoint;
    float ndcZ = projected.z() / projected.w();
    
    EXPECT_GE(ndcZ, -1.0f);
    EXPECT_LE(ndcZ, 1.0f);
}

TEST_F(MatrixProjectionTest, PerspectiveAspectRatio)
{
    float fov = std::numbers::pi_v<float> / 4.0f;
    float aspect = 2.0f;  // Wide screen
    float nearPlane = 1.0f;
    float farPlane = 100.0f;
    
    Matrix m = Matrix::Perspective(fov, aspect, nearPlane, farPlane);
    
    // Aspect ratio should affect horizontal vs vertical FOV
    // m(0,0) and m(1,1) should reflect aspect ratio difference
    float horizontalScale = m(0, 0);
    float verticalScale = m(1, 1);
    
    // For aspect > 1, horizontal scale should be smaller
    EXPECT_LT(std::abs(horizontalScale), std::abs(verticalScale));
}

TEST_F(MatrixProjectionTest, PerspectiveLH_vs_RH)
{
    float fov = std::numbers::pi_v<float> / 4.0f;
    float aspect = 1.0f;
    float nearPlane = 1.0f;
    float farPlane = 100.0f;
    
    Matrix lh = Matrix::PerspectiveLH(fov, aspect, nearPlane, farPlane);
    Matrix rh = Matrix::PerspectiveRH(fov, aspect, nearPlane, farPlane);
    
    // Left-handed and right-handed should differ
    EXPECT_NE(lh(2, 2), rh(2, 2));
}

// Orthographic Projection Tests
TEST_F(MatrixProjectionTest, OrthographicBasicConstruction)
{
    Matrix m = Matrix::Orthographic(10.0f, 10.0f, 1.0f, 100.0f);
    
    // Matrix should be valid
    EXPECT_NE(m(0, 0), 0.0f);
    EXPECT_NE(m(1, 1), 0.0f);
}

TEST_F(MatrixProjectionTest, OrthographicNoDistortion)
{
    Matrix m = Matrix::OrthographicRH(10.0f, 10.0f, 1.0f, 100.0f);
    
    // Parallel lines should remain parallel (no perspective divide needed)
    Vector p1 = ToVector(Float4(1.0f, 0.0f, -50.0f, 1.0f));
    Vector p2 = ToVector(Float4(1.0f, 1.0f, -50.0f, 1.0f));
    
    Vector proj1 = m * p1;
    Vector proj2 = m * p2;
    
    // X coordinates should have same relationship
    float dx1 = proj1.x() / proj1.w();
    float dx2 = proj2.x() / proj2.w();
    
    EXPECT_NEAR(dx1, dx2, EPSILON);
}

TEST_F(MatrixProjectionTest, OrthographicDepthMapping)
{
    Matrix m = Matrix::OrthographicRH(10.0f, 10.0f, 1.0f, 100.0f);
    
    // Near plane
    Vector nearPoint = ToVector(Float4(0.0f, 0.0f, -1.0f, 1.0f));
    Vector projectedNear = m * nearPoint;
    float ndcZNear = projectedNear.z() / projectedNear.w();
    EXPECT_NEAR(ndcZNear, -1.0f, EPSILON);
    
    // Far plane
    Vector farPoint = ToVector(Float4(0.0f, 0.0f, -100.0f, 1.0f));
    Vector projectedFar = m * farPoint;
    float ndcZFar = projectedFar.z() / projectedFar.w();
    EXPECT_NEAR(ndcZFar, 1.0f, EPSILON);
}

TEST_F(MatrixProjectionTest, OrthographicDepthLinear)
{
    Matrix m = Matrix::OrthographicRH(10.0f, 10.0f, 1.0f, 100.0f);
    
    // Orthographic projection should have linear depth
    Vector point1 = ToVector(Float4(0.0f, 0.0f, -25.0f, 1.0f));
    Vector point2 = ToVector(Float4(0.0f, 0.0f, -50.0f, 1.0f));
    Vector point3 = ToVector(Float4(0.0f, 0.0f, -75.0f, 1.0f));
    
    Vector proj1 = m * point1;
    Vector proj2 = m * point2;
    Vector proj3 = m * point3;
    
    float z1 = proj1.z() / proj1.w();
    float z2 = proj2.z() / proj2.w();
    float z3 = proj3.z() / proj3.w();
    
    // Check linearity
    float diff1 = z2 - z1;
    float diff2 = z3 - z2;
    
    EXPECT_NEAR(diff1, diff2, EPSILON);
}

TEST_F(MatrixProjectionTest, OrthographicPreservesParallelism)
{
    Matrix m = Matrix::Orthographic(10.0f, 10.0f, 1.0f, 100.0f);
    
    // Two parallel vectors at different depths
    Vector v1 = ToVector(Float4(1.0f, 0.0f, -10.0f, 1.0f));
    Vector v2 = ToVector(Float4(2.0f, 0.0f, -10.0f, 1.0f));
    Vector v3 = ToVector(Float4(1.0f, 0.0f, -50.0f, 1.0f));
    Vector v4 = ToVector(Float4(2.0f, 0.0f, -50.0f, 1.0f));
    
    Vector p1 = m * v1;
    Vector p2 = m * v2;
    Vector p3 = m * v3;
    Vector p4 = m * v4;
    
    float dx_near = (p2.x() / p2.w()) - (p1.x() / p1.w());
    float dx_far = (p4.x() / p4.w()) - (p3.x() / p3.w());
    
    // Distance should be same regardless of depth
    EXPECT_NEAR(dx_near, dx_far, EPSILON);
}

// View Matrix Tests
class MatrixViewTest : public ::testing::Test
{
protected:
    static constexpr float EPSILON = 1e-4f;
};

TEST_F(MatrixViewTest, LookAt_BasicConstruction)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 5.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookAt(eye, target, up);
    
    // Matrix should be valid
    EXPECT_NE(view(0, 0), 0.0f);
}

TEST_F(MatrixViewTest, LookAt_TransformsEyeToOrigin)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 10.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookAt(eye, target, up);
    Vector eyeTransformed = view * ToVector(Float4(eye.x(), eye.y(), eye.z(), 1.0f));
    
    // Eye position should be at origin in view space
    EXPECT_NEAR(eyeTransformed.x(), 0.0f, EPSILON);
    EXPECT_NEAR(eyeTransformed.y(), 0.0f, EPSILON);
    EXPECT_NEAR(eyeTransformed.z(), 0.0f, EPSILON);
}

TEST_F(MatrixViewTest, LookAt_ViewDirection)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 5.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookAt(eye, target, up);
    Vector targetPos = ToVector(Float4(target.x(), target.y(), target.z(), 1.0f));
    Vector targetTransformed = view * targetPos;
    
    // Target should be along negative Z in view space (right-handed)
    EXPECT_LT(targetTransformed.z(), 0.0f);
}

TEST_F(MatrixViewTest, LookAt_UpVector)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 5.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookAt(eye, target, up);
    
    // Point above eye should have positive Y in view space
    Vector pointAbove = ToVector(Float4(0.0f, 1.0f, 5.0f, 1.0f));
    Vector transformed = view * pointAbove;
    
    EXPECT_GT(transformed.y(), 0.0f);
}

TEST_F(MatrixViewTest, LookTo_Direction)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 5.0f));
    Vector direction = ToVector(Float3(0.0f, 0.0f, -1.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookTo(eye, direction, up);
    
    // Should produce similar result to LookAt
    EXPECT_NE(view(0, 0), 0.0f);
}

TEST_F(MatrixViewTest, LookAt_RightVector)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 5.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookAt(eye, target, up);
    
    // Point to the right of eye should have positive X in view space
    Vector pointRight = ToVector(Float4(1.0f, 0.0f, 5.0f, 1.0f));
    Vector transformed = view * pointRight;
    
    EXPECT_GT(transformed.x(), 0.0f);
}

TEST_F(MatrixViewTest, LookAt_InvertedView)
{
    Vector eye = ToVector(Float3(0.0f, 0.0f, 5.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    
    Matrix view = Matrix::LookAt(eye, target, up);
    Matrix inverse = view.Inverse();
    
    // Inverse should transform origin back to eye position
    Vector origin = ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f));
    Vector backToWorld = inverse * origin;
    
    EXPECT_NEAR(backToWorld.x(), eye.x(), EPSILON);
    EXPECT_NEAR(backToWorld.y(), eye.y(), EPSILON);
    EXPECT_NEAR(backToWorld.z(), eye.z(), EPSILON);
}

// Combined View-Projection Tests
TEST_F(MatrixViewTest, ViewProjectionPipeline)
{
    // Create view matrix
    Vector eye = ToVector(Float3(0.0f, 0.0f, 10.0f));
    Vector target = ToVector(Float3(0.0f, 0.0f, 0.0f));
    Vector up = ToVector(Float3(0.0f, 1.0f, 0.0f));
    Matrix view = Matrix::LookAt(eye, target, up);
    
    // Create projection matrix
    float fov = std::numbers::pi_v<float> / 4.0f;
    Matrix projection = Matrix::PerspectiveRH(fov, 1.0f, 1.0f, 100.0f);
    
    // Combined view-projection
    Matrix vp = projection * view;
    
    // Transform a point
    Vector worldPoint = ToVector(Float4(0.0f, 0.0f, 0.0f, 1.0f));
    Vector clipSpace = vp * worldPoint;
    
    // Target is at origin, should be visible
    float ndcX = clipSpace.x() / clipSpace.w();
    float ndcY = clipSpace.y() / clipSpace.w();
    float ndcZ = clipSpace.z() / clipSpace.w();
    
    // Should be within NDC cube [-1, 1]
    EXPECT_GE(ndcX, -1.0f);
    EXPECT_LE(ndcX, 1.0f);
    EXPECT_GE(ndcY, -1.0f);
    EXPECT_LE(ndcY, 1.0f);
    EXPECT_GE(ndcZ, -1.0f);
    EXPECT_LE(ndcZ, 1.0f);
}

}
