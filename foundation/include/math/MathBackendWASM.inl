#pragma once
// WASM SIMD128 backend for math::Vector / math::Matrix. Included by Math.inl when
// VX_MATH_BACKEND_WASM is 1 (Emscripten builds, or a forced override).
// Requires <wasm_simd128.h>, <cmath>, and <limits>, already included by Math.hpp.
// Do not include Math.hpp/Math.inl from here: this file is reached *through* them, and re-entering
// that cycle would parse every definition below twice (the pragma above only takes effect once the
// nested inclusion has already completed). Match MathBackendDX.inl - include nothing.
namespace velox::math
{
namespace detail
{
    // a * b + c
    VX_MATH_FORCEINLINE v128_t MulAdd(v128_t a, v128_t b, v128_t c) noexcept
    {
#if defined(VX_MATH_RELAXED_SIMD)
        return wasm_f32x4_relaxed_madd(a, b, c);
#else
        return wasm_f32x4_add(wasm_f32x4_mul(a, b), c);
#endif
    }

    // c - a * b
    VX_MATH_FORCEINLINE v128_t NegMulAdd(v128_t a, v128_t b, v128_t c) noexcept
    {
#if defined(VX_MATH_RELAXED_SIMD)
        return wasm_f32x4_relaxed_nmadd(a, b, c);
#else
        return wasm_f32x4_sub(c, wasm_f32x4_mul(a, b));
#endif
    }

    inline float Det3x3(
        float a1, float a2, float a3, float b1, float b2, float b3, float c1, float c2, float c3) noexcept
    {
        return a1 * (b2 * c3 - b3 * c2) - a2 * (b1 * c3 - b3 * c1) + a3 * (b1 * c2 - b2 * c1);
    }

    inline float Determinant4x4(const float m[4][4]) noexcept
    {
        return m[0][0] *
                   Det3x3(m[1][1], m[1][2], m[1][3], m[2][1], m[2][2], m[2][3], m[3][1], m[3][2], m[3][3]) -
               m[0][1] *
                   Det3x3(m[1][0], m[1][2], m[1][3], m[2][0], m[2][2], m[2][3], m[3][0], m[3][2], m[3][3]) +
               m[0][2] *
                   Det3x3(m[1][0], m[1][1], m[1][3], m[2][0], m[2][1], m[2][3], m[3][0], m[3][1], m[3][3]) -
               m[0][3] *
                   Det3x3(m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2], m[3][0], m[3][1], m[3][2]);
    }
} // namespace detail

// ================================
// VectorMask Implementation (WASM SIMD128)
// ================================

VX_MATH_FORCEINLINE VectorMask::VectorMask() noexcept
    : data{ wasm_i32x4_const_splat(0) }
{
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator&(VectorMask rhs) const noexcept
{
    return VectorMask{ wasm_v128_and(data, rhs.data) };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator|(VectorMask rhs) const noexcept
{
    return VectorMask{ wasm_v128_or(data, rhs.data) };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator^(VectorMask rhs) const noexcept
{
    return VectorMask{ wasm_v128_xor(data, rhs.data) };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator~() const noexcept
{
    return VectorMask{ wasm_v128_not(data) };
}

VX_MATH_FORCEINLINE VectorMask& VectorMask::operator&=(VectorMask rhs) noexcept
{
    data = wasm_v128_and(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE VectorMask& VectorMask::operator|=(VectorMask rhs) noexcept
{
    data = wasm_v128_or(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE VectorMask& VectorMask::operator^=(VectorMask rhs) noexcept
{
    data = wasm_v128_xor(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE uint32_t VectorMask::LaneBits() const noexcept
{
    return static_cast<uint32_t>(wasm_i32x4_bitmask(data));
}

// Via the lane bitmask, not wasm_i32x4_all_true: that always tests all four lanes and N may be 2 or 3
namespace detail
{
    template<int N>
    constexpr uint32_t LaneBitsFor() noexcept
    {
        static_assert(N >= 2 && N <= 4, "Mask width must be 2, 3, or 4");
        return (1u << N) - 1u;
    }
} // namespace detail

template<int N>
VX_MATH_FORCEINLINE bool VectorMask::AllTrue() const noexcept
{
    return (LaneBits() & detail::LaneBitsFor<N>()) == detail::LaneBitsFor<N>();
}

template<int N>
VX_MATH_FORCEINLINE bool VectorMask::AnyTrue() const noexcept
{
    return (LaneBits() & detail::LaneBitsFor<N>()) != 0u;
}

VX_MATH_FORCEINLINE VectorMask VectorMask::AllSet() noexcept
{
    return VectorMask{ wasm_i32x4_const_splat(-1) };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::AllClear() noexcept
{
    return VectorMask{ wasm_i32x4_const_splat(0) };
}

// ================================
// Vector SIMD Implementation (WASM SIMD128)
// ================================

VX_MATH_FORCEINLINE Vector::Vector() noexcept
    : data{ wasm_f32x4_splat(0.0f) }
{
}

VX_MATH_FORCEINLINE Vector::Vector(float x, float y, float z, float w) noexcept
    : data{ wasm_f32x4_make(x, y, z, w) }
{
}

VX_MATH_FORCEINLINE Vector::Vector(float x, float y, float z) noexcept
    : data{ wasm_f32x4_make(x, y, z, 0.0f) }
{
}

VX_MATH_FORCEINLINE Vector::Vector(float x, float y) noexcept
    : data{ wasm_f32x4_make(x, y, 0.0f, 0.0f) }
{
}

VX_MATH_FORCEINLINE Vector::Vector(float scalar) noexcept
    : data{ wasm_f32x4_splat(scalar) }
{
}

// Component accessors
VX_MATH_FORCEINLINE float Vector::x() const noexcept
{
    return wasm_f32x4_extract_lane(data, 0);
}
VX_MATH_FORCEINLINE float Vector::y() const noexcept
{
    return wasm_f32x4_extract_lane(data, 1);
}
VX_MATH_FORCEINLINE float Vector::z() const noexcept
{
    return wasm_f32x4_extract_lane(data, 2);
}
VX_MATH_FORCEINLINE float Vector::w() const noexcept
{
    return wasm_f32x4_extract_lane(data, 3);
}

// Static factory methods
VX_MATH_FORCEINLINE Vector Vector::Zero() noexcept
{
    return Vector{ wasm_f32x4_splat(0.0f) };
}
VX_MATH_FORCEINLINE Vector Vector::Replicate(float scalar) noexcept
{
    return Vector{ wasm_f32x4_splat(scalar) };
}
VX_MATH_FORCEINLINE Vector Vector::Identity() noexcept
{
    return Vector{ wasm_f32x4_splat(1.0f) };
}

// Arithmetic operators
VX_MATH_FORCEINLINE Vector Vector::operator+(Vector rhs) const noexcept
{
    return Vector{ wasm_f32x4_add(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator-(Vector rhs) const noexcept
{
    return Vector{ wasm_f32x4_sub(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator*(Vector rhs) const noexcept
{
    return Vector{ wasm_f32x4_mul(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator/(Vector rhs) const noexcept
{
    return Vector{ wasm_f32x4_div(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator*(float scalar) const noexcept
{
    return Vector{ wasm_f32x4_mul(data, wasm_f32x4_splat(scalar)) };
}
VX_MATH_FORCEINLINE Vector Vector::operator/(float scalar) const noexcept
{
    return Vector{ wasm_f32x4_div(data, wasm_f32x4_splat(scalar)) };
}
VX_MATH_FORCEINLINE Vector Vector::operator-() const noexcept
{
    return Vector{ wasm_f32x4_neg(data) };
}

VX_MATH_FORCEINLINE Vector Vector::MultiplyAdd(Vector factor, Vector addend) const noexcept
{
#if defined(VX_MATH_RELAXED_SIMD)
    return Vector{ wasm_f32x4_relaxed_madd(data, factor.data, addend.data) };
#else
    return Vector{ wasm_f32x4_add(wasm_f32x4_mul(data, factor.data), addend.data) };
#endif
}

// Compound assignment operators
VX_MATH_FORCEINLINE Vector& Vector::operator+=(Vector rhs) noexcept
{
    data = wasm_f32x4_add(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator-=(Vector rhs) noexcept
{
    data = wasm_f32x4_sub(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator*=(Vector rhs) noexcept
{
    data = wasm_f32x4_mul(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator/=(Vector rhs) noexcept
{
    data = wasm_f32x4_div(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator*=(float scalar) noexcept
{
    data = wasm_f32x4_mul(data, wasm_f32x4_splat(scalar));
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator/=(float scalar) noexcept
{
    data = wasm_f32x4_div(data, wasm_f32x4_splat(scalar));
    return *this;
}

VX_MATH_FORCEINLINE Vector Vector::Reciprocal() const noexcept
{
    return Vector{ wasm_f32x4_div(wasm_f32x4_splat(1.0f), data) };
}
VX_MATH_FORCEINLINE Vector Vector::Sqrt() const noexcept
{
    return Vector{ wasm_f32x4_sqrt(data) };
}
VX_MATH_FORCEINLINE Vector Vector::ReciprocalSqrt() const noexcept
{
    return Vector{ wasm_f32x4_div(wasm_f32x4_splat(1.0f), wasm_f32x4_sqrt(data)) };
}

// Full-width butterfly, not a port of DirectXMath's _mm_add_ss sequences - wasm has no single-lane
// add, and the butterfly leaves the result splatted, which is what callers want anyway. Lanes above N
// are masked first: unused lanes are ignored regardless of what garbage they hold.
namespace detail
{
    // two shuffle/add pairs sum all four lanes, total ending up in every one
    VX_MATH_FORCEINLINE v128_t HorizontalSum4(v128_t vec) noexcept
    {
        vec = wasm_f32x4_add(vec, wasm_i32x4_shuffle(vec, vec, 2, 3, 0, 1));
        return wasm_f32x4_add(vec, wasm_i32x4_shuffle(vec, vec, 1, 0, 3, 2));
    }
} // namespace detail

template<>
VX_MATH_FORCEINLINE Vector Vector::DotVec<2>(Vector other) const noexcept
{
    const v128_t products = wasm_f32x4_mul(data, other.data);
    const v128_t zero = wasm_f32x4_splat(0.0f);
    // keep x and y, force z and w to zero
    const v128_t masked = wasm_i32x4_shuffle(products, zero, 0, 1, 4, 5);
    return Vector{ detail::HorizontalSum4(masked) };
}

template<>
VX_MATH_FORCEINLINE float Vector::Dot<2>(Vector other) const noexcept
{
    return wasm_f32x4_extract_lane(DotVec<2>(other).data, 0);
}

template<>
VX_MATH_FORCEINLINE Vector Vector::DotVec<3>(Vector other) const noexcept
{
    const v128_t products = wasm_f32x4_mul(data, other.data);
    const v128_t zero = wasm_f32x4_splat(0.0f);
    // keep x, y and z, force w to zero
    const v128_t masked = wasm_i32x4_shuffle(products, zero, 0, 1, 2, 4);
    return Vector{ detail::HorizontalSum4(masked) };
}

template<>
VX_MATH_FORCEINLINE float Vector::Dot<3>(Vector other) const noexcept
{
    return wasm_f32x4_extract_lane(DotVec<3>(other).data, 0);
}

template<>
VX_MATH_FORCEINLINE Vector Vector::DotVec<4>(Vector other) const noexcept
{
    return Vector{ detail::HorizontalSum4(wasm_f32x4_mul(data, other.data)) };
}

template<>
VX_MATH_FORCEINLINE float Vector::Dot<4>(Vector other) const noexcept
{
    return wasm_f32x4_extract_lane(DotVec<4>(other).data, 0);
}

template<int N>
VX_MATH_FORCEINLINE float Vector::LengthSq() const noexcept
{
    static_assert(N >= 2 && N <= 4, "LengthSq dimensionality must be 2, 3, or 4");
    return Dot<N>(*this);
}

template<int N>
VX_MATH_FORCEINLINE float Vector::Length() const noexcept
{
    static_assert(N >= 2 && N <= 4, "Length dimensionality must be 2, 3, or 4");
    return std::sqrt(Dot<N>(*this));
}

// DotVec splats the squared length, so the divide stays in the vector domain - no lane extraction,
// no scalar sqrt. Zero length yields infinities rather than being special-cased.
template<int N>
VX_MATH_FORCEINLINE Vector Vector::Normalize() const noexcept
{
    static_assert(N >= 2 && N <= 4, "Normalize dimensionality must be 2, 3, or 4");
    const v128_t lengthSq = DotVec<N>(*this).Data();
    return Vector{ wasm_f32x4_div(data, wasm_f32x4_sqrt(lengthSq)) };
}

VX_MATH_FORCEINLINE Vector Vector::Cross(Vector other) const noexcept
{
    // we might be able to get this to use less registers?
    v128_t vYZXW = wasm_i32x4_shuffle(data, data, 1, 2, 0, 3);
    v128_t vOtherZXYW = wasm_i32x4_shuffle(other.data, other.data, 2, 0, 1, 3);
    v128_t vSelfZXYW = wasm_i32x4_shuffle(data, data, 2, 0, 1, 3);
    v128_t vOtherYZXW = wasm_i32x4_shuffle(other.data, other.data, 1, 2, 0, 3);
    vYZXW = wasm_f32x4_mul(vYZXW, vOtherZXYW);
    vSelfZXYW = wasm_f32x4_mul(vSelfZXYW, vOtherYZXW);
    return Vector{ wasm_f32x4_sub(vYZXW, vSelfZXYW) };
}

VX_MATH_FORCEINLINE Vector Vector::Lerp(Vector target, float t) const noexcept
{
    const v128_t diff = wasm_f32x4_sub(target.data, data);
#if defined(VX_MATH_RELAXED_SIMD)
    return Vector{ wasm_f32x4_relaxed_madd(diff, wasm_f32x4_splat(t), data) };
#else
    return Vector{ wasm_f32x4_add(data, wasm_f32x4_mul(diff, wasm_f32x4_splat(t))) };
#endif
}

// incident - 2 * dot(incident, normal) * normal. relaxed_nmadd is -(a * b) + c, i.e. this in one op
template<int N>
VX_MATH_FORCEINLINE Vector Vector::Reflect(Vector normal) const noexcept
{
    static_assert(N >= 2 && N <= 4, "Reflect dimensionality must be 2, 3, or 4");
    const v128_t scaledDot = wasm_f32x4_mul(DotVec<N>(normal).Data(), wasm_f32x4_splat(2.0f));
#if defined(VX_MATH_RELAXED_SIMD)
    return Vector{ wasm_f32x4_relaxed_nmadd(normal.data, scaledDot, data) };
#else
    return Vector{ wasm_f32x4_sub(data, wasm_f32x4_mul(normal.data, scaledDot)) };
#endif
}

// Not wasm_f32x4_min/max: those propagate NaN per IEEE-754 and lower to several instructions.
// relaxed_min/max and pmin/pmax give the C-style "a < b ? a : b" that graphics code wants.
//
// Consequence: NaN and signed-zero handling is unspecified here, and differs again on DirectXMath.
// None of Min/Max/Clamp/Saturate is safe for NaN scrubbing - use IsNaN() and Select() for that.
namespace detail
{
    VX_MATH_FORCEINLINE v128_t FastMin(v128_t a, v128_t b) noexcept
    {
#if defined(VX_MATH_RELAXED_SIMD)
        return wasm_f32x4_relaxed_min(a, b);
#else
        return wasm_f32x4_pmin(a, b);
#endif
    }

    VX_MATH_FORCEINLINE v128_t FastMax(v128_t a, v128_t b) noexcept
    {
#if defined(VX_MATH_RELAXED_SIMD)
        return wasm_f32x4_relaxed_max(a, b);
#else
        return wasm_f32x4_pmax(a, b);
#endif
    }
} // namespace detail

VX_MATH_FORCEINLINE Vector Vector::Clamp(Vector min, Vector max) const noexcept
{
    return Vector{ detail::FastMin(detail::FastMax(data, min.data), max.data) };
}

VX_MATH_FORCEINLINE Vector Vector::Saturate() const noexcept
{
    return Vector{ detail::FastMin(detail::FastMax(data, wasm_f32x4_const_splat(0.0f)),
                                   wasm_f32x4_const_splat(1.0f)) };
}

VX_MATH_FORCEINLINE Vector Vector::Abs() const noexcept
{
    return Vector{ wasm_f32x4_abs(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Min(Vector other) const noexcept
{
    return Vector{ detail::FastMin(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::Max(Vector other) const noexcept
{
    return Vector{ detail::FastMax(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::Infinity() noexcept
{
    return Vector{ wasm_f32x4_const_splat(std::numeric_limits<float>::infinity()) };
}

VX_MATH_FORCEINLINE Vector Vector::QuietNaN() noexcept
{
    return Vector{ wasm_f32x4_const_splat(std::numeric_limits<float>::quiet_NaN()) };
}

VX_MATH_FORCEINLINE Vector Vector::Epsilon() noexcept
{
    return Vector{ wasm_f32x4_const_splat(std::numeric_limits<float>::epsilon()) };
}


VX_MATH_FORCEINLINE VectorMask Vector::CompareEqual(Vector other) const noexcept
{
    return VectorMask{ wasm_f32x4_eq(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareNotEqual(Vector other) const noexcept
{
    return VectorMask{ wasm_f32x4_ne(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareLess(Vector other) const noexcept
{
    return VectorMask{ wasm_f32x4_lt(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareLessOrEqual(Vector other) const noexcept
{
    return VectorMask{ wasm_f32x4_le(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareGreater(Vector other) const noexcept
{
    return VectorMask{ wasm_f32x4_gt(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareGreaterOrEqual(Vector other) const noexcept
{
    return VectorMask{ wasm_f32x4_ge(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareNearEqual(Vector other, Vector epsilon) const noexcept
{
    const v128_t difference = wasm_f32x4_abs(wasm_f32x4_sub(data, other.data));
    return VectorMask{ wasm_f32x4_le(difference, epsilon.data) };
}

// A NaN is the only value that compares unequal to itself
VX_MATH_FORCEINLINE VectorMask Vector::IsNaN() const noexcept
{
    return VectorMask{ wasm_f32x4_ne(data, data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::IsInfinite() const noexcept
{
    const v128_t magnitude = wasm_f32x4_abs(data);
    return VectorMask{ wasm_f32x4_eq(magnitude, wasm_f32x4_const_splat(std::numeric_limits<float>::infinity())) };
}


VX_MATH_FORCEINLINE Vector Vector::AndInt(Vector other) const noexcept
{
    return Vector{ wasm_v128_and(data, other.data) };
}

// Note the operand order: this clears the bits that `other` has set, i.e. `~other & this`
VX_MATH_FORCEINLINE Vector Vector::AndNotInt(Vector other) const noexcept
{
    return Vector{ wasm_v128_andnot(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::OrInt(Vector other) const noexcept
{
    return Vector{ wasm_v128_or(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::XorInt(Vector other) const noexcept
{
    return Vector{ wasm_v128_xor(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::NorInt(Vector other) const noexcept
{
    return Vector{ wasm_v128_not(wasm_v128_or(data, other.data)) };
}

// All four are single native instructions on wasm, where SSE needs SSE4.1's _mm_round_ps

VX_MATH_FORCEINLINE Vector Vector::Round() const noexcept
{
    return Vector{ wasm_f32x4_nearest(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Truncate() const noexcept
{
    return Vector{ wasm_f32x4_trunc(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Floor() const noexcept
{
    return Vector{ wasm_f32x4_floor(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Ceil() const noexcept
{
    return Vector{ wasm_f32x4_ceil(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Mod(Vector divisor) const noexcept
{
    const v128_t quotient = wasm_f32x4_trunc(wasm_f32x4_div(data, divisor.data));
    return Vector{ detail::NegMulAdd(quotient, divisor.data, data) };
}

// x - 2pi * round(x / 2pi). Round-to-nearest is what makes this land in [-pi, pi] rather than
// [0, 2pi) - truncation toward zero would leave the negative half unreduced
VX_MATH_FORCEINLINE Vector Vector::ModAngles() const noexcept
{
    constexpr float k_TwoPi = 2.0f * std::numbers::pi_v<float>;
    constexpr float k_ReciprocalTwoPi = 1.0f / k_TwoPi;

    const v128_t revolutions = wasm_f32x4_nearest(
        wasm_f32x4_mul(data, wasm_f32x4_const_splat(k_ReciprocalTwoPi)));
    return Vector{ detail::NegMulAdd(revolutions, wasm_f32x4_const_splat(k_TwoPi), data) };
}


VX_MATH_FORCEINLINE Vector Vector::SplatX() const noexcept
{
    return Vector{ wasm_i32x4_shuffle(data, data, 0, 0, 0, 0) };
}

VX_MATH_FORCEINLINE Vector Vector::SplatY() const noexcept
{
    return Vector{ wasm_i32x4_shuffle(data, data, 1, 1, 1, 1) };
}

VX_MATH_FORCEINLINE Vector Vector::SplatZ() const noexcept
{
    return Vector{ wasm_i32x4_shuffle(data, data, 2, 2, 2, 2) };
}

VX_MATH_FORCEINLINE Vector Vector::SplatW() const noexcept
{
    return Vector{ wasm_i32x4_shuffle(data, data, 3, 3, 3, 3) };
}

VX_MATH_FORCEINLINE Vector Vector::MergeXY(Vector other) const noexcept
{
    return Vector{ wasm_i32x4_shuffle(data, other.data, 0, 4, 1, 5) };
}

VX_MATH_FORCEINLINE Vector Vector::MergeZW(Vector other) const noexcept
{
    return Vector{ wasm_i32x4_shuffle(data, other.data, 2, 6, 3, 7) };
}

template<int X, int Y, int Z, int W>
VX_MATH_FORCEINLINE Vector Vector::Swizzle() const noexcept
{
    static_assert(X >= 0 && X <= 3 && Y >= 0 && Y <= 3 && Z >= 0 && Z <= 3 && W >= 0 && W <= 3,
                  "Swizzle lane indices must be 0-3");
    return Vector{ wasm_i32x4_shuffle(data, data, X, Y, Z, W) };
}

template<int X, int Y, int Z, int W>
VX_MATH_FORCEINLINE Vector Vector::Permute(Vector other) const noexcept
{
    static_assert(X >= 0 && X <= 7 && Y >= 0 && Y <= 7 && Z >= 0 && Z <= 7 && W >= 0 && W <= 7,
                  "Permute lane indices must be 0-7 (0-3 select this vector, 4-7 select the other)");
    return Vector{ wasm_i32x4_shuffle(data, other.data, X, Y, Z, W) };
}

// ================================
// Free Function Implementations (Vector)
// ================================

VX_MATH_FORCEINLINE Vector Select(VectorMask mask, Vector when_clear, Vector when_set) noexcept
{
#if defined(VX_MATH_RELAXED_SIMD)
    // relaxed_laneselect leaves non-canonical masks implementation-defined, which is fine: every
    // mask reaching here came out of a comparison instruction and is all-zeros or all-ones per lane
    return Vector{ wasm_i32x4_relaxed_laneselect(when_set.Data(), when_clear.Data(), mask.Data()) };
#else
    return Vector{ wasm_v128_bitselect(when_set.Data(), when_clear.Data(), mask.Data()) };
#endif
}

VX_MATH_FORCEINLINE Vector operator*(float scalar, Vector vec) noexcept
{
    return vec * scalar;
}

VX_MATH_FORCEINLINE Vector ToVector(const Float2& in) noexcept
{
    return Vector{ wasm_f32x4_make(in.x, in.y, 0.0f, 0.0f) };
}

VX_MATH_FORCEINLINE Vector ToVector(const Float3& in) noexcept
{
    return Vector{ wasm_f32x4_make(in.x, in.y, in.z, 0.0f) };
}

VX_MATH_FORCEINLINE Vector ToVector(const Float4& in) noexcept
{
    return Vector{ wasm_f32x4_make(in.x, in.y, in.z, in.w) };
}

namespace detail
{
    VX_MATH_FORCEINLINE FromVectorProxy::operator Float2() const noexcept
    {
        Float2 result;
        wasm_v128_store64_lane(&result, vec.Data(), 0);
        return result;
    }

    VX_MATH_FORCEINLINE FromVectorProxy::operator Float3() const noexcept
    {
        // there is no wasm_f32x4_store3_lane, so we have to extract each lane individually (unsurprisingly)
        // this is still potentially one less instruction than doing lane-by-lane stores though
        Float3 result;
        wasm_v128_store64_lane(&result, vec.Data(), 0);
        wasm_v128_store32_lane(&result.z, vec.Data(), 2);
        return result;
    }

    VX_MATH_FORCEINLINE FromVectorProxy::operator Float4() const noexcept
    {
        Float4 result;
        wasm_v128_store(&result, vec.Data());
        return result;
    }
} // namespace detail

// ================================
// SIMD Matrix Implementation (WASM SIMD128)
// ================================

VX_MATH_FORCEINLINE Matrix::Matrix() noexcept
    : data{ wasm_f32x4_make(1.0f, 0.0f, 0.0f, 0.0f),
            wasm_f32x4_make(0.0f, 1.0f, 0.0f, 0.0f),
            wasm_f32x4_make(0.0f, 0.0f, 1.0f, 0.0f),
            wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) }
{
}

VX_MATH_FORCEINLINE Matrix::Matrix(float m00,
                                   float m01,
                                   float m02,
                                   float m03,
                                   float m10,
                                   float m11,
                                   float m12,
                                   float m13,
                                   float m20,
                                   float m21,
                                   float m22,
                                   float m23,
                                   float m30,
                                   float m31,
                                   float m32,
                                   float m33) noexcept
    : data{ wasm_f32x4_make(m00, m01, m02, m03),
            wasm_f32x4_make(m10, m11, m12, m13),
            wasm_f32x4_make(m20, m21, m22, m23),
            wasm_f32x4_make(m30, m31, m32, m33) }
{
}

VX_MATH_FORCEINLINE Matrix::Matrix(Vector row0, Vector row1, Vector row2, Vector row3) noexcept
    : data{ row0.Data(), row1.Data(), row2.Data(), row3.Data() }
{
}

VX_MATH_FORCEINLINE Vector Matrix::GetRow(size_t index) const noexcept
{
    return Vector{ data[index] };
}

VX_MATH_FORCEINLINE void Matrix::SetRow(size_t index, Vector row) noexcept
{
    data[index] = row.Data();
}

// extract_lane/replace_lane encode the lane as an immediate, so it must be a compile-time constant.
// Dispatching over the four keeps the value in a register; indexing a local array instead cannot be
// promoted out of memory when the index is a runtime value, costing a store/load round trip per call.
namespace detail
{
    VX_MATH_FORCEINLINE float ExtractLane(v128_t vec, size_t lane) noexcept
    {
        switch (lane)
        {
        case 0:
            return wasm_f32x4_extract_lane(vec, 0);
        case 1:
            return wasm_f32x4_extract_lane(vec, 1);
        case 2:
            return wasm_f32x4_extract_lane(vec, 2);
        default:
            return wasm_f32x4_extract_lane(vec, 3);
        }
    }

    VX_MATH_FORCEINLINE v128_t ReplaceLane(v128_t vec, size_t lane, float value) noexcept
    {
        switch (lane)
        {
        case 0:
            return wasm_f32x4_replace_lane(vec, 0, value);
        case 1:
            return wasm_f32x4_replace_lane(vec, 1, value);
        case 2:
            return wasm_f32x4_replace_lane(vec, 2, value);
        default:
            return wasm_f32x4_replace_lane(vec, 3, value);
        }
    }
} // namespace detail

VX_MATH_FORCEINLINE Vector Matrix::GetColumn(size_t index) const noexcept
{
    return Vector{ detail::ExtractLane(data[0], index),
                   detail::ExtractLane(data[1], index),
                   detail::ExtractLane(data[2], index),
                   detail::ExtractLane(data[3], index) };
}

VX_MATH_FORCEINLINE void Matrix::SetColumn(size_t index, Vector column) noexcept
{
    const v128_t columnData = column.Data();
    data[0] = detail::ReplaceLane(data[0], index, wasm_f32x4_extract_lane(columnData, 0));
    data[1] = detail::ReplaceLane(data[1], index, wasm_f32x4_extract_lane(columnData, 1));
    data[2] = detail::ReplaceLane(data[2], index, wasm_f32x4_extract_lane(columnData, 2));
    data[3] = detail::ReplaceLane(data[3], index, wasm_f32x4_extract_lane(columnData, 3));
}

VX_MATH_FORCEINLINE float Matrix::operator[](size_t row, size_t col) const noexcept
{
    return detail::ExtractLane(data[row], col);
}

VX_MATH_FORCEINLINE void Matrix::SetElement(size_t row, size_t col, float value) noexcept
{
    data[row] = detail::ReplaceLane(data[row], col, value);
}

VX_MATH_FORCEINLINE Matrix Matrix::operator+(const Matrix& rhs) const noexcept
{
    return Matrix{ wasm_f32x4_add(data[0], rhs.data[0]),
                   wasm_f32x4_add(data[1], rhs.data[1]),
                   wasm_f32x4_add(data[2], rhs.data[2]),
                   wasm_f32x4_add(data[3], rhs.data[3]) };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator-(const Matrix& rhs) const noexcept
{
    return Matrix{ wasm_f32x4_sub(data[0], rhs.data[0]),
                   wasm_f32x4_sub(data[1], rhs.data[1]),
                   wasm_f32x4_sub(data[2], rhs.data[2]),
                   wasm_f32x4_sub(data[3], rhs.data[3]) };
}

namespace detail
{
    // Row-vector * row-major matrix multiply for a single row:
    // result = sum_i row[i] * mat.row(i)
    VX_MATH_FORCEINLINE v128_t MulRowByMatrix(v128_t row, const v128_t m[4]) noexcept
    {
#if defined(VX_MATH_RELAXED_SIMD)
        v128_t r = wasm_f32x4_mul(wasm_i32x4_shuffle(row, row, 0, 0, 0, 0), m[0]);
        r = wasm_f32x4_relaxed_madd(wasm_i32x4_shuffle(row, row, 1, 1, 1, 1), m[1], r);
        r = wasm_f32x4_relaxed_madd(wasm_i32x4_shuffle(row, row, 2, 2, 2, 2), m[2], r);
        r = wasm_f32x4_relaxed_madd(wasm_i32x4_shuffle(row, row, 3, 3, 3, 3), m[3], r);
        return r;
#else
        v128_t r = wasm_f32x4_mul(wasm_i32x4_shuffle(row, row, 0, 0, 0, 0), m[0]);
        r = wasm_f32x4_add(r, wasm_f32x4_mul(wasm_i32x4_shuffle(row, row, 1, 1, 1, 1), m[1]));
        r = wasm_f32x4_add(r, wasm_f32x4_mul(wasm_i32x4_shuffle(row, row, 2, 2, 2, 2), m[2]));
        r = wasm_f32x4_add(r, wasm_f32x4_mul(wasm_i32x4_shuffle(row, row, 3, 3, 3, 3), m[3]));
        return r;
#endif
    }
} // namespace detail

VX_MATH_FORCEINLINE Matrix Matrix::operator*(const Matrix& rhs) const noexcept
{
    return Matrix{ detail::MulRowByMatrix(data[0], rhs.data),
                   detail::MulRowByMatrix(data[1], rhs.data),
                   detail::MulRowByMatrix(data[2], rhs.data),
                   detail::MulRowByMatrix(data[3], rhs.data) };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator*(float scalar) const noexcept
{
    v128_t s = wasm_f32x4_splat(scalar);
    return Matrix{ wasm_f32x4_mul(data[0], s),
                   wasm_f32x4_mul(data[1], s),
                   wasm_f32x4_mul(data[2], s),
                   wasm_f32x4_mul(data[3], s) };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator-() const noexcept
{
    return Matrix{
        wasm_f32x4_neg(data[0]), wasm_f32x4_neg(data[1]), wasm_f32x4_neg(data[2]), wasm_f32x4_neg(data[3])
    };
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator+=(const Matrix& rhs) noexcept
{
    data[0] = wasm_f32x4_add(data[0], rhs.data[0]);
    data[1] = wasm_f32x4_add(data[1], rhs.data[1]);
    data[2] = wasm_f32x4_add(data[2], rhs.data[2]);
    data[3] = wasm_f32x4_add(data[3], rhs.data[3]);
    return *this;
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator-=(const Matrix& rhs) noexcept
{
    data[0] = wasm_f32x4_sub(data[0], rhs.data[0]);
    data[1] = wasm_f32x4_sub(data[1], rhs.data[1]);
    data[2] = wasm_f32x4_sub(data[2], rhs.data[2]);
    data[3] = wasm_f32x4_sub(data[3], rhs.data[3]);
    return *this;
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator*=(const Matrix& rhs) noexcept
{
    *this = *this * rhs;
    return *this;
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator*=(float scalar) noexcept
{
    v128_t s = wasm_f32x4_splat(scalar);
    data[0] = wasm_f32x4_mul(data[0], s);
    data[1] = wasm_f32x4_mul(data[1], s);
    data[2] = wasm_f32x4_mul(data[2], s);
    data[3] = wasm_f32x4_mul(data[3], s);
    return *this;
}

VX_MATH_FORCEINLINE Vector Matrix::operator*(Vector vec) const noexcept
{
    return Vector{ detail::MulRowByMatrix(vec.Data(), data) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Transpose() const noexcept
{
    // Standard 4x4 SIMD transpose (the _MM_TRANSPOSE4_PS pattern).
    v128_t tmp0 = wasm_i32x4_shuffle(data[0], data[1], 0, 4, 1, 5);
    v128_t tmp1 = wasm_i32x4_shuffle(data[2], data[3], 0, 4, 1, 5);
    v128_t tmp2 = wasm_i32x4_shuffle(data[0], data[1], 2, 6, 3, 7);
    v128_t tmp3 = wasm_i32x4_shuffle(data[2], data[3], 2, 6, 3, 7);
    return Matrix{ wasm_i32x4_shuffle(tmp0, tmp1, 0, 1, 4, 5),
                   wasm_i32x4_shuffle(tmp0, tmp1, 2, 3, 6, 7),
                   wasm_i32x4_shuffle(tmp2, tmp3, 0, 1, 4, 5),
                   wasm_i32x4_shuffle(tmp2, tmp3, 2, 3, 6, 7) };
}

// Cofactor expansion over the transpose, ported from XMMatrixInverse (MIT, Microsoft;
// DirectXMathMatrix.inl in the Windows SDK). Branch-free and no stack round trip, unlike the
// Gauss-Jordan it replaced. Its XMVectorSwizzle/XMVectorPermute lane encodings map exactly onto
// wasm_i32x4_shuffle, including 4..7 indexing the second operand.
//
// Singular input is not special-cased: the result fills with infinities and NaNs. Check Determinant()
// first if that matters.
inline Matrix Matrix::Inverse() const noexcept
{
    const Matrix transposed = Transpose();
    const v128_t mt0 = transposed.data[0];
    const v128_t mt1 = transposed.data[1];
    const v128_t mt2 = transposed.data[2];
    const v128_t mt3 = transposed.data[3];

    v128_t d0 = wasm_f32x4_mul(wasm_i32x4_shuffle(mt2, mt2, 0, 0, 1, 1),
                               wasm_i32x4_shuffle(mt3, mt3, 2, 3, 2, 3));
    v128_t d1 = wasm_f32x4_mul(wasm_i32x4_shuffle(mt0, mt0, 0, 0, 1, 1),
                               wasm_i32x4_shuffle(mt1, mt1, 2, 3, 2, 3));
    v128_t d2 = wasm_f32x4_mul(wasm_i32x4_shuffle(mt2, mt0, 0, 2, 4, 6),
                               wasm_i32x4_shuffle(mt3, mt1, 1, 3, 5, 7));

    d0 = detail::NegMulAdd(wasm_i32x4_shuffle(mt2, mt2, 2, 3, 2, 3),
                           wasm_i32x4_shuffle(mt3, mt3, 0, 0, 1, 1),
                           d0);
    d1 = detail::NegMulAdd(wasm_i32x4_shuffle(mt0, mt0, 2, 3, 2, 3),
                           wasm_i32x4_shuffle(mt1, mt1, 0, 0, 1, 1),
                           d1);
    d2 = detail::NegMulAdd(wasm_i32x4_shuffle(mt2, mt0, 1, 3, 5, 7),
                           wasm_i32x4_shuffle(mt3, mt1, 0, 2, 4, 6),
                           d2);

    const v128_t v00 = wasm_i32x4_shuffle(mt1, mt1, 1, 2, 0, 1);
    const v128_t v10 = wasm_i32x4_shuffle(d0, d2, 5, 1, 3, 0);
    const v128_t v01 = wasm_i32x4_shuffle(mt0, mt0, 2, 0, 1, 0);
    const v128_t v11 = wasm_i32x4_shuffle(d0, d2, 3, 5, 1, 2);
    const v128_t v02 = wasm_i32x4_shuffle(mt3, mt3, 1, 2, 0, 1);
    const v128_t v12 = wasm_i32x4_shuffle(d1, d2, 7, 1, 3, 0);
    const v128_t v03 = wasm_i32x4_shuffle(mt2, mt2, 2, 0, 1, 0);
    const v128_t v13 = wasm_i32x4_shuffle(d1, d2, 3, 7, 1, 2);

    v128_t c0 = wasm_f32x4_mul(v00, v10);
    v128_t c2 = wasm_f32x4_mul(v01, v11);
    v128_t c4 = wasm_f32x4_mul(v02, v12);
    v128_t c6 = wasm_f32x4_mul(v03, v13);

    const v128_t v20 = wasm_i32x4_shuffle(mt1, mt1, 2, 3, 1, 2);
    const v128_t v30 = wasm_i32x4_shuffle(d0, d2, 3, 0, 1, 4);
    const v128_t v21 = wasm_i32x4_shuffle(mt0, mt0, 3, 2, 3, 1);
    const v128_t v31 = wasm_i32x4_shuffle(d0, d2, 2, 1, 4, 0);
    const v128_t v22 = wasm_i32x4_shuffle(mt3, mt3, 2, 3, 1, 2);
    const v128_t v32 = wasm_i32x4_shuffle(d1, d2, 3, 0, 1, 6);
    const v128_t v23 = wasm_i32x4_shuffle(mt2, mt2, 3, 2, 3, 1);
    const v128_t v33 = wasm_i32x4_shuffle(d1, d2, 2, 1, 6, 0);

    c0 = detail::NegMulAdd(v20, v30, c0);
    c2 = detail::NegMulAdd(v21, v31, c2);
    c4 = detail::NegMulAdd(v22, v32, c4);
    c6 = detail::NegMulAdd(v23, v33, c6);

    const v128_t v40 = wasm_i32x4_shuffle(mt1, mt1, 3, 0, 3, 0);
    const v128_t v50 = wasm_i32x4_shuffle(d0, d2, 2, 5, 4, 2);
    const v128_t v41 = wasm_i32x4_shuffle(mt0, mt0, 1, 3, 0, 2);
    const v128_t v51 = wasm_i32x4_shuffle(d0, d2, 5, 0, 3, 4);
    const v128_t v42 = wasm_i32x4_shuffle(mt3, mt3, 3, 0, 3, 0);
    const v128_t v52 = wasm_i32x4_shuffle(d1, d2, 2, 7, 6, 2);
    const v128_t v43 = wasm_i32x4_shuffle(mt2, mt2, 1, 3, 0, 2);
    const v128_t v53 = wasm_i32x4_shuffle(d1, d2, 7, 0, 3, 6);

    const v128_t c1 = detail::NegMulAdd(v40, v50, c0);
    c0 = detail::MulAdd(v40, v50, c0);
    const v128_t c3 = detail::MulAdd(v41, v51, c2);
    c2 = detail::NegMulAdd(v41, v51, c2);
    const v128_t c5 = detail::NegMulAdd(v42, v52, c4);
    c4 = detail::MulAdd(v42, v52, c4);
    const v128_t c7 = detail::MulAdd(v43, v53, c6);
    c6 = detail::NegMulAdd(v43, v53, c6);

    // interleave the even/odd cofactor halves
    const v128_t row0 = wasm_i32x4_shuffle(c0, c1, 0, 5, 2, 7);
    const v128_t row1 = wasm_i32x4_shuffle(c2, c3, 0, 5, 2, 7);
    const v128_t row2 = wasm_i32x4_shuffle(c4, c5, 0, 5, 2, 7);
    const v128_t row3 = wasm_i32x4_shuffle(c6, c7, 0, 5, 2, 7);

    const v128_t determinant = Vector{ row0 }.DotVec<4>(Vector{ mt0 }).Data();
    const v128_t reciprocal = wasm_f32x4_div(wasm_f32x4_splat(1.0f), determinant);

    return Matrix{ wasm_f32x4_mul(row0, reciprocal),
                   wasm_f32x4_mul(row1, reciprocal),
                   wasm_f32x4_mul(row2, reciprocal),
                   wasm_f32x4_mul(row3, reciprocal) };
}

VX_MATH_FORCEINLINE float Matrix::Determinant() const noexcept
{
    alignas(16) float m[4][4];
    wasm_v128_store(m[0], data[0]);
    wasm_v128_store(m[1], data[1]);
    wasm_v128_store(m[2], data[2]);
    wasm_v128_store(m[3], data[3]);
    return detail::Determinant4x4(m);
}

// Static factory methods for transformations
VX_MATH_FORCEINLINE Matrix Matrix::Translation(Vector translation) noexcept
{
    return Translation(translation.x(), translation.y(), translation.z());
}

VX_MATH_FORCEINLINE Matrix Matrix::Translation(float x, float y, float z) noexcept
{
    return Matrix{ wasm_f32x4_make(1.0f, 0.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 1.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 1.0f, 0.0f),
                   wasm_f32x4_make(x, y, z, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Scale(Vector scale) noexcept
{
    return Scale(scale.x(), scale.y(), scale.z());
}

VX_MATH_FORCEINLINE Matrix Matrix::Scale(float x, float y, float z) noexcept
{
    return Matrix{ wasm_f32x4_make(x, 0.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, y, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, z, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Scale(float uniform_scale) noexcept
{
    return Scale(uniform_scale, uniform_scale, uniform_scale);
}

// Row-vector, row-major, matching DirectXMath's XMMatrixRotationX/Y/Z. SinCos rather than separate
// sin and cos calls: they share one range reduction, so the pair costs about 1.3x one of them.
VX_MATH_FORCEINLINE Matrix Matrix::RotationX(float radians) noexcept
{
    const ScalarSinCos angle = SinCos(radians);
    const float s = angle.sin;
    const float c = angle.cos;
    return Matrix{ wasm_f32x4_make(1.0f, 0.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, c, s, 0.0f),
                   wasm_f32x4_make(0.0f, -s, c, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::RotationY(float radians) noexcept
{
    const ScalarSinCos angle = SinCos(radians);
    const float s = angle.sin;
    const float c = angle.cos;
    return Matrix{ wasm_f32x4_make(c, 0.0f, -s, 0.0f),
                   wasm_f32x4_make(0.0f, 1.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(s, 0.0f, c, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::RotationZ(float radians) noexcept
{
    const ScalarSinCos angle = SinCos(radians);
    const float s = angle.sin;
    const float c = angle.cos;
    return Matrix{ wasm_f32x4_make(c, s, 0.0f, 0.0f),
                   wasm_f32x4_make(-s, c, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 1.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) };
}

// Rodrigues' rotation formula, row-vector convention.
VX_MATH_FORCEINLINE Matrix Matrix::RotationAxis(Vector axis, float radians) noexcept
{
    Vector n = axis.Normalize<3>();
    float x = n.x(), y = n.y(), z = n.z();
    float c = std::cos(radians), s = std::sin(radians), t = 1.0f - c;

    return Matrix{ wasm_f32x4_make(t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0.0f),
                   wasm_f32x4_make(t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0.0f),
                   wasm_f32x4_make(t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) };
}

// Standard quaternion-to-matrix, row-vector convention (matches
// DirectXMath's XMMatrixRotationQuaternion).
VX_MATH_FORCEINLINE Matrix Matrix::RotationQuaternion(Quaternion rotation) noexcept
{
    float x = rotation.x(), y = rotation.y(), z = rotation.z(), w = rotation.w();

    return Matrix{
        wasm_f32x4_make(1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w), 0.0f),
        wasm_f32x4_make(2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w), 0.0f),
        wasm_f32x4_make(2.0f * (x * z + y * w), 2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + y * y), 0.0f),
        wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f)
    };
}

// Forwards to the quaternion form so the composition order is defined in one place
VX_MATH_FORCEINLINE Matrix Matrix::RotationRollPitchYaw(float pitch, float yaw, float roll) noexcept
{
    return Matrix::RotationQuaternion(Quaternion::RotationRollPitchYaw(pitch, yaw, roll));
}

// Analytical S * R * T. S is diagonal, so (S * R) is each rotation row scaled by one component; rows
// 0-2 of a rotation have w == 0, so the * T step only drops the translation into row 3. Three
// multiplies and a lane write against ~90 emitted instructions, and more accurate too - the general
// path accumulates rounding through dot products whose terms are nearly all exact zeros.
VX_MATH_FORCEINLINE Matrix Matrix::TRS(Vector translation, Quaternion rotation_quaternion, Vector scale) noexcept
{
    const Matrix rotation = Matrix::RotationQuaternion(rotation_quaternion);
    const v128_t scaleData = scale.Data();

    return Matrix{ wasm_f32x4_mul(rotation.data[0], wasm_i32x4_shuffle(scaleData, scaleData, 0, 0, 0, 0)),
                   wasm_f32x4_mul(rotation.data[1], wasm_i32x4_shuffle(scaleData, scaleData, 1, 1, 1, 1)),
                   wasm_f32x4_mul(rotation.data[2], wasm_i32x4_shuffle(scaleData, scaleData, 2, 2, 2, 2)),
                   wasm_f32x4_replace_lane(translation.Data(), 3, 1.0f) };
}

// A rotation origin leaves the linear part identical and only shifts the translation row to
// t + Ro - Ro * R, with Ro passing through the *unscaled* rotation.
VX_MATH_FORCEINLINE Matrix Matrix::TRS(Vector translation,
                                       Quaternion rotation_quaternion,
                                       Vector scale,
                                       Vector rotation_origin) noexcept
{
    const Matrix rotation = Matrix::RotationQuaternion(rotation_quaternion);
    const v128_t scaleData = scale.Data();
    const v128_t originData = wasm_f32x4_replace_lane(rotation_origin.Data(), 3, 0.0f);
    const v128_t rotatedOrigin = detail::MulRowByMatrix(originData, rotation.data);

    v128_t translationRow = wasm_f32x4_add(translation.Data(), wasm_f32x4_sub(originData, rotatedOrigin));
    translationRow = wasm_f32x4_replace_lane(translationRow, 3, 1.0f);

    return Matrix{ wasm_f32x4_mul(rotation.data[0], wasm_i32x4_shuffle(scaleData, scaleData, 0, 0, 0, 0)),
                   wasm_f32x4_mul(rotation.data[1], wasm_i32x4_shuffle(scaleData, scaleData, 1, 1, 1, 1)),
                   wasm_f32x4_mul(rotation.data[2], wasm_i32x4_shuffle(scaleData, scaleData, 2, 2, 2, 2)),
                   translationRow };
}

VX_MATH_FORCEINLINE Matrix Matrix::LookAt(Vector eye, Vector target, Vector up) noexcept
{
    Vector zaxis = (eye - target).Normalize<3>();
    Vector xaxis = up.Cross(zaxis).Normalize<3>();
    Vector yaxis = zaxis.Cross(xaxis);

    return Matrix{ wasm_f32x4_make(xaxis.x(), yaxis.x(), zaxis.x(), 0.0f),
                   wasm_f32x4_make(xaxis.y(), yaxis.y(), zaxis.y(), 0.0f),
                   wasm_f32x4_make(xaxis.z(), yaxis.z(), zaxis.z(), 0.0f),
                   wasm_f32x4_make(-xaxis.Dot<3>(eye), -yaxis.Dot<3>(eye), -zaxis.Dot<3>(eye), 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::LookTo(Vector eye, Vector direction, Vector up) noexcept
{
    Vector zaxis = (-direction).Normalize<3>();
    Vector xaxis = up.Cross(zaxis).Normalize<3>();
    Vector yaxis = zaxis.Cross(xaxis);

    return Matrix{ wasm_f32x4_make(xaxis.x(), yaxis.x(), zaxis.x(), 0.0f),
                   wasm_f32x4_make(xaxis.y(), yaxis.y(), zaxis.y(), 0.0f),
                   wasm_f32x4_make(xaxis.z(), yaxis.z(), zaxis.z(), 0.0f),
                   wasm_f32x4_make(-xaxis.Dot<3>(eye), -yaxis.Dot<3>(eye), -zaxis.Dot<3>(eye), 1.0f) };
}

// D3D/WebGPU-style depth range [0, 1] throughout, matching the original
// DirectXMath-backed source.
namespace detail
{
    VX_MATH_FORCEINLINE Matrix PerspectiveFovLH(float fov_y_radians,
                                                float aspect_ratio,
                                                float near_plane,
                                                float far_plane) noexcept
    {
        float height = 1.0f / std::tan(fov_y_radians * 0.5f);
        float width = height / aspect_ratio;
        float range = far_plane / (far_plane - near_plane);

        return Matrix{ wasm_f32x4_make(width, 0.0f, 0.0f, 0.0f),
                       wasm_f32x4_make(0.0f, height, 0.0f, 0.0f),
                       wasm_f32x4_make(0.0f, 0.0f, range, 1.0f),
                       wasm_f32x4_make(0.0f, 0.0f, -range * near_plane, 0.0f) };
    }
} // namespace detail

VX_MATH_FORCEINLINE Matrix Matrix::Perspective(float fov_y_radians,
                                               float aspect_ratio,
                                               float near_plane,
                                               float far_plane) noexcept
{
    return detail::PerspectiveFovLH(fov_y_radians, aspect_ratio, near_plane, far_plane);
}

VX_MATH_FORCEINLINE Matrix Matrix::PerspectiveLH(float fov_y_radians,
                                                 float aspect_ratio,
                                                 float near_plane,
                                                 float far_plane) noexcept
{
    return detail::PerspectiveFovLH(fov_y_radians, aspect_ratio, near_plane, far_plane);
}

VX_MATH_FORCEINLINE Matrix Matrix::PerspectiveRH(float fov_y_radians,
                                                 float aspect_ratio,
                                                 float near_plane,
                                                 float far_plane) noexcept
{
    float height = 1.0f / std::tan(fov_y_radians * 0.5f);
    float width = height / aspect_ratio;
    float range = far_plane / (near_plane - far_plane);

    return Matrix{ wasm_f32x4_make(width, 0.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, height, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, range, -1.0f),
                   wasm_f32x4_make(0.0f, 0.0f, range * near_plane, 0.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Orthographic(float width,
                                                float height,
                                                float near_plane,
                                                float far_plane) noexcept
{
    return OrthographicLH(width, height, near_plane, far_plane);
}

VX_MATH_FORCEINLINE Matrix Matrix::OrthographicLH(float width,
                                                  float height,
                                                  float near_plane,
                                                  float far_plane) noexcept
{
    float range = 1.0f / (far_plane - near_plane);
    return Matrix{ wasm_f32x4_make(2.0f / width, 0.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 2.0f / height, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, range, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, -range * near_plane, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::OrthographicRH(float width,
                                                  float height,
                                                  float near_plane,
                                                  float far_plane) noexcept
{
    float range = 1.0f / (near_plane - far_plane);
    return Matrix{ wasm_f32x4_make(2.0f / width, 0.0f, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 2.0f / height, 0.0f, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, range, 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, range * near_plane, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Identity() noexcept
{
    return Matrix{};
}

VX_MATH_FORCEINLINE Matrix Matrix::Zero() noexcept
{
    v128_t z = wasm_f32x4_splat(0.0f);
    return Matrix{ z, z, z, z };
}

VX_MATH_FORCEINLINE bool Matrix::IsIdentity() const noexcept
{
    Matrix identity{};
    return IsNearlyEqual(identity, 0.0f);
}

VX_MATH_FORCEINLINE bool Matrix::IsNearlyEqual(const Matrix& other, float epsilon) const noexcept
{
    v128_t eps = wasm_f32x4_splat(epsilon);
    for (int i = 0; i < 4; ++i)
    {
        v128_t diff = wasm_f32x4_abs(wasm_f32x4_sub(data[i], other.data[i]));
        v128_t cmp = wasm_f32x4_le(diff, eps);
        // 4-bit mask, one bit per lane; 0xF means all four lanes matched.
        if (wasm_i32x4_bitmask(cmp) != 0xF)
        {
            return false;
        }
    }
    return true;
}

// ================================
// Matrix Free Functions
// ================================

template<int N>
VX_MATH_FORCEINLINE Vector Transform(Vector vector, Matrix matrix) noexcept
{
    static_assert(N >= 2 && N <= 4, "Transform dimensionality must be 2, 3, or 4");
    if constexpr (N == 4)
    {
        return matrix * vector;
    }
    else
    {
        // TransformCoord semantics: treat vector as a point (w=1 going in),
        // then perspective-divide by the resulting w.
        // Use wasm_f32x4_replace_lane as needed based on dims
        v128_t data = vector.Data();
        if constexpr (N == 2)
        {
            data = wasm_f32x4_replace_lane(data, 2, 0.0f);
            data = wasm_f32x4_replace_lane(data, 3, 1.0f);
        }
        else if constexpr (N == 3)
        {
            data = wasm_f32x4_replace_lane(data, 3, 1.0f);
        }
        // perspective divide stays in the vector domain: splat w and divide, no scalar round trip
        const v128_t transformed = (matrix * Vector{ data }).Data();
        return Vector{ wasm_f32x4_div(transformed,
                                      wasm_i32x4_shuffle(transformed, transformed, 3, 3, 3, 3)) };
    }
}

VX_MATH_FORCEINLINE Vector TransformNormal(Vector normal, Matrix matrix) noexcept
{
    // Directions transform by the upper-left 3x3 only (w forced to 0 so
    // translation doesn't leak in). use replace_lane again to zero out the w lane
    return matrix * Vector{ wasm_f32x4_replace_lane(normal.Data(), 3, 0.0f) };
}

VX_MATH_FORCEINLINE Matrix operator*(float scalar, const Matrix& mat) noexcept
{
    return mat * scalar;
}

// Matrix conversion functions
VX_MATH_FORCEINLINE Matrix ToMatrix(const Float3x3& storage) noexcept
{
    const auto& m = storage.Data();
    return Matrix{ wasm_f32x4_make(m[0][0], m[0][1], m[0][2], 0.0f),
                   wasm_f32x4_make(m[1][0], m[1][1], m[1][2], 0.0f),
                   wasm_f32x4_make(m[2][0], m[2][1], m[2][2], 0.0f),
                   wasm_f32x4_make(0.0f, 0.0f, 0.0f, 1.0f) };
}

VX_MATH_FORCEINLINE Matrix ToMatrix(const Float4x3& storage) noexcept
{
    const auto& m = storage.Data();
    return Matrix{ wasm_f32x4_make(m[0][0], m[0][1], m[0][2], 0.0f),
                   wasm_f32x4_make(m[1][0], m[1][1], m[1][2], 0.0f),
                   wasm_f32x4_make(m[2][0], m[2][1], m[2][2], 0.0f),
                   wasm_f32x4_make(m[3][0], m[3][1], m[3][2], 1.0f) };
}

VX_MATH_FORCEINLINE Matrix ToMatrix(const Float4x4& storage) noexcept
{
    const auto& m = storage.Data();
    return Matrix{ wasm_f32x4_make(m[0][0], m[0][1], m[0][2], m[0][3]),
                   wasm_f32x4_make(m[1][0], m[1][1], m[1][2], m[1][3]),
                   wasm_f32x4_make(m[2][0], m[2][1], m[2][2], m[2][3]),
                   wasm_f32x4_make(m[3][0], m[3][1], m[3][2], m[3][3]) };
}

// Whole-row stores. Element-by-element would go through Matrix::operator[], whose runtime column
// index costs a row store plus a scalar load each time - sixteen round trips for four stores' work.
// The three-wide types have a row stride of 3 floats, so they need a 64- plus a 32-bit lane store.
namespace detail
{
    VX_MATH_FORCEINLINE void StoreRowXYZ(float* dest, v128_t row) noexcept
    {
        wasm_v128_store64_lane(dest, row, 0);
        wasm_v128_store32_lane(dest + 2, row, 2);
    }
} // namespace detail

template<>
VX_MATH_FORCEINLINE Float3x3 FromMatrix(const Matrix& mat) noexcept
{
    const Matrix::NativeType rows = mat.Data();
    Float3x3 result;
    detail::StoreRowXYZ(&result.Data()[0][0], rows.r[0]);
    detail::StoreRowXYZ(&result.Data()[1][0], rows.r[1]);
    detail::StoreRowXYZ(&result.Data()[2][0], rows.r[2]);
    return result;
}

template<>
VX_MATH_FORCEINLINE Float4x3 FromMatrix(const Matrix& mat) noexcept
{
    const Matrix::NativeType rows = mat.Data();
    Float4x3 result;
    detail::StoreRowXYZ(&result.Data()[0][0], rows.r[0]);
    detail::StoreRowXYZ(&result.Data()[1][0], rows.r[1]);
    detail::StoreRowXYZ(&result.Data()[2][0], rows.r[2]);
    detail::StoreRowXYZ(&result.Data()[3][0], rows.r[3]);
    return result;
}

template<>
VX_MATH_FORCEINLINE Float4x4 FromMatrix(const Matrix& mat) noexcept
{
    const Matrix::NativeType rows = mat.Data();
    Float4x4 result;
    wasm_v128_store(&result.Data()[0][0], rows.r[0]);
    wasm_v128_store(&result.Data()[1][0], rows.r[1]);
    wasm_v128_store(&result.Data()[2][0], rows.r[2]);
    wasm_v128_store(&result.Data()[3][0], rows.r[3]);
    return result;
}

} // namespace velox::math

// Quaternion implementations live in their own file to keep this one navigable
#include "math/QuaternionBackendWASM.inl"
#include "math/TranscendentalBackendWASM.inl"
