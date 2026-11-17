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
#include "math/MathStorage.hpp"


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

// Forward declarations
struct Vector;
struct VectorMask;
struct Quaternion;
struct Matrix;
// Defined after Vector, which it holds two of; Vector::SinCos only needs the declaration
struct SinCosResult;

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

constexpr float DegreesToRadians(float degrees) noexcept;
constexpr float RadiansToDegrees(float radians) noexcept;
constexpr float Lerp(float from, float to, float t) noexcept;
constexpr float Clamp(float value, float min, float max) noexcept;
constexpr float Saturate(float value) noexcept;

// Vector transformation functions that use matrix and vector types together
template<int N>
Vector Transform(Vector vector, Matrix matrix) noexcept;
Vector TransformNormal(Vector vector, Matrix matrix) noexcept;

// Free function scalar multiplication for SIMD Vector and Matrix
Vector operator*(float scalar, Vector vec) noexcept;
Matrix operator*(float scalar, const Matrix& mat) noexcept;

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

/**
 * @brief Row of a storage matrix as a SIMD Vector. Float3x3 and Float4x3 rows have w = 0.
 */
Vector RowVector(const Float3x3& matrix, size_t index) noexcept;
Vector RowVector(const Float4x3& matrix, size_t index) noexcept;
Vector RowVector(const Float4x4& matrix, size_t index) noexcept;

/**
 * @brief Column of a storage matrix as a SIMD Vector. Float3x3 columns have w = 0.
 */
Vector ColumnVector(const Float3x3& matrix, size_t index) noexcept;
Vector ColumnVector(const Float4x3& matrix, size_t index) noexcept;
Vector ColumnVector(const Float4x4& matrix, size_t index) noexcept;

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

} // namespace velox::math

#include "math/Math.inl"

#endif // !VELOX_RHI_MATH_HPP
