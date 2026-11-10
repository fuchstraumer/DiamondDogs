#include "ArcballCamera.hpp"
#include "ArcballController.hpp"
#include <gtest/gtest.h>
#include <cmath>
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
}

TEST(OceanController, C1_PanScreenKeepsGrabbedPointUnderCursor)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    controller.Settings.Cinematic = false;

    const Float2 cursorA{ 900.0f, 600.0f };
    const Float2 cursorB{ 1100.0f, 650.0f };
    const std::optional<Float3> hit = camera.PickPlane(cursorA, k_Viewport);

    ASSERT_TRUE(hit.has_value());

    controller.PanScreen(camera, cursorA, cursorB, k_Viewport);

    const std::optional<Float2> projected = camera.ProjectToScreen(*hit, k_Viewport);

    ASSERT_TRUE(projected.has_value());
    EXPECT_NEAR(projected->x, cursorB.x, 0.05f);
    EXPECT_NEAR(projected->y, cursorB.y, 0.05f);
    EXPECT_NEAR(camera.Current.Target.y, 0.0f, 1e-3f);
}

TEST(OceanController, C2_SmoothedZoomKeepsAnchorEveryFrame)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    controller.Settings.SmoothSteps = true;
    controller.Settings.Cinematic = false;

    const Float2 cursor{ 1400.0f, 700.0f };
    const std::optional<Float3> hit = camera.PickPlane(cursor, k_Viewport);

    ASSERT_TRUE(hit.has_value());

    const float startDistance = camera.Current.Distance;

    controller.ZoomAt(camera, 1.0f, cursor, k_Viewport, CameraMotionKind::Step);

    for (int32_t frame = 0; frame < 240; ++frame)
    {
        controller.Update(camera, 1.0f / 240.0f);
        const std::optional<Float2> projected = camera.ProjectToScreen(*hit, k_Viewport);

        ASSERT_TRUE(projected.has_value());
        EXPECT_NEAR(projected->x, cursor.x, 0.1f);
        EXPECT_NEAR(projected->y, cursor.y, 0.1f);
    }

    const float expectedDistance = startDistance * std::exp(-1.0f);

    EXPECT_NEAR(camera.Current.Distance, expectedDistance, expectedDistance * 0.01f);

    for (int32_t frame = 240; frame < 600; ++frame)
    {
        controller.Update(camera, 1.0f / 240.0f);
    }

    EXPECT_FALSE(controller.IsMoving());
}

TEST(OceanController, C3_SmoothedZoomSumsToImpulse)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    const float startDistance = camera.Current.Distance;
    const Float2 cursor{ 960.0f, 540.0f };

    controller.ZoomAt(camera, 0.3f, cursor, k_Viewport, CameraMotionKind::Step);
    controller.ZoomAt(camera, 0.3f, cursor, k_Viewport, CameraMotionKind::Step);
    controller.ZoomAt(camera, 0.3f, cursor, k_Viewport, CameraMotionKind::Step);

    for (int32_t frame = 0; frame < 240; ++frame)
    {
        controller.Update(camera, 1.0f / 120.0f);
    }

    const float expectedDistance = startDistance * std::exp(-0.9f);

    EXPECT_NEAR(camera.Current.Distance, expectedDistance, expectedDistance * 0.01f);
}

TEST(OceanController, C4_FrameRateIndependence)
{
    ArcballCamera camera1 = MakeDefaultCamera();
    ArcballCamera camera2 = MakeDefaultCamera();
    ArcballController controller1;
    ArcballController controller2;
    const Float2 cursor{ 960.0f, 540.0f };

    controller1.ZoomAt(camera1, 1.0f, cursor, k_Viewport, CameraMotionKind::Step);
    controller2.ZoomAt(camera2, 1.0f, cursor, k_Viewport, CameraMotionKind::Step);

    for (int32_t frame = 0; frame < 30; ++frame)
    {
        controller1.Update(camera1, 1.0f / 60.0f);
    }

    for (int32_t frame = 0; frame < 120; ++frame)
    {
        controller2.Update(camera2, 1.0f / 240.0f);
    }

    EXPECT_NEAR(camera1.Current.Distance, camera2.Current.Distance, camera1.Current.Distance * 0.005f);
}

TEST(OceanController, C5_SmoothingOffAppliesAtOnce)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    controller.Settings.SmoothSteps = false;
    controller.Settings.Cinematic = false;

    const float startDistance = camera.Current.Distance;
    const Float2 cursor{ 960.0f, 540.0f };

    controller.ZoomAt(camera, 1.0f, cursor, k_Viewport, CameraMotionKind::Step);

    const float expectedDistance = startDistance * std::exp(-1.0f);

    EXPECT_NEAR(camera.Current.Distance, expectedDistance, 1e-3f);
}

TEST(OceanController, C6_CinematicSpringConverges)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    controller.Settings.Cinematic = true;

    const float startYaw = camera.Current.Yaw;

    controller.Orbit(camera, 1.0f, 0.0f, CameraMotionKind::Drag);

    for (int32_t frame = 0; frame < 480; ++frame)
    {
        controller.Update(camera, 1.0f / 120.0f);
        EXPECT_LE(camera.Current.Yaw, startYaw + 1.0f + 1e-3f);
    }

    EXPECT_NEAR(camera.Current.Yaw, startYaw + 1.0f, 1e-3f);
}

TEST(OceanController, C7_InertiaDecays)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    controller.Settings.Inertia = true;

    controller.BeginManipulation(camera);

    for (int32_t frame = 0; frame < 10; ++frame)
    {
        controller.Orbit(camera, 0.01f, 0.0f, CameraMotionKind::Drag);
        controller.Update(camera, 1.0f / 120.0f);
    }

    controller.EndManipulation();
    const float yawAtEnd = camera.Current.Yaw;

    for (int32_t frame = 0; frame < 360; ++frame)
    {
        controller.Update(camera, 1.0f / 120.0f);
    }

    EXPECT_GT(camera.Current.Yaw, yawAtEnd);
    EXPECT_FALSE(controller.IsMoving());
}

TEST(OceanController, C8_InertiaOffStopsAtOnce)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    controller.Settings.Inertia = false;

    controller.BeginManipulation(camera);

    for (int32_t frame = 0; frame < 10; ++frame)
    {
        controller.Orbit(camera, 0.01f, 0.0f, CameraMotionKind::Drag);
        controller.Update(camera, 1.0f / 120.0f);
    }

    controller.EndManipulation();
    const float yawAtEnd = camera.Current.Yaw;

    for (int32_t frame = 0; frame < 360; ++frame)
    {
        controller.Update(camera, 1.0f / 120.0f);
    }

    EXPECT_NEAR(camera.Current.Yaw, yawAtEnd, 1e-6f);
}

TEST(OceanController, C9_TransitionReachesGoalAndIsMonotonic)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    const ArcballCamera::State goal
    {
        .Target = Float3{ 0.0f, 0.0f, 0.0f },
        .Yaw = -1.0f,
        .Pitch = 0.2f,
        .Distance = 500.0f,
    };

    controller.TransitionTo(camera, goal, 1.0f);
    float previousDistance = camera.Current.Distance;

    for (int32_t frame = 0; frame < 240; ++frame)
    {
        controller.Update(camera, 1.0f / 120.0f);
        EXPECT_GE(camera.Current.Distance, previousDistance - 1e-3f);
        previousDistance = camera.Current.Distance;
    }

    EXPECT_NEAR(camera.Current.Target.x, goal.Target.x, 1e-3f);
    EXPECT_NEAR(camera.Current.Target.y, goal.Target.y, 1e-3f);
    EXPECT_NEAR(camera.Current.Target.z, goal.Target.z, 1e-3f);
    EXPECT_NEAR(camera.Current.Yaw, goal.Yaw, 1e-3f);
    EXPECT_NEAR(camera.Current.Pitch, goal.Pitch, 1e-3f);
    EXPECT_NEAR(camera.Current.Distance, goal.Distance, 1e-3f);
}

TEST(OceanController, C10_TransitionZeroSecondsAppliesAtOnce)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    const ArcballCamera::State goal
    {
        .Target = Float3{ 0.0f, 0.0f, 0.0f },
        .Yaw = -1.0f,
        .Pitch = 0.2f,
        .Distance = 500.0f,
    };

    controller.TransitionTo(camera, goal, 0.0f);

    EXPECT_NEAR(camera.Current.Target.x, goal.Target.x, 1e-3f);
    EXPECT_NEAR(camera.Current.Target.y, goal.Target.y, 1e-3f);
    EXPECT_NEAR(camera.Current.Target.z, goal.Target.z, 1e-3f);
    EXPECT_NEAR(camera.Current.Yaw, goal.Yaw, 1e-3f);
    EXPECT_NEAR(camera.Current.Pitch, goal.Pitch, 1e-3f);
    EXPECT_NEAR(camera.Current.Distance, goal.Distance, 1e-3f);
    EXPECT_FALSE(controller.IsMoving());
}

TEST(OceanController, C11_DragCancelsTransition)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    const ArcballCamera::State goal
    {
        .Target = Float3{ 0.0f, 0.0f, 0.0f },
        .Yaw = -1.0f,
        .Pitch = 0.2f,
        .Distance = 500.0f,
    };

    controller.TransitionTo(camera, goal, 2.0f);
    controller.Orbit(camera, 0.05f, 0.0f, CameraMotionKind::Drag);

    for (int32_t frame = 0; frame < 120; ++frame)
    {
        controller.Update(camera, 1.0f / 120.0f);
    }

    EXPECT_FALSE(controller.IsMoving());
    EXPECT_NE(camera.Current.Distance, goal.Distance);
}

TEST(OceanController, C12_Gesture2MatchesPanScreen)
{
    ArcballCamera camera1 = MakeDefaultCamera();
    ArcballCamera camera2 = MakeDefaultCamera();
    ArcballController controller1;
    ArcballController controller2;
    controller1.Settings.Cinematic = false;
    controller1.Settings.SmoothSteps = false;
    controller2.Settings.Cinematic = false;
    controller2.Settings.SmoothSteps = false;

    const Float2 fromPixels{ 900.0f, 600.0f };
    const Float2 toPixels{ 1100.0f, 650.0f };

    controller1.Gesture2(camera1, fromPixels, toPixels, 1.0f, 0.0f, 0.0f, k_Viewport);
    controller2.PanScreen(camera2, fromPixels, toPixels, k_Viewport);

    EXPECT_NEAR(camera1.Current.Target.x, camera2.Current.Target.x, 1e-3f);
    EXPECT_NEAR(camera1.Current.Target.y, camera2.Current.Target.y, 1e-3f);
    EXPECT_NEAR(camera1.Current.Target.z, camera2.Current.Target.z, 1e-3f);
}

TEST(OceanController, C13_DeltaClamp)
{
    ArcballCamera camera = MakeDefaultCamera();
    ArcballController controller;
    const float startDistance = camera.Current.Distance;
    const Float2 cursor{ 960.0f, 540.0f };

    controller.ZoomAt(camera, 1.0f, cursor, k_Viewport, CameraMotionKind::Step);
    controller.Update(camera, 5.0f);

    const float fullDistance = startDistance * std::exp(-1.0f);

    EXPECT_GT(camera.Current.Distance, fullDistance + 0.5f);

    const float appliedRatio = (startDistance - camera.Current.Distance) / (startDistance - fullDistance);

    EXPECT_LT(appliedRatio, 0.99f);
}

TEST(OceanController, C14_ZoomReseatsAFloatingPivot)
{
    ArcballCamera camera = MakeDefaultCamera();
    camera.SetState(ArcballCamera::State
    {
        .Target = Float3{ 10.0f, 40.0f, -5.0f },
        .Yaw = 0.6f,
        .Pitch = 0.35f,
        .Distance = 120.0f,
    });
    ArcballController controller;
    controller.Settings.SmoothSteps = false;
    controller.Settings.Cinematic = false;

    const Float3 eyeBefore = camera.Position();

    controller.ZoomAt(camera, 0.0f, Float2{ 960.0f, 540.0f }, k_Viewport, CameraMotionKind::Step);

    // the pivot sat 40 m above the sea. a zoom must put it back on the plane without moving the eye
    EXPECT_NEAR(camera.Current.Target.y, 0.0f, 1e-3f);
    EXPECT_NEAR(camera.Position().x, eyeBefore.x, 1e-2f);
    EXPECT_NEAR(camera.Position().y, eyeBefore.y, 1e-2f);
    EXPECT_NEAR(camera.Position().z, eyeBefore.z, 1e-2f);
}

TEST(OceanController, C15_BeginManipulationReseatsAFloatingPivot)
{
    ArcballCamera camera = MakeDefaultCamera();
    camera.SetState(ArcballCamera::State
    {
        .Target = Float3{ 10.0f, 40.0f, -5.0f },
        .Yaw = 0.6f,
        .Pitch = 0.35f,
        .Distance = 120.0f,
    });
    ArcballController controller;

    const Float3 eyeBefore = camera.Position();
    const float distanceBefore = camera.Current.Distance;

    controller.BeginManipulation(camera);

    EXPECT_NEAR(camera.Current.Target.y, 0.0f, 1e-3f);
    EXPECT_NE(camera.Current.Distance, distanceBefore);
    EXPECT_NEAR(camera.Position().x, eyeBefore.x, 1e-2f);
    EXPECT_NEAR(camera.Position().y, eyeBefore.y, 1e-2f);
    EXPECT_NEAR(camera.Position().z, eyeBefore.z, 1e-2f);
}

TEST(OceanController, C16_PanSpeedTracksTheWaterAfterAZoomIn)
{
    ArcballCamera camera = MakeDefaultCamera();
    camera.SetState(ArcballCamera::State
    {
        .Target = Float3{ 10.0f, 40.0f, -5.0f },
        .Yaw = 0.6f,
        .Pitch = 0.35f,
        .Distance = 3.0f,
    });
    ArcballController controller;
    controller.Settings.Cinematic = false;
    controller.BeginManipulation(camera);

    const Float2 cursorA{ 960.0f, 700.0f };
    const Float2 cursorB{ 1060.0f, 700.0f };
    const std::optional<Float3> hit = camera.PickPlane(cursorA, k_Viewport);

    ASSERT_TRUE(hit.has_value());

    controller.PanScreen(camera, cursorA, cursorB, k_Viewport);

    const std::optional<Float2> projected = camera.ProjectToScreen(*hit, k_Viewport);

    ASSERT_TRUE(projected.has_value());
    EXPECT_NEAR(projected->x, cursorB.x, 0.05f);
    EXPECT_NEAR(projected->y, cursorB.y, 0.05f);
}
