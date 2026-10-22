#pragma once
// Implementation file for Math.hpp. This file holds the *portable* bits -
// storage-type (Float2/3/4, Float3x3/4x3/4x4) implementations
// Reference implementation, including DirectXMath SIMD inline: 
// https://github.com/fuchstraumer/DiamondDogs/blob/master/foundation/include/math/Math.inl
// other SIMD backends are in MathBackendWASM.inl and MathBackendDX.inl

namespace velox::math
{
// ================================
// Float2 implementations
// ================================
constexpr inline Float2 Float2::operator+(const Float2& rhs) const noexcept
{
    return Float2(x + rhs.x, y + rhs.y);
}

constexpr inline Float2 Float2::operator-(const Float2& rhs) const noexcept
{
    return Float2(x - rhs.x, y - rhs.y);
}

constexpr inline Float2 Float2::operator*(const Float2& rhs) const noexcept
{
    return Float2(x * rhs.x, y * rhs.y);
}

constexpr inline Float2 Float2::operator/(const Float2& rhs) const noexcept
{
    return Float2(x / rhs.x, y / rhs.y);
}

constexpr inline Float2 Float2::operator*(float scalar) const noexcept
{
    return Float2(x * scalar, y * scalar);
}

constexpr inline Float2 Float2::operator/(float scalar) const noexcept
{
    return Float2(x / scalar, y / scalar);
}

constexpr inline Float2 Float2::operator-() const noexcept
{
    return Float2(-x, -y);
}

constexpr inline Float2& Float2::operator+=(const Float2& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    return *this;
}

constexpr inline Float2& Float2::operator-=(const Float2& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    return *this;
}

constexpr inline Float2& Float2::operator*=(const Float2& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    return *this;
}

constexpr inline Float2& Float2::operator/=(const Float2& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    return *this;
}

constexpr inline Float2& Float2::operator*=(float scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    return *this;
}

constexpr inline Float2& Float2::operator/=(float scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    return *this;
}

constexpr inline bool Float2::operator==(const Float2& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y;
}

constexpr inline bool Float2::operator!=(const Float2& rhs) const noexcept
{
    return !(*this == rhs);
}

// ================================
// Float3 implementations
// ================================
constexpr inline Float3 Float3::operator+(const Float3& rhs) const noexcept
{
    return Float3(x + rhs.x, y + rhs.y, z + rhs.z);
}

constexpr inline Float3 Float3::operator-(const Float3& rhs) const noexcept
{
    return Float3(x - rhs.x, y - rhs.y, z - rhs.z);
}

constexpr inline Float3 Float3::operator*(const Float3& rhs) const noexcept
{
    return Float3(x * rhs.x, y * rhs.y, z * rhs.z);
}

constexpr inline Float3 Float3::operator/(const Float3& rhs) const noexcept
{
    return Float3(x / rhs.x, y / rhs.y, z / rhs.z);
}

constexpr inline Float3 Float3::operator*(float scalar) const noexcept
{
    return Float3(x * scalar, y * scalar, z * scalar);
}

constexpr inline Float3 Float3::operator/(float scalar) const noexcept
{
    return Float3(x / scalar, y / scalar, z / scalar);
}

constexpr inline Float3 Float3::operator-() const noexcept
{
    return Float3(-x, -y, -z);
}

constexpr Float3& Float3::operator+=(const Float3& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
}

constexpr Float3& Float3::operator-=(const Float3& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
}

constexpr Float3& Float3::operator*=(const Float3& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    z *= rhs.z;
    return *this;
}

constexpr Float3& Float3::operator/=(const Float3& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    z /= rhs.z;
    return *this;
}

constexpr Float3& Float3::operator*=(float scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

constexpr Float3& Float3::operator/=(float scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

constexpr bool Float3::operator==(const Float3& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y && z == rhs.z;
}

constexpr bool Float3::operator!=(const Float3& rhs) const noexcept
{
    return !(*this == rhs);
}

// ================================
// Float4 implementations
// ================================
constexpr inline Float4 Float4::operator+(const Float4& rhs) const noexcept
{
    return Float4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
}

constexpr inline Float4 Float4::operator-(const Float4& rhs) const noexcept
{
    return Float4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
}

constexpr inline Float4 Float4::operator*(const Float4& rhs) const noexcept
{
    return Float4(x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w);
}

constexpr inline Float4 Float4::operator/(const Float4& rhs) const noexcept
{
    return Float4(x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w);
}

constexpr inline Float4 Float4::operator*(float scalar) const noexcept
{
    return Float4(x * scalar, y * scalar, z * scalar, w * scalar);
}

constexpr inline Float4 Float4::operator/(float scalar) const noexcept
{
    return Float4(x / scalar, y / scalar, z / scalar, w / scalar);
}

constexpr inline Float4 Float4::operator-() const noexcept
{
    return Float4(-x, -y, -z, -w);
}

constexpr Float4& Float4::operator+=(const Float4& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
}

constexpr Float4& Float4::operator-=(const Float4& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
}

constexpr Float4& Float4::operator*=(const Float4& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    z *= rhs.z;
    w *= rhs.w;
    return *this;
}

constexpr Float4& Float4::operator/=(const Float4& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    z /= rhs.z;
    w /= rhs.w;
    return *this;
}

constexpr Float4& Float4::operator*=(float scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

constexpr Float4& Float4::operator/=(float scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

constexpr bool Float4::operator==(const Float4& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
}

constexpr bool Float4::operator!=(const Float4& rhs) const noexcept
{
    return !(*this == rhs);
}

// ================================
// Free Function Implementations (storage types - portable)
// ================================

constexpr Float2 operator*(float scalar, const Float2& vec) noexcept
{
    return vec * scalar;
}

constexpr Float3 operator*(float scalar, const Float3& vec) noexcept
{
    return vec * scalar;
}

constexpr Float4 operator*(float scalar, const Float4& vec) noexcept
{
    return vec * scalar;
}

// ================================
// Matrix3x3 Storage Implementation
// ================================

constexpr Float3x3::Float3x3(const Float4x4& mat4x4) noexcept
    : m{
        {mat4x4[0, 0], mat4x4[0, 1], mat4x4[0, 2]},
        {mat4x4[1, 0], mat4x4[1, 1], mat4x4[1, 2]},
        {mat4x4[2, 0], mat4x4[2, 1], mat4x4[2, 2]}
    }
{
}

constexpr Float3 Float3x3::Row(size_t index) const noexcept
{
    return Float3(m[index][0], m[index][1], m[index][2]);
}

constexpr void Float3x3::SetRow(size_t index, const Float3& row) noexcept
{
    m[index][0] = row.x;
    m[index][1] = row.y;
    m[index][2] = row.z;
}

constexpr Float3 Float3x3::Column(size_t index) const noexcept
{
    return Float3(m[0][index], m[1][index], m[2][index]);
}

constexpr void Float3x3::SetColumn(size_t index, const Float3& column) noexcept
{
    m[0][index] = column.x;
    m[1][index] = column.y;
    m[2][index] = column.z;
}

constexpr bool Float3x3::operator==(const Float3x3& rhs) const noexcept
{
    return m[0][0] == rhs.m[0][0] && m[0][1] == rhs.m[0][1] && m[0][2] == rhs.m[0][2] &&
            m[1][0] == rhs.m[1][0] && m[1][1] == rhs.m[1][1] && m[1][2] == rhs.m[1][2] &&
            m[2][0] == rhs.m[2][0] && m[2][1] == rhs.m[2][1] && m[2][2] == rhs.m[2][2];
}

constexpr bool Float3x3::operator!=(const Float3x3& rhs) const noexcept
{
    return !(*this == rhs);
}

constexpr Float3x3 Float3x3::Identity() noexcept
{
    return Float3x3();
}

constexpr Float3x3 Float3x3::Zero() noexcept
{
    return Float3x3(
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    );
}

// ================================
// Matrix4x3 Storage Implementation
// ================================

constexpr Float3 Float4x3::Row(size_t index) const noexcept
{
    return Float3(m[index][0], m[index][1], m[index][2]);
}

constexpr void Float4x3::SetRow(size_t index, const Float3& row) noexcept
{
    m[index][0] = row.x;
    m[index][1] = row.y;
    m[index][2] = row.z;
}

constexpr Float4 Float4x3::Column(size_t index) const noexcept
{
    return Float4(m[0][index], m[1][index], m[2][index], m[3][index]);
}

constexpr void Float4x3::SetColumn(size_t index, const Float4& column) noexcept
{
    m[0][index] = column.x;
    m[1][index] = column.y;
    m[2][index] = column.z;
    m[3][index] = column.w;
}

constexpr bool Float4x3::operator==(const Float4x3& rhs) const noexcept
{
    return m[0][0] == rhs.m[0][0] && m[0][1] == rhs.m[0][1] && m[0][2] == rhs.m[0][2] &&
            m[1][0] == rhs.m[1][0] && m[1][1] == rhs.m[1][1] && m[1][2] == rhs.m[1][2] &&
            m[2][0] == rhs.m[2][0] && m[2][1] == rhs.m[2][1] && m[2][2] == rhs.m[2][2] &&
            m[3][0] == rhs.m[3][0] && m[3][1] == rhs.m[3][1] && m[3][2] == rhs.m[3][2];
}

constexpr bool Float4x3::operator!=(const Float4x3& rhs) const noexcept
{
    return !(*this == rhs);
}

constexpr Float4x3 Float4x3::Identity() noexcept
{
    return Float4x3();
}

constexpr Float4x3 Float4x3::Zero() noexcept
{
    return Float4x3(
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    );
}

// ================================
// Matrix4x4 Storage Implementation
// ================================

constexpr Float4x4::Float4x4(const Float3x3& mat3x3) noexcept
    : m{
        {mat3x3[0, 0], mat3x3[0, 1], mat3x3[0, 2], 0.0f},
        {mat3x3[1, 0], mat3x3[1, 1], mat3x3[1, 2], 0.0f},
        {mat3x3[2, 0], mat3x3[2, 1], mat3x3[2, 2], 0.0f},
        {0.0f,         0.0f,         0.0f,         1.0f}
    }
{
}

constexpr Float4 Float4x4::Row(size_t index) const noexcept
{
    return Float4(m[index][0], m[index][1], m[index][2], m[index][3]);
}

constexpr void Float4x4::SetRow(size_t index, const Float4& row) noexcept
{
    m[index][0] = row.x;
    m[index][1] = row.y;
    m[index][2] = row.z;
    m[index][3] = row.w;
}

constexpr Float4 Float4x4::Column(size_t index) const noexcept
{
    return Float4(m[0][index], m[1][index], m[2][index], m[3][index]);
}

constexpr void Float4x4::SetColumn(size_t index, const Float4& column) noexcept
{
    m[0][index] = column.x;
    m[1][index] = column.y;
    m[2][index] = column.z;
    m[3][index] = column.w;
}

constexpr bool Float4x4::operator==(const Float4x4& rhs) const noexcept
{
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            if (m[i][j] != rhs.m[i][j])
            {
                return false;
            }
        }
    }
    return true;
}

constexpr bool Float4x4::operator!=(const Float4x4& rhs) const noexcept
{
    return !(*this == rhs);
}

constexpr Float4x4 Float4x4::Identity() noexcept
{
    return Float4x4();
}

constexpr Float4x4 Float4x4::Zero() noexcept
{
    return Float4x4(
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f
    );
}

// The proxy carries the Vector until a conversion operator picks the destination width; those
// operators need SIMD and live in the backend .inl files
VX_MATH_FORCEINLINE detail::FromVectorProxy FromVector(Vector vec) noexcept
{
    return detail::FromVectorProxy{ vec };
}

// Scalar on purpose: one value in, one out, and a splat-then-extract round trip would cost more than
// the arithmetic. Coefficients come from math/MathPolynomials.hpp, shared with the SIMD paths.

constexpr VX_MATH_FORCEINLINE float DegreesToRadians(float degrees) noexcept
{
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

constexpr VX_MATH_FORCEINLINE float RadiansToDegrees(float radians) noexcept
{
    return radians * (180.0f / std::numbers::pi_v<float>);
}

constexpr VX_MATH_FORCEINLINE float Lerp(float from, float to, float t) noexcept
{
    return from + (to - from) * t;
}

constexpr VX_MATH_FORCEINLINE float Clamp(float value, float min, float max) noexcept
{
    return value < min ? min : (value > max ? max : value);
}

constexpr VX_MATH_FORCEINLINE float Saturate(float value) noexcept
{
    return Clamp(value, 0.0f, 1.0f);
}

// x - 2pi * round(x / 2pi). Round-to-nearest, not truncation: truncating leaves the negative half
// unreduced
constexpr VX_MATH_FORCEINLINE float ModAngles(float radians) noexcept
{
    constexpr float k_TwoPi = 2.0f * std::numbers::pi_v<float>;
    constexpr float k_ReciprocalTwoPi = 1.0f / k_TwoPi;

    const float revolutions = radians * k_ReciprocalTwoPi;
    // hand-rolled because std::round is not reliably constexpr; equivalent for any angle magnitude
    const float rounded = static_cast<float>(static_cast<int32_t>(revolutions + (revolutions < 0.0f ? -0.5f : 0.5f)));
    return radians - k_TwoPi * rounded;
}

namespace detail
{
    // Scalar twin of ReduceForTrig in the backend files, which has to use Select where this branches
    struct ScalarTrigReduction
    {
        float angle;
        float cosSign;
    };

    constexpr VX_MATH_FORCEINLINE ScalarTrigReduction ReduceForTrigScalar(float radians) noexcept
    {
        constexpr float k_Pi = std::numbers::pi_v<float>;
        constexpr float k_HalfPi = k_Pi * 0.5f;

        const float wrapped = ModAngles(radians);
        const float magnitude = wrapped < 0.0f ? -wrapped : wrapped;
        if (magnitude <= k_HalfPi)
        {
            return ScalarTrigReduction{ wrapped, 1.0f };
        }

        // sin(pi - x) == sin(x), cos(pi - x) == -cos(x)
        const float signedPi = wrapped < 0.0f ? -k_Pi : k_Pi;
        return ScalarTrigReduction{ signedPi - wrapped, -1.0f };
    }

    constexpr VX_MATH_FORCEINLINE float SinPolynomialScalar(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_SinPoly11;
        polynomial = polynomial * squared + k_SinPoly9;
        polynomial = polynomial * squared + k_SinPoly7;
        polynomial = polynomial * squared + k_SinPoly5;
        polynomial = polynomial * squared + k_SinPoly3;
        polynomial = polynomial * squared + 1.0f;
        return polynomial * angle;
    }

    constexpr VX_MATH_FORCEINLINE float CosPolynomialScalar(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_CosPoly10;
        polynomial = polynomial * squared + k_CosPoly8;
        polynomial = polynomial * squared + k_CosPoly6;
        polynomial = polynomial * squared + k_CosPoly4;
        polynomial = polynomial * squared + k_CosPoly2;
        return polynomial * squared + 1.0f;
    }

    constexpr VX_MATH_FORCEINLINE float SinPolynomialScalarEst(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_SinEstPoly7;
        polynomial = polynomial * squared + k_SinEstPoly5;
        polynomial = polynomial * squared + k_SinEstPoly3;
        polynomial = polynomial * squared + 1.0f;
        return polynomial * angle;
    }

    constexpr VX_MATH_FORCEINLINE float CosPolynomialScalarEst(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_CosEstPoly6;
        polynomial = polynomial * squared + k_CosEstPoly4;
        polynomial = polynomial * squared + k_CosEstPoly2;
        return polynomial * squared + 1.0f;
    }
} // namespace detail

VX_MATH_FORCEINLINE ScalarSinCos SinCos(float radians) noexcept
{
    const detail::ScalarTrigReduction reduction = detail::ReduceForTrigScalar(radians);
    return ScalarSinCos{ detail::SinPolynomialScalar(reduction.angle),
                         detail::CosPolynomialScalar(reduction.angle) * reduction.cosSign };
}

VX_MATH_FORCEINLINE ScalarSinCos SinCosEst(float radians) noexcept
{
    const detail::ScalarTrigReduction reduction = detail::ReduceForTrigScalar(radians);
    return ScalarSinCos{ detail::SinPolynomialScalarEst(reduction.angle),
                         detail::CosPolynomialScalarEst(reduction.angle) * reduction.cosSign };
}

VX_MATH_FORCEINLINE float Sin(float radians) noexcept
{
    return detail::SinPolynomialScalar(detail::ReduceForTrigScalar(radians).angle);
}

VX_MATH_FORCEINLINE float Cos(float radians) noexcept
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
    polynomial = polynomial * fraction + detail::k_Exp2Poly5;
    polynomial = polynomial * fraction + detail::k_Exp2Poly4;
    polynomial = polynomial * fraction + detail::k_Exp2Poly3;
    polynomial = polynomial * fraction + detail::k_Exp2Poly2;
    polynomial = polynomial * fraction + detail::k_Exp2Poly1;
    polynomial = polynomial * fraction + 1.0f;

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

    constexpr float k_Sqrt2 = 1.41421356f;
    if (mantissa > k_Sqrt2)
    {
        mantissa *= 0.5f;
        exponent += 1.0f;
    }

    const float ratio = (mantissa - 1.0f) / (mantissa + 1.0f);
    const float ratioSquared = ratio * ratio;

    float series = detail::k_Log2Poly7;
    series = series * ratioSquared + detail::k_Log2Poly5;
    series = series * ratioSquared + detail::k_Log2Poly3;
    series = series * ratioSquared + detail::k_Log2Poly1;

    return exponent + series * ratio * detail::k_Log2Scale;
}

// ================================
// Storage-Matrix * Storage-Vector free functions
// ================================
// Row-vector convention throughout (result = vec * mat), consistent with
// the SIMD Matrix type. NONE of these five free functions had an
// implementation in the original .inl (declared in the header only) -
// implemented here under the documented assumptions below; please
// double-check them against your intended usage before relying on them.

// Pure linear transform (rotation/scale, no translation): Float3 in, Float3 out.
constexpr Float3 operator*(const Float3x3& mat, const Float3& vec) noexcept
{
    return Float3(
        vec.x * mat[0, 0] + vec.y * mat[1, 0] + vec.z * mat[2, 0],
        vec.x * mat[0, 1] + vec.y * mat[1, 1] + vec.z * mat[2, 1],
        vec.x * mat[0, 2] + vec.y * mat[1, 2] + vec.z * mat[2, 2]
    );
}

// Assumption: `vec` is a point (implicit w=1); rows 0-2 are the linear part,
// row 3 is translation. Returns the transformed point with w=1 so it can be
// chained into further homogeneous math.
constexpr Float4 operator*(const Float4x3& mat, const Float3& vec) noexcept
{
    return Float4(
        vec.x * mat[0, 0] + vec.y * mat[1, 0] + vec.z * mat[2, 0] + mat[3, 0],
        vec.x * mat[0, 1] + vec.y * mat[1, 1] + vec.z * mat[2, 1] + mat[3, 1],
        vec.x * mat[0, 2] + vec.y * mat[1, 2] + vec.z * mat[2, 2] + mat[3, 2],
        1.0f
    );
}

// Assumption: Float4x3 is treated as a 4x4 matrix with an implicit last
// column of (0, 0, 0, 1) - i.e. rows 0-2 are the linear part scaled by
// vec.xyz, row 3 (translation) is scaled by vec.w, and vec.w passes
// through unchanged into the result's w (since the implicit column only
// contributes a "1" against w).
constexpr Float4 operator*(const Float4x3& mat, const Float4& vec) noexcept
{
    return Float4(
        vec.x * mat[0, 0] + vec.y * mat[1, 0] + vec.z * mat[2, 0] + vec.w * mat[3, 0],
        vec.x * mat[0, 1] + vec.y * mat[1, 1] + vec.z * mat[2, 1] + vec.w * mat[3, 1],
        vec.x * mat[0, 2] + vec.y * mat[1, 2] + vec.z * mat[2, 2] + vec.w * mat[3, 2],
        vec.w
    );
}

// Standard full homogeneous transform.
constexpr Float4 operator*(const Float4x4& mat, const Float4& vec) noexcept
{
    return Float4(
        vec.x * mat[0, 0] + vec.y * mat[1, 0] + vec.z * mat[2, 0] + vec.w * mat[3, 0],
        vec.x * mat[0, 1] + vec.y * mat[1, 1] + vec.z * mat[2, 1] + vec.w * mat[3, 1],
        vec.x * mat[0, 2] + vec.y * mat[1, 2] + vec.z * mat[2, 2] + vec.w * mat[3, 2],
        vec.x * mat[0, 3] + vec.y * mat[1, 3] + vec.z * mat[2, 3] + vec.w * mat[3, 3]
    );
}

// Assumption: `vec` is a point (implicit w=1); result drops w with NO
// perspective divide (i.e. this assumes an affine matrix - use the SIMD
// Matrix/Vector path's Transform<3>() if you need true perspective-correct
// TransformCoord semantics).
constexpr Float3 operator*(const Float4x4& mat, const Float3& vec) noexcept
{
    return Float3(
        vec.x * mat[0, 0] + vec.y * mat[1, 0] + vec.z * mat[2, 0] + mat[3, 0],
        vec.x * mat[0, 1] + vec.y * mat[1, 1] + vec.z * mat[2, 1] + mat[3, 1],
        vec.x * mat[0, 2] + vec.y * mat[1, 2] + vec.z * mat[2, 2] + mat[3, 2]
    );
}

VX_MATH_FORCEINLINE float Tan(float radians) noexcept
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
    polynomial = polynomial * squared + detail::k_ATanPoly15;
    polynomial = polynomial * squared + detail::k_ATanPoly13;
    polynomial = polynomial * squared + detail::k_ATanPoly11;
    polynomial = polynomial * squared + detail::k_ATanPoly9;
    polynomial = polynomial * squared + detail::k_ATanPoly7;
    polynomial = polynomial * squared + detail::k_ATanPoly5;
    polynomial = polynomial * squared + detail::k_ATanPoly3;
    polynomial = polynomial * squared + 1.0f;
    const float inner = polynomial * reduced;

    if (magnitude <= 1.0f)
    {
        return inner;
    }
    return (value < 0.0f ? -k_HalfPi : k_HalfPi) - inner;
}

VX_MATH_FORCEINLINE float ATan2(float y, float x) noexcept
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

VX_MATH_FORCEINLINE float ASin(float value) noexcept
{
    return ATan2(value, std::sqrt(1.0f - value * value));
}

VX_MATH_FORCEINLINE float ACos(float value) noexcept
{
    return ATan2(std::sqrt(1.0f - value * value), value);
}

VX_MATH_FORCEINLINE float Pow(float base, float exponent) noexcept
{
    return Exp2(exponent * Log2(base));
}

// 1 - 2 / (e^2x + 1); saturates to 1 rather than producing inf/inf where the difference form would
VX_MATH_FORCEINLINE float TanH(float value) noexcept
{
    return 1.0f - 2.0f / (Exp2(2.0f * value * detail::k_Log2OfE) + 1.0f);
}

// Shortest arc: q and -q are the same rotation, so a negative dot means the other representation is
// nearer and negating one endpoint avoids interpolating the long way round. Near-parallel endpoints
// fall back to a normalized lerp, where sin(theta) is too small to divide by.
VX_MATH_FORCEINLINE Quaternion Quaternion::Slerp(Quaternion from, Quaternion to, float t) noexcept
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
