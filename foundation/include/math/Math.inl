#pragma once
#include <cstring>
#include <numbers>

#include "math/Math.hpp"
// Implementation file for Math.hpp. This file holds the portable scalar bits and the glue between
// storage and SIMD types. The storage types themselves are implemented in MathStorage.inl
// Reference implementation, including DirectXMath SIMD inline: 
// https://github.com/fuchstraumer/DiamondDogs/blob/master/foundation/include/math/Math.inl
// other SIMD backends are in MathBackendWASM.inl and MathBackendDX.inl

namespace math
{
// Storage rows and columns are plain values, so these are ToVector on the accessor
DD_MATH_FORCEINLINE Vector RowVector(const Float3x3& matrix, size_t index) noexcept
{
    return ToVector(matrix.Row(index));
}

DD_MATH_FORCEINLINE Vector RowVector(const Float4x3& matrix, size_t index) noexcept
{
    return ToVector(matrix.Row(index));
}

DD_MATH_FORCEINLINE Vector RowVector(const Float4x4& matrix, size_t index) noexcept
{
    return ToVector(matrix.Row(index));
}

DD_MATH_FORCEINLINE Vector ColumnVector(const Float3x3& matrix, size_t index) noexcept
{
    return ToVector(matrix.Column(index));
}

DD_MATH_FORCEINLINE Vector ColumnVector(const Float4x3& matrix, size_t index) noexcept
{
    return ToVector(matrix.Column(index));
}

DD_MATH_FORCEINLINE Vector ColumnVector(const Float4x4& matrix, size_t index) noexcept
{
    return ToVector(matrix.Column(index));
}

// The proxy carries the Vector until a conversion operator picks the destination width; those
// operators need SIMD and live in the backend .inl files
DD_MATH_FORCEINLINE detail::FromVectorProxy FromVector(Vector vec) noexcept
{
    return detail::FromVectorProxy{ vec };
}

// Scalar on purpose: one value in, one out, and a splat-then-extract round trip would cost more than
// the arithmetic. Coefficients come from math/MathPolynomials.hpp, shared with the SIMD paths.

constexpr DD_MATH_FORCEINLINE float DegreesToRadians(float degrees) noexcept
{
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

constexpr DD_MATH_FORCEINLINE float RadiansToDegrees(float radians) noexcept
{
    return radians * (180.0f / std::numbers::pi_v<float>);
}

constexpr DD_MATH_FORCEINLINE float Lerp(float from, float to, float t) noexcept
{
    return from + ((to - from) * t);
}

constexpr DD_MATH_FORCEINLINE float Clamp(float value, float min, float max) noexcept
{
    return value < min ? min : (value > max ? max : value);
}

constexpr DD_MATH_FORCEINLINE float Saturate(float value) noexcept
{
    return Clamp(value, 0.0f, 1.0f);
}

DD_MATH_FORCEINLINE ScalarSinCos SinCosEst(float radians) noexcept
{
    const detail::ScalarTrigReduction reduction = detail::ReduceForTrigScalar(radians);
    return ScalarSinCos{ detail::SinPolynomialScalarEst(reduction.angle),
                         detail::CosPolynomialScalarEst(reduction.angle) * reduction.cosSign };
}

DD_MATH_FORCEINLINE float Sin(float radians) noexcept
{
    return detail::SinPolynomialScalar(detail::ReduceForTrigScalar(radians).angle);
}

DD_MATH_FORCEINLINE float Cos(float radians) noexcept
{
    const detail::ScalarTrigReduction reduction = detail::ReduceForTrigScalar(radians);
    return detail::CosPolynomialScalar(reduction.angle) * reduction.cosSign;
}

// 2^n written straight into the exponent field, times the series for 2^f, f in [-0.5, 0.5]
inline float Exp2(float value) noexcept
{
    const float nearest = static_cast<float>(static_cast<int32_t>(value + (value < 0.0f ? -0.5f : 0.5f)));
    const float fraction = value - nearest;

    float polynomial = detail::k_Exp2Poly6;
    polynomial = (polynomial * fraction) + detail::k_Exp2Poly5;
    polynomial = (polynomial * fraction) + detail::k_Exp2Poly4;
    polynomial = (polynomial * fraction) + detail::k_Exp2Poly3;
    polynomial = (polynomial * fraction) + detail::k_Exp2Poly2;
    polynomial = (polynomial * fraction) + detail::k_Exp2Poly1;
    polynomial = (polynomial * fraction) + 1.0f;

    const int32_t biasedExponent = static_cast<int32_t>(nearest) + 127;
    const uint32_t scaleBits = static_cast<uint32_t>(biasedExponent) << 23;
    float scale = 0.0f;
    std::memcpy(&scale, &scaleBits, sizeof(scale));

    return polynomial * scale;
}

// Exponent from the bit pattern, mantissa folded into [1/sqrt2, sqrt2), then the atanh series
inline float Log2(float value) noexcept
{
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));

    float exponent = static_cast<float>(static_cast<int32_t>((bits >> 23) & 0xFFu) - 127);

    const uint32_t mantissaBits = (bits & 0x007FFFFFu) | 0x3F800000u;
    float mantissa = 0.0f;
    std::memcpy(&mantissa, &mantissaBits, sizeof(mantissa));

    constexpr float k_Sqrt2 = std::numbers::sqrt2_v<float>;
    if (mantissa > k_Sqrt2)
    {
        mantissa *= 0.5f;
        exponent += 1.0f;
    }

    const float ratio = (mantissa - 1.0f) / (mantissa + 1.0f);
    const float ratioSquared = ratio * ratio;

    float series = detail::k_Log2Poly7;
    series = (series * ratioSquared) + detail::k_Log2Poly5;
    series = (series * ratioSquared) + detail::k_Log2Poly3;
    series = (series * ratioSquared) + detail::k_Log2Poly1;

    return exponent + series * ratio * detail::k_Log2Scale;
}

DD_MATH_FORCEINLINE float Tan(float radians) noexcept
{
    const ScalarSinCos pair = SinCos(radians);
    return pair.sin / pair.cos;
}

inline float ATan(float value) noexcept
{
    constexpr float k_HalfPi = std::numbers::pi_v<float> * 0.5f;

    const float magnitude = value < 0.0f ? -value : value;
    // atan(x) = sign(x) * pi/2 - atan(1/x) outside [-1, 1], which is where the table is fitted
    const float reduced = magnitude <= 1.0f ? value : 1.0f / value;

    const float squared = reduced * reduced;
    float polynomial = detail::k_ATanPoly17;
    polynomial = (polynomial * squared) + detail::k_ATanPoly15;
    polynomial = (polynomial * squared) + detail::k_ATanPoly13;
    polynomial = (polynomial * squared) + detail::k_ATanPoly11;
    polynomial = (polynomial * squared) + detail::k_ATanPoly9;
    polynomial = (polynomial * squared) + detail::k_ATanPoly7;
    polynomial = (polynomial * squared) + detail::k_ATanPoly5;
    polynomial = (polynomial * squared) + detail::k_ATanPoly3;
    polynomial = (polynomial * squared) + 1.0f;
    const float inner = polynomial * reduced;

    if (magnitude <= 1.0f)
    {
        return inner;
    }
    return (value < 0.0f ? -k_HalfPi : k_HalfPi) - inner;
}

DD_MATH_FORCEINLINE float ATan2(float y, float x) noexcept
{
    constexpr float k_Pi = std::numbers::pi_v<float>;

    if (x == 0.0f && y == 0.0f)
    {
        return 0.0f;
    }

    const float base = ATan(y / x);
    if (x >= 0.0f)
    {
        return base;
    }
    return base + (y < 0.0f ? -k_Pi : k_Pi);
}

DD_MATH_FORCEINLINE float ASin(float value) noexcept
{
    return ATan2(value, std::sqrt(1.0f - (value * value)));
}

DD_MATH_FORCEINLINE float ACos(float value) noexcept
{
    return ATan2(std::sqrt(1.0f - (value * value)), value);
}

DD_MATH_FORCEINLINE float Pow(float base, float exponent) noexcept
{
    return Exp2(exponent * Log2(base));
}

// 1 - 2 / (e^2x + 1); saturates to 1 rather than producing inf/inf where the difference form would
DD_MATH_FORCEINLINE float TanH(float value) noexcept
{
    return 1.0f - (2.0f / (Exp2(2.0f * value * detail::k_Log2OfE) + 1.0f));
}

// Shortest arc: q and -q are the same rotation, so a negative dot means the other representation is
// nearer and negating one endpoint avoids interpolating the long way round. Near-parallel endpoints
// fall back to a normalized lerp, where sin(theta) is too small to divide by.
DD_MATH_FORCEINLINE Quaternion Quaternion::Slerp(Quaternion from, Quaternion to, float t) noexcept
{
    float cosine = from.Dot(to);
    Quaternion target = to;
    if (cosine < 0.0f)
    {
        target = Quaternion{ -static_cast<Vector>(to) };
        cosine = -cosine;
    }

    constexpr float k_ParallelThreshold = 0.9995f;
    if (cosine > k_ParallelThreshold)
    {
        const Vector blended =
            static_cast<Vector>(from) + (static_cast<Vector>(target) - static_cast<Vector>(from)) * t;
        return Quaternion{ blended }.Normalize();
    }

    const float theta = ACos(Clamp(cosine, -1.0f, 1.0f));
    const float reciprocalSine = 1.0f / Sin(theta);
    const float fromWeight = Sin((1.0f - t) * theta) * reciprocalSine;
    const float toWeight = Sin(t * theta) * reciprocalSine;

    return Quaternion{ static_cast<Vector>(from) * fromWeight + static_cast<Vector>(target) * toWeight };
}

// ============================================================================
// Half-precision float conversions (IEEE-754 round-to-nearest-even)
// ============================================================================
inline float HalfToFloat(uint16_t value) noexcept
{
    const uint32_t sign = (static_cast<uint32_t>(value) & 0x8000u) << 16u;
    const uint32_t exponent = (static_cast<uint32_t>(value) >> 10u) & 0x1Fu;
    const uint32_t mantissa = static_cast<uint32_t>(value) & 0x3FFu;

    if (exponent == 0u)
    {
        // zero or denormal: mantissa * 2^-24
        const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
        return sign != 0u ? -magnitude : magnitude;
    }

    if (exponent == 31u)
    {
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13u));
    }

    return std::bit_cast<float>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
}

inline uint16_t FloatToHalf(float value) noexcept
{
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
    const float magnitude = std::fabs(value);

    if (std::isnan(value))
    {
        return static_cast<uint16_t>(sign | 0x7E00u);
    }

    if (magnitude >= 65520.0f)
    {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }

    if (magnitude < 6.103515625e-05f)
    {
        // denormal: round to the nearest multiple of 2^-24
        return static_cast<uint16_t>(sign | static_cast<uint16_t>(std::nearbyint(magnitude * 16777216.0f)));
    }

    // round to nearest even on the 13 dropped mantissa bits
    const uint32_t magnitudeBits = bits & 0x7FFFFFFFu;
    const uint32_t rounded = magnitudeBits + 0x0FFFu + ((magnitudeBits >> 13u) & 1u);
    return static_cast<uint16_t>(sign | ((rounded - (112u << 23u)) >> 13u));
}

// ============================================================================
// Frustum plane extraction
// ============================================================================
inline std::array<Float4, 6> ExtractFrustumPlanes(const Float4x4& view_proj, bool reverse_z) noexcept
{
    const Float4 r0 = view_proj.Row(0);
    const Float4 r1 = view_proj.Row(1);
    const Float4 r2 = view_proj.Row(2);
    const Float4 r3 = view_proj.Row(3);

    std::array<Float4, 6> planes{
        r3 + r0, // Left:   row3 + row0
        r3 - r0, // Right:  row3 - row0
        r3 + r1, // Bottom: row3 + row1
        r3 - r1, // Top:    row3 - row1
        reverse_z ? (r3 - r2) : r2, // Near: reverse_z ? (z_clip <= w_clip) : (z_clip >= 0)
        reverse_z ? r2 : (r3 - r2)  // Far:  reverse_z ? (z_clip >= 0) : (z_clip <= w_clip)
    };

    for (auto& plane : planes)
    {
        const float len = std::sqrt((plane.x * plane.x) + (plane.y * plane.y) + (plane.z * plane.z));
        if (len > 1e-7f)
        {
            const float inv_len = 1.0f / len;
            plane.x *= inv_len;
            plane.y *= inv_len;
            plane.z *= inv_len;
            plane.w *= inv_len;
        }
    }

    return planes;
}

} // namespace math

// ============================================================================
// Backend dispatch - Vector/Matrix implementations
// ============================================================================
// C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\DirectXMath.h
#if VX_MATH_BACKEND_WASM
    #include "math/MathBackendWASM.inl"
#else
    #include "math/MathBackendDX.inl"
#endif
