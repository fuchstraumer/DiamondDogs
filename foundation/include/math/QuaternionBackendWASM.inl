#pragma once
// WASM SIMD128 implementations for math::Quaternion. Included from MathBackendWASM.inl; include
// nothing here, re-entering that cycle would parse every definition below twice.
namespace velox::math
{

VX_MATH_FORCEINLINE Quaternion::Quaternion() noexcept
    : data{ wasm_f32x4_const(0.0f, 0.0f, 0.0f, 1.0f) }
{
}

VX_MATH_FORCEINLINE Quaternion::Quaternion(float x, float y, float z, float w) noexcept
    : data{ wasm_f32x4_make(x, y, z, w) }
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
    return wasm_f32x4_extract_lane(data, 0);
}
VX_MATH_FORCEINLINE float Quaternion::y() const noexcept
{
    return wasm_f32x4_extract_lane(data, 1);
}
VX_MATH_FORCEINLINE float Quaternion::z() const noexcept
{
    return wasm_f32x4_extract_lane(data, 2);
}
VX_MATH_FORCEINLINE float Quaternion::w() const noexcept
{
    return wasm_f32x4_extract_lane(data, 3);
}

// Hamilton product as four FMA chains against broadcast components of `second` - no horizontal
// reductions, same shape as MulRowByMatrix. Sign patterns come from expanding the product with
// ij = k, jk = i, ki = j, ii = jj = kk = -1.
//
// `*this` applies first, then `second` (DirectXMath's order, i.e. the product second * this).
VX_MATH_FORCEINLINE Quaternion Quaternion::Multiply(Quaternion second) const noexcept
{
    const v128_t lhs = data;
    const v128_t rhs = second.data;

    const v128_t rhsX = wasm_i32x4_shuffle(rhs, rhs, 0, 0, 0, 0);
    const v128_t rhsY = wasm_i32x4_shuffle(rhs, rhs, 1, 1, 1, 1);
    const v128_t rhsZ = wasm_i32x4_shuffle(rhs, rhs, 2, 2, 2, 2);
    const v128_t rhsW = wasm_i32x4_shuffle(rhs, rhs, 3, 3, 3, 3);

    // cross-term reorderings, each with its own sign pattern
    const v128_t lhsWZYX = wasm_i32x4_shuffle(lhs, lhs, 3, 2, 1, 0);
    const v128_t lhsZWXY = wasm_i32x4_shuffle(lhs, lhs, 2, 3, 0, 1);
    const v128_t lhsYXWZ = wasm_i32x4_shuffle(lhs, lhs, 1, 0, 3, 2);

    v128_t result = wasm_f32x4_mul(lhs, rhsW);
    result = detail::MulAdd(wasm_f32x4_mul(lhsWZYX, wasm_f32x4_const(1.0f, -1.0f, 1.0f, -1.0f)),
                            rhsX,
                            result);
    result = detail::MulAdd(wasm_f32x4_mul(lhsZWXY, wasm_f32x4_const(1.0f, 1.0f, -1.0f, -1.0f)),
                            rhsY,
                            result);
    result = detail::MulAdd(wasm_f32x4_mul(lhsYXWZ, wasm_f32x4_const(-1.0f, 1.0f, 1.0f, -1.0f)),
                            rhsZ,
                            result);
    return Quaternion{ result };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Conjugate() const noexcept
{
    return Quaternion{ wasm_f32x4_mul(data, wasm_f32x4_const(-1.0f, -1.0f, -1.0f, 1.0f)) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Inverse() const noexcept
{
    const v128_t lengthSq = Vector{ data }.DotVec<4>(Vector{ data }).Data();
    const v128_t conjugated = wasm_f32x4_mul(data, wasm_f32x4_const(-1.0f, -1.0f, -1.0f, 1.0f));
    return Quaternion{ wasm_f32x4_div(conjugated, lengthSq) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Normalize() const noexcept
{
    return Quaternion{ Vector{ data }.Normalize<4>().Data() };
}

VX_MATH_FORCEINLINE float Quaternion::Dot(Quaternion other) const noexcept
{
    return Vector{ data }.Dot<4>(Vector{ other.data });
}

VX_MATH_FORCEINLINE float Quaternion::Length() const noexcept
{
    return Vector{ data }.Length<4>();
}

VX_MATH_FORCEINLINE float Quaternion::LengthSq() const noexcept
{
    return Vector{ data }.LengthSq<4>();
}

VX_MATH_FORCEINLINE bool Quaternion::IsIdentity() const noexcept
{
    const v128_t identity = wasm_f32x4_const(0.0f, 0.0f, 0.0f, 1.0f);
    return VectorMask{ wasm_f32x4_eq(data, identity) }.AllTrue<4>();
}

// q * (v, 0) * q^-1, expanded to avoid building either intermediate quaternion:
// v' = v + 2w(u X v) + 2(u X (u X v)),  with u the vector part
// i.e, the Rogrigues' rotation formula to significantly reduce instruction count
// means we are ignoring the w of the input vector, but this is rarely a problem
// (since our most common case, especially on the CPU, is rotating 3D vecs)
VX_MATH_FORCEINLINE Vector Quaternion::RotateVector(Vector vec) const noexcept
{
    // zero the .w of the quaternion and input vector, and broadcast the scalar part of the quaternion
    const Vector axis{ wasm_f32x4_replace_lane(data, 3, 0.0f) };
    const Vector input{ wasm_f32x4_replace_lane(vec.Data(), 3, 0.0f) };
    const Vector scalar{ wasm_i32x4_shuffle(data, data, 3, 3, 3, 3) };
    // perform u X v
    const Vector firstCross = axis.Cross(input);
    // now perform u X (u X v)
    const Vector secondCross = axis.Cross(firstCross);
    // now splat the 2.0f constant so we can use it for the mul
    const v128_t innerProduct = detail::MulAdd(firstCross.Data(), scalar.Data(), secondCross.Data());
    const v128_t result = detail::MulAdd(innerProduct, wasm_f32x4_const_splat(2.0f), input.Data());
    return Vector{ result };
}

VX_MATH_FORCEINLINE Matrix Quaternion::ToMatrix() const noexcept
{
    return Matrix::RotationQuaternion(*this);
}

VX_MATH_FORCEINLINE void Quaternion::ToAxisAngle(Vector& out_axis, float& out_radians) const noexcept
{
    out_axis = Vector{ wasm_f32x4_replace_lane(data, 3, 0.0f) };
    // clamped: a quaternion drifted past unit length would hand std::acos an out-of-range argument
    const float scalarPart = std::fmin(std::fmax(w(), -1.0f), 1.0f);
    out_radians = 2.0f * std::acos(scalarPart);
}

VX_MATH_FORCEINLINE Quaternion Quaternion::Identity() noexcept
{
    return Quaternion{ wasm_f32x4_const(0.0f, 0.0f, 0.0f, 1.0f) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::RotationAxis(Vector axis, float radians) noexcept
{
    return RotationNormal(axis.Normalize<3>(), radians);
}

VX_MATH_FORCEINLINE Quaternion Quaternion::RotationNormal(Vector normal_axis, float radians) noexcept
{
    const float halfAngle = radians * 0.5f;
    const float sinHalf = std::sin(halfAngle);
    const float cosHalf = std::cos(halfAngle);

    // (axis * sin(theta/2), cos(theta/2))
    const v128_t scaled = wasm_f32x4_mul(normal_axis.Data(), wasm_f32x4_splat(sinHalf));
    return Quaternion{ wasm_f32x4_replace_lane(scaled, 3, cosHalf) };
}

VX_MATH_FORCEINLINE Quaternion Quaternion::RotationRollPitchYaw(float pitch,
                                                                float yaw,
                                                                float roll) noexcept
{
    const Quaternion aboutX = RotationNormal(Vector{ 1.0f, 0.0f, 0.0f, 0.0f }, pitch);
    const Quaternion aboutY = RotationNormal(Vector{ 0.0f, 1.0f, 0.0f, 0.0f }, yaw);
    const Quaternion aboutZ = RotationNormal(Vector{ 0.0f, 0.0f, 1.0f, 0.0f }, roll);
    return aboutX.Multiply(aboutY).Multiply(aboutZ);
}

// Shepperd's method: four equivalent recoveries, each dividing by a different component. The trace
// comparisons pick the largest, avoiding the near-zero divide that wrecks the single-formula version.
// Scalar and branchy on purpose - not a hot path, so clarity wins over SIMD.
inline Quaternion Quaternion::FromMatrix(const Matrix& mat) noexcept
{
    const float m00 = mat[0, 0];
    const float m01 = mat[0, 1];
    const float m02 = mat[0, 2];
    const float m10 = mat[1, 0];
    const float m11 = mat[1, 1];
    const float m12 = mat[1, 2];
    const float m20 = mat[2, 0];
    const float m21 = mat[2, 1];
    const float m22 = mat[2, 2];

    if (m22 <= 0.0f)
    {
        // x^2 + y^2 >= z^2 + w^2
        const float difference10 = m11 - m00;
        const float oneMinusM22 = 1.0f - m22;
        if (difference10 <= 0.0f)
        {
            // x^2 >= y^2
            const float fourXSquared = oneMinusM22 - difference10;
            const float inverse4x = 0.5f / std::sqrt(fourXSquared);
            return Quaternion{ fourXSquared * inverse4x,
                               (m01 + m10) * inverse4x,
                               (m02 + m20) * inverse4x,
                               (m12 - m21) * inverse4x };
        }

        const float fourYSquared = oneMinusM22 + difference10;
        const float inverse4y = 0.5f / std::sqrt(fourYSquared);
        return Quaternion{ (m01 + m10) * inverse4y,
                           fourYSquared * inverse4y,
                           (m12 + m21) * inverse4y,
                           (m20 - m02) * inverse4y };
    }

    // z^2 + w^2 >= x^2 + y^2
    const float sum10 = m11 + m00;
    const float onePlusM22 = 1.0f + m22;
    if (sum10 <= 0.0f)
    {
        // z^2 >= w^2
        const float fourZSquared = onePlusM22 - sum10;
        const float inverse4z = 0.5f / std::sqrt(fourZSquared);
        return Quaternion{ (m02 + m20) * inverse4z,
                           (m12 + m21) * inverse4z,
                           fourZSquared * inverse4z,
                           (m01 - m10) * inverse4z };
    }

    const float fourWSquared = onePlusM22 + sum10;
    const float inverse4w = 0.5f / std::sqrt(fourWSquared);
    return Quaternion{ (m12 - m21) * inverse4w,
                       (m20 - m02) * inverse4w,
                       (m01 - m10) * inverse4w,
                       fourWSquared * inverse4w };
}

} // namespace velox::math
