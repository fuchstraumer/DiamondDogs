#pragma once
#ifndef DIAMOND_DOGS_OCEAN_ARCBALL_CAMERA_HPP
#define DIAMOND_DOGS_OCEAN_ARCBALL_CAMERA_HPP
#include "math/Math.hpp"

// Orbits a target point: drag to rotate around it, pan to move the target in the view plane, scroll to change the
// distance. World space is y-up and right-handed. Matrices are row-major arrays of rows (M[row][column]) that
// multiply column vectors, and the shaders take them as four float4 rows, so no matrix layout rule is involved.
class ArcballCamera
{
public:
    using Vector3 = math::Float3;
    using Matrix4 = math::Float4x4;

    struct State
    {
        Vector3 Target{ 0.0f, 0.0f, 0.0f };
        float Yaw{ 0.6f };      // radians around +y, 0 looks along -z
        float Pitch{ 0.35f };   // radians above the horizon
        float Distance{ 120.0f };
    };

    float FovY{ 0.9f };
    float NearPlane{ 0.25f };
    // far enough for a camera at MaxDistance looking at the whole ring grid around it
    float FarPlane{ 100000.0f };
    static constexpr float MaxDistance{ 20000.0f };
    State Current;

    /** @brief Mouse deltas in pixels: rotate turns by the drag, pan moves the target along the view plane. */
    void Rotate(float dx_pixels, float dy_pixels) noexcept;
    void Pan(float dx_pixels, float dy_pixels, float viewport_height_pixels) noexcept;
    /** @brief Positive steps move closer. */
    void Zoom(float wheel_steps) noexcept;

    [[nodiscard]] Vector3 Position() const noexcept;
    [[nodiscard]] Matrix4 View() const noexcept;
    [[nodiscard]] Matrix4 ViewRotationOnly() const noexcept;
    /** @brief Vulkan clip space Reverse-Z [1, 0]: y down, depth 1 at the near plane and 0 at the far plane. */
    [[nodiscard]] Matrix4 Projection(float aspect) const noexcept;
    [[nodiscard]] Matrix4 ViewProjection(float aspect) const noexcept;
    [[nodiscard]] Matrix4 ViewRotationProjection(float aspect) const noexcept;
};

#endif //!DIAMOND_DOGS_OCEAN_ARCBALL_CAMERA_HPP
