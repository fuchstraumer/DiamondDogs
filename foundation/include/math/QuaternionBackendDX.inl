#pragma once
// DirectXMath implementations for math::Quaternion. Included from MathBackendDX.inl; include nothing.
// Mostly forwarding to XMQuaternion*, with three marked exceptions where matching the WASM backend's
// semantics matters more than reusing DirectXMath's spelling.
namespace velox::math
{

VX_MATH_FORCEINLINE Quaternion::Quaternion() noexcept
    : data{ DirectX::XMQuaternionIdentity() }
{
}

VX_MATH_FORCEINLINE Quaternion::Quaternion(float x, float y, float z, float w) noexcept
    : data{ DirectX::XMVectorSet(x, y, z, w) }
{
}

VX_MATH_FORCEINLINE Quaternion::Quaternion(Vector vec) noexcept
    : data{ vec.Data() }
{
}

VX_MATH_FORCEINLINE Quaternion::operator Vector() const noexcept
{
    return Vector{ data };
}

VX_MATH_FORCEINLINE float Quaternion::x() const noexcept
{
    return DirectX::XMVectorGetX(data);
}
VX_MATH_FORCEINLINE float Quaternion::y() const noexcept
{
    return DirectX::XMVectorGetY(data);
}
VX_MATH_FORCEINLINE float Quaternion::z() const noexcept
{
    return DirectX::XMVectorGetZ(data);
}
VX_MATH_FORCEINLINE float Quaternion::w() const noexcept
{
    return DirectX::XMVectorGetW(data);
}

// XMQuaternionMultiply(Q1, Q2) is "rotate by Q1 then Q2", already the order this method promises
VX_MATH_FORCEINLINE Quaternion Quaternion::Multiply(Quaternion second) const noexcept
{
    return Quaternion{ DirectX::XMQuaternionMultiply(data, second.data) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Conjugate() const noexcept
{
    return Quaternion{ DirectX::XMQuaternionConjugate(data) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Inverse() const noexcept
{
    return Quaternion{ DirectX::XMQuaternionInverse(data) };
}

// Not XMQuaternionNormalize: it special-cases zero and infinite length, which Vector::Normalize<4>
// deliberately does not. Going through Vector keeps degenerate input consistent between the two.
VX_MATH_FORCEINLINE Quaternion Quaternion::Normalize() const noexcept
{
    return Quaternion{ Vector{ data }.Normalize<4>().Data() };
}

VX_MATH_FORCEINLINE float Quaternion::Dot(Quaternion other) const noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMQuaternionDot(data, other.data));
}

VX_MATH_FORCEINLINE float Quaternion::Length() const noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMQuaternionLength(data));
}

VX_MATH_FORCEINLINE float Quaternion::LengthSq() const noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMQuaternionLengthSq(data));
}

VX_MATH_FORCEINLINE bool Quaternion::IsIdentity() const noexcept
{
    return DirectX::XMQuaternionIsIdentity(data);
}

VX_MATH_FORCEINLINE Vector Quaternion::RotateVector(Vector vec) const noexcept
{
    return Vector{ DirectX::XMVector3Rotate(vec.Data(), data) };
}

VX_MATH_FORCEINLINE Matrix Quaternion::ToMatrix() const noexcept
{
    return Matrix::RotationQuaternion(*this);
}

// XMQuaternionToAxisAngle puts the whole quaternion in the axis output, leaving cos(theta/2) in
// lane 3. Zeroed: an axis is a direction, and keeping it would diverge from the WASM backend.
// Magnitude is still sin(theta/2), so normalize if you need a unit axis.
VX_MATH_FORCEINLINE void Quaternion::ToAxisAngle(Vector& out_axis, float& out_radians) const noexcept
{
    DirectX::XMVECTOR axis;
    DirectX::XMQuaternionToAxisAngle(&axis, &out_radians, data);
    out_axis = Vector{ DirectX::XMVectorSetW(axis, 0.0f) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Identity() noexcept
{
    return Quaternion{ DirectX::XMQuaternionIdentity() };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::RotationAxis(Vector axis, float radians) noexcept
{
    return Quaternion{ DirectX::XMQuaternionRotationAxis(axis.Data(), radians) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::RotationNormal(Vector normal_axis, float radians) noexcept
{
    return Quaternion{ DirectX::XMQuaternionRotationNormal(normal_axis.Data(), radians) };
}

// Composed explicitly, not via XMQuaternionRotationRollPitchYaw: the WASM backend composes by hand
// anyway, and disagreeing on order would make one Euler triple mean two different rotations.
inline Quaternion Quaternion::RotationRollPitchYaw(float pitch,
                                                                float yaw,
                                                                float roll) noexcept
{
    const Quaternion aboutX = RotationNormal(Vector{ 1.0f, 0.0f, 0.0f, 0.0f }, pitch);
    const Quaternion aboutY = RotationNormal(Vector{ 0.0f, 1.0f, 0.0f, 0.0f }, yaw);
    const Quaternion aboutZ = RotationNormal(Vector{ 0.0f, 0.0f, 1.0f, 0.0f }, roll);
    return aboutX.Multiply(aboutY).Multiply(aboutZ);
}

inline Quaternion Quaternion::FromMatrix(const Matrix& mat) noexcept
{
    return Quaternion{ DirectX::XMQuaternionRotationMatrix(mat.Data()) };
}

} // namespace velox::math
