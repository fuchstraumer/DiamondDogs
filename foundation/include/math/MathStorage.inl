#pragma once
#include <numbers>

#include "math/MathPolynomials.hpp"
#include "math/MathStorage.hpp"
// Implementation file for MathStorage.hpp: portable code only, with no SIMD backend

namespace math
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

inline float Float2::Length() const noexcept
{
    return std::sqrtf((x * x) + (y * y));
}

inline float Float2::LengthSquared() const noexcept
{
    return (x * x) + (y * y);
}

inline float Float2::Dot(const Float2& other) const noexcept
{
    return (x * other.x) + (y * other.y);
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

inline float Float3::Length() const noexcept
{
    return std::sqrtf((x * x) + (y * y) + (z * z));
}

inline float Float3::LengthSquared() const noexcept
{
    return (x * x) + (y * y) + (z * z);
}

inline float Float3::Dot(const Float3& other) const noexcept
{
    return (x * other.x) + (y * other.y) + (z * other.z);
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

inline float Float4::Length() const noexcept
{
    return std::sqrtf((x * x) + (y * y) + (z * z) + (w * w));
}

inline float Float4::LengthSquared() const noexcept
{
    return (x * x) + (y * y) + (z * z) + (w * w);
}

inline float Float4::Dot(const Float4& other) const noexcept
{
    return (x * other.x) + (y * other.y) + (z * other.z) + (w * other.w);
}

// ================================
// Int3 implementations
// ================================
constexpr inline Int3 Int3::operator+(const Int3& rhs) const noexcept
{
    return Int3(x + rhs.x, y + rhs.y, z + rhs.z);
}

constexpr inline Int3 Int3::operator-(const Int3& rhs) const noexcept
{
    return Int3(x - rhs.x, y - rhs.y, z - rhs.z);
}

constexpr inline Int3 Int3::operator*(const Int3& rhs) const noexcept
{
    return Int3(x * rhs.x, y * rhs.y, z * rhs.z);
}

constexpr inline Int3 Int3::operator/(const Int3& rhs) const noexcept
{
    return Int3(x / rhs.x, y / rhs.y, z / rhs.z);
}

constexpr inline Int3 Int3::operator%(const Int3& rhs) const noexcept
{
    return Int3(x % rhs.x, y % rhs.y, z % rhs.z);
}

constexpr inline Int3 Int3::operator*(int32_t scalar) const noexcept
{
    return Int3(x * scalar, y * scalar, z * scalar);
}

constexpr inline Int3 Int3::operator/(int32_t scalar) const noexcept
{
    return Int3(x / scalar, y / scalar, z / scalar);
}

constexpr inline Int3 Int3::operator%(int32_t scalar) const noexcept
{
    return Int3(x % scalar, y % scalar, z % scalar);
}

constexpr inline Int3 Int3::operator-() const noexcept
{
    return Int3(-x, -y, -z);
}

constexpr Int3& Int3::operator+=(const Int3& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
}

constexpr Int3& Int3::operator-=(const Int3& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
}

constexpr Int3& Int3::operator*=(const Int3& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    z *= rhs.z;
    return *this;
}

constexpr Int3& Int3::operator/=(const Int3& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    z /= rhs.z;
    return *this;
}

constexpr Int3& Int3::operator%=(const Int3& rhs) noexcept
{
    x %= rhs.x;
    y %= rhs.y;
    z %= rhs.z;
    return *this;
}

constexpr Int3& Int3::operator*=(int32_t scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

constexpr Int3& Int3::operator/=(int32_t scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

constexpr Int3& Int3::operator%=(int32_t scalar) noexcept
{
    x %= scalar;
    y %= scalar;
    z %= scalar;
    return *this;
}

constexpr bool Int3::operator==(const Int3& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y && z == rhs.z;
}

constexpr std::strong_ordering Int3::operator<=>(const Int3& rhs) const noexcept
{
    return std::tie(x, y, z) <=> std::tie(rhs.x, rhs.y, rhs.z);
}

inline float Int3::Length() const noexcept
{
    return std::sqrtf(static_cast<float>(LengthSquared()));
}

constexpr int32_t Int3::LengthSquared() const noexcept
{
    return (x * x) + (y * y) + (z * z);
}

constexpr int32_t Int3::Dot(const Int3& other) const noexcept
{
    return (x * other.x) + (y * other.y) + (z * other.z);
}

constexpr Int3 Int3::Min(const Int3& other) const noexcept
{
    return Int3(x < other.x ? x : other.x, y < other.y ? y : other.y, z < other.z ? z : other.z);
}

constexpr Int3 Int3::Max(const Int3& other) const noexcept
{
    return Int3(x > other.x ? x : other.x, y > other.y ? y : other.y, z > other.z ? z : other.z);
}

constexpr bool Int3::NoneOf(int32_t value) const noexcept
{
    return x != value && y != value && z != value;
}

constexpr bool Int3::AnyOf(int32_t value) const noexcept
{
    return x == value || y == value || z == value;
}

constexpr bool Int3::AllOf(int32_t value) const noexcept
{
    return x == value && y == value && z == value;
}

// ================================
// Int4 implementations
// ================================
constexpr inline Int4 Int4::operator+(const Int4& rhs) const noexcept
{
    return Int4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
}

constexpr inline Int4 Int4::operator-(const Int4& rhs) const noexcept
{
    return Int4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
}

constexpr inline Int4 Int4::operator*(const Int4& rhs) const noexcept
{
    return Int4(x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w);
}

constexpr inline Int4 Int4::operator/(const Int4& rhs) const noexcept
{
    return Int4(x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w);
}

constexpr inline Int4 Int4::operator%(const Int4& rhs) const noexcept
{
    return Int4(x % rhs.x, y % rhs.y, z % rhs.z, w % rhs.w);
}

constexpr inline Int4 Int4::operator*(int32_t scalar) const noexcept
{
    return Int4(x * scalar, y * scalar, z * scalar, w * scalar);
}

constexpr inline Int4 Int4::operator/(int32_t scalar) const noexcept
{
    return Int4(x / scalar, y / scalar, z / scalar, w / scalar);
}

constexpr inline Int4 Int4::operator%(int32_t scalar) const noexcept
{
    return Int4(x % scalar, y % scalar, z % scalar, w % scalar);
}

constexpr inline Int4 Int4::operator-() const noexcept
{
    return Int4(-x, -y, -z, -w);
}

constexpr Int4& Int4::operator+=(const Int4& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
}

constexpr Int4& Int4::operator-=(const Int4& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
}

constexpr Int4& Int4::operator*=(const Int4& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    z *= rhs.z;
    w *= rhs.w;
    return *this;
}

constexpr Int4& Int4::operator/=(const Int4& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    z /= rhs.z;
    w /= rhs.w;
    return *this;
}

constexpr Int4& Int4::operator%=(const Int4& rhs) noexcept
{
    x %= rhs.x;
    y %= rhs.y;
    z %= rhs.z;
    w %= rhs.w;
    return *this;
}

constexpr Int4& Int4::operator*=(int32_t scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

constexpr Int4& Int4::operator/=(int32_t scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

constexpr Int4& Int4::operator%=(int32_t scalar) noexcept
{
    x %= scalar;
    y %= scalar;
    z %= scalar;
    w %= scalar;
    return *this;
}

constexpr bool Int4::operator==(const Int4& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
}

constexpr std::strong_ordering Int4::operator<=>(const Int4& rhs) const noexcept
{
    return std::tie(x, y, z, w) <=> std::tie(rhs.x, rhs.y, rhs.z, rhs.w);
}

inline float Int4::Length() const noexcept
{
    return std::sqrtf(static_cast<float>(LengthSquared()));
}

constexpr int32_t Int4::LengthSquared() const noexcept
{
    return (x * x) + (y * y) + (z * z) + (w * w);
}

constexpr int32_t Int4::Dot(const Int4& other) const noexcept
{
    return (x * other.x) + (y * other.y) + (z * other.z) + (w * other.w);
}

constexpr Int4 Int4::Min(const Int4& other) const noexcept
{
    return Int4(
        x < other.x ? x : other.x,
        y < other.y ? y : other.y,
        z < other.z ? z : other.z,
        w < other.w ? w : other.w
    );
}

constexpr Int4 Int4::Max(const Int4& other) const noexcept
{
    return Int4(
        x > other.x ? x : other.x,
        y > other.y ? y : other.y,
        z > other.z ? z : other.z,
        w > other.w ? w : other.w
    );
}

constexpr bool Int4::NoneOf(int32_t value) const noexcept
{
    return x != value && y != value && z != value && w != value;
}

constexpr bool Int4::AnyOf(int32_t value) const noexcept
{
    return x == value || y == value || z == value || w == value;
}

constexpr bool Int4::AllOf(int32_t value) const noexcept
{
    return x == value && y == value && z == value && w == value;
}

// ================================
// UInt3 implementations
// ================================
constexpr inline UInt3 UInt3::operator+(const UInt3& rhs) const noexcept
{
    return UInt3(x + rhs.x, y + rhs.y, z + rhs.z);
}

constexpr inline UInt3 UInt3::operator-(const UInt3& rhs) const noexcept
{
    return UInt3(x - rhs.x, y - rhs.y, z - rhs.z);
}

constexpr inline UInt3 UInt3::operator*(const UInt3& rhs) const noexcept
{
    return UInt3(x * rhs.x, y * rhs.y, z * rhs.z);
}

constexpr inline UInt3 UInt3::operator/(const UInt3& rhs) const noexcept
{
    return UInt3(x / rhs.x, y / rhs.y, z / rhs.z);
}

constexpr inline UInt3 UInt3::operator%(const UInt3& rhs) const noexcept
{
    return UInt3(x % rhs.x, y % rhs.y, z % rhs.z);
}

constexpr inline UInt3 UInt3::operator*(uint32_t scalar) const noexcept
{
    return UInt3(x * scalar, y * scalar, z * scalar);
}

constexpr inline UInt3 UInt3::operator/(uint32_t scalar) const noexcept
{
    return UInt3(x / scalar, y / scalar, z / scalar);
}

constexpr inline UInt3 UInt3::operator%(uint32_t scalar) const noexcept
{
    return UInt3(x % scalar, y % scalar, z % scalar);
}

constexpr UInt3& UInt3::operator+=(const UInt3& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
}

constexpr UInt3& UInt3::operator-=(const UInt3& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
}

constexpr UInt3& UInt3::operator*=(const UInt3& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    z *= rhs.z;
    return *this;
}

constexpr UInt3& UInt3::operator/=(const UInt3& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    z /= rhs.z;
    return *this;
}

constexpr UInt3& UInt3::operator%=(const UInt3& rhs) noexcept
{
    x %= rhs.x;
    y %= rhs.y;
    z %= rhs.z;
    return *this;
}

constexpr UInt3& UInt3::operator*=(uint32_t scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

constexpr UInt3& UInt3::operator/=(uint32_t scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

constexpr UInt3& UInt3::operator%=(uint32_t scalar) noexcept
{
    x %= scalar;
    y %= scalar;
    z %= scalar;
    return *this;
}

constexpr bool UInt3::operator==(const UInt3& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y && z == rhs.z;
}

constexpr std::strong_ordering UInt3::operator<=>(const UInt3& rhs) const noexcept
{
    return std::tie(x, y, z) <=> std::tie(rhs.x, rhs.y, rhs.z);
}

inline float UInt3::Length() const noexcept
{
    return std::sqrtf(static_cast<float>(LengthSquared()));
}

constexpr uint32_t UInt3::LengthSquared() const noexcept
{
    return (x * x) + (y * y) + (z * z);
}

constexpr uint32_t UInt3::Dot(const UInt3& other) const noexcept
{
    return (x * other.x) + (y * other.y) + (z * other.z);
}

constexpr UInt3 UInt3::Min(const UInt3& other) const noexcept
{
    return UInt3(x < other.x ? x : other.x, y < other.y ? y : other.y, z < other.z ? z : other.z);
}

constexpr UInt3 UInt3::Max(const UInt3& other) const noexcept
{
    return UInt3(x > other.x ? x : other.x, y > other.y ? y : other.y, z > other.z ? z : other.z);
}

constexpr bool UInt3::NoneOf(uint32_t value) const noexcept
{
    return x != value && y != value && z != value;
}

constexpr bool UInt3::AnyOf(uint32_t value) const noexcept
{
    return x == value || y == value || z == value;
}

constexpr bool UInt3::AllOf(uint32_t value) const noexcept
{
    return x == value && y == value && z == value;
}

// ================================
// UInt4 implementations
// ================================
constexpr inline UInt4 UInt4::operator+(const UInt4& rhs) const noexcept
{
    return UInt4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
}

constexpr inline UInt4 UInt4::operator-(const UInt4& rhs) const noexcept
{
    return UInt4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
}

constexpr inline UInt4 UInt4::operator*(const UInt4& rhs) const noexcept
{
    return UInt4(x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w);
}

constexpr inline UInt4 UInt4::operator/(const UInt4& rhs) const noexcept
{
    return UInt4(x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w);
}

constexpr inline UInt4 UInt4::operator%(const UInt4& rhs) const noexcept
{
    return UInt4(x % rhs.x, y % rhs.y, z % rhs.z, w % rhs.w);
}

constexpr inline UInt4 UInt4::operator*(uint32_t scalar) const noexcept
{
    return UInt4(x * scalar, y * scalar, z * scalar, w * scalar);
}

constexpr inline UInt4 UInt4::operator/(uint32_t scalar) const noexcept
{
    return UInt4(x / scalar, y / scalar, z / scalar, w / scalar);
}

constexpr inline UInt4 UInt4::operator%(uint32_t scalar) const noexcept
{
    return UInt4(x % scalar, y % scalar, z % scalar, w % scalar);
}

constexpr UInt4& UInt4::operator+=(const UInt4& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
}

constexpr UInt4& UInt4::operator-=(const UInt4& rhs) noexcept
{
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
}

constexpr UInt4& UInt4::operator*=(const UInt4& rhs) noexcept
{
    x *= rhs.x;
    y *= rhs.y;
    z *= rhs.z;
    w *= rhs.w;
    return *this;
}

constexpr UInt4& UInt4::operator/=(const UInt4& rhs) noexcept
{
    x /= rhs.x;
    y /= rhs.y;
    z /= rhs.z;
    w /= rhs.w;
    return *this;
}

constexpr UInt4& UInt4::operator%=(const UInt4& rhs) noexcept
{
    x %= rhs.x;
    y %= rhs.y;
    z %= rhs.z;
    w %= rhs.w;
    return *this;
}

constexpr UInt4& UInt4::operator*=(uint32_t scalar) noexcept
{
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

constexpr UInt4& UInt4::operator/=(uint32_t scalar) noexcept
{
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

constexpr UInt4& UInt4::operator%=(uint32_t scalar) noexcept
{
    x %= scalar;
    y %= scalar;
    z %= scalar;
    w %= scalar;
    return *this;
}

constexpr bool UInt4::operator==(const UInt4& rhs) const noexcept
{
    return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
}

constexpr std::strong_ordering UInt4::operator<=>(const UInt4& rhs) const noexcept
{
    return std::tie(x, y, z, w) <=> std::tie(rhs.x, rhs.y, rhs.z, rhs.w);
}

inline float UInt4::Length() const noexcept
{
    return std::sqrtf(static_cast<float>(LengthSquared()));
}

constexpr uint32_t UInt4::LengthSquared() const noexcept
{
    return (x * x) + (y * y) + (z * z) + (w * w);
}

constexpr uint32_t UInt4::Dot(const UInt4& other) const noexcept
{
    return (x * other.x) + (y * other.y) + (z * other.z) + (w * other.w);
}

constexpr UInt4 UInt4::Min(const UInt4& other) const noexcept
{
    return UInt4(
        x < other.x ? x : other.x,
        y < other.y ? y : other.y,
        z < other.z ? z : other.z,
        w < other.w ? w : other.w
    );
}

constexpr UInt4 UInt4::Max(const UInt4& other) const noexcept
{
    return UInt4(
        x > other.x ? x : other.x,
        y > other.y ? y : other.y,
        z > other.z ? z : other.z,
        w > other.w ? w : other.w
    );
}

constexpr bool UInt4::NoneOf(uint32_t value) const noexcept
{
    return x != value && y != value && z != value && w != value;
}

constexpr bool UInt4::AnyOf(uint32_t value) const noexcept
{
    return x == value || y == value || z == value || w == value;
}

constexpr bool UInt4::AllOf(uint32_t value) const noexcept
{
    return x == value && y == value && z == value && w == value;
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

constexpr Int3 operator*(int32_t scalar, const Int3& vec) noexcept
{
    return vec * scalar;
}

constexpr Int4 operator*(int32_t scalar, const Int4& vec) noexcept
{
    return vec * scalar;
}

constexpr UInt3 operator*(uint32_t scalar, const UInt3& vec) noexcept
{
    return vec * scalar;
}

constexpr UInt4 operator*(uint32_t scalar, const UInt4& vec) noexcept
{
    return vec * scalar;
}

constexpr Int3 Min(const Int3& a, const Int3& b) noexcept
{
    return a.Min(b);
}

constexpr Int3 Max(const Int3& a, const Int3& b) noexcept
{
    return a.Max(b);
}

constexpr Int4 Min(const Int4& a, const Int4& b) noexcept
{
    return a.Min(b);
}

constexpr Int4 Max(const Int4& a, const Int4& b) noexcept
{
    return a.Max(b);
}

constexpr UInt3 Min(const UInt3& a, const UInt3& b) noexcept
{
    return a.Min(b);
}

constexpr UInt3 Max(const UInt3& a, const UInt3& b) noexcept
{
    return a.Max(b);
}

constexpr UInt4 Min(const UInt4& a, const UInt4& b) noexcept
{
    return a.Min(b);
}

constexpr UInt4 Max(const UInt4& a, const UInt4& b) noexcept
{
    return a.Max(b);
}

constexpr bool Float4::AnyOf(float value) const noexcept
{
    return x == value || y == value || z == value || w == value;
}

constexpr bool Float4::AllOf(float value) const noexcept
{
    return x == value && y == value && z == value && w == value;
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

constexpr void Float3x3::SetRow(size_t index, const Float3& row) noexcept
{
    m[index][0] = row.x;
    m[index][1] = row.y;
    m[index][2] = row.z;
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

constexpr void Float4x3::SetRow(size_t index, const Float3& row) noexcept
{
    m[index][0] = row.x;
    m[index][1] = row.y;
    m[index][2] = row.z;
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
    return {};
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

constexpr void Float4x4::SetRow(size_t index, const Float4& row) noexcept
{
    m[index][0] = row.x;
    m[index][1] = row.y;
    m[index][2] = row.z;
    m[index][3] = row.w;
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

constexpr Float3 Float3x3::Row(size_t index) const noexcept
{
    return Float3{ m[index][0], m[index][1], m[index][2] };
}

constexpr Float3 Float3x3::Column(size_t index) const noexcept
{
    return Float3{ m[0][index], m[1][index], m[2][index] };
}

constexpr Float3 Float4x3::Row(size_t index) const noexcept
{
    return Float3{ m[index][0], m[index][1], m[index][2] };
}

constexpr Float4 Float4x3::Column(size_t index) const noexcept
{
    return Float4{ m[0][index], m[1][index], m[2][index], m[3][index] };
}

constexpr Float4 Float4x4::Row(size_t index) const noexcept
{
    return Float4{ m[index][0], m[index][1], m[index][2], m[index][3] };
}

constexpr Float4 Float4x4::Column(size_t index) const noexcept
{
    return Float4{ m[0][index], m[1][index], m[2][index], m[3][index] };
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

// x - 2pi * round(x / 2pi). Round-to-nearest, not truncation: truncating leaves the negative half
// unreduced
constexpr DD_MATH_FORCEINLINE float ModAngles(float radians) noexcept
{
    constexpr float k_TwoPi = 2.0f * std::numbers::pi_v<float>;
    constexpr float k_ReciprocalTwoPi = 1.0f / k_TwoPi;

    const float revolutions = radians * k_ReciprocalTwoPi;
    // hand-rolled because std::round is not reliably constexpr; equivalent for any angle magnitude
    const float rounded = static_cast<float>(static_cast<int32_t>(revolutions + (revolutions < 0.0f ? -0.5f : 0.5f)));
    return radians - (k_TwoPi * rounded);
}

namespace detail
{
    // Scalar twin of ReduceForTrig in the backend files, which has to use Select where this branches
    struct ScalarTrigReduction
    {
        float angle;
        float cosSign;
    };

    constexpr DD_MATH_FORCEINLINE ScalarTrigReduction ReduceForTrigScalar(float radians) noexcept
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

    constexpr DD_MATH_FORCEINLINE float SinPolynomialScalar(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_SinPoly11;
        polynomial = (polynomial * squared) + k_SinPoly9;
        polynomial = (polynomial * squared) + k_SinPoly7;
        polynomial = (polynomial * squared) + k_SinPoly5;
        polynomial = (polynomial * squared) + k_SinPoly3;
        polynomial = (polynomial * squared) + 1.0f;
        return polynomial * angle;
    }

    constexpr DD_MATH_FORCEINLINE float CosPolynomialScalar(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_CosPoly10;
        polynomial = (polynomial * squared) + k_CosPoly8;
        polynomial = (polynomial * squared) + k_CosPoly6;
        polynomial = (polynomial * squared) + k_CosPoly4;
        polynomial = (polynomial * squared) + k_CosPoly2;
        return (polynomial * squared) + 1.0f;
    }

    constexpr DD_MATH_FORCEINLINE float SinPolynomialScalarEst(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_SinEstPoly7;
        polynomial = (polynomial * squared) + k_SinEstPoly5;
        polynomial = (polynomial * squared) + k_SinEstPoly3;
        polynomial = (polynomial * squared) + 1.0f;
        return polynomial * angle;
    }

    constexpr DD_MATH_FORCEINLINE float CosPolynomialScalarEst(float angle) noexcept
    {
        const float squared = angle * angle;
        float polynomial = k_CosEstPoly6;
        polynomial = (polynomial * squared) + k_CosEstPoly4;
        polynomial = (polynomial * squared) + k_CosEstPoly2;
        return (polynomial * squared) + 1.0f;
    }

} // namespace detail

DD_MATH_FORCEINLINE ScalarSinCos SinCos(float radians) noexcept
{
    const detail::ScalarTrigReduction reduction = detail::ReduceForTrigScalar(radians);
    return ScalarSinCos{ detail::SinPolynomialScalar(reduction.angle),
                         detail::CosPolynomialScalar(reduction.angle) * reduction.cosSign };
}

inline Float3 Float3::RotateAroundY(const Float3& value, float radians) noexcept
{
    // the same sense as yaw: +radians turns +z toward +x
    const math::ScalarSinCos sinCos = SinCos(radians);
    return math::Float3
    {
        (value.x * sinCos.cos) + (value.z * sinCos.sin),
        value.y,
        (-value.x * sinCos.sin) + (value.z * sinCos.cos),
    };
}
} // namespace math
