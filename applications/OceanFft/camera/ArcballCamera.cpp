#include "ArcballCamera.hpp"
#include "math/Math.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    // rays flatter than this never reach the reference plane inside any sane range
    constexpr float k_MinPlaneRayY = 1.0e-4f;
    constexpr float k_MinPickRange = 100.0f;
    constexpr float k_PickRangePerDistance = 25.0f;
    // the dolly past MinDistance stops this far short of the anchor, so that the eye never reaches it
    constexpr float k_MinAnchorClearance = 1.0f;
}

using namespace math;

void ArcballCamera::SetState(const State& state) noexcept
{
    Current.Target = state.Target;
    Current.Yaw = math::ModAngles(state.Yaw);
    Current.Pitch = std::clamp(state.Pitch, -MaxPitch, MaxPitch);
    Current.Distance = std::clamp(state.Distance, MinDistance, MaxDistance);
}

void ArcballCamera::LookAt(const Float3& eye, const Float3& target) noexcept
{
    const Float3 offset = eye - target;
    const float distance = offset.Length();

    State state = Current;
    state.Target = target;
    state.Distance = distance;

    if (distance > 1.0e-4f)
    {
        state.Pitch = math::ASin(std::clamp(offset.y / distance, -1.0f, 1.0f));
        state.Yaw = math::ATan2(offset.x, offset.z);
    }

    SetState(state);
}

void ArcballCamera::Orbit(float d_yaw_radians, float d_pitch_radians) noexcept
{
    Current.Yaw = math::ModAngles(Current.Yaw + d_yaw_radians);
    Current.Pitch = std::clamp(Current.Pitch + d_pitch_radians, -MaxPitch, MaxPitch);
}

void ArcballCamera::ScaleAbout(const Float3& anchor, float log_scale) noexcept
{
    const float requestedDistance = Current.Distance * std::exp(-log_scale);
    const float clampedDistance = std::clamp(requestedDistance, MinDistance, MaxDistance);
    const float appliedFactor = clampedDistance / Current.Distance;
    Current.Target = anchor + (Current.Target - anchor) * appliedFactor;
    Current.Distance = clampedDistance;

    if (requestedDistance >= MinDistance)
    {
        return;
    }

    // the scale stopped at MinDistance. the rest of it moves the whole rig along the eye's ray to the anchor, so the
    // anchor keeps its pixel and the zoom does not stall
    const Float3 toAnchor = anchor - Position();
    const float anchorDistance = toAnchor.Length();
    const float unappliedFactor = requestedDistance / clampedDistance;
    const float travel = std::min(anchorDistance * (1.0f - unappliedFactor), anchorDistance - k_MinAnchorClearance);

    if (travel > 0.0f)
    {
        TranslateTarget(toAnchor * (travel / anchorDistance));
    }
}

void ArcballCamera::RotateAbout(const Float3& anchor, float d_yaw_radians) noexcept
{
    using namespace math;
    Current.Target = anchor + Float3::RotateAroundY(Current.Target - anchor, d_yaw_radians);
    Current.Yaw = ModAngles(Current.Yaw + d_yaw_radians);
}

void ArcballCamera::TranslateTarget(const Float3& delta) noexcept
{
    Current.Target = Current.Target + delta;
}

void ArcballCamera::Reseat() noexcept
{
    const Basis basis = ComputeBasis();
    const Float3 eye = Position();
    const Float3 forward = -basis.Back;

    if (std::abs(forward.y) < k_MinPlaneRayY)
    {
        return;
    }

    const float hitDistance = (ReferencePlaneY - eye.y) / forward.y;

    if (hitDistance <= 0.0f || hitDistance > pickRange())
    {
        return;
    }

    const float seatedDistance = std::clamp(hitDistance, MinDistance, MaxDistance);
    Current.Target = eye + forward * seatedDistance;
    Current.Distance = seatedDistance;
}

void ArcballCamera::PanViewPlane(float dx_pixels, float dy_pixels, const CameraViewport& viewport) noexcept
{
    // one pixel moves the target by the size of one pixel at the target's distance
    const float worldPerPixel = 2.0f * Current.Distance * math::Tan(0.5f * FovY) / std::max(viewport.Height, 1.0f);
    const Basis basis = ComputeBasis();
    Current.Target = Current.Target + basis.Right * (-dx_pixels * worldPerPixel) + basis.Up * (dy_pixels * worldPerPixel);
}

Quaternion ArcballCamera::Orientation() const noexcept
{
    // Multiply applies its left side first: tilt around the local x axis, then turn around world y
    const Quaternion pitchRotation = Quaternion::RotationNormal(Vector{ 1.0f, 0.0f, 0.0f, 0.0f }, -Current.Pitch);
    const Quaternion yawRotation = Quaternion::RotationNormal(Vector{ 0.0f, 1.0f, 0.0f, 0.0f }, Current.Yaw);
    return pitchRotation.Multiply(yawRotation);
}

ArcballCamera::Basis ArcballCamera::ComputeBasis() const noexcept
{
    const Quaternion orientation = Orientation();
    const Float3 right = math::FromVector(orientation.RotateVector(Vector{ 1.0f, 0.0f, 0.0f, 0.0f }));
    const Float3 up = math::FromVector(orientation.RotateVector(Vector{ 0.0f, 1.0f, 0.0f, 0.0f }));
    const Float3 back = math::FromVector(orientation.RotateVector(Vector{ 0.0f, 0.0f, 1.0f, 0.0f }));
    return Basis{ right, up, back };
}

Float3 ArcballCamera::Position() const noexcept
{
    const Basis basis = ComputeBasis();
    return Current.Target + basis.Back * Current.Distance;
}

ArcballCamera::Ray ArcballCamera::CursorRay(const Float2& cursor_pixels, const CameraViewport& viewport) const noexcept
{
    const float width = std::max(viewport.Width, 1.0f);
    const float height = std::max(viewport.Height, 1.0f);
    const float ndcX = ((2.0f * cursor_pixels.x) / width) - 1.0f;
    const float ndcY = 1.0f - ((2.0f * cursor_pixels.y) / height);
    const float tanHalfFov = math::Tan(0.5f * FovY);
    const float aspect = width / height;

    const Basis basis = ComputeBasis();
    const Float3 direction = -basis.Back + basis.Right * (ndcX * tanHalfFov * aspect) + basis.Up * (ndcY * tanHalfFov);
    return Ray{ Position(), direction / direction.Length() };
}

std::optional<Float3> ArcballCamera::PickPlane(const Float2& cursor_pixels, const CameraViewport& viewport) const noexcept
{
    const Ray ray = CursorRay(cursor_pixels, viewport);

    if (std::abs(ray.Direction.y) < k_MinPlaneRayY)
    {
        return std::nullopt;
    }

    const float hitDistance = (ReferencePlaneY - ray.Origin.y) / ray.Direction.y;

    if (hitDistance <= 0.0f || hitDistance > pickRange())
    {
        return std::nullopt;
    }

    // y is written exactly, so that the difference of two picks has no vertical part
    const Float3 hit = ray.Origin + ray.Direction * hitDistance;
    return Float3{ hit.x, ReferencePlaneY, hit.z };
}

std::optional<Float2> ArcballCamera::ProjectToScreen(const Float3& world, const CameraViewport& viewport) const noexcept
{
    const Basis basis = ComputeBasis();
    const Float3 relative = world - Position();
    const float depth = -relative.Dot(basis.Back);

    if (depth <= 1.0e-6f)
    {
        return std::nullopt;
    }

    const float width = std::max(viewport.Width, 1.0f);
    const float height = std::max(viewport.Height, 1.0f);
    const float tanHalfFov = math::Tan(0.5f * FovY);
    const float aspect = width / height;
    const float ndcX = relative.Dot(basis.Right) / (depth * tanHalfFov * aspect);
    const float ndcY = relative.Dot(basis.Up) / (depth * tanHalfFov);
    return Float2{ (ndcX + 1.0f) * 0.5f * width, (1.0f - ndcY) * 0.5f * height };
}

Float4x4 ArcballCamera::View() const noexcept
{
    const Basis basis = ComputeBasis();
    const Float3 eye = Position();
    const float dotRightEye = basis.Right.Dot(eye);
    const float dotUpEye = basis.Up.Dot(eye);
    const float dotBackEye = basis.Back.Dot(eye);

    return Float4x4
    {
        basis.Right.x, basis.Right.y, basis.Right.z, -dotRightEye,
        basis.Up.x, basis.Up.y, basis.Up.z, -dotUpEye,
        basis.Back.x, basis.Back.y, basis.Back.z, -dotBackEye,
        0.0f, 0.0f, 0.0f, 1.0f
    };
}

Float4x4 ArcballCamera::ViewRotationOnly() const noexcept
{
    Float4x4 view = View();
    view[0][3] = 0.0f;
    view[1][3] = 0.0f;
    view[2][3] = 0.0f;
    return view;
}

Float4x4 ArcballCamera::Projection(float aspect) const noexcept
{
    // right-handed view space looks down -z. Vulkan's clip space has y down, so y is negated here
    // Vulkan Reverse-Z [1, 0]: NearPlane maps to 1.0, FarPlane maps to 0.0
    const float f = 1.0f / math::Tan(0.5f * FovY);
    const float scale = NearPlane / (FarPlane - NearPlane);
    const float bias = (FarPlane * NearPlane) / (FarPlane - NearPlane);
    return Float4x4
    {
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, -f, 0.0f, 0.0f,
        0.0f, 0.0f, scale, bias,
        0.0f, 0.0f, -1.0f, 0.0f
    };
}

Float4x4 ArcballCamera::ViewProjection(float aspect) const noexcept
{
    using namespace math;
    const Matrix proj = ToMatrix(Projection(aspect));
    const Matrix view = ToMatrix(View());
    // remember: always use the SIMD type for matmul, bc even with register overhead 
    // this is so many ops that it still benefits from SIMD
    return FromMatrix<Float4x4>(proj * view);
}

Float4x4 ArcballCamera::ViewRotationProjection(float aspect) const noexcept
{
    using namespace math;
    const Matrix proj = ToMatrix(Projection(aspect));
    const Matrix view = ToMatrix(ViewRotationOnly());
    return FromMatrix<Float4x4>(proj * view);
}

float ArcballCamera::pickRange() const noexcept
{
    return std::clamp(Current.Distance * k_PickRangePerDistance, k_MinPickRange, 2.0f * MaxDistance);
}

