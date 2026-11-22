#pragma once
#ifndef DIAMONDDOGS_FOUNDATION_MATH_STORAGE_HPP
#define DIAMONDDOGS_FOUNDATION_MATH_STORAGE_HPP
#include <array>
#include <cassert>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <tuple>

// Storage types only: standard headers, and no SIMD backend. Code that stores and sends data
// (generated shader host headers, for example) includes this file and not Math.hpp.

// needed early to prefix the swizzle accessors
#if !defined(DD_MATH_FORCEINLINE)
#if defined(_MSC_VER)
#define DD_MATH_FORCEINLINE __forceinline
#elif defined(__clang__)
#define DD_MATH_FORCEINLINE __attribute__((always_inline)) inline
#elif defined(__GNUC__)
#define DD_MATH_FORCEINLINE __attribute__((always_inline)) inline
#else
#define DD_MATH_FORCEINLINE inline
#endif
#endif

namespace math
{

// Swizzle generation macros - these create combinations from storage members
#define SWIZZLE_2_T(Type, a, b) \
    constexpr DD_MATH_FORCEINLINE Type a##b() const noexcept { return Type(a, b); }

#define SWIZZLE_3_T(Type, a, b, c) \
    constexpr DD_MATH_FORCEINLINE Type a##b##c() const noexcept { return Type(a, b, c); }

#define SWIZZLE_4_T(Type, a, b, c, d) \
    constexpr DD_MATH_FORCEINLINE Type a##b##c##d() const noexcept { return Type(a, b, c, d); }

#define SWIZZLE_2(a, b) SWIZZLE_2_T(Float2, a, b)
#define SWIZZLE_3(a, b, c) SWIZZLE_3_T(Float3, a, b, c)
#define SWIZZLE_4(a, b, c, d) SWIZZLE_4_T(Float4, a, b, c, d)

struct Float3;
struct Float4;
struct Int3;
struct Int4;
struct UInt3;
struct UInt4;
struct Float3x3;
struct Float4x3;
struct Float4x4;

/** @brief Scalar counterpart of SinCosResult */
struct ScalarSinCos
{
    float sin;
    float cos;
};

/** @brief Wraps into [-pi, pi] */
constexpr float ModAngles(float radians) noexcept;

/** @brief Both at once, sharing one range reduction */
ScalarSinCos SinCos(float radians) noexcept;

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

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] float LengthSquared() const noexcept;
    [[nodiscard]] float Dot(const Float2& other) const noexcept;

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

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] float LengthSquared() const noexcept;
    [[nodiscard]] float Dot(const Float3& other) const noexcept;
    [[nodiscard]] static Float3 RotateAroundY(const Float3& value, float radians) noexcept;

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

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] float LengthSquared() const noexcept;
    [[nodiscard]] float Dot(const Float4& other) const noexcept;

    [[nodiscard]] constexpr bool NoneOf(float value) const noexcept;
    [[nodiscard]] constexpr bool AnyOf(float value) const noexcept;
    [[nodiscard]] constexpr bool AllOf(float value) const noexcept;

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
    SWIZZLE_4(b, g, r, a)
    SWIZZLE_4(a, r, g, b)
    SWIZZLE_3(x, y, z)
    SWIZZLE_3(r, g, b)
    SWIZZLE_2(x, y)
    SWIZZLE_2(z, w)

    union
    {
        struct { float x, y, z, w; };
        struct { float r, g, b, a; };
        struct { float u, v, s, t; };
    };
};

struct Int3
{
public:
    // Constructors
    constexpr Int3() noexcept : x(0), y(0), z(0) {}
    constexpr Int3(int32_t x, int32_t y, int32_t z) noexcept : x(x), y(y), z(z) {}
    constexpr explicit Int3(int32_t scalar) noexcept : x(scalar), y(scalar), z(scalar) {}

    constexpr Int3(const Int3& other) noexcept = default;
    constexpr Int3(Int3&& other) noexcept = default;
    constexpr Int3& operator=(const Int3& other) noexcept = default;
    constexpr Int3& operator=(Int3&& other) noexcept = default;

    // std::array interop
    constexpr Int3(const std::array<int32_t, 3>& arr) noexcept : x(arr[0]), y(arr[1]), z(arr[2]) {}
    constexpr operator std::array<int32_t, 3>() const noexcept { return { x, y, z }; }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr Int3 operator+(const Int3& rhs) const noexcept;
    constexpr Int3 operator-(const Int3& rhs) const noexcept;
    constexpr Int3 operator*(const Int3& rhs) const noexcept;
    constexpr Int3 operator/(const Int3& rhs) const noexcept;
    constexpr Int3 operator%(const Int3& rhs) const noexcept;

    // Scalar operators
    constexpr Int3 operator*(int32_t scalar) const noexcept;
    constexpr Int3 operator/(int32_t scalar) const noexcept;
    constexpr Int3 operator%(int32_t scalar) const noexcept;

    // Unary operators
    constexpr Int3 operator-() const noexcept;

    // Compound assignment operators
    constexpr Int3& operator+=(const Int3& rhs) noexcept;
    constexpr Int3& operator-=(const Int3& rhs) noexcept;
    constexpr Int3& operator*=(const Int3& rhs) noexcept;
    constexpr Int3& operator/=(const Int3& rhs) noexcept;
    constexpr Int3& operator%=(const Int3& rhs) noexcept;
    constexpr Int3& operator*=(int32_t scalar) noexcept;
    constexpr Int3& operator/=(int32_t scalar) noexcept;
    constexpr Int3& operator%=(int32_t scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const Int3& rhs) const noexcept;
    constexpr std::strong_ordering operator<=>(const Int3& rhs) const noexcept;

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] constexpr int32_t LengthSquared() const noexcept;
    [[nodiscard]] constexpr int32_t Dot(const Int3& other) const noexcept;

    [[nodiscard]] constexpr bool NoneOf(int32_t value) const noexcept;
    [[nodiscard]] constexpr bool AnyOf(int32_t value) const noexcept;
    [[nodiscard]] constexpr bool AllOf(int32_t value) const noexcept;

    SWIZZLE_3_T(Int3, x, y, z)
    SWIZZLE_3_T(Int3, x, z, y)
    SWIZZLE_3_T(Int3, y, x, z)
    SWIZZLE_3_T(Int3, y, z, x)
    SWIZZLE_3_T(Int3, z, x, y)
    SWIZZLE_3_T(Int3, z, y, x)
    SWIZZLE_3_T(Int3, x, x, x)
    SWIZZLE_3_T(Int3, y, y, y)
    SWIZZLE_3_T(Int3, z, z, z)

    union
    {
        struct { int32_t x, y, z; };
        struct { int32_t r, g, b; };
        struct { int32_t u, v, w; };
    };
};

struct Int4
{
public:
    // Constructors
    constexpr Int4() noexcept : x(0), y(0), z(0), w(0) {}
    constexpr Int4(int32_t x, int32_t y, int32_t z, int32_t w) noexcept : x(x), y(y), z(z), w(w) {}
    constexpr explicit Int4(int32_t scalar) noexcept : x(scalar), y(scalar), z(scalar), w(scalar) {}
    constexpr Int4(const Int3& xyz, int32_t w) noexcept : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
    constexpr Int4(int32_t x, const Int3& yzw) noexcept : x(x), y(yzw.x), z(yzw.y), w(yzw.z) {}

    constexpr Int4(const Int4& other) noexcept = default;
    constexpr Int4(Int4&& other) noexcept = default;
    constexpr Int4& operator=(const Int4& other) noexcept = default;
    constexpr Int4& operator=(Int4&& other) noexcept = default;

    // std::array interop
    constexpr Int4(const std::array<int32_t, 4>& arr) noexcept : x(arr[0]), y(arr[1]), z(arr[2]), w(arr[3]) {}
    constexpr operator std::array<int32_t, 4>() const noexcept { return { x, y, z, w }; }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr Int4 operator+(const Int4& rhs) const noexcept;
    constexpr Int4 operator-(const Int4& rhs) const noexcept;
    constexpr Int4 operator*(const Int4& rhs) const noexcept;
    constexpr Int4 operator/(const Int4& rhs) const noexcept;
    constexpr Int4 operator%(const Int4& rhs) const noexcept;

    // Scalar operators
    constexpr Int4 operator*(int32_t scalar) const noexcept;
    constexpr Int4 operator/(int32_t scalar) const noexcept;
    constexpr Int4 operator%(int32_t scalar) const noexcept;

    // Unary operators
    constexpr Int4 operator-() const noexcept;

    // Compound assignment operators
    constexpr Int4& operator+=(const Int4& rhs) noexcept;
    constexpr Int4& operator-=(const Int4& rhs) noexcept;
    constexpr Int4& operator*=(const Int4& rhs) noexcept;
    constexpr Int4& operator/=(const Int4& rhs) noexcept;
    constexpr Int4& operator%=(const Int4& rhs) noexcept;
    constexpr Int4& operator*=(int32_t scalar) noexcept;
    constexpr Int4& operator/=(int32_t scalar) noexcept;
    constexpr Int4& operator%=(int32_t scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const Int4& rhs) const noexcept;
    constexpr std::strong_ordering operator<=>(const Int4& rhs) const noexcept;

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] constexpr int32_t LengthSquared() const noexcept;
    [[nodiscard]] constexpr int32_t Dot(const Int4& other) const noexcept;

    [[nodiscard]] constexpr bool NoneOf(int32_t value) const noexcept;
    [[nodiscard]] constexpr bool AnyOf(int32_t value) const noexcept;
    [[nodiscard]] constexpr bool AllOf(int32_t value) const noexcept;

    SWIZZLE_4_T(Int4, x, y, z, w)
    SWIZZLE_4_T(Int4, x, y, w, z)
    SWIZZLE_4_T(Int4, x, z, y, w)
    SWIZZLE_4_T(Int4, x, z, w, y)
    SWIZZLE_4_T(Int4, x, w, y, z)
    SWIZZLE_4_T(Int4, x, w, z, y)
    SWIZZLE_4_T(Int4, w, x, y, z)
    SWIZZLE_4_T(Int4, x, x, x, x)
    SWIZZLE_4_T(Int4, y, y, y, y)
    SWIZZLE_4_T(Int4, z, z, z, z)
    SWIZZLE_4_T(Int4, w, w, w, w)
    SWIZZLE_4_T(Int4, b, g, r, a)
    SWIZZLE_4_T(Int4, a, r, g, b)
    SWIZZLE_3_T(Int3, x, y, z)
    SWIZZLE_3_T(Int3, r, g, b)

    union
    {
        struct { int32_t x, y, z, w; };
        struct { int32_t r, g, b, a; };
        struct { int32_t u, v, s, t; };
    };
};

struct UInt3
{
public:
    // Constructors
    constexpr UInt3() noexcept : x(0u), y(0u), z(0u) {}
    constexpr UInt3(uint32_t x, uint32_t y, uint32_t z) noexcept : x(x), y(y), z(z) {}
    constexpr explicit UInt3(uint32_t scalar) noexcept : x(scalar), y(scalar), z(scalar) {}

    constexpr UInt3(const UInt3& other) noexcept = default;
    constexpr UInt3(UInt3&& other) noexcept = default;
    constexpr UInt3& operator=(const UInt3& other) noexcept = default;
    constexpr UInt3& operator=(UInt3&& other) noexcept = default;

    // std::array interop
    constexpr UInt3(const std::array<uint32_t, 3>& arr) noexcept : x(arr[0]), y(arr[1]), z(arr[2]) {}
    constexpr operator std::array<uint32_t, 3>() const noexcept { return { x, y, z }; }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr UInt3 operator+(const UInt3& rhs) const noexcept;
    constexpr UInt3 operator-(const UInt3& rhs) const noexcept;
    constexpr UInt3 operator*(const UInt3& rhs) const noexcept;
    constexpr UInt3 operator/(const UInt3& rhs) const noexcept;
    constexpr UInt3 operator%(const UInt3& rhs) const noexcept;

    // Scalar operators
    constexpr UInt3 operator*(uint32_t scalar) const noexcept;
    constexpr UInt3 operator/(uint32_t scalar) const noexcept;
    constexpr UInt3 operator%(uint32_t scalar) const noexcept;

    // Compound assignment operators
    constexpr UInt3& operator+=(const UInt3& rhs) noexcept;
    constexpr UInt3& operator-=(const UInt3& rhs) noexcept;
    constexpr UInt3& operator*=(const UInt3& rhs) noexcept;
    constexpr UInt3& operator/=(const UInt3& rhs) noexcept;
    constexpr UInt3& operator%=(const UInt3& rhs) noexcept;
    constexpr UInt3& operator*=(uint32_t scalar) noexcept;
    constexpr UInt3& operator/=(uint32_t scalar) noexcept;
    constexpr UInt3& operator%=(uint32_t scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const UInt3& rhs) const noexcept;
    constexpr std::strong_ordering operator<=>(const UInt3& rhs) const noexcept;

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] constexpr uint32_t LengthSquared() const noexcept;
    [[nodiscard]] constexpr uint32_t Dot(const UInt3& other) const noexcept;

    [[nodiscard]] constexpr bool NoneOf(uint32_t value) const noexcept;
    [[nodiscard]] constexpr bool AnyOf(uint32_t value) const noexcept;
    [[nodiscard]] constexpr bool AllOf(uint32_t value) const noexcept;

    SWIZZLE_3_T(UInt3, x, y, z)
    SWIZZLE_3_T(UInt3, x, z, y)
    SWIZZLE_3_T(UInt3, y, x, z)
    SWIZZLE_3_T(UInt3, y, z, x)
    SWIZZLE_3_T(UInt3, z, x, y)
    SWIZZLE_3_T(UInt3, z, y, x)
    SWIZZLE_3_T(UInt3, x, x, x)
    SWIZZLE_3_T(UInt3, y, y, y)
    SWIZZLE_3_T(UInt3, z, z, z)

    union
    {
        struct { uint32_t x, y, z; };
        struct { uint32_t r, g, b; };
        struct { uint32_t u, v, w; };
    };
};

struct UInt4
{
public:
    // Constructors
    constexpr UInt4() noexcept : x(0u), y(0u), z(0u), w(0u) {}
    constexpr UInt4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) noexcept : x(x), y(y), z(z), w(w) {}
    constexpr explicit UInt4(uint32_t scalar) noexcept : x(scalar), y(scalar), z(scalar), w(scalar) {}
    constexpr UInt4(const UInt3& xyz, uint32_t w) noexcept : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}
    constexpr UInt4(uint32_t x, const UInt3& yzw) noexcept : x(x), y(yzw.x), z(yzw.y), w(yzw.z) {}

    constexpr UInt4(const UInt4& other) noexcept = default;
    constexpr UInt4(UInt4&& other) noexcept = default;
    constexpr UInt4& operator=(const UInt4& other) noexcept = default;
    constexpr UInt4& operator=(UInt4&& other) noexcept = default;

    // std::array interop
    constexpr UInt4(const std::array<uint32_t, 4>& arr) noexcept : x(arr[0]), y(arr[1]), z(arr[2]), w(arr[3]) {}
    constexpr operator std::array<uint32_t, 4>() const noexcept { return { x, y, z, w }; }

    template <class Self>
    constexpr auto&& operator[](this Self&& self, size_t index) noexcept
    {
        return (&self.x)[index];
    }

    // Arithmetic operators
    constexpr UInt4 operator+(const UInt4& rhs) const noexcept;
    constexpr UInt4 operator-(const UInt4& rhs) const noexcept;
    constexpr UInt4 operator*(const UInt4& rhs) const noexcept;
    constexpr UInt4 operator/(const UInt4& rhs) const noexcept;
    constexpr UInt4 operator%(const UInt4& rhs) const noexcept;

    // Scalar operators
    constexpr UInt4 operator*(uint32_t scalar) const noexcept;
    constexpr UInt4 operator/(uint32_t scalar) const noexcept;
    constexpr UInt4 operator%(uint32_t scalar) const noexcept;

    // Compound assignment operators
    constexpr UInt4& operator+=(const UInt4& rhs) noexcept;
    constexpr UInt4& operator-=(const UInt4& rhs) noexcept;
    constexpr UInt4& operator*=(const UInt4& rhs) noexcept;
    constexpr UInt4& operator/=(const UInt4& rhs) noexcept;
    constexpr UInt4& operator%=(const UInt4& rhs) noexcept;
    constexpr UInt4& operator*=(uint32_t scalar) noexcept;
    constexpr UInt4& operator/=(uint32_t scalar) noexcept;
    constexpr UInt4& operator%=(uint32_t scalar) noexcept;

    // Comparison operators
    constexpr bool operator==(const UInt4& rhs) const noexcept;
    constexpr std::strong_ordering operator<=>(const UInt4& rhs) const noexcept;

    [[nodiscard]] float Length() const noexcept;
    [[nodiscard]] constexpr uint32_t LengthSquared() const noexcept;
    [[nodiscard]] constexpr uint32_t Dot(const UInt4& other) const noexcept;

    [[nodiscard]] constexpr bool NoneOf(uint32_t value) const noexcept;
    [[nodiscard]] constexpr bool AnyOf(uint32_t value) const noexcept;
    [[nodiscard]] constexpr bool AllOf(uint32_t value) const noexcept;

    SWIZZLE_4_T(UInt4, x, y, z, w)
    SWIZZLE_4_T(UInt4, x, y, w, z)
    SWIZZLE_4_T(UInt4, x, z, y, w)
    SWIZZLE_4_T(UInt4, x, z, w, y)
    SWIZZLE_4_T(UInt4, x, w, y, z)
    SWIZZLE_4_T(UInt4, x, w, z, y)
    SWIZZLE_4_T(UInt4, w, x, y, z)
    SWIZZLE_4_T(UInt4, x, x, x, x)
    SWIZZLE_4_T(UInt4, y, y, y, y)
    SWIZZLE_4_T(UInt4, z, z, z, z)
    SWIZZLE_4_T(UInt4, w, w, w, w)
    SWIZZLE_4_T(UInt4, b, g, r, a)
    SWIZZLE_4_T(UInt4, a, r, g, b)
    SWIZZLE_3_T(UInt3, x, y, z)
    SWIZZLE_3_T(UInt3, r, g, b)

    union
    {
        struct { uint32_t x, y, z, w; };
        struct { uint32_t r, g, b, a; };
        struct { uint32_t u, v, s, t; };
    };
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

    [[nodiscard]] constexpr Float3 Row(size_t index) const noexcept;
    [[nodiscard]] constexpr Float3 Column(size_t index) const noexcept;

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

    [[nodiscard]] constexpr Float3 Row(size_t index) const noexcept;
    [[nodiscard]] constexpr Float4 Column(size_t index) const noexcept;

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

    [[nodiscard]] constexpr Float4 Row(size_t index) const noexcept;
    [[nodiscard]] constexpr Float4 Column(size_t index) const noexcept;

private:
    float m[4][4];
};


// Free function scalar multiplication (scalar * vector) for storage types
constexpr Float2 operator*(float scalar, const Float2& vec) noexcept;
constexpr Float3 operator*(float scalar, const Float3& vec) noexcept;
constexpr Float4 operator*(float scalar, const Float4& vec) noexcept;
constexpr Int3 operator*(int32_t scalar, const Int3& vec) noexcept;
constexpr Int4 operator*(int32_t scalar, const Int4& vec) noexcept;
constexpr UInt3 operator*(uint32_t scalar, const UInt3& vec) noexcept;
constexpr UInt4 operator*(uint32_t scalar, const UInt4& vec) noexcept;

// Matrix-Vector operations (mixing storage and SIMD types). See the
// "worth flagging" note above the storage types regarding the Float4x3
// variants' assumptions.
constexpr Float3 operator*(const Float3x3& mat, const Float3& vec) noexcept;
constexpr Float4 operator*(const Float4x3& mat, const Float3& vec) noexcept;
constexpr Float4 operator*(const Float4x3& mat, const Float4& vec) noexcept;
constexpr Float4 operator*(const Float4x4& mat, const Float4& vec) noexcept;
constexpr Float3 operator*(const Float4x4& mat, const Float3& vec) noexcept;

#undef SWIZZLE_2
#undef SWIZZLE_3
#undef SWIZZLE_4
#undef SWIZZLE_2_T
#undef SWIZZLE_3_T
#undef SWIZZLE_4_T

} // namespace math

#include "math/MathStorage.inl"

#endif // !DIAMONDDOGS_FOUNDATION_MATH_STORAGE_HPP
