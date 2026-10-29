#pragma once
#ifndef DIAMONDDOGS_FOUNDATION_MATH_HPP
#define DIAMONDDOGS_FOUNDATION_MATH_HPP
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

#include "math/MathPolynomials.hpp"

// needed early to prefix the swizzle accessors
#if defined(_MSC_VER)
#define DD_MATH_FORCEINLINE __forceinline
#elif defined(__clang__)
#define DD_MATH_FORCEINLINE __attribute__((always_inline)) inline
#elif defined(__GNUC__)
#define DD_MATH_FORCEINLINE __attribute__((always_inline)) inline
#else
#define DD_MATH_FORCEINLINE inline
#endif


// Autodetects Emscripten and selects the WASM SIMD128 backend; otherwise falls
// back to DirectXMath. Override with DD_MATH_FORCE_BACKEND_WASM or
// DD_MATH_FORCE_BACKEND_DIRECTX to force a specific backend
#if defined(DD_MATH_FORCE_BACKEND_WASM) && defined(DD_MATH_FORCE_BACKEND_DIRECTX)
    #error "Define at most one of DD_MATH_FORCE_BACKEND_WASM / DD_MATH_FORCE_BACKEND_DIRECTX"
#elif defined(DD_MATH_FORCE_BACKEND_WASM)
    #define DD_MATH_BACKEND_WASM 1
#elif defined(DD_MATH_FORCE_BACKEND_DIRECTX)
    #define DD_MATH_BACKEND_WASM 0
#elif defined(__EMSCRIPTEN__)
    #define DD_MATH_BACKEND_WASM 1
#else
    #define DD_MATH_BACKEND_WASM 0
#endif

#ifndef VX_MATH_BACKEND_WASM
    #define VX_MATH_BACKEND_WASM DD_MATH_BACKEND_WASM
#endif

#if DD_MATH_BACKEND_WASM
    #include <wasm_simd128.h>
#else
    #include <DirectXMath.h>
#endif

// DD_MATH_RELAXED_SIMD opts the WASM backend into relaxed-simd: relaxed_madd / nmadd, relaxed_min /
// max, relaxed_trunc, relaxed_laneselect. Every use has a plain-SIMD #else fallback. Set by the
// VELOX_RHI_MATH_RELAXED_SIMD CMake option, which also passes -mrelaxed-simd; the builtins do not
// compile without it, so never define this by hand.

// This does NOT degrade at runtime - a module carrying relaxed opcodes fails validation outright on an
// engine without support. Chrome 114+, Firefox 120+, Safari 18+ are fine; older mobile is not.
// todo-ship: runtime capability check with two compiled variants

// Use analytical solutions and approximations in key locations to reduce
// instruction count (called SHORTCUTS because there's a few diff types of opts, not just approximation)
// On by default for now since these are (generally) safe without undue accuracy impacts
#define DD_MATH_USE_SHORTCUTS

namespace math
{

// Swizzle generation macros - these create combinations from storage members
#define SWIZZLE_2(a, b) \
    constexpr DD_MATH_FORCEINLINE Float2 a##b() const noexcept { return Float2(a, b); }

#define SWIZZLE_3(a, b, c) \
    constexpr DD_MATH_FORCEINLINE Float3 a##b##c() const noexcept { return Float3(a, b, c); }

#define SWIZZLE_4(a, b, c, d) \
    constexpr DD_MATH_FORCEINLINE Float4 a##b##c##d() const noexcept { return Float4(a, b, c, d); }

// Forward declarations
struct Float3;
struct Float4;
struct Vector;
struct VectorMask;
struct Quaternion;
struct Float3x3;
struct Float4x3;
struct Float4x4;
struct Matrix;
// Defined after Vector, which it holds two of; Vector::SinCos only needs the declaration
struct SinCosResult;

/** @brief Scalar counterpart of SinCosResult */
struct ScalarSinCos
{
    float sin;
    float cos;
};

/**
 * This file defines unoptimized vector/matrix storage types (Float2/3/4,
 * Float3x3/4x3/4x4) alongside optimized SIMD types (Vector, Matrix). The
 * storage types are plain, backend-agnostic, and constexpr-friendly; they
 * can be kept around, stored, or transferred as needed. Vector/Matrix are
 * backed by either WASM SIMD128 (Emscripten) or DirectXMath, selected
 * above. If you need to do a lot of repeated math in a hot loop, convert
 * to the SIMD type via ToVector/ToMatrix, and back via FromVector/FromMatrix
 * when you're done.
 *
 * When to stay in the unoptimized storage types (Float2/3/4, Float3x3/4x3/4x4):
 * - One off operations, like singular add/fmas or a length/dot product calculation
 * - Highly branchy logic, where using VectorMask and running both branches isn't efficient or desirable
 * - Repeated component-wise access: extracting scalar values from the SIMD types
 *   is *quite* inefficient
 *
 * When to prefer the optimized SIMD types (Vector, Matrix):
 * - If you're doing 3 or more vector math ops, use the SIMD types
 * - Any kind of matrix math: extracting rows or columns is fine, but as soon
 *   as you start doing matrix arithmetic at all... go to SIMD. Even for one matmul.
 * - Most transcendentals are likely good candidates, as the polynomial fittings 
 *   used in the SIMD implementations are generally well-fitted for most uses in our
     engines, unless you're targeting somewhere where precision is of the utmost importance.
 *
 * -----------------------------------------------------------------------
 * Removed / deferred during the WASM port (add back if/when needed):
 * -----------------------------------------------------------------------
 * - Integer vector types trimmed prior to this port
 * - Vector::Refract<N> - no native intrinsic on the WASM side and
 *   comparatively rarely used
 * - Vector::AlmostEqual(Vector), AlmostEqual(Vector, float), AlmostZero() removed;
 *   CompareNearEqual + VectorMask::AllTrue<N> covers these now
 * - Hermite/CatmullRom/BaryCentric, AngleBetweenVectors, ClampLength and Orthogonal
 *   are not ported yet; nothing needs them so far
 * -----------------------------------------------------------------------
 * Also worth flagging (pre-existing in the source, preserved as-is):
 * -----------------------------------------------------------------------
 * - Matrix::Perspective/Orthographic default to LH, but Matrix::LookAt/
 *   LookTo default to RH. That LH-projection / RH-view mismatch was
 *   already present in the original file; it's preserved here rather
 *   than silently "fixed", but it's worth deciding intentionally for a
 *   new WebGPU target rather than inheriting it by accident.
*/

struct Float2
{
public:
    // Constructors
    constexpr Float2() noexcept : x(0.0f), y(0.0f) {}
    constexpr Float2(float x, float y) noexcept : x(x), y(y) {}
    constexpr explicit Float2(float scalar) noexcept : x(scalar), y(scalar) {}

    constexpr Float2(const Float2& other) noexcept = default;
    constexpr Float2(Float2&& other) noexcept = default;
    constexpr Float2& operator=(const Float2& other) noexcept = default;
    constexpr Float2& operator=(Float2&& other) noexcept = default;

    // Array-style accessor (const + mutable collapsed via deducing this)
    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr Float2 operator+(const Float2& rhs) const noexcept;
    constexpr Float2 operator-(const Float2& rhs) const noexcept;
    constexpr Float2 operator*(const Float2& rhs) const noexcept;
    constexpr Float2 operator/(const Float2& rhs) const noexcept;

    // Scalar operators
    constexpr Float2 operator*(float scalar) const noexcept;
    constexpr Float2 operator/(float scalar) const noexcept;

    // Unary operators
    constexpr Float2 operator-() const noexcept;

    // Compound assignment operators
    constexpr Float2& operator+=(const Float2& rhs) noexcept;
    constexpr Float2& operator-=(const Float2& rhs) noexcept;
    constexpr Float2& operator*=(const Float2& rhs) noexcept;
    constexpr Float2& operator/=(const Float2& rhs) noexcept;
    constexpr Float2& operator*=(float scalar) noexcept;
    constexpr Float2& operator/=(float scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const Float2& rhs) const noexcept;
    constexpr bool operator!=(const Float2& rhs) const noexcept;

    union
    {
        struct { float x, y; };
        struct { float u, v; };
    };
};

struct Float3
{
public:
    // Constructors
    constexpr Float3() noexcept : x(0.0f), y(0.0f), z(0.0f) {}
    constexpr Float3(float x, float y, float z) noexcept : x(x), y(y), z(z) {}
    constexpr explicit Float3(float scalar) noexcept : x(scalar), y(scalar), z(scalar) {}
    constexpr Float3(const Float2& xy, float z) noexcept : x(xy.x), y(xy.y), z(z) {}
    constexpr Float3(float x, const Float2& yz) noexcept : x(x), y(yz.x), z(yz.y) {}

    constexpr Float3(const Float3& other) noexcept = default;
    constexpr Float3(Float3&& other) noexcept = default;
    constexpr Float3& operator=(const Float3& other) noexcept = default;
    constexpr Float3& operator=(Float3&& other) noexcept = default;

    // std::array interop
    constexpr Float3(const std::array<float, 3>& arr) noexcept : x(arr[0]), y(arr[1]), z(arr[2]) {}
    constexpr operator std::array<float, 3>() const noexcept { return { x, y, z }; }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr Float3 operator+(const Float3& rhs) const noexcept;
    constexpr Float3 operator-(const Float3& rhs) const noexcept;
    constexpr Float3 operator*(const Float3& rhs) const noexcept;
    constexpr Float3 operator/(const Float3& rhs) const noexcept;

    // Scalar operators
    constexpr Float3 operator*(float scalar) const noexcept;
    constexpr Float3 operator/(float scalar) const noexcept;

    // Unary operators
    constexpr Float3 operator-() const noexcept;

    // Compound assignment operators
    constexpr Float3& operator+=(const Float3& rhs) noexcept;
    constexpr Float3& operator-=(const Float3& rhs) noexcept;
    constexpr Float3& operator*=(const Float3& rhs) noexcept;
    constexpr Float3& operator/=(const Float3& rhs) noexcept;
    constexpr Float3& operator*=(float scalar) noexcept;
    constexpr Float3& operator/=(float scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const Float3& rhs) const noexcept;
    constexpr bool operator!=(const Float3& rhs) const noexcept;

    SWIZZLE_3(x, y, z)
    SWIZZLE_3(x, z, y)
    SWIZZLE_3(y, x, z)
    SWIZZLE_3(y, z, x)
    SWIZZLE_3(z, x, y)
    SWIZZLE_3(z, y, x)
    SWIZZLE_3(x, x, x)
    SWIZZLE_3(y, y, y)
    SWIZZLE_3(z, z, z)
    SWIZZLE_2(x, y)
    SWIZZLE_2(x, z)
    SWIZZLE_2(y, z)

    union
    {
        struct { float x, y, z; };
        struct { float r, g, b; };
        struct { float u, v, w; };
    };
};

struct Float4
{
public:
    // Constructors
    constexpr Float4() noexcept : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    constexpr Float4(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}
    constexpr explicit Float4(float scalar) noexcept : x(scalar), y(scalar), z(scalar), w(scalar) {}
    constexpr Float4(const Float3& xyz, float w) noexcept : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
    constexpr Float4(float x, const Float3& yzw) noexcept : x(x), y(yzw.x), z(yzw.y), w(yzw.z) {}
    constexpr Float4(const Float2& xy, const Float2& zw) noexcept : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
    constexpr Float4(const Float2& xy, float z, float w) noexcept : x(xy.x), y(xy.y), z(z), w(w) {}
    constexpr Float4(float x, const Float2& yz, float w) noexcept : x(x), y(yz.x), z(yz.y), w(w) {}
    constexpr Float4(float x, float y, const Float2& zw) noexcept : x(x), y(y), z(zw.x), w(zw.y) {}

    constexpr Float4(const Float4& other) noexcept = default;
    constexpr Float4(Float4&& other) noexcept = default;
    constexpr Float4& operator=(const Float4& other) noexcept = default;
    constexpr Float4& operator=(Float4&& other) noexcept = default;

    // std::array interop
    constexpr Float4(const std::array<float, 4>& arr) noexcept : x(arr[0]), y(arr[1]), z(arr[2]), w(arr[3]) {}
    constexpr operator std::array<float, 4>() const noexcept { return { x, y, z, w }; }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr Float4 operator+(const Float4& rhs) const noexcept;
    constexpr Float4 operator-(const Float4& rhs) const noexcept;
    constexpr Float4 operator*(const Float4& rhs) const noexcept;
    constexpr Float4 operator/(const Float4& rhs) const noexcept;

    // Scalar operators
    constexpr Float4 operator*(float scalar) const noexcept;
    constexpr Float4 operator/(float scalar) const noexcept;

    // Unary operators
    constexpr Float4 operator-() const noexcept;

    // Compound assignment operators
    constexpr Float4& operator+=(const Float4& rhs) noexcept;
    constexpr Float4& operator-=(const Float4& rhs) noexcept;
    constexpr Float4& operator*=(const Float4& rhs) noexcept;
    constexpr Float4& operator/=(const Float4& rhs) noexcept;
    constexpr Float4& operator*=(float scalar) noexcept;
    constexpr Float4& operator/=(float scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const Float4& rhs) const noexcept;
    constexpr bool operator!=(const Float4& rhs) const noexcept;

    // Swizzle accessors - 4D permutations. will expand if missing any later
    SWIZZLE_4(x, y, z, w)
    SWIZZLE_4(x, y, w, z)
    SWIZZLE_4(x, z, y, w)
    SWIZZLE_4(x, z, w, y)
    SWIZZLE_4(x, w, y, z)
    SWIZZLE_4(x, w, z, y)
    SWIZZLE_4(w, x, y, z)
    SWIZZLE_4(x, x, x, x)
    SWIZZLE_4(y, y, y, y)
    SWIZZLE_4(z, z, z, z)
    SWIZZLE_4(w, w, w, w)
    SWIZZLE_3(x, y, z)
    SWIZZLE_3(r, g, b)
    SWIZZLE_2(x, y)
    SWIZZLE_2(z, w)

    union
    {
        struct { float x{ 0.0f }, y{ 0.0f }, z{ 0.0f }, w{ 0.0f }; };
        struct { float r, g, b, a; };
        struct { float u, v, s, t; };
    };
};

/**
 * Lane-wise result of a Vector comparison; every lane is all-zero or all-one bits.
 *
 * A distinct type rather than a reused Vector (which is what DirectXMath does) because arithmetic on a
 * mask, or passing one where a coordinate belongs, is meaningless but well-formed if the type is
 * Vector. Hence no arithmetic operators: combine with the bitwise ones, consume via Select() or
 * AllTrue/AnyTrue.
 *
 * Distinct from Vector's AndInt/OrInt/XorInt, which manipulate float bit patterns rather than combine
 * predicates.
 */
struct alignas(16) VectorMask
{
public:
    VectorMask() noexcept;
#if VX_MATH_BACKEND_WASM
    VectorMask(v128_t mask) noexcept : data{mask} {}
    [[nodiscard]] v128_t Data() const noexcept { return data; }
#else
    VectorMask(DirectX::XMVECTOR mask) noexcept : data{mask} {}
    [[nodiscard]] DirectX::XMVECTOR Data() const noexcept { return data; }
#endif

    VectorMask(const VectorMask& other) noexcept = default;
    VectorMask(VectorMask&& other) noexcept = default;
    VectorMask& operator=(const VectorMask& other) noexcept = default;
    VectorMask& operator=(VectorMask&& other) noexcept = default;

    VectorMask operator&(VectorMask rhs) const noexcept;
    VectorMask operator|(VectorMask rhs) const noexcept;
    VectorMask operator^(VectorMask rhs) const noexcept;
    VectorMask operator~() const noexcept;

    VectorMask& operator&=(VectorMask rhs) noexcept;
    VectorMask& operator|=(VectorMask rhs) noexcept;
    VectorMask& operator^=(VectorMask rhs) noexcept;

    /** @brief True when every one of the first N lanes is set */
    template<int N>
    [[nodiscard]] bool AllTrue() const noexcept;

    /** @brief True when any of the first N lanes is set */
    template<int N>
    [[nodiscard]] bool AnyTrue() const noexcept;

    /** @brief One bit per lane, lane 0 in bit 0 */
    [[nodiscard]] uint32_t LaneBits() const noexcept;

    static VectorMask AllSet() noexcept;
    static VectorMask AllClear() noexcept;

private:
#if VX_MATH_BACKEND_WASM
    v128_t data;
#else
    DirectX::XMVECTOR data;
#endif
};

/**
 * SIMD Vector type - optimized for mathematical operations, backed by
 * either WASM SIMD128 (Emscripten builds) or DirectXMath (native builds).
 * This type should NOT be stored or persisted. Convert to storage types
 * (Float2/3/4) at the end of mathematical operations, and from storage
 * types at the beginning, via ToVector/FromVector.
 *
 * All operations pass and return by value for optimal SIMD performance.
 * 
 * Operations like Normalize() and Length() are templated on number of
 * input components: make sure to get this right and be explicit about
 * your vector widths
 */
struct alignas(16) Vector
{
public:
    // Constructors
    Vector() noexcept;
    Vector(float x, float y, float z, float w) noexcept;
    Vector(float x, float y, float z) noexcept;
    Vector(float x, float y) noexcept;
    explicit Vector(float scalar) noexcept;
#if VX_MATH_BACKEND_WASM
    Vector(v128_t vec) noexcept : data{vec} {}
#else
    Vector(DirectX::XMVECTOR vec) noexcept : data{vec} {}
#endif

    Vector(const Vector& other) noexcept = default;
    Vector(Vector&& other) noexcept = default;
    Vector& operator=(const Vector& other) noexcept = default;
    Vector& operator=(Vector&& other) noexcept = default;

    // Native SIMD interop
#if VX_MATH_BACKEND_WASM
    v128_t Data() const noexcept { return data; }
#else
    DirectX::XMVECTOR Data() const noexcept { return data; }
#endif

    /** @note Accessing a single scalar value in this vector type is comparatively slow */
    [[nodiscard]] float x() const noexcept;
    /** @note Accessing a single scalar value in this vector type is comparatively slow */
    [[nodiscard]] float y() const noexcept;
    /** @note Accessing a single scalar value in this vector type is comparatively slow */
    [[nodiscard]] float z() const noexcept;
    /** @note Accessing a single scalar value in this vector type is comparatively slow */
    [[nodiscard]] float w() const noexcept;

    // Arithmetic operators
    Vector operator+(Vector rhs) const noexcept;
    Vector operator-(Vector rhs) const noexcept;
    Vector operator*(Vector rhs) const noexcept;
    Vector operator/(Vector rhs) const noexcept;
    Vector operator*(float scalar) const noexcept;
    Vector operator/(float scalar) const noexcept;
    Vector operator-() const noexcept;

    // on WebAssembly, this only lowers to a true fused FMA instruction if VX_MATH_RELAXED_SIMD is defined :'(
    [[nodiscard]] Vector MultiplyAdd(Vector factor, Vector addend) const noexcept;

    // Compound assignment operators
    Vector& operator+=(Vector rhs) noexcept;
    Vector& operator-=(Vector rhs) noexcept;
    Vector& operator*=(Vector rhs) noexcept;
    Vector& operator/=(Vector rhs) noexcept;
    Vector& operator*=(float scalar) noexcept;
    Vector& operator/=(float scalar) noexcept;

    [[nodiscard]] Vector Reciprocal() const noexcept;
    [[nodiscard]] Vector Sqrt() const noexcept;
    [[nodiscard]] Vector ReciprocalSqrt() const noexcept;

    template<int N>
    [[nodiscard]] Vector Normalize() const noexcept;

    template<int N>
    [[nodiscard]] float Length() const noexcept;

    template<int N>
    [[nodiscard]] float LengthSq() const noexcept;

    [[nodiscard]] Vector Cross(Vector other) const noexcept;

    template<int N>
    [[nodiscard]] Vector DotVec(Vector other) const noexcept;
    
    // Scalar dot product will involve at least one extra instruction. Favor vector returning Dot<N>
    template<int N>
    [[nodiscard]] float Dot(Vector other) const noexcept;

    [[nodiscard]] Vector Lerp(Vector target, float t) const noexcept;

    template<int N>
    [[nodiscard]] Vector Reflect(Vector normal) const noexcept;

    [[nodiscard]] Vector Clamp(Vector min, Vector max) const noexcept;
    [[nodiscard]] Vector Saturate() const noexcept;
    [[nodiscard]] Vector Abs() const noexcept;
    [[nodiscard]] Vector Min(Vector other) const noexcept;
    [[nodiscard]] Vector Max(Vector other) const noexcept;
    [[nodiscard]] Vector Pow(float exponent) const noexcept;
    [[nodiscard]] Vector Pow(Vector exponent) const noexcept;

    // NaN operands compare false everywhere except CompareNotEqual
    [[nodiscard]] VectorMask CompareEqual(Vector other) const noexcept;
    [[nodiscard]] VectorMask CompareNotEqual(Vector other) const noexcept;
    [[nodiscard]] VectorMask CompareLess(Vector other) const noexcept;
    [[nodiscard]] VectorMask CompareLessOrEqual(Vector other) const noexcept;
    [[nodiscard]] VectorMask CompareGreater(Vector other) const noexcept;
    [[nodiscard]] VectorMask CompareGreaterOrEqual(Vector other) const noexcept;
    [[nodiscard]] VectorMask CompareNearEqual(Vector other, Vector epsilon) const noexcept;
    [[nodiscard]] VectorMask IsNaN() const noexcept;
    [[nodiscard]] VectorMask IsInfinite() const noexcept;

    // Lanes as raw bit patterns, for exponent and sign work. To combine predicates, use VectorMask
    [[nodiscard]] Vector AndInt(Vector other) const noexcept;
    [[nodiscard]] Vector AndNotInt(Vector other) const noexcept;
    [[nodiscard]] Vector OrInt(Vector other) const noexcept;
    [[nodiscard]] Vector XorInt(Vector other) const noexcept;
    [[nodiscard]] Vector NorInt(Vector other) const noexcept;

    // Round is to-nearest-even, matching both backends' native instruction
    [[nodiscard]] Vector Round() const noexcept;
    [[nodiscard]] Vector Truncate() const noexcept;
    [[nodiscard]] Vector Floor() const noexcept;
    [[nodiscard]] Vector Ceil() const noexcept;
    /** @brief Remainder of this / divisor, truncated toward zero, so the sign follows the dividend */
    [[nodiscard]] Vector Mod(Vector divisor) const noexcept;
    /** @brief Wraps each lane into [-pi, pi] */
    [[nodiscard]] Vector ModAngles() const noexcept;

    [[nodiscard]] Vector SplatX() const noexcept;
    [[nodiscard]] Vector SplatY() const noexcept;
    [[nodiscard]] Vector SplatZ() const noexcept;
    [[nodiscard]] Vector SplatW() const noexcept;
    /** @brief (this.x, other.x, this.y, other.y) */
    [[nodiscard]] Vector MergeXY(Vector other) const noexcept;
    /** @brief (this.z, other.z, this.w, other.w) */
    [[nodiscard]] Vector MergeZW(Vector other) const noexcept;

    // Lane indices 0-3 for X-W
    template<int X, int Y, int Z, int W>
    [[nodiscard]] Vector Swizzle() const noexcept;
    // As above, but 4-7 index `other`
    template<int X, int Y, int Z, int W>
    [[nodiscard]] Vector Permute(Vector other) const noexcept;

    // Every error figure below is the *measured* worst case from the accuracy sweeps in
    // tests/unit_tests/MathTests.cpp, taken as the worse of the two backends. Nothing here is
    // bit-comparable across backends. Re-run those sweeps after touching a coefficient: a wrong
    // constant still gives a curve of the right shape and magnitude, so nothing else notices.
    // Pow and the hyperbolics build on Exp2/Log2.

    /**
     * @brief 2^x. Relative error under 2e-5.
     * @note That bound is DirectXMath's ~1.0e-5; the WASM series measures ~1.9e-7. The tighter figure
     * exists only on the web target.
     */
    [[nodiscard]] Vector Exp2() const noexcept;
    /** @brief Relative error under 1e-4. Falls back to Exp2 on DirectXMath */
    [[nodiscard]] Vector Exp2Est() const noexcept;
    /** @brief Absolute error under 2e-6. Negative input yields NaN, zero yields -infinity */
    [[nodiscard]] Vector Log2() const noexcept;
    /** @brief Absolute error under 1e-5 */
    [[nodiscard]] Vector Log2Est() const noexcept;

    /** @brief Prefer this over separate Sin/Cos - they share a range reduction */
    [[nodiscard]] SinCosResult SinCos() const noexcept;
    [[nodiscard]] SinCosResult SinCosEst() const noexcept;
    /** @brief Absolute error under 3e-7 within one period, 5e-6 out to +-40 radians */
    [[nodiscard]] Vector Sin() const noexcept;
    /** @brief Absolute error under 3e-7 within one period, 5e-6 out to +-40 radians */
    [[nodiscard]] Vector Cos() const noexcept;
    /** @brief Absolute error under 2e-5 */
    [[nodiscard]] Vector SinEst() const noexcept;
    /** @brief Absolute error under 2e-5 */
    [[nodiscard]] Vector CosEst() const noexcept;

    /** @brief Sin/Cos, so accuracy degrades near the poles at odd multiples of pi/2 */
    [[nodiscard]] Vector Tan() const noexcept;
    [[nodiscard]] Vector TanEst() const noexcept;

    /** @brief Input outside [-1, 1] yields NaN. Absolute error under 1e-5 */
    [[nodiscard]] Vector ASin() const noexcept;
    /** @brief Input outside [-1, 1] yields NaN. Absolute error under 1e-5 */
    [[nodiscard]] Vector ACos() const noexcept;
    /** @brief Result in [-pi/2, pi/2]. Absolute error under 1e-6 */
    [[nodiscard]] Vector ATan() const noexcept;

    /** @brief Absolute error under 1e-5; saturates to +-1 rather than overflowing */
    [[nodiscard]] Vector TanH() const noexcept;
    /** @brief Overflows to infinity past |x| ~= 89.
     *  @note A difference of two exponentials, so relative error is poor near zero where sinh(x) -> x
     *  and the two terms cancel. Absolute error stays under 2e-4. */
    [[nodiscard]] Vector SinH() const noexcept;
    /** @brief Overflows to infinity past |x| ~= 89 */
    [[nodiscard]] Vector CosH() const noexcept;

    // Est forms of the reciprocals. Backend-dependent accuracy, and more so than the polynomial
    // approximations: DirectXMath has hardware rcp/rsqrt, wasm has neither. On wasm, reciprocal and
    // square root are already single instructions, so only ReciprocalSqrtEst actually approximates -
    // via a seeded Newton-Raphson step, landing near 2e-3 against DirectXMath's ~4e-4.
    [[nodiscard]] Vector ReciprocalEst() const noexcept;
    [[nodiscard]] Vector ReciprocalSqrtEst() const noexcept;
    [[nodiscard]] Vector SqrtEst() const noexcept;

    template<int N>
    [[nodiscard]] Vector NormalizeEst() const noexcept;

    template<int N>
    [[nodiscard]] float LengthEst() const noexcept;

    template<int N>
    [[nodiscard]] float ReciprocalLengthEst() const noexcept;

    static Vector Replicate(float scalar) noexcept;
    static Vector Zero() noexcept;
    static Vector Identity() noexcept;
    static Vector Abs(Vector vec) noexcept;
    static Vector Pow(Vector base, float exponent) noexcept;
    static Vector Pow(Vector base, Vector exponent) noexcept;
    /** @brief e^x, via Exp2 */
    static Vector Exp(Vector value) noexcept;
    /** @brief 10^x, via Exp2 */
    static Vector Exp10(Vector value) noexcept;
    /** @brief Natural log, via Log2 */
    static Vector Log(Vector value) noexcept;
    static Vector Log10(Vector value) noexcept;
    /** @brief Result in [-pi, pi], quadrant from both signs. x == y == 0 yields 0 */
    static Vector ATan2(Vector y, Vector x) noexcept;
    static Vector Infinity() noexcept;
    static Vector QuietNaN() noexcept;
    static Vector Epsilon() noexcept;
    
    // RH coordinate system basis vectors, assuming a right-handed system with Y up and Z forward
    static Vector Up() noexcept;
    static Vector Down() noexcept;
    static Vector Right() noexcept;
    static Vector Left() noexcept;
    static Vector Forward() noexcept;
    static Vector Backward() noexcept;

private:
#if VX_MATH_BACKEND_WASM
    v128_t data;
#else
    DirectX::XMVECTOR data;
#endif
};

/**
 * Structured binding works: `auto [sine, cosine] = angle.SinCos();`. The pair shares its range
 * reduction and half the polynomial work, costing ~1.3x one of them rather than 2x.
 */
struct SinCosResult
{
    Vector Sin;
    Vector Cos;
};

/**
 * Unit quaternion, laid out (x, y, z, w) with w the scalar part.
 *
 * The conversion is one-way on purpose - implicit *to* Vector, explicit *from* - so a position cannot
 * be passed where a rotation belongs. TRS takes four same-typed arguments and a silent swap there
 * yields a plausible-looking wrong matrix. It also puts Identity() where (0,0,0,1) is right;
 * Vector::Identity() is (1,1,1,1).
 *
 * Everything here assumes unit length. Non-unit quaternions encode a rotation composed with a scale,
 * so Normalize() after accumulating.
 */
struct alignas(16) Quaternion
{
public:
    /** @brief The identity rotation, (0, 0, 0, 1) */
    Quaternion() noexcept;
    Quaternion(float x, float y, float z, float w) noexcept;
    explicit Quaternion(Vector vec) noexcept;

#if VX_MATH_BACKEND_WASM
    explicit Quaternion(v128_t vec) noexcept : data{vec} {}
    [[nodiscard]] v128_t Data() const noexcept { return data; }
#else
    explicit Quaternion(DirectX::XMVECTOR vec) noexcept : data{vec} {}
    [[nodiscard]] DirectX::XMVECTOR Data() const noexcept { return data; }
#endif

    Quaternion(const Quaternion& other) noexcept = default;
    Quaternion(Quaternion&& other) noexcept = default;
    Quaternion& operator=(const Quaternion& other) noexcept = default;
    Quaternion& operator=(Quaternion&& other) noexcept = default;

    /** @brief Implicit, so a Quaternion can be stored or passed to Vector-taking interop */
    operator Vector() const noexcept;

    /** Accessing a single scalar value in this type is comparatively slow */
    [[nodiscard]] float x() const noexcept;
    /** Accessing a single scalar value in this type is comparatively slow */
    [[nodiscard]] float y() const noexcept;
    /** Accessing a single scalar value in this type is comparatively slow */
    [[nodiscard]] float z() const noexcept;
    /** Accessing a single scalar value in this type is comparatively slow */
    [[nodiscard]] float w() const noexcept;

    /**
     * @brief Composition. **This rotation applies first, then `second`.**
     *
     * DirectXMath's XMQuaternionMultiply order, and the opposite of how the product q1*q2 usually
     * reads. Named rather than operator* so the order has somewhere to live: backwards, it yields
     * rotations that look fine alone and only go wrong once they compose.
     */
    [[nodiscard]] Quaternion Multiply(Quaternion second) const noexcept;

    /** @brief Negates the vector part; for a unit quaternion this is also the inverse */
    [[nodiscard]] Quaternion Conjugate() const noexcept;
    /** @brief Conjugate over squared length, so correct for non-unit quaternions too */
    [[nodiscard]] Quaternion Inverse() const noexcept;
    [[nodiscard]] Quaternion Normalize() const noexcept;

    [[nodiscard]] float Dot(Quaternion other) const noexcept;
    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] float LengthSq() const noexcept;

    [[nodiscard]] bool IsIdentity() const noexcept;

    /** @brief 3-vector only: input w ignored, output w zero. Cheaper, but *not* a 4D rotation - for
     *  that, multiply by Matrix::RotationQuaternion(*this) instead. */
    [[nodiscard]] Vector RotateVector(Vector vec) const noexcept;

    /** @brief Equivalent to Matrix::RotationQuaternion(*this) */
    [[nodiscard]] Matrix ToMatrix() const noexcept;

    /** @brief Axis has magnitude sin(angle/2) and w == 0; normalize if you need it unit.
     *  @note Partly scalar, so comparatively slow. Use sparingly. */
    void ToAxisAngle(Vector& out_axis, float& out_radians) const noexcept;

    static Quaternion Identity() noexcept;
    /** @brief Normalizes `axis` for you; RotationNormal skips that */
    static Quaternion RotationAxis(Vector axis, float radians) noexcept;
    /** @brief Assumes `normal_axis` is already unit length */
    static Quaternion RotationNormal(Vector normal_axis, float radians) noexcept;
    /** @brief Euler angles in radians, composed X (pitch), Y (yaw), Z (roll), so ToMatrix() equals
     *  RotationX(pitch) * RotationY(yaw) * RotationZ(roll). Composed by hand on both backends rather
     *  than forwarding, so they cannot disagree on the order. */
    static Quaternion RotationRollPitchYaw(float pitch, float yaw, float roll) noexcept;
    /** @brief Assumes the upper-left 3x3 is orthonormal.
     *  @note Scalar and branchy for robustness (see QuaternionBackendWASM.inl), so slow. Use sparingly. */
    static Quaternion FromMatrix(const Matrix& mat) noexcept;
    /** @brief Shortest-arc interpolation. Falls back to a normalized lerp for near-parallel endpoints */
    static Quaternion Slerp(Quaternion from, Quaternion to, float t) noexcept;

private:
#if VX_MATH_BACKEND_WASM
    v128_t data;
#else
    DirectX::XMVECTOR data;
#endif
};

/**
 * Matrix3x3 storage type for persistence and interop.
 * @note You cannot perform mathematical operations directly on these types, you
 * must convert them to the SIMD Matrix type first via ToMatrix(). This is an
 * intentional design choice!
 */
struct Float3x3
{
public:
    // Constructors
    constexpr Float3x3() noexcept : m{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    } {}

    constexpr Float3x3(
        float m00, float m01, float m02,
        float m10, float m11, float m12,
        float m20, float m21, float m22
    ) noexcept : m{
        {m00, m01, m02},
        {m10, m11, m12},
        {m20, m21, m22}
    } {}

    constexpr Float3x3(const Float3& row0, const Float3& row1, const Float3& row2) noexcept
        : m{
            {row0.x, row0.y, row0.z},
            {row1.x, row1.y, row1.z},
            {row2.x, row2.y, row2.z}
        } {}

    // Matrix conversion constructors
    // Extracts upper-left 3x3 portion from 4x4 matrix (useful for removing translation from view matrices for skybox rendering)
    explicit constexpr Float3x3(const Float4x4& mat4x4) noexcept;

    constexpr Float3x3(const Float3x3& other) noexcept = default;
    constexpr Float3x3(Float3x3&& other) noexcept = default;
    constexpr Float3x3& operator=(const Float3x3& other) noexcept = default;
    constexpr Float3x3& operator=(Float3x3&& other) noexcept = default;

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t row) noexcept
    {
        assert(row < 3);
        return self.m[row];
    }

    // C++23 multidimensional subscript operator; const/non-const collapsed via deducing this.
    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t row, size_t col) noexcept
    {
        assert(row < 3 && col < 3);
        return self.m[row][col];
    }

    constexpr void SetRow(size_t index, const Float3& row) noexcept;
    constexpr void SetColumn(size_t index, const Float3& column) noexcept;

    template <class Self>
    constexpr auto&& Data(this Self&& self) noexcept
    {
        return self.m;
    }

    constexpr bool operator==(const Float3x3& rhs) const noexcept;
    constexpr bool operator!=(const Float3x3& rhs) const noexcept;

    // Static factory methods
    static constexpr Float3x3 Identity() noexcept;
    static constexpr Float3x3 Zero() noexcept;

    struct RowProxy
    {
        const Float3x3& Matrix;
        size_t RowIdx;

        [[nodiscard]] constexpr operator Float2() const noexcept
        {
            return Float2{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1] };
        }

        [[nodiscard]] constexpr operator Float3() const noexcept
        {
            return Float3{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2] };
        }

        [[nodiscard]] constexpr operator Float4() const noexcept
        {
            return Float4{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2], 0.0f };
        }

        [[nodiscard]] constexpr operator Vector() const noexcept
        {
            return Vector{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2], 0.0f };
        }
    };

    // Note: does *not* work with auto type deduction directly, because the return type
    // is a proxy type, and C++ cannot deduce the return type of a proxy like this automatically
    [[nodiscard]] constexpr RowProxy Row(size_t index) const noexcept
    {
        return RowProxy{ *this, index };
    }

    // `GetRow()` uses explicit templating to allow compatability with auto, or if 
    // users just prefer to explicitly specify the return type.
    template<typename T>
    [[nodiscard]] constexpr T GetRow(size_t idx) const noexcept
    {
        return Row(idx);
    }

    struct ColumnProxy
    {
        const Float3x3& Matrix;
        size_t ColIdx;

        [[nodiscard]] constexpr operator Float2() const noexcept
        {
            return Float2{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx] };
        }

        [[nodiscard]] constexpr operator Float3() const noexcept
        {
            return Float3{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx] };
        }

        [[nodiscard]] constexpr operator Float4() const noexcept
        {
            return Float4{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx], 0.0f };
        }

        [[nodiscard]] constexpr operator Vector() const noexcept
        {
            return Vector{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx], 0.0f };
        }
    };

    [[nodiscard]] constexpr ColumnProxy Column(size_t index) const noexcept
    {
        return ColumnProxy{ *this, index };
    }

    template<typename T>
    [[nodiscard]] constexpr T GetColumn(size_t idx) const noexcept
    {
        return Column(idx);
    }

private:
    float m[3][3];
};

/**
 * Matrix4x3 storage type for persistence and interop.
 * 4x3 matrices are commonly used for affine transformations, those without perspective.
 * Row 3 is the translation row; rows 0-2 are the linear (rotation/scale) part.
 * @note You cannot perform mathematical operations directly on these types, you
 * must convert them to the SIMD Matrix type first via ToMatrix(). This is an
 * intentional design choice!
 */
struct Float4x3
{
public:
    constexpr Float4x3() noexcept : m{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f}
    } {}

    constexpr Float4x3(
        float m00, float m01, float m02,
        float m10, float m11, float m12,
        float m20, float m21, float m22,
        float m30, float m31, float m32
    ) noexcept : m{
        {m00, m01, m02},
        {m10, m11, m12},
        {m20, m21, m22},
        {m30, m31, m32}
    } {}

    constexpr Float4x3(const Float3& row0, const Float3& row1, const Float3& row2, const Float3& row3) noexcept
        : m{
            {row0.x, row0.y, row0.z},
            {row1.x, row1.y, row1.z},
            {row2.x, row2.y, row2.z},
            {row3.x, row3.y, row3.z}
        } {}

    constexpr Float4x3(const Float4x3& other) noexcept = default;
    constexpr Float4x3(Float4x3&& other) noexcept = default;
    constexpr Float4x3& operator=(const Float4x3& other) noexcept = default;
    constexpr Float4x3& operator=(Float4x3&& other) noexcept = default;

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t row, size_t col) noexcept
    {
        assert(row < 4 && col < 3);
        return self.m[row][col];
    }

    constexpr void SetRow(size_t index, const Float3& row) noexcept;
    constexpr void SetColumn(size_t index, const Float4& column) noexcept;

    template <class Self>
    constexpr auto&& Data(this Self&& self) noexcept
    {
        return self.m;
    }

    constexpr bool operator==(const Float4x3& rhs) const noexcept;
    constexpr bool operator!=(const Float4x3& rhs) const noexcept;

    static constexpr Float4x3 Identity() noexcept;
    static constexpr Float4x3 Zero() noexcept;

    struct RowProxy
    {
        const Float4x3& Matrix;
        size_t RowIdx{ 0u };

        [[nodiscard]] constexpr operator Float2() const noexcept
        {
            return Float2{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1] };
        }

        [[nodiscard]] constexpr operator Float3() const noexcept
        {
            return Float3{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2] };
        }

        [[nodiscard]] constexpr operator Float4() const noexcept
        {
            return Float4{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2], 0.0f };
        }

        [[nodiscard]] constexpr operator Vector() const noexcept
        {
            return Vector{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2], 0.0f };
        }

    };

    [[nodiscard]] constexpr RowProxy Row(size_t index) const noexcept
    {
        return RowProxy{ *this, index };
    }

    template<typename T>
    [[nodiscard]] constexpr T GetRow(size_t index) const noexcept
    {
        return Row(index);
    }

    struct ColumnProxy
    {
        const Float4x3& Matrix;
        size_t ColIdx{ 0u };

        [[nodiscard]] constexpr operator Float2() const noexcept
        {
            return Float2{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx] };
        }

        [[nodiscard]] constexpr operator Float3() const noexcept
        {
            return Float3{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx] };
        }

        [[nodiscard]] constexpr operator Float4() const noexcept
        {
            return Float4{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx], Matrix.m[3][ColIdx] };
        }
        
        [[nodiscard]] constexpr operator Vector() const noexcept
        {
            return Vector{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx], Matrix.m[3][ColIdx] };
        }
    };

    [[nodiscard]] constexpr ColumnProxy Column(size_t index) const noexcept
    {
        return ColumnProxy{ *this, index };
    }

    template<typename T>
    [[nodiscard]] constexpr T GetColumn(size_t index) const noexcept
    {
        return Column(index);
    }

private:
    float m[4][3];
};

/**
 * Matrix4x4 storage type for persistence and interop.
 * @note You cannot perform mathematical operations directly on these types, you
 * must convert them to the SIMD Matrix type first via ToMatrix(). This is an
 * intentional design choice!
 */
struct Float4x4
{
public:
    constexpr Float4x4() noexcept : m{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    } {}

    constexpr Float4x4(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33
    ) noexcept : m{
        {m00, m01, m02, m03},
        {m10, m11, m12, m13},
        {m20, m21, m22, m23},
        {m30, m31, m32, m33}
    } {}

    constexpr Float4x4(const Float4& row0, const Float4& row1, const Float4& row2, const Float4& row3) noexcept
        : m{
            {row0.x, row0.y, row0.z, row0.w},
            {row1.x, row1.y, row1.z, row1.w},
            {row2.x, row2.y, row2.z, row2.w},
            {row3.x, row3.y, row3.z, row3.w}
        } {}

    // Matrix conversion constructors
    // Expands 3x3 matrix to 4x4 with identity translation and w component (useful for converting rotation/scale matrices to full transforms)
    explicit constexpr Float4x4(const Float3x3& mat3x3) noexcept;

    constexpr Float4x4(const Float4x4& other) noexcept = default;
    constexpr Float4x4(Float4x4&& other) noexcept = default;
    constexpr Float4x4& operator=(const Float4x4& other) noexcept = default;
    constexpr Float4x4& operator=(Float4x4&& other) noexcept = default;

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t row) noexcept
    {
        assert(row < 4);
        return self.m[row];
    }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t row, size_t col) noexcept
    {
        assert(row < 4 && col < 4);
        return self.m[row][col];
    }

    // std::array interop
    constexpr Float4x4(const std::array<std::array<float, 4>, 4>& arr) noexcept
        : m{
            {arr[0][0], arr[0][1], arr[0][2], arr[0][3]},
            {arr[1][0], arr[1][1], arr[1][2], arr[1][3]},
            {arr[2][0], arr[2][1], arr[2][2], arr[2][3]},
            {arr[3][0], arr[3][1], arr[3][2], arr[3][3]}
        } {}

    constexpr operator std::array<std::array<float, 4>, 4>() const noexcept
    {
        return std::array<std::array<float, 4>, 4>{
            std::array<float, 4>{ m[0][0], m[0][1], m[0][2], m[0][3] },
            std::array<float, 4>{ m[1][0], m[1][1], m[1][2], m[1][3] },
            std::array<float, 4>{ m[2][0], m[2][1], m[2][2], m[2][3] },
            std::array<float, 4>{ m[3][0], m[3][1], m[3][2], m[3][3] }
        };
    }

    constexpr void SetRow(size_t index, const Float4& row) noexcept;

    [[nodiscard]] constexpr Float4 Column(size_t index) const noexcept;
    constexpr void SetColumn(size_t index, const Float4& column) noexcept;

    template <class Self>
    constexpr auto&& Data(this Self&& self) noexcept
    {
        return self.m;
    }

    constexpr bool operator==(const Float4x4& rhs) const noexcept;
    constexpr bool operator!=(const Float4x4& rhs) const noexcept;

    static constexpr Float4x4 Identity() noexcept;
    static constexpr Float4x4 Zero() noexcept;

    struct RowProxy
    {
        const Float4x4& Matrix;
        size_t RowIdx{ 0u };
        // defined later after complete Vector definition
        [[nodiscard]] constexpr operator Vector() const noexcept
        {
            return Vector{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2], Matrix.m[RowIdx][3] };
        }

        [[nodiscard]] constexpr operator Float2() const noexcept
        {
            return Float2{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1] };
        }

        [[nodiscard]] constexpr operator Float3() const noexcept
        {
            return Float3{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2] };
        }

        [[nodiscard]] constexpr operator Float4() const noexcept
        {
            return Float4{ Matrix.m[RowIdx][0], Matrix.m[RowIdx][1], Matrix.m[RowIdx][2], Matrix.m[RowIdx][3] };
        }

    };

    [[nodiscard]] constexpr RowProxy Row(size_t index) const noexcept
    {
        return RowProxy{ *this, index };
    }

    template<typename T>
    [[nodiscard]] constexpr T GetRow(size_t idx) const noexcept
    {
        return Row(idx);
    }

    struct ColumnProxy
    {
        const Float4x4& Matrix;
        size_t ColIdx{ 0u };

        [[nodiscard]] constexpr operator Float2() const noexcept
        {
            return Float2{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx] };
        }

        [[nodiscard]] constexpr operator Float3() const noexcept
        {
            return Float3{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx] };
        }

        [[nodiscard]] constexpr operator Float4() const noexcept
        {
            return Float4{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx], Matrix.m[3][ColIdx] };
        }

        [[nodiscard]] constexpr operator Vector() const noexcept
        {
            return Vector{ Matrix.m[0][ColIdx], Matrix.m[1][ColIdx], Matrix.m[2][ColIdx], Matrix.m[3][ColIdx] };
        }
    };

    [[nodiscard]] constexpr ColumnProxy Column(size_t index) const noexcept
    {
        return ColumnProxy{ *this, index };
    }

    template<typename T>
    [[nodiscard]] constexpr T GetColumn(size_t idx) const noexcept
    {
        return Column(idx);
    }

private:
    float m[4][4];
};

/**
 * SIMD Matrix type - optimized for mathematical operations, backed by either
 * WASM SIMD128 (Emscripten builds) or DirectXMath (native builds).
 * This type should NOT be stored or persisted. Convert to storage types
 * (Float3x3/4x3/4x4) at the end of mathematical operations and from storage
 * types at the beginning, via ToMatrix/FromMatrix.
 *
 * All operations pass and return by value for optimal SIMD performance.
 * Row-major storage, row-vector convention (v' = v * M), matching the
 * original DirectXMath-backed source this was ported from.
 */
struct alignas(16) Matrix
{
public:
    // Constructors
    Matrix() noexcept;
#if VX_MATH_BACKEND_WASM
    Matrix(v128_t row0, v128_t row1, v128_t row2, v128_t row3) noexcept : data{row0, row1, row2, row3} {}
#else
    Matrix(DirectX::XMMATRIX mat) noexcept : data{mat} {}
#endif
    // Construct from individual elements (4x4)
    Matrix(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33
    ) noexcept;

    // Construct from row vectors
    Matrix(Vector row0, Vector row1, Vector row2, Vector row3) noexcept;

    // Defaulted: 64 bytes of trivially-copyable SIMD registers, so a hand-written copy buys nothing
    // and a self-assignment guard is pure cost. Also keeps Matrix trivially copyable
    Matrix(const Matrix& other) noexcept = default;
    Matrix(Matrix&& other) noexcept = default;
    Matrix& operator=(const Matrix& other) noexcept = default;
    Matrix& operator=(Matrix&& other) noexcept = default;

    // Native SIMD interop
#if VX_MATH_BACKEND_WASM
    struct NativeType { v128_t r[4]; };
    NativeType Data() const noexcept { return NativeType{data[0], data[1], data[2], data[3]}; }
#else
    DirectX::XMMATRIX Data() const noexcept { return data; }
#endif

    // Row access
    [[nodiscard]] Vector GetRow(size_t index) const noexcept;
    void SetRow(size_t index, Vector row) noexcept;

    // Column access
    [[nodiscard]] Vector GetColumn(size_t index) const noexcept;
    void SetColumn(size_t index, Vector column) noexcept;

    // Element access. Read-only: SIMD lanes aren't addressable as float&,
    // so mutation goes through SetElement (matches the original API shape).
    float operator[](size_t row, size_t col) const noexcept;
    void SetElement(size_t row, size_t col, float value) noexcept;

    // Matrix arithmetic operations
    Matrix operator+(const Matrix& rhs) const noexcept;
    Matrix operator-(const Matrix& rhs) const noexcept;
    Matrix operator*(const Matrix& rhs) const noexcept;
    Matrix operator*(float scalar) const noexcept;
    Matrix operator-() const noexcept;

    // Compound assignment operators
    Matrix& operator+=(const Matrix& rhs) noexcept;
    Matrix& operator-=(const Matrix& rhs) noexcept;
    Matrix& operator*=(const Matrix& rhs) noexcept;
    Matrix& operator*=(float scalar) noexcept;

    // Matrix-vector operations
    Vector operator*(Vector vec) const noexcept;

    // Matrix operations
    Matrix Transpose() const noexcept;
    // Cofactor expansion, branch-free. A singular matrix is not special-cased - it yields infinities
    // and NaNs, so call Determinant() first if that is possible.
    Matrix Inverse() const noexcept;
    float Determinant() const noexcept;

    // Transformation matrices (static factory methods)
    static Matrix Translation(Vector translation) noexcept;
    static Matrix Translation(float x, float y, float z) noexcept;
    static Matrix Scale(Vector scale) noexcept;
    static Matrix Scale(float x, float y, float z) noexcept;
    static Matrix Scale(float uniform_scale) noexcept;

    // Rotation matrices
    static Matrix RotationX(float radians) noexcept;
    static Matrix RotationY(float radians) noexcept;
    static Matrix RotationZ(float radians) noexcept;
    static Matrix RotationAxis(Vector axis, float radians) noexcept;
    static Matrix RotationQuaternion(Quaternion rotation) noexcept;
    /** @brief Euler angles in radians. See Quaternion::RotationRollPitchYaw for the exact order */
    static Matrix RotationRollPitchYaw(float pitch, float yaw, float roll) noexcept;

    // Composes scale, then rotation, then translation - the order the name lists them in, and the
    // order they are applied to a row vector (v * S * R * T). Non-uniform scale therefore acts in
    // the object's *local* frame, so the scale axes rotate with the object; that matches glTF,
    // DirectXMath, and every major engine (they write it T * R * S because they put the vector on
    // the other side of the matrix, which is the same transform).
    // The quaternion is assumed to be unit-length - a non-unit quaternion silently yields a scaled
    // and skewed rotation rather than an error.
    static Matrix TRS(Vector translation, Quaternion rotation, Vector scale) noexcept;
    // As above, but rotation happens about rotation_origin instead of the local origin. Only the
    // translation row differs, so this costs a handful of extra instructions over the three-argument
    // form. Rarely needed: in a parented hierarchy the parent transform already acts as the pivot.
    static Matrix TRS(Vector translation,
                      Quaternion rotation,
                      Vector scale,
                      Vector rotation_origin) noexcept;
    // NOTE: LookAt/LookTo default to a right-handed view space, while
    // Perspective/Orthographic below default to left-handed projections.
    // That mismatch was already present in the source this was ported
    // from; preserved as-is here rather than silently changed. Prefer
    // the explicit *LH/*RH variants if you want to be sure.
    static Matrix LookAt(Vector eye, Vector target, Vector up) noexcept;
    static Matrix LookTo(Vector eye, Vector direction, Vector up) noexcept;

    // Projection matrices (D3D/WebGPU-style depth range [0, 1])
    static Matrix Perspective(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) noexcept;
    static Matrix PerspectiveLH(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) noexcept;
    static Matrix PerspectiveRH(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) noexcept;
    static Matrix Orthographic(float width, float height, float near_plane, float far_plane) noexcept;
    static Matrix OrthographicLH(float width, float height, float near_plane, float far_plane) noexcept;
    static Matrix OrthographicRH(float width, float height, float near_plane, float far_plane) noexcept;

    // Utility matrices
    static Matrix Identity() noexcept;
    static Matrix Zero() noexcept;

    // Comparison utilities
    bool IsIdentity() const noexcept;
    bool IsNearlyEqual(const Matrix& other, float epsilon = 0.0001f) const noexcept;

private:
#if VX_MATH_BACKEND_WASM
    v128_t data[4];
#else
    DirectX::XMMATRIX data;
#endif
};

/**
 * Per-lane choice between two vectors: lanes where `mask` is set take `when_set`, lanes where
 * it is clear take `when_clear`. Allows computing both branches of a conditional in parallel
 * and then masking out the result (just like GPUs like to do as well)
 */
Vector Select(VectorMask mask, Vector when_clear, Vector when_set) noexcept;

// Portable, and reading the same coefficient tables as the Vector forms. These exist because libm is
// correctly rounded and therefore slow, and because control-rate code wants one value, not four.

/** @brief Both at once, sharing one range reduction */
ScalarSinCos SinCos(float radians) noexcept;
ScalarSinCos SinCosEst(float radians) noexcept;
float Sin(float radians) noexcept;
float Cos(float radians) noexcept;

float Exp2(float value) noexcept;
float Log2(float value) noexcept;
float Tan(float radians) noexcept;
/** @brief Input outside [-1, 1] yields NaN */
float ASin(float value) noexcept;
/** @brief Input outside [-1, 1] yields NaN */
float ACos(float value) noexcept;
float ATan(float value) noexcept;
float ATan2(float y, float x) noexcept;
float Pow(float base, float exponent) noexcept;
float TanH(float value) noexcept;

/** @brief Wraps into [-pi, pi] */
constexpr float ModAngles(float radians) noexcept;

constexpr float DegreesToRadians(float degrees) noexcept;
constexpr float RadiansToDegrees(float radians) noexcept;
constexpr float Lerp(float from, float to, float t) noexcept;
constexpr float Clamp(float value, float min, float max) noexcept;
constexpr float Saturate(float value) noexcept;

// Vector transformation functions that use matrix and vector types together
template<int N>
Vector Transform(Vector vector, Matrix matrix) noexcept;
Vector TransformNormal(Vector vector, Matrix matrix) noexcept;

// Free function scalar multiplication (scalar * vector) for storage types
constexpr Float2 operator*(float scalar, const Float2& vec) noexcept;
constexpr Float3 operator*(float scalar, const Float3& vec) noexcept;
constexpr Float4 operator*(float scalar, const Float4& vec) noexcept;

// Free function scalar multiplication for SIMD Vector and Matrix
Vector operator*(float scalar, Vector vec) noexcept;
Matrix operator*(float scalar, const Matrix& mat) noexcept;

// Matrix-Vector operations (mixing storage and SIMD types). See the
// "worth flagging" note above the storage types regarding the Float4x3
// variants' assumptions.
constexpr Float3 operator*(const Float3x3& mat, const Float3& vec) noexcept;
constexpr Float4 operator*(const Float4x3& mat, const Float3& vec) noexcept;
constexpr Float4 operator*(const Float4x3& mat, const Float4& vec) noexcept;
constexpr Float4 operator*(const Float4x4& mat, const Float4& vec) noexcept;
constexpr Float3 operator*(const Float4x4& mat, const Float3& vec) noexcept;

// Note: Matrix * Vector is handled by Matrix::operator*(Vector) member function

/**
 * @brief Convert Matrix3x3 storage type to SIMD Matrix
 * Use this function at the START of mathematical operations to convert from storage format
 */
Matrix ToMatrix(const Float3x3& storage) noexcept;

/**
 * @brief Convert Matrix4x3 storage type to SIMD Matrix
 * Use this function at the START of mathematical operations to convert from storage format
 */
Matrix ToMatrix(const Float4x3& storage) noexcept;

/**
 * @brief Convert Matrix4x4 storage type to SIMD Matrix
 * Use this function at the START of mathematical operations to convert from storage format
 */
Matrix ToMatrix(const Float4x4& storage) noexcept;

template<typename T>
T FromMatrix(const Matrix& mat) noexcept;

/**
 * @brief Convert SIMD Matrix to Matrix3x3 storage type
 * Use this function at the END of mathematical operations to convert back to storage format
 */
template<>
Float3x3 FromMatrix(const Matrix& mat) noexcept;

/**
 * @brief Convert SIMD Matrix to Matrix4x3 storage type
 * Use this function at the END of mathematical operations to convert back to storage format
 */
template<>
Float4x3 FromMatrix(const Matrix& mat) noexcept;

/**
 * @brief Convert SIMD Matrix to Matrix4x4 storage type
 * Use this function at the END of mathematical operations to convert back to storage format
 */
template<>
Float4x4 FromMatrix(const Matrix& mat) noexcept;

/**
 * @brief Convert Float2 storage type to SIMD Vector
 * Use this function at the START of mathematical operations to convert from storage format
 */
Vector ToVector(const Float2& storage) noexcept;

/**
 * @brief Convert Float3 storage type to SIMD Vector
 * Use this function at the START of mathematical operations to convert from storage format
 */
Vector ToVector(const Float3& storage) noexcept;

/**
 * @brief Convert Float4 storage type to SIMD Vector
 * Use this function at the START of mathematical operations to convert from storage format
 */
Vector ToVector(const Float4& storage) noexcept;

namespace detail
{
    struct FromVectorProxy
    {
        Vector vec;
        operator Float2() const noexcept;
        operator Float3() const noexcept;
        operator Float4() const noexcept;
    };
}

detail::FromVectorProxy FromVector(Vector vec) noexcept;

/**
 * @brief Converts a 32-bit float to a 16-bit half-precision float (IEEE 754 round-to-nearest-even).
 */
uint16_t FloatToHalf(float value) noexcept;

/**
 * @brief Converts a 16-bit half-precision float to a 32-bit float.
 */
float HalfToFloat(uint16_t value) noexcept;

/**
 * @brief Extracts normalized frustum planes from a row-major view-projection matrix.
 * 
 * Planes are returned in order: Left, Right, Bottom, Top, Near, Far.
 * Each plane normal (xyz) is unit length, and w is the plane distance.
 * Inward-pointing convention: dot(plane.xyz, p) + plane.w >= 0 inside frustum.
 */
std::array<Float4, 6> ExtractFrustumPlanes(const Float4x4& view_proj, bool reverse_z = true) noexcept;

#undef SWIZZLE_2
#undef SWIZZLE_3
#undef SWIZZLE_4

} // namespace velox::math

#include "math/Math.inl"

#endif // !VELOX_RHI_MATH_HPP
