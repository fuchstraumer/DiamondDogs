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
}

void ArcballCamera::Rotate(float dx_pixels, float dy_pixels) noexcept
{
    Current.Yaw -= dx_pixels * k_RotateRadiansPerPixel;
    Current.Pitch = std::clamp(Current.Pitch + dy_pixels * k_RotateRadiansPerPixel, -k_MaxPitch, k_MaxPitch);
}

void ArcballCamera::Pan(float dx_pixels, float dy_pixels, float viewport_height_pixels) noexcept
{
    // one pixel moves the target by the size of one pixel at the target's distance, so the point under the cursor follows it
    const float worldPerPixel = 2.0f * Current.Distance * math::Tan(0.5f * FovY) / std::max(viewport_height_pixels, 1.0f);
    const Matrix4 view = View();
    const Vector3 right{ view[0][0], view[0][1], view[0][2] };
    const Vector3 up{ view[1][0], view[1][1], view[1][2] };
    Current.Target = Current.Target + right * (-dx_pixels * worldPerPixel) + up * (dy_pixels * worldPerPixel);
}

void ArcballCamera::Zoom(float wheel_steps) noexcept
{
    Current.Distance = std::clamp(Current.Distance * std::pow(k_ZoomPerStep, wheel_steps), k_MinDistance, MaxDistance);
}

ArcballCamera::Vector3 ArcballCamera::Position() const noexcept
{
    const auto [sinYaw, cosYaw] = math::SinCos(Current.Yaw);
    const auto [sinPitch, cosPitch] = math::SinCos(Current.Pitch);
    const Vector3 offset{ sinYaw * cosPitch, sinPitch, cosYaw * cosPitch };
    return Current.Target + offset * Current.Distance;
}

ArcballCamera::Matrix4 ArcballCamera::View() const noexcept
{
    const Vector3 eye = Position();
    const math::Vector eyeV = math::ToVector(eye);
    const math::Vector targetV = math::ToVector(Current.Target);
    const math::Vector forwardV = (targetV - eyeV).Normalize<3>();
    const math::Vector worldUp{ 0.0f, 1.0f, 0.0f, 0.0f };
    const math::Vector rightV = forwardV.Cross(worldUp).Normalize<3>();
    const math::Vector upV = rightV.Cross(forwardV);

    const Vector3 forward = math::FromVector(forwardV);
    const Vector3 right = math::FromVector(rightV);
    const Vector3 up = math::FromVector(upV);

    const float dotRightEye = rightV.Dot<3>(eyeV);
    const float dotUpEye = upV.Dot<3>(eyeV);
    const float dotForwardEye = forwardV.Dot<3>(eyeV);

    return Matrix4
    {
        right.x, right.y, right.z, -dotRightEye,
        up.x, up.y, up.z, -dotUpEye,
        -forward.x, -forward.y, -forward.z, dotForwardEye,
        0.0f, 0.0f, 0.0f, 1.0f
    };
}

ArcballCamera::Matrix4 ArcballCamera::ViewRotationOnly() const noexcept
{
    Matrix4 view = View();
    view[0][3] = 0.0f;
    view[1][3] = 0.0f;
    view[2][3] = 0.0f;
    return view;
}

ArcballCamera::Matrix4 ArcballCamera::Projection(float aspect) const noexcept
{
    // right-handed view space looks down -z. Vulkan's clip space has y down, so y is negated here
    // Vulkan Reverse-Z [1, 0]: NearPlane maps to 1.0, FarPlane maps to 0.0
    const float f = 1.0f / math::Tan(0.5f * FovY);
    const float scale = NearPlane / (FarPlane - NearPlane);
    const float bias = (FarPlane * NearPlane) / (FarPlane - NearPlane);
    return Matrix4
    {
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, -f, 0.0f, 0.0f,
        0.0f, 0.0f, scale, bias,
        0.0f, 0.0f, -1.0f, 0.0f
    };
}

ArcballCamera::Matrix4 ArcballCamera::ViewProjection(float aspect) const noexcept
{
    return math::FromMatrix<Matrix4>(math::ToMatrix(Projection(aspect)) * math::ToMatrix(View()));
}

ArcballCamera::Matrix4 ArcballCamera::ViewRotationProjection(float aspect) const noexcept
{
    return math::FromMatrix<Matrix4>(math::ToMatrix(Projection(aspect)) * math::ToMatrix(ViewRotationOnly()));
}
