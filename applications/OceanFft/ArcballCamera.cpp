#include "ArcballCamera.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    using Vector3 = ArcballCamera::Vector3;
    using Matrix4 = ArcballCamera::Matrix4;

    constexpr float k_RotateRadiansPerPixel = 0.005f;
    constexpr float k_ZoomPerStep = 0.9f;
    constexpr float k_MinDistance = 2.0f;
    // just short of straight up or down, where the view's right vector is undefined
    constexpr float k_MaxPitch = 0.5f * std::numbers::pi_v<float> - 0.01f;

    Vector3 Subtract(const Vector3& a, const Vector3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
    Vector3 Scale(const Vector3& a, float s) noexcept { return { a[0] * s, a[1] * s, a[2] * s }; }
    Vector3 Add(const Vector3& a, const Vector3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
    float Dot(const Vector3& a, const Vector3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    }
    Vector3 Normalize(const Vector3& a) noexcept
    {
        const float length = std::sqrt(Dot(a, a));
        return length > 0.0f ? Scale(a, 1.0f / length) : a;
    }

    Matrix4 Multiply(const Matrix4& a, const Matrix4& b) noexcept
    {
        Matrix4 result{};
        for (size_t row = 0; row < 4; ++row)
        {
            for (size_t column = 0; column < 4; ++column)
            {
                for (size_t k = 0; k < 4; ++k)
                {
                    result[row][column] += a[row][k] * b[k][column];
                }
            }
        }
        return result;
    }
}

void ArcballCamera::Rotate(float dx_pixels, float dy_pixels) noexcept
{
    Current.Yaw -= dx_pixels * k_RotateRadiansPerPixel;
    Current.Pitch = std::clamp(Current.Pitch + dy_pixels * k_RotateRadiansPerPixel, -k_MaxPitch, k_MaxPitch);
}

void ArcballCamera::Pan(float dx_pixels, float dy_pixels, float viewport_height_pixels) noexcept
{
    // one pixel moves the target by the size of one pixel at the target's distance, so the point under the cursor follows it
    const float worldPerPixel = 2.0f * Current.Distance * std::tan(0.5f * FovY) / std::max(viewport_height_pixels, 1.0f);
    const Matrix4 view = View();
    const Vector3 right{ view[0][0], view[0][1], view[0][2] };
    const Vector3 up{ view[1][0], view[1][1], view[1][2] };
    Current.Target = Add(Current.Target, Add(Scale(right, -dx_pixels * worldPerPixel), Scale(up, dy_pixels * worldPerPixel)));
}

void ArcballCamera::Zoom(float wheel_steps) noexcept
{
    Current.Distance = std::clamp(Current.Distance * std::pow(k_ZoomPerStep, wheel_steps), k_MinDistance, MaxDistance);
}

ArcballCamera::Vector3 ArcballCamera::Position() const noexcept
{
    const float cosPitch = std::cos(Current.Pitch);
    const Vector3 offset{ std::sin(Current.Yaw) * cosPitch, std::sin(Current.Pitch), std::cos(Current.Yaw) * cosPitch };
    return Add(Current.Target, Scale(offset, Current.Distance));
}

ArcballCamera::Matrix4 ArcballCamera::View() const noexcept
{
    const Vector3 eye = Position();
    const Vector3 forward = Normalize(Subtract(Current.Target, eye));
    const Vector3 right = Normalize(Cross(forward, Vector3{ 0.0f, 1.0f, 0.0f }));
    const Vector3 up = Cross(right, forward);
    return Matrix4
    {{
        { right[0], right[1], right[2], -Dot(right, eye) },
        { up[0], up[1], up[2], -Dot(up, eye) },
        { -forward[0], -forward[1], -forward[2], Dot(forward, eye) },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    }};
}

ArcballCamera::Matrix4 ArcballCamera::Projection(float aspect) const noexcept
{
    // right-handed view space looks down -z. Vulkan's clip space has y down, so y is negated here
    const float f = 1.0f / std::tan(0.5f * FovY);
    const float depthScale = FarPlane / (NearPlane - FarPlane);
    return Matrix4
    {{
        { f / aspect, 0.0f, 0.0f, 0.0f },
        { 0.0f, -f, 0.0f, 0.0f },
        { 0.0f, 0.0f, depthScale, NearPlane * depthScale },
        { 0.0f, 0.0f, -1.0f, 0.0f },
    }};
}

ArcballCamera::Matrix4 ArcballCamera::ViewProjection(float aspect) const noexcept
{
    return Multiply(Projection(aspect), View());
}
