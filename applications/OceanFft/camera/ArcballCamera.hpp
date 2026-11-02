#pragma once
#ifndef DIAMOND_DOGS_OCEAN_ARCBALL_CAMERA_HPP
#define DIAMOND_DOGS_OCEAN_ARCBALL_CAMERA_HPP
#include "math/Math.hpp"
#include <numbers>
#include <optional>

/** @brief The size of the render target in pixels. Cursor positions use the same pixel space, origin top-left. */
struct CameraViewport
{
    float Width{ 1.0f };
    float Height{ 1.0f };
};

// A turntable orbit rig: yaw around world +y, pitch around the camera's right axis, a target point and a distance.
// Every operator here applies at once and keeps its invariant. ArcballController adds smoothing and inertia on top.
// World space is y-up and right-handed. Matrices are row-major arrays of rows (M[row][column]) that multiply column
// vectors, and the shaders take them as four float4 rows, so no matrix layout rule is involved.
class ArcballCamera
{
public:

    struct State
    {
        math::Float3 Target{ 0.0f, 0.0f, 0.0f };
        float Yaw{ 0.6f };      // radians around +y, 0 looks along -z
        float Pitch{ 0.35f };   // radians above the horizon
        float Distance{ 120.0f };
    };

    struct Ray
    {
        math::Float3 Origin;
        math::Float3 Direction;
    };

    /** @brief The camera axes in world space. Back points from the target to the eye. */
    struct Basis
    {
        math::Float3 Right;
        math::Float3 Up;
        math::Float3 Back;
    };

    static constexpr float MinDistance{ 2.0f };
    static constexpr float MaxDistance{ 20000.0f };
    // the quaternion basis has no singularity at the pole. This limit only stops the turntable flipping over it
    static constexpr float MaxPitch{ 0.5f * std::numbers::pi_v<float> - 0.002f };

    float FovY{ 0.9f };
    float NearPlane{ 0.25f };
    // far enough for a camera at MaxDistance looking at the whole ring grid around it
    float FarPlane{ 100000.0f };
    // the height of the plane that picking, panning and re-seating use: the mean sea level
    float ReferencePlaneY{ 0.0f };
    State Current;

    /** @brief Clamps pitch and distance into range and wraps yaw into [-pi, pi], then stores the state. */
    void SetState(const State& state) noexcept;
    /** @brief Sets the target, distance, yaw and pitch so that the camera sits at the eye and looks at the target. */
    void LookAt(const math::Float3& eye, const math::Float3& target) noexcept;

    /** @brief Turns the camera around its target. Pitch clamps at MaxPitch. */
    void Orbit(float d_yaw_radians, float d_pitch_radians) noexcept;
    /** @brief Scales the rig about a world point, so that the point stays at the same pixel. Positive moves closer.
     *  Past MinDistance, the eye keeps moving toward the anchor along its ray instead of stopping. */
    void ScaleAbout(const math::Float3& anchor, float log_scale) noexcept;
    /** @brief Turns the rig around the vertical axis through a world point. */
    void RotateAbout(const math::Float3& anchor, float d_yaw_radians) noexcept;
    void TranslateTarget(const math::Float3& delta) noexcept;
    /** @brief Moves the target along the view direction onto the reference plane, and keeps the eye where it is.
     *  The view matrix does not change. Does nothing when the view direction does not reach the plane. */
    void Reseat() noexcept;
    /** @brief Moves the target in the view plane by a pixel delta, at the scale of the target's distance. */
    void PanViewPlane(float dx_pixels, float dy_pixels, const CameraViewport& viewport) noexcept;

    [[nodiscard]] math::Quaternion Orientation() const noexcept;
    [[nodiscard]] Basis ComputeBasis() const noexcept;
    [[nodiscard]] math::Float3 Position() const noexcept;
    /** @brief Returns the world ray from the eye through the center of a pixel. */
    [[nodiscard]] Ray CursorRay(const math::Float2& cursor_pixels, const CameraViewport& viewport) const noexcept;
    /** @brief Returns where the cursor ray meets the reference plane, or nothing when the hit is behind the eye or
     *  farther than the pick range. The pick range grows with distance, so that picks near the horizon fail. */
    [[nodiscard]] std::optional<math::Float3> PickPlane(const math::Float2& cursor_pixels, const CameraViewport& viewport) const noexcept;
    /** @brief Returns the pixel position of a world point, or nothing when the point is behind the eye. */
    [[nodiscard]] std::optional<math::Float2> ProjectToScreen(const math::Float3& world, const CameraViewport& viewport) const noexcept;

    [[nodiscard]] math::Float4x4 View() const noexcept;
    [[nodiscard]] math::Float4x4 ViewRotationOnly() const noexcept;
    /** @brief Vulkan clip space Reverse-Z [1, 0]: y down, depth 1 at the near plane and 0 at the far plane. */
    [[nodiscard]] math::Float4x4 Projection(float aspect) const noexcept;
    [[nodiscard]] math::Float4x4 ViewProjection(float aspect) const noexcept;
    [[nodiscard]] math::Float4x4 ViewRotationProjection(float aspect) const noexcept;

private:

    [[nodiscard]] float pickRange() const noexcept;
};

#endif //!DIAMOND_DOGS_OCEAN_ARCBALL_CAMERA_HPP
