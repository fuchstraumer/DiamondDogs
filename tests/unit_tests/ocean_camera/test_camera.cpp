#include "ArcballCamera.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace
{
    using namespace math;

    constexpr CameraViewport k_Viewport{ .Width = 1920.0f, .Height = 1080.0f };

    ArcballCamera MakeDefaultCamera() noexcept
    {
        ArcballCamera camera;
        camera.ReferencePlaneY = 0.0f;
        camera.SetState(ArcballCamera::State
        {
            .Target = Float3{ 10.0f, 0.0f, -5.0f },
            .Yaw = 0.6f,
            .Pitch = 0.35f,
            .Distance = 120.0f,
        });
        return camera;
    }

    Float3 CrossProduct(const Float3& lhs, const Float3& rhs) noexcept
    {
        return Float3
        {
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }
}

TEST(OceanCamera, T1_BasisMatchesEulerFormula)
{
    ArcballCamera camera = MakeDefaultCamera();
    const std::array<float, 5> yaws{ -3.0f, -1.0f, 0.0f, 0.6f, 2.5f };
    const std::array<float, 5> pitches{ -1.4f, -0.3f, 0.0f, 0.35f, 1.2f };

    for (int32_t i = 0; i < 5; ++i)
    {
        const float yaw = yaws[static_cast<size_t>(i)];

        for (int32_t j = 0; j < 5; ++j)
        {
            const float pitch = pitches[static_cast<size_t>(j)];
            ArcballCamera::State state = camera.Current;
            state.Yaw = yaw;
            state.Pitch = pitch;
            camera.SetState(state);

            const Float3 offsetDirection
            {
                std::sin(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::cos(yaw) * std::cos(pitch),
            };
            const Float3 expectedOffset = offsetDirection * state.Distance;
            const Float3 expectedPosition = state.Target + expectedOffset;
            const Float3 actualPosition = camera.Position();
            const ArcballCamera::Basis basis = camera.ComputeBasis();

            EXPECT_NEAR(actualPosition.x, expectedPosition.x, 1e-3f);
            EXPECT_NEAR(actualPosition.y, expectedPosition.y, 1e-3f);
            EXPECT_NEAR(actualPosition.z, expectedPosition.z, 1e-3f);

            EXPECT_NEAR(basis.Back.x, offsetDirection.x, 1e-3f);
            EXPECT_NEAR(basis.Back.y, offsetDirection.y, 1e-3f);
            EXPECT_NEAR(basis.Back.z, offsetDirection.z, 1e-3f);

            EXPECT_NEAR(basis.Right.y, 0.0f, 1e-3f);
            EXPECT_NEAR(basis.Right.Length(), 1.0f, 1e-3f);
            EXPECT_NEAR(basis.Up.Length(), 1.0f, 1e-3f);
            EXPECT_NEAR(basis.Back.Length(), 1.0f, 1e-3f);

            EXPECT_NEAR(basis.Right.Dot(basis.Up), 0.0f, 1e-3f);
            EXPECT_NEAR(basis.Right.Dot(basis.Back), 0.0f, 1e-3f);
            EXPECT_NEAR(basis.Up.Dot(basis.Back), 0.0f, 1e-3f);

            const Float3 rightCrossUp = CrossProduct(basis.Right, basis.Up);

            EXPECT_NEAR(rightCrossUp.x, basis.Back.x, 1e-3f);
            EXPECT_NEAR(rightCrossUp.y, basis.Back.y, 1e-3f);
            EXPECT_NEAR(rightCrossUp.z, basis.Back.z, 1e-3f);
        }
    }
}

TEST(OceanCamera, T2_ViewMapsTargetToMinusZ)
{
    ArcballCamera camera = MakeDefaultCamera();
    const std::array<float, 5> yaws{ -3.0f, -1.0f, 0.0f, 0.6f, 2.5f };
    const std::array<float, 5> pitches{ -1.4f, -0.3f, 0.0f, 0.35f, 1.2f };

    for (int32_t i = 0; i < 5; ++i)
    {
        const float yaw = yaws[static_cast<size_t>(i)];

        for (int32_t j = 0; j < 5; ++j)
        {
            const float pitch = pitches[static_cast<size_t>(j)];
            ArcballCamera::State state = camera.Current;
            state.Yaw = yaw;
            state.Pitch = pitch;
            camera.SetState(state);

            const Float4x4 view = camera.View();
            const Float4 targetHomogeneous{ state.Target.x, state.Target.y, state.Target.z, 1.0f };
            const Float4 result
            {
                view[0][0] * targetHomogeneous.x + view[0][1] * targetHomogeneous.y + view[0][2] * targetHomogeneous.z + view[0][3] * targetHomogeneous.w,
                view[1][0] * targetHomogeneous.x + view[1][1] * targetHomogeneous.y + view[1][2] * targetHomogeneous.z + view[1][3] * targetHomogeneous.w,
                view[2][0] * targetHomogeneous.x + view[2][1] * targetHomogeneous.y + view[2][2] * targetHomogeneous.z + view[2][3] * targetHomogeneous.w,
                view[3][0] * targetHomogeneous.x + view[3][1] * targetHomogeneous.y + view[3][2] * targetHomogeneous.z + view[3][3] * targetHomogeneous.w,
            };

            EXPECT_NEAR(result.x, 0.0f, 1e-3f);
            EXPECT_NEAR(result.y, 0.0f, 1e-3f);
            EXPECT_NEAR(result.z, -state.Distance, 1e-3f);
            EXPECT_NEAR(result.w, 1.0f, 1e-3f);
        }
    }
}

TEST(OceanCamera, T3_ReseatKeepsView)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballCamera::State state = camera.Current;
    state.Pitch = 0.35f;
    state.Target = Float3{ 10.0f, 40.0f, -5.0f };
    camera.SetState(state);

    const Float4x4 beforeView = camera.View();
    const Float3 beforePosition = camera.Position();
    const float beforeDistance = camera.Current.Distance;

    camera.Reseat();

    const Float4x4 afterView = camera.View();
    const Float3 afterPosition = camera.Position();

    for (size_t row = 0; row < 4; ++row)
    {
        for (size_t col = 0; col < 4; ++col)
        {
            EXPECT_NEAR(afterView[row][col], beforeView[row][col], 1e-3f);
        }
    }

    EXPECT_NEAR(afterPosition.x, beforePosition.x, 1e-3f);
    EXPECT_NEAR(afterPosition.y, beforePosition.y, 1e-3f);
    EXPECT_NEAR(afterPosition.z, beforePosition.z, 1e-3f);
    EXPECT_NEAR(camera.Current.Target.y, 0.0f, 1e-3f);
    EXPECT_NE(camera.Current.Distance, beforeDistance);
}

TEST(OceanCamera, T4_PickPlaneRoundTrip)
{
    const ArcballCamera camera = MakeDefaultCamera();
    const std::array<Float2, 3> cursors
    {
        Float2{ 960.0f, 540.0f },
        Float2{ 200.0f, 900.0f },
        Float2{ 1700.0f, 800.0f },
    };

    for (int32_t i = 0; i < 3; ++i)
    {
        const Float2 cursor = cursors[static_cast<size_t>(i)];
        const std::optional<Float3> hit = camera.PickPlane(cursor, k_Viewport);

        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->y, 0.0f);

        const std::optional<Float2> screen = camera.ProjectToScreen(*hit, k_Viewport);

        ASSERT_TRUE(screen.has_value());
        EXPECT_NEAR(screen->x, cursor.x, 0.01f);
        EXPECT_NEAR(screen->y, cursor.y, 0.01f);
    }
}

TEST(OceanCamera, T5_PickPlaneAboveHorizonFails)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballCamera::State state = camera.Current;
    state.Pitch = -0.0f;
    camera.SetState(state);

    const Float2 topCursor{ 960.0f, 5.0f };
    const std::optional<Float3> hit = camera.PickPlane(topCursor, k_Viewport);

    EXPECT_FALSE(hit.has_value());
}

TEST(OceanCamera, T6_ScaleAboutKeepsAnchorPixel)
{
    ArcballCamera camera = MakeDefaultCamera();
    const Float2 cursor{ 1400.0f, 700.0f };
    const std::optional<Float3> hit = camera.PickPlane(cursor, k_Viewport);

    ASSERT_TRUE(hit.has_value());

    camera.ScaleAbout(*hit, 0.5f);

    const std::optional<Float2> screen = camera.ProjectToScreen(*hit, k_Viewport);

    ASSERT_TRUE(screen.has_value());
    EXPECT_NEAR(screen->x, 1400.0f, 0.05f);
    EXPECT_NEAR(screen->y, 700.0f, 0.05f);

    const float expectedDistance = 120.0f * std::exp(-0.5f);

    EXPECT_NEAR(camera.Current.Distance, expectedDistance, 1e-2f);
}

TEST(OceanCamera, T7_ScaleAboutDollyPastMinimum)
{
    ArcballCamera camera = MakeDefaultCamera();
    camera.Current.Distance = 2.5f;
    camera.Current.Target = Float3{ 10.0f, 0.0f, -5.0f };
    const Float3 anchor = camera.Current.Target;

    camera.ScaleAbout(anchor, 2.0f);
    camera.ScaleAbout(anchor, 2.0f);
    camera.ScaleAbout(anchor, 2.0f);

    EXPECT_NEAR(camera.Current.Distance, ArcballCamera::MinDistance, 1e-3f);

    const float eyeDistanceToAnchor = (camera.Position() - anchor).Length();

    EXPECT_LT(eyeDistanceToAnchor, 2.5f);
    EXPECT_GE(eyeDistanceToAnchor, 1.0f - 1e-3f);
}

TEST(OceanCamera, T8_RotateAboutKeepsAnchorFixed)
{
    ArcballCamera camera = MakeDefaultCamera();
    const Float2 cursor{ 1400.0f, 700.0f };
    const std::optional<Float3> hit = camera.PickPlane(cursor, k_Viewport);

    ASSERT_TRUE(hit.has_value());

    camera.RotateAbout(*hit, 0.7f);

    const std::optional<Float2> screen = camera.ProjectToScreen(*hit, k_Viewport);

    ASSERT_TRUE(screen.has_value());
    EXPECT_NEAR(screen->x, 1400.0f, 0.05f);
    EXPECT_NEAR(screen->y, 700.0f, 0.05f);
}

TEST(OceanCamera, T9_LookAtRoundTrip)
{
    ArcballCamera camera = MakeDefaultCamera();
    const Float3 eye{ 30.0f, 50.0f, 80.0f };
    const Float3 target{ 0.0f, 0.0f, 0.0f };

    camera.LookAt(eye, target);

    const Float3 position = camera.Position();

    EXPECT_NEAR(position.x, eye.x, 1e-2f);
    EXPECT_NEAR(position.y, eye.y, 1e-2f);
    EXPECT_NEAR(position.z, eye.z, 1e-2f);
}

TEST(OceanCamera, T10_SetStateClamps)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballCamera::State state;
    state.Pitch = 5.0f;
    state.Distance = 1.0f;
    state.Yaw = 10.0f;

    camera.SetState(state);

    EXPECT_NEAR(camera.Current.Pitch, ArcballCamera::MaxPitch, 1e-3f);
    EXPECT_NEAR(camera.Current.Distance, ArcballCamera::MinDistance, 1e-3f);
    EXPECT_GE(camera.Current.Yaw, -std::numbers::pi_v<float>);
    EXPECT_LE(camera.Current.Yaw, std::numbers::pi_v<float>);
}
