#include "Math.hpp"
#pragma once
// Implementation file for Math.hpp - contains all function implementations to keep the header readable

#if defined(_MSC_VER)
#define DD_MATH_FORCEINLINE __forceinline
#elif defined(__clang__)
#define DD_MATH_FORCEINLINE __attribute__((always_inline)) inline
#elif defined(__GNUC__)
#define DD_MATH_FORCEINLINE __attribute__((always_inline)) inline
#else
#define DD_MATH_FORCEINLINE inline
#endif

namespace math
{
    // using inline instead of forceinline here because it's not as critical, we'll let compiler choose
    constexpr inline Float2 Float2::operator+(const Float2& rhs) const noexcept 
    {
        return Float2(storage.x + rhs.storage.x, storage.y + rhs.storage.y);
    }
    
    constexpr inline Float2 Float2::operator-(const Float2& rhs) const noexcept 
    {
        return Float2(storage.x - rhs.storage.x, storage.y - rhs.storage.y);
    }
    
    constexpr inline Float2 Float2::operator*(const Float2& rhs) const noexcept 
    {
        return Float2(storage.x * rhs.storage.x, storage.y * rhs.storage.y);
    }
    
    constexpr inline Float2 Float2::operator/(const Float2& rhs) const noexcept 
    {
        return Float2(storage.x / rhs.storage.x, storage.y / rhs.storage.y);
    }
    
    constexpr inline Float2 Float2::operator*(float scalar) const noexcept 
    {
        return Float2(storage.x * scalar, storage.y * scalar);
    }
    
    constexpr inline Float2 Float2::operator/(float scalar) const noexcept 
    {
        return Float2(storage.x / scalar, storage.y / scalar);
    }
    
    constexpr inline Float2 Float2::operator-() const noexcept 
    {
        return Float2(-storage.x, -storage.y);
    }
    
    constexpr inline Float2& Float2::operator+=(const Float2& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        return *this;
    }
    
    constexpr inline Float2& Float2::operator-=(const Float2& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        return *this;
    }
    
    constexpr inline Float2& Float2::operator*=(const Float2& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        return *this;
    }
    
    constexpr inline Float2& Float2::operator/=(const Float2& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        return *this;
    }
    
    constexpr inline Float2& Float2::operator*=(float scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        return *this;
    }
    
    constexpr inline Float2& Float2::operator/=(float scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        return *this;
    }
    
    constexpr inline bool Float2::operator==(const Float2& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y;
    }
    
    constexpr inline bool Float2::operator!=(const Float2& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Float3 implementations
    constexpr inline Float3 Float3::operator+(const Float3& rhs) const noexcept 
    {
        return Float3(storage.x + rhs.storage.x, storage.y + rhs.storage.y, storage.z + rhs.storage.z);
    }
    
    constexpr inline Float3 Float3::operator-(const Float3& rhs) const noexcept 
    {
        return Float3(storage.x - rhs.storage.x, storage.y - rhs.storage.y, storage.z - rhs.storage.z);
    }
    
    constexpr inline Float3 Float3::operator*(const Float3& rhs) const noexcept 
    {
        return Float3(storage.x * rhs.storage.x, storage.y * rhs.storage.y, storage.z * rhs.storage.z);
    }
    
    constexpr inline Float3 Float3::operator/(const Float3& rhs) const noexcept 
    {
        return Float3(storage.x / rhs.storage.x, storage.y / rhs.storage.y, storage.z / rhs.storage.z);
    }
    
    constexpr inline Float3 Float3::operator*(float scalar) const noexcept 
    {
        return Float3(storage.x * scalar, storage.y * scalar, storage.z * scalar);
    }
    
    constexpr inline Float3 Float3::operator/(float scalar) const noexcept 
    {
        return Float3(storage.x / scalar, storage.y / scalar, storage.z / scalar);
    }
    
    constexpr inline Float3 Float3::operator-() const noexcept 
    {
        return Float3(-storage.x, -storage.y, -storage.z);
    }
    
    constexpr Float3& Float3::operator+=(const Float3& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        storage.z += rhs.storage.z;
        return *this;
    }
    
    constexpr Float3& Float3::operator-=(const Float3& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        storage.z -= rhs.storage.z;
        return *this;
    }
    
    constexpr Float3& Float3::operator*=(const Float3& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        storage.z *= rhs.storage.z;
        return *this;
    }
    
    constexpr Float3& Float3::operator/=(const Float3& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        storage.z /= rhs.storage.z;
        return *this;
    }
    
    constexpr Float3& Float3::operator*=(float scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        storage.z *= scalar;
        return *this;
    }
    
    constexpr Float3& Float3::operator/=(float scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        storage.z /= scalar;
        return *this;
    }
    
    constexpr bool Float3::operator==(const Float3& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y && storage.z == rhs.storage.z;
    }
    
    constexpr bool Float3::operator!=(const Float3& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Float4 implementations
    constexpr inline Float4 Float4::operator+(const Float4& rhs) const noexcept 
    {
        return Float4(storage.x + rhs.storage.x, storage.y + rhs.storage.y, 
                    storage.z + rhs.storage.z, storage.w + rhs.storage.w);
    }
    
    constexpr inline Float4 Float4::operator-(const Float4& rhs) const noexcept 
    {
        return Float4(storage.x - rhs.storage.x, storage.y - rhs.storage.y, 
                    storage.z - rhs.storage.z, storage.w - rhs.storage.w);
    }
    
    constexpr inline Float4 Float4::operator*(const Float4& rhs) const noexcept 
    {
        return Float4(storage.x * rhs.storage.x, storage.y * rhs.storage.y, 
                    storage.z * rhs.storage.z, storage.w * rhs.storage.w);
    }
    
    constexpr inline Float4 Float4::operator/(const Float4& rhs) const noexcept 
    {
        return Float4(storage.x / rhs.storage.x, storage.y / rhs.storage.y, 
                    storage.z / rhs.storage.z, storage.w / rhs.storage.w);
    }
    
    constexpr inline Float4 Float4::operator*(float scalar) const noexcept 
    {
        return Float4(storage.x * scalar, storage.y * scalar, 
                    storage.z * scalar, storage.w * scalar);
    }
    
    constexpr inline Float4 Float4::operator/(float scalar) const noexcept 
    {
        return Float4(storage.x / scalar, storage.y / scalar, 
                    storage.z / scalar, storage.w / scalar);
    }
    
    constexpr inline Float4 Float4::operator-() const noexcept 
    {
        return Float4(-storage.x, -storage.y, -storage.z, -storage.w);
    }
    
    constexpr Float4& Float4::operator+=(const Float4& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        storage.z += rhs.storage.z;
        storage.w += rhs.storage.w;
        return *this;
    }
    
    constexpr Float4& Float4::operator-=(const Float4& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        storage.z -= rhs.storage.z;
        storage.w -= rhs.storage.w;
        return *this;
    }
    
    constexpr Float4& Float4::operator*=(const Float4& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        storage.z *= rhs.storage.z;
        storage.w *= rhs.storage.w;
        return *this;
    }
    
    constexpr Float4& Float4::operator/=(const Float4& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        storage.z /= rhs.storage.z;
        storage.w /= rhs.storage.w;
        return *this;
    }
    
    constexpr Float4& Float4::operator*=(float scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        storage.z *= scalar;
        storage.w *= scalar;
        return *this;
    }
    
    constexpr Float4& Float4::operator/=(float scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        storage.z /= scalar;
        storage.w /= scalar;
        return *this;
    }
    
    constexpr bool Float4::operator==(const Float4& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y && 
            storage.z == rhs.storage.z && storage.w == rhs.storage.w;
    }
    
    constexpr bool Float4::operator!=(const Float4& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Float4 swizzle implementations
    constexpr Float4 Float4::xyzw() const noexcept 
    { 
        return Float4(storage.x, storage.y, storage.z, storage.w); 
    }
    
    constexpr Float4 Float4::xywz() const noexcept 
    { 
        return Float4(storage.x, storage.y, storage.w, storage.z); 
    }
    
    constexpr Float4 Float4::xzyw() const noexcept 
    { 
        return Float4(storage.x, storage.z, storage.y, storage.w); 
    }
    
    constexpr Float4 Float4::xzwy() const noexcept 
    { 
        return Float4(storage.x, storage.z, storage.w, storage.y); 
    }
    
    constexpr Float4 Float4::xwyz() const noexcept 
    { 
        return Float4(storage.x, storage.w, storage.y, storage.z); 
    }
    
    constexpr Float4 Float4::xwzy() const noexcept 
    { 
        return Float4(storage.x, storage.w, storage.z, storage.y); 
    }
    
    constexpr Float4 Float4::xxxx() const noexcept 
    { 
        return Float4(storage.x, storage.x, storage.x, storage.x); 
    }
    
    constexpr Float4 Float4::yyyy() const noexcept 
    { 
        return Float4(storage.y, storage.y, storage.y, storage.y); 
    }
    
    constexpr Float4 Float4::zzzz() const noexcept 
    { 
        return Float4(storage.z, storage.z, storage.z, storage.z); 
    }
    
    constexpr Float4 Float4::wwww() const noexcept 
    { 
        return Float4(storage.w, storage.w, storage.w, storage.w); 
    }

    // ================================
    // Int2 Implementation
    // ================================
    
    // Arithmetic operators (integer)
    constexpr inline Int2 Int2::operator+(const Int2& rhs) const noexcept 
    {
        return Int2(storage.x + rhs.storage.x, storage.y + rhs.storage.y);
    }
    
    constexpr inline Int2 Int2::operator-(const Int2& rhs) const noexcept 
    {
        return Int2(storage.x - rhs.storage.x, storage.y - rhs.storage.y);
    }
    
    constexpr inline Int2 Int2::operator*(const Int2& rhs) const noexcept 
    {
        return Int2(storage.x * rhs.storage.x, storage.y * rhs.storage.y);
    }
    
    constexpr inline Int2 Int2::operator/(const Int2& rhs) const noexcept 
    {
        return Int2(storage.x / rhs.storage.x, storage.y / rhs.storage.y);
    }
    
    // Mixed operations with Float2 (promote to float)
    constexpr Float2 Int2::operator+(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) + rhs.x, static_cast<float>(storage.y) + rhs.y);
    }
    
    constexpr Float2 Int2::operator-(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) - rhs.x, static_cast<float>(storage.y) - rhs.y);
    }
    
    constexpr Float2 Int2::operator*(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) * rhs.x, static_cast<float>(storage.y) * rhs.y);
    }
    
    constexpr Float2 Int2::operator/(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) / rhs.x, static_cast<float>(storage.y) / rhs.y);
    }
    
    // Scalar operators
    constexpr inline Int2 Int2::operator*(int32_t scalar) const noexcept 
    {
        return Int2(storage.x * scalar, storage.y * scalar);
    }
    
    constexpr inline Int2 Int2::operator/(int32_t scalar) const noexcept 
    {
        return Int2(storage.x / scalar, storage.y / scalar);
    }
    
    // Mixed scalar operations (promote to float)
    constexpr Float2 Int2::operator*(float scalar) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) * scalar, static_cast<float>(storage.y) * scalar);
    }
    
    constexpr Float2 Int2::operator/(float scalar) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) / scalar, static_cast<float>(storage.y) / scalar);
    }
    
    // Unary operators
    constexpr inline Int2 Int2::operator-() const noexcept 
    {
        return Int2(-storage.x, -storage.y);
    }
    
    // Compound assignment operators
    constexpr inline Int2& Int2::operator+=(const Int2& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        return *this;
    }
    
    constexpr inline Int2& Int2::operator-=(const Int2& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        return *this;
    }
    
    constexpr inline Int2& Int2::operator*=(const Int2& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        return *this;
    }
    
    constexpr inline Int2& Int2::operator/=(const Int2& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        return *this;
    }
    
    constexpr inline Int2& Int2::operator*=(int32_t scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        return *this;
    }
    
    constexpr inline Int2& Int2::operator/=(int32_t scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        return *this;
    }
    
    // Comparison operators
    constexpr bool Int2::operator==(const Int2& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y;
    }
    
    constexpr bool Int2::operator!=(const Int2& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Swizzle accessors - all 2D combinations
    constexpr inline Int2 Int2::xx() const noexcept 
    { 
        return Int2(storage.x, storage.x); 
    }

    constexpr inline Int2 Int2::xy() const noexcept 
    { 
        return Int2(storage.x, storage.y); 
    }

    constexpr inline Int2 Int2::yx() const noexcept 
    { 
        return Int2(storage.y, storage.x); 
    }

    constexpr inline Int2 Int2::yy() const noexcept 
    { 
        return Int2(storage.y, storage.y); 
    }

    // ================================
    // UInt2 Implementation
    // ================================
    
    // Arithmetic operators (unsigned integer)
    constexpr inline UInt2 UInt2::operator+(const UInt2& rhs) const noexcept 
    {
        return UInt2(storage.x + rhs.storage.x, storage.y + rhs.storage.y);
    }
    
    constexpr inline UInt2 UInt2::operator-(const UInt2& rhs) const noexcept 
    {
        return UInt2(storage.x - rhs.storage.x, storage.y - rhs.storage.y);
    }
    
    constexpr inline UInt2 UInt2::operator*(const UInt2& rhs) const noexcept 
    {
        return UInt2(storage.x * rhs.storage.x, storage.y * rhs.storage.y);
    }
    
    constexpr inline UInt2 UInt2::operator/(const UInt2& rhs) const noexcept 
    {
        return UInt2(storage.x / rhs.storage.x, storage.y / rhs.storage.y);
    }
    
    // Mixed operations with Float2 (promote to float)
    constexpr Float2 UInt2::operator+(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) + rhs.x, static_cast<float>(storage.y) + rhs.y);
    }
    
    constexpr Float2 UInt2::operator-(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) - rhs.x, static_cast<float>(storage.y) - rhs.y);
    }
    
    constexpr Float2 UInt2::operator*(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) * rhs.x, static_cast<float>(storage.y) * rhs.y);
    }
    
    constexpr Float2 UInt2::operator/(const Float2& rhs) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) / rhs.x, static_cast<float>(storage.y) / rhs.y);
    }
    
    // Scalar operators
    constexpr inline UInt2 UInt2::operator*(uint32_t scalar) const noexcept 
    {
        return UInt2(storage.x * scalar, storage.y * scalar);
    }
    
    constexpr inline UInt2 UInt2::operator/(uint32_t scalar) const noexcept 
    {
        return UInt2(storage.x / scalar, storage.y / scalar);
    }
    
    // Mixed scalar operations (promote to float)
    constexpr Float2 UInt2::operator*(float scalar) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) * scalar, static_cast<float>(storage.y) * scalar);
    }
    
    constexpr Float2 UInt2::operator/(float scalar) const noexcept 
    {
        return Float2(static_cast<float>(storage.x) / scalar, static_cast<float>(storage.y) / scalar);
    }
    
    // Compound assignment operators
    constexpr inline UInt2& UInt2::operator+=(const UInt2& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        return *this;
    }
    
    constexpr inline UInt2& UInt2::operator-=(const UInt2& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        return *this;
    }
    
    constexpr inline UInt2& UInt2::operator*=(const UInt2& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        return *this;
    }
    
    constexpr inline UInt2& UInt2::operator/=(const UInt2& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        return *this;
    }
    
    constexpr inline UInt2& UInt2::operator*=(uint32_t scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        return *this;
    }
    
    constexpr inline UInt2& UInt2::operator/=(uint32_t scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        return *this;
    }
    
    // Comparison operators
    constexpr bool UInt2::operator==(const UInt2& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y;
    }
    
    constexpr bool UInt2::operator!=(const UInt2& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Swizzle accessors - all 2D combinations
    constexpr inline UInt2 UInt2::xx() const noexcept 
    { 
        return UInt2(storage.x, storage.x); 
    }

    constexpr inline UInt2 UInt2::xy() const noexcept 
    { 
        return UInt2(storage.x, storage.y); 
    }

    constexpr inline UInt2 UInt2::yx() const noexcept 
    { 
        return UInt2(storage.y, storage.x); 
    }

    constexpr inline UInt2 UInt2::yy() const noexcept 
    { 
        return UInt2(storage.y, storage.y); 
    }

    // ================================
    // Int3 Implementation
    // ================================
    
    // Arithmetic operators (integer)
    constexpr inline Int3 Int3::operator+(const Int3& rhs) const noexcept 
    {
        return Int3(storage.x + rhs.storage.x, storage.y + rhs.storage.y, storage.z + rhs.storage.z);
    }
    
    constexpr inline Int3 Int3::operator-(const Int3& rhs) const noexcept 
    {
        return Int3(storage.x - rhs.storage.x, storage.y - rhs.storage.y, storage.z - rhs.storage.z);
    }
    
    constexpr inline Int3 Int3::operator*(const Int3& rhs) const noexcept 
    {
        return Int3(storage.x * rhs.storage.x, storage.y * rhs.storage.y, storage.z * rhs.storage.z);
    }
    
    constexpr inline Int3 Int3::operator/(const Int3& rhs) const noexcept 
    {
        return Int3(storage.x / rhs.storage.x, storage.y / rhs.storage.y, storage.z / rhs.storage.z);
    }
    
    // Mixed operations with Float3 (promote to float)
    constexpr Float3 Int3::operator+(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) + rhs.x, static_cast<float>(storage.y) + rhs.y, static_cast<float>(storage.z) + rhs.z);
    }
    
    constexpr Float3 Int3::operator-(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) - rhs.x, static_cast<float>(storage.y) - rhs.y, static_cast<float>(storage.z) - rhs.z);
    }
    
    constexpr Float3 Int3::operator*(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) * rhs.x, static_cast<float>(storage.y) * rhs.y, static_cast<float>(storage.z) * rhs.z);
    }
    
    constexpr Float3 Int3::operator/(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) / rhs.x, static_cast<float>(storage.y) / rhs.y, static_cast<float>(storage.z) / rhs.z);
    }
    
    // Scalar operators
    constexpr inline Int3 Int3::operator*(int32_t scalar) const noexcept 
    {
        return Int3(storage.x * scalar, storage.y * scalar, storage.z * scalar);
    }
    
    constexpr inline Int3 Int3::operator/(int32_t scalar) const noexcept 
    {
        return Int3(storage.x / scalar, storage.y / scalar, storage.z / scalar);
    }
    
    // Mixed scalar operations (promote to float)
    constexpr Float3 Int3::operator*(float scalar) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) * scalar, static_cast<float>(storage.y) * scalar, static_cast<float>(storage.z) * scalar);
    }
    
    constexpr Float3 Int3::operator/(float scalar) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) / scalar, static_cast<float>(storage.y) / scalar, static_cast<float>(storage.z) / scalar);
    }
    
    // Unary operators
    constexpr inline Int3 Int3::operator-() const noexcept 
    {
        return Int3(-storage.x, -storage.y, -storage.z);
    }
    
    // Compound assignment operators
    constexpr inline Int3& Int3::operator+=(const Int3& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        storage.z += rhs.storage.z;
        return *this;
    }
    
    constexpr inline Int3& Int3::operator-=(const Int3& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        storage.z -= rhs.storage.z;
        return *this;
    }
    
    constexpr inline Int3& Int3::operator*=(const Int3& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        storage.z *= rhs.storage.z;
        return *this;
    }
    
    constexpr inline Int3& Int3::operator/=(const Int3& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        storage.z /= rhs.storage.z;
        return *this;
    }
    
    constexpr inline Int3& Int3::operator*=(int32_t scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        storage.z *= scalar;
        return *this;
    }
    
    constexpr inline Int3& Int3::operator/=(int32_t scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        storage.z /= scalar;
        return *this;
    }
    
    // Comparison operators
    constexpr bool Int3::operator==(const Int3& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y && storage.z == rhs.storage.z;
    }
    
    constexpr bool Int3::operator!=(const Int3& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Swizzle accessors - common 3D combinations
    constexpr inline Int3 Int3::xyz() const noexcept 
    { 
        return Int3(storage.x, storage.y, storage.z); 
    }

    constexpr inline Int3 Int3::xzy() const noexcept 
    { 
        return Int3(storage.x, storage.z, storage.y); 
    }

    constexpr inline Int3 Int3::yxz() const noexcept 
    { 
        return Int3(storage.y, storage.x, storage.z); 
    }

    constexpr inline Int3 Int3::yzx() const noexcept 
    { 
        return Int3(storage.y, storage.z, storage.x); 
    }

    constexpr inline Int3 Int3::zxy() const noexcept 
    { 
        return Int3(storage.z, storage.x, storage.y); 
    }

    constexpr inline Int3 Int3::zyx() const noexcept 
    { 
        return Int3(storage.z, storage.y, storage.x); 
    }

    constexpr inline Int3 Int3::xxx() const noexcept 
    { 
        return Int3(storage.x, storage.x, storage.x); 
    }

    constexpr inline Int3 Int3::yyy() const noexcept 
    { 
        return Int3(storage.y, storage.y, storage.y); 
    }

    constexpr inline Int3 Int3::zzz() const noexcept 
    { 
        return Int3(storage.z, storage.z, storage.z); 
    }

    // 2D swizzles from 3D
    constexpr inline Int2 Int3::xy() const noexcept 
    { 
        return Int2(storage.x, storage.y); 
    }

    constexpr inline Int2 Int3::xz() const noexcept 
    { 
        return Int2(storage.x, storage.z); 
    }

    constexpr inline Int2 Int3::yz() const noexcept 
    { 
        return Int2(storage.y, storage.z); 
    }

    // ================================
    // UInt3 Implementation
    // ================================
    
    // Arithmetic operators (unsigned integer)
    constexpr inline UInt3 UInt3::operator+(const UInt3& rhs) const noexcept 
    {
        return UInt3(storage.x + rhs.storage.x, storage.y + rhs.storage.y, storage.z + rhs.storage.z);
    }
    
    constexpr inline UInt3 UInt3::operator-(const UInt3& rhs) const noexcept 
    {
        return UInt3(storage.x - rhs.storage.x, storage.y - rhs.storage.y, storage.z - rhs.storage.z);
    }
    
    constexpr inline UInt3 UInt3::operator*(const UInt3& rhs) const noexcept 
    {
        return UInt3(storage.x * rhs.storage.x, storage.y * rhs.storage.y, storage.z * rhs.storage.z);
    }
    
    constexpr inline UInt3 UInt3::operator/(const UInt3& rhs) const noexcept 
    {
        return UInt3(storage.x / rhs.storage.x, storage.y / rhs.storage.y, storage.z / rhs.storage.z);
    }
    
    // Mixed operations with Float3 (promote to float)
    constexpr Float3 UInt3::operator+(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) + rhs.x, static_cast<float>(storage.y) + rhs.y, static_cast<float>(storage.z) + rhs.z);
    }
    
    constexpr Float3 UInt3::operator-(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) - rhs.x, static_cast<float>(storage.y) - rhs.y, static_cast<float>(storage.z) - rhs.z);
    }
    
    constexpr Float3 UInt3::operator*(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) * rhs.x, static_cast<float>(storage.y) * rhs.y, static_cast<float>(storage.z) * rhs.z);
    }
    
    constexpr Float3 UInt3::operator/(const Float3& rhs) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) / rhs.x, static_cast<float>(storage.y) / rhs.y, static_cast<float>(storage.z) / rhs.z);
    }
    
    // Scalar operators
    constexpr inline UInt3 UInt3::operator*(uint32_t scalar) const noexcept 
    {
        return UInt3(storage.x * scalar, storage.y * scalar, storage.z * scalar);
    }
    
    constexpr inline UInt3 UInt3::operator/(uint32_t scalar) const noexcept 
    {
        return UInt3(storage.x / scalar, storage.y / scalar, storage.z / scalar);
    }
    
    // Mixed scalar operations (promote to float)
    constexpr Float3 UInt3::operator*(float scalar) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) * scalar, static_cast<float>(storage.y) * scalar, static_cast<float>(storage.z) * scalar);
    }
    
    constexpr Float3 UInt3::operator/(float scalar) const noexcept 
    {
        return Float3(static_cast<float>(storage.x) / scalar, static_cast<float>(storage.y) / scalar, static_cast<float>(storage.z) / scalar);
    }
    
    // Compound assignment operators
    constexpr inline UInt3& UInt3::operator+=(const UInt3& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        storage.z += rhs.storage.z;
        return *this;
    }
    
    constexpr inline UInt3& UInt3::operator-=(const UInt3& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        storage.z -= rhs.storage.z;
        return *this;
    }
    
    constexpr inline UInt3& UInt3::operator*=(const UInt3& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        storage.z *= rhs.storage.z;
        return *this;
    }
    
    constexpr inline UInt3& UInt3::operator/=(const UInt3& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        storage.z /= rhs.storage.z;
        return *this;
    }
    
    constexpr inline UInt3& UInt3::operator*=(uint32_t scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        storage.z *= scalar;
        return *this;
    }
    
    constexpr inline UInt3& UInt3::operator/=(uint32_t scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        storage.z /= scalar;
        return *this;
    }
    
    // Comparison operators
    constexpr bool UInt3::operator==(const UInt3& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y && storage.z == rhs.storage.z;
    }
    
    constexpr bool UInt3::operator!=(const UInt3& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Swizzle accessors - common 3D combinations
    constexpr inline UInt3 UInt3::xyz() const noexcept 
    { 
        return UInt3(storage.x, storage.y, storage.z); 
    }

    constexpr inline UInt3 UInt3::xzy() const noexcept 
    { 
        return UInt3(storage.x, storage.z, storage.y); 
    }

    constexpr inline UInt3 UInt3::yxz() const noexcept 
    { 
        return UInt3(storage.y, storage.x, storage.z); 
    }

    constexpr inline UInt3 UInt3::yzx() const noexcept 
    { 
        return UInt3(storage.y, storage.z, storage.x); 
    }

    constexpr inline UInt3 UInt3::zxy() const noexcept 
    { 
        return UInt3(storage.z, storage.x, storage.y); 
    }

    constexpr inline UInt3 UInt3::zyx() const noexcept 
    { 
        return UInt3(storage.z, storage.y, storage.x); 
    }

    constexpr inline UInt3 UInt3::xxx() const noexcept 
    { 
        return UInt3(storage.x, storage.x, storage.x); 
    }

    constexpr inline UInt3 UInt3::yyy() const noexcept 
    { 
        return UInt3(storage.y, storage.y, storage.y); 
    }

    constexpr inline UInt3 UInt3::zzz() const noexcept 
    { 
        return UInt3(storage.z, storage.z, storage.z); 
    }

    // 2D swizzles from 3D
    constexpr inline UInt2 UInt3::xy() const noexcept 
    { 
        return UInt2(storage.x, storage.y); 
    }

    constexpr inline UInt2 UInt3::xz() const noexcept 
    { 
        return UInt2(storage.x, storage.z); 
    }

    constexpr inline UInt2 UInt3::yz() const noexcept 
    { 
        return UInt2(storage.y, storage.z); 
    }

    // ================================
    // Int4 Implementation
    // ================================
    
    // Arithmetic operators (integer)
    constexpr inline Int4 Int4::operator+(const Int4& rhs) const noexcept 
    {
        return Int4(storage.x + rhs.storage.x, storage.y + rhs.storage.y, storage.z + rhs.storage.z, storage.w + rhs.storage.w);
    }
    
    constexpr inline Int4 Int4::operator-(const Int4& rhs) const noexcept 
    {
        return Int4(storage.x - rhs.storage.x, storage.y - rhs.storage.y, storage.z - rhs.storage.z, storage.w - rhs.storage.w);
    }
    
    constexpr inline Int4 Int4::operator*(const Int4& rhs) const noexcept 
    {
        return Int4(storage.x * rhs.storage.x, storage.y * rhs.storage.y, storage.z * rhs.storage.z, storage.w * rhs.storage.w);
    }
    
    constexpr inline Int4 Int4::operator/(const Int4& rhs) const noexcept 
    {
        return Int4(storage.x / rhs.storage.x, storage.y / rhs.storage.y, storage.z / rhs.storage.z, storage.w / rhs.storage.w);
    }
    
    // Mixed operations with Float4 (promote to float)
    constexpr Float4 Int4::operator+(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) + rhs.x, static_cast<float>(storage.y) + rhs.y, static_cast<float>(storage.z) + rhs.z, static_cast<float>(storage.w) + rhs.w);
    }
    
    constexpr Float4 Int4::operator-(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) - rhs.x, static_cast<float>(storage.y) - rhs.y, static_cast<float>(storage.z) - rhs.z, static_cast<float>(storage.w) - rhs.w);
    }
    
    constexpr Float4 Int4::operator*(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) * rhs.x, static_cast<float>(storage.y) * rhs.y, static_cast<float>(storage.z) * rhs.z, static_cast<float>(storage.w) * rhs.w);
    }
    
    constexpr Float4 Int4::operator/(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) / rhs.x, static_cast<float>(storage.y) / rhs.y, static_cast<float>(storage.z) / rhs.z, static_cast<float>(storage.w) / rhs.w);
    }
    
    // Scalar operators
    constexpr inline Int4 Int4::operator*(int32_t scalar) const noexcept 
    {
        return Int4(storage.x * scalar, storage.y * scalar, storage.z * scalar, storage.w * scalar);
    }
    
    constexpr inline Int4 Int4::operator/(int32_t scalar) const noexcept 
    {
        return Int4(storage.x / scalar, storage.y / scalar, storage.z / scalar, storage.w / scalar);
    }
    
    // Mixed scalar operations (promote to float)
    constexpr Float4 Int4::operator*(float scalar) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) * scalar, static_cast<float>(storage.y) * scalar, static_cast<float>(storage.z) * scalar, static_cast<float>(storage.w) * scalar);
    }
    
    constexpr Float4 Int4::operator/(float scalar) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) / scalar, static_cast<float>(storage.y) / scalar, static_cast<float>(storage.z) / scalar, static_cast<float>(storage.w) / scalar);
    }
    
    // Unary operators
    constexpr inline Int4 Int4::operator-() const noexcept 
    {
        return Int4(-storage.x, -storage.y, -storage.z, -storage.w);
    }
    
    // Compound assignment operators
    constexpr inline Int4& Int4::operator+=(const Int4& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        storage.z += rhs.storage.z;
        storage.w += rhs.storage.w;
        return *this;
    }
    
    constexpr inline Int4& Int4::operator-=(const Int4& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        storage.z -= rhs.storage.z;
        storage.w -= rhs.storage.w;
        return *this;
    }
    
    constexpr inline Int4& Int4::operator*=(const Int4& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        storage.z *= rhs.storage.z;
        storage.w *= rhs.storage.w;
        return *this;
    }
    
    constexpr inline Int4& Int4::operator/=(const Int4& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        storage.z /= rhs.storage.z;
        storage.w /= rhs.storage.w;
        return *this;
    }
    
    constexpr inline Int4& Int4::operator*=(int32_t scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        storage.z *= scalar;
        storage.w *= scalar;
        return *this;
    }
    
    constexpr inline Int4& Int4::operator/=(int32_t scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        storage.z /= scalar;
        storage.w /= scalar;
        return *this;
    }
    
    // Comparison operators
    constexpr bool Int4::operator==(const Int4& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y && storage.z == rhs.storage.z && storage.w == rhs.storage.w;
    }
    
    constexpr bool Int4::operator!=(const Int4& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Swizzle accessors - common 4D combinations
    constexpr inline Int4 Int4::xyzw() const noexcept 
    { 
        return Int4(storage.x, storage.y, storage.z, storage.w); 
    }

    constexpr inline Int4 Int4::wxyz() const noexcept 
    { 
        return Int4(storage.w, storage.x, storage.y, storage.z); 
    }

    constexpr inline Int4 Int4::zwxy() const noexcept 
    { 
        return Int4(storage.z, storage.w, storage.x, storage.y); 
    }

    constexpr inline Int4 Int4::yzwx() const noexcept 
    { 
        return Int4(storage.y, storage.z, storage.w, storage.x); 
    }

    constexpr inline Int4 Int4::wwww() const noexcept 
    { 
        return Int4(storage.w, storage.w, storage.w, storage.w); 
    }

    // 3D swizzles from 4D
    constexpr inline Int3 Int4::xyz() const noexcept 
    { 
        return Int3(storage.x, storage.y, storage.z); 
    }

    constexpr inline Int3 Int4::rgb() const noexcept 
    { 
        return Int3(storage.x, storage.y, storage.z); 
    }

    // 2D swizzles from 4D
    constexpr inline Int2 Int4::xy() const noexcept 
    { 
        return Int2(storage.x, storage.y); 
    }

    constexpr inline Int2 Int4::zw() const noexcept 
    { 
        return Int2(storage.z, storage.w); 
    }

    // ================================
    // UInt4 Implementation
    // ================================
    
    // Arithmetic operators (unsigned integer)
    constexpr inline UInt4 UInt4::operator+(const UInt4& rhs) const noexcept 
    {
        return UInt4(storage.x + rhs.storage.x, storage.y + rhs.storage.y, storage.z + rhs.storage.z, storage.w + rhs.storage.w);
    }
    
    constexpr inline UInt4 UInt4::operator-(const UInt4& rhs) const noexcept 
    {
        return UInt4(storage.x - rhs.storage.x, storage.y - rhs.storage.y, storage.z - rhs.storage.z, storage.w - rhs.storage.w);
    }
    
    constexpr inline UInt4 UInt4::operator*(const UInt4& rhs) const noexcept 
    {
        return UInt4(storage.x * rhs.storage.x, storage.y * rhs.storage.y, storage.z * rhs.storage.z, storage.w * rhs.storage.w);
    }
    
    constexpr inline UInt4 UInt4::operator/(const UInt4& rhs) const noexcept 
    {
        return UInt4(storage.x / rhs.storage.x, storage.y / rhs.storage.y, storage.z / rhs.storage.z, storage.w / rhs.storage.w);
    }
    
    // Mixed operations with Float4 (promote to float)
    constexpr Float4 UInt4::operator+(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) + rhs.x, static_cast<float>(storage.y) + rhs.y, static_cast<float>(storage.z) + rhs.z, static_cast<float>(storage.w) + rhs.w);
    }
    
    constexpr Float4 UInt4::operator-(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) - rhs.x, static_cast<float>(storage.y) - rhs.y, static_cast<float>(storage.z) - rhs.z, static_cast<float>(storage.w) - rhs.w);
    }
    
    constexpr Float4 UInt4::operator*(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) * rhs.x, static_cast<float>(storage.y) * rhs.y, static_cast<float>(storage.z) * rhs.z, static_cast<float>(storage.w) * rhs.w);
    }
    
    constexpr Float4 UInt4::operator/(const Float4& rhs) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) / rhs.x, static_cast<float>(storage.y) / rhs.y, static_cast<float>(storage.z) / rhs.z, static_cast<float>(storage.w) / rhs.w);
    }
    
    // Scalar operators
    constexpr inline UInt4 UInt4::operator*(uint32_t scalar) const noexcept 
    {
        return UInt4(storage.x * scalar, storage.y * scalar, storage.z * scalar, storage.w * scalar);
    }
    
    constexpr inline UInt4 UInt4::operator/(uint32_t scalar) const noexcept 
    {
        return UInt4(storage.x / scalar, storage.y / scalar, storage.z / scalar, storage.w / scalar);
    }
    
    // Mixed scalar operations (promote to float)
    constexpr Float4 UInt4::operator*(float scalar) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) * scalar, static_cast<float>(storage.y) * scalar, static_cast<float>(storage.z) * scalar, static_cast<float>(storage.w) * scalar);
    }
    
    constexpr Float4 UInt4::operator/(float scalar) const noexcept 
    {
        return Float4(static_cast<float>(storage.x) / scalar, static_cast<float>(storage.y) / scalar, static_cast<float>(storage.z) / scalar, static_cast<float>(storage.w) / scalar);
    }
    
    // Compound assignment operators
    constexpr inline UInt4& UInt4::operator+=(const UInt4& rhs) noexcept 
    {
        storage.x += rhs.storage.x;
        storage.y += rhs.storage.y;
        storage.z += rhs.storage.z;
        storage.w += rhs.storage.w;
        return *this;
    }
    
    constexpr inline UInt4& UInt4::operator-=(const UInt4& rhs) noexcept 
    {
        storage.x -= rhs.storage.x;
        storage.y -= rhs.storage.y;
        storage.z -= rhs.storage.z;
        storage.w -= rhs.storage.w;
        return *this;
    }
    
    constexpr inline UInt4& UInt4::operator*=(const UInt4& rhs) noexcept 
    {
        storage.x *= rhs.storage.x;
        storage.y *= rhs.storage.y;
        storage.z *= rhs.storage.z;
        storage.w *= rhs.storage.w;
        return *this;
    }
    
    constexpr inline UInt4& UInt4::operator/=(const UInt4& rhs) noexcept 
    {
        storage.x /= rhs.storage.x;
        storage.y /= rhs.storage.y;
        storage.z /= rhs.storage.z;
        storage.w /= rhs.storage.w;
        return *this;
    }
    
    constexpr inline UInt4& UInt4::operator*=(uint32_t scalar) noexcept 
    {
        storage.x *= scalar;
        storage.y *= scalar;
        storage.z *= scalar;
        storage.w *= scalar;
        return *this;
    }
    
    constexpr inline UInt4& UInt4::operator/=(uint32_t scalar) noexcept 
    {
        storage.x /= scalar;
        storage.y /= scalar;
        storage.z /= scalar;
        storage.w /= scalar;
        return *this;
    }
    
    // Comparison operators
    constexpr bool UInt4::operator==(const UInt4& rhs) const noexcept 
    {
        return storage.x == rhs.storage.x && storage.y == rhs.storage.y && storage.z == rhs.storage.z && storage.w == rhs.storage.w;
    }
    
    constexpr bool UInt4::operator!=(const UInt4& rhs) const noexcept 
    {
        return !(*this == rhs);
    }

    // Swizzle accessors - common 4D combinations
    constexpr inline UInt4 UInt4::xyzw() const noexcept 
    { 
        return UInt4(storage.x, storage.y, storage.z, storage.w); 
    }

    constexpr inline UInt4 UInt4::wxyz() const noexcept 
    { 
        return UInt4(storage.w, storage.x, storage.y, storage.z); 
    }

    constexpr inline UInt4 UInt4::zwxy() const noexcept 
    { 
        return UInt4(storage.z, storage.w, storage.x, storage.y); 
    }

    constexpr inline UInt4 UInt4::yzwx() const noexcept 
    { 
        return UInt4(storage.y, storage.z, storage.w, storage.x); 
    }

    constexpr inline UInt4 UInt4::wwww() const noexcept 
    { 
        return UInt4(storage.w, storage.w, storage.w, storage.w); 
    }

    // 3D swizzles from 4D
    constexpr inline UInt3 UInt4::xyz() const noexcept 
    { 
        return UInt3(storage.x, storage.y, storage.z); 
    }

    constexpr inline UInt3 UInt4::rgb() const noexcept 
    { 
        return UInt3(storage.x, storage.y, storage.z); 
    }

    // 2D swizzles from 4D
    constexpr inline UInt2 UInt4::xy() const noexcept 
    { 
        return UInt2(storage.x, storage.y); 
    }

    constexpr inline UInt2 UInt4::zw() const noexcept 
    { 
        return UInt2(storage.z, storage.w); 
    }

    // ================================
    // Vector SIMD Implementation
    // ================================
    
    // Component accessors
    DD_MATH_FORCEINLINE float Vector::x() const noexcept 
    { 
        return DirectX::XMVectorGetX(data); 
    }
    
    DD_MATH_FORCEINLINE float Vector::y() const noexcept 
    { 
        return DirectX::XMVectorGetY(data); 
    }
    
    DD_MATH_FORCEINLINE float Vector::z() const noexcept 
    { 
        return DirectX::XMVectorGetZ(data); 
    }
    
    DD_MATH_FORCEINLINE float Vector::w() const noexcept 
    { 
        return DirectX::XMVectorGetW(data); 
    }
    
    // Arithmetic operators
    DD_MATH_FORCEINLINE Vector Vector::operator+(Vector rhs) const noexcept 
    {
        return Vector{DirectX::XMVectorAdd(data, rhs.data)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::operator-(Vector rhs) const noexcept 
    {
        return Vector{DirectX::XMVectorSubtract(data, rhs.data)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::operator*(Vector rhs) const noexcept 
    {
        return Vector{DirectX::XMVectorMultiply(data, rhs.data)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::operator/(Vector rhs) const noexcept 
    {
        return Vector{DirectX::XMVectorDivide(data, rhs.data)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::operator*(float scalar) const noexcept 
    {
        return Vector{DirectX::XMVectorScale(data, scalar)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::operator/(float scalar) const noexcept 
    {
        return Vector{DirectX::XMVectorScale(data, 1.0f / scalar)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::operator-() const noexcept 
    {
        return Vector{DirectX::XMVectorNegate(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::MultiplyAdd(Vector Factor, Vector Addend) const noexcept 
    {
        return Vector{DirectX::XMVectorMultiplyAdd(data, Factor.data, Addend.data)};
    }
    
    // Compound assignment operators
    DD_MATH_FORCEINLINE Vector& Vector::operator+=(Vector rhs) noexcept 
    {
        data = DirectX::XMVectorAdd(data, rhs.data);
        return *this;
    }
    
    DD_MATH_FORCEINLINE Vector& Vector::operator-=(Vector rhs) noexcept 
    {
        data = DirectX::XMVectorSubtract(data, rhs.data);
        return *this;
    }
    
    DD_MATH_FORCEINLINE Vector& Vector::operator*=(Vector rhs) noexcept 
    {
        data = DirectX::XMVectorMultiply(data, rhs.data);
        return *this;
    }
    
    DD_MATH_FORCEINLINE Vector& Vector::operator/=(Vector rhs) noexcept 
    {
        data = DirectX::XMVectorDivide(data, rhs.data);
        return *this;
    }
    
    DD_MATH_FORCEINLINE Vector& Vector::operator*=(float scalar) noexcept 
    {
        data = DirectX::XMVectorScale(data, scalar);
        return *this;
    }
    
    DD_MATH_FORCEINLINE Vector& Vector::operator/=(float scalar) noexcept 
    {
        data = DirectX::XMVectorScale(data, 1.0f / scalar);
        return *this;
    }

    DD_MATH_FORCEINLINE Vector Vector::Reciprocal() const noexcept 
    {
        return Vector{DirectX::XMVectorReciprocal(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::ReciprocalEst() const noexcept 
    {
        return Vector{DirectX::XMVectorReciprocalEst(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Sqrt() const noexcept 
    {
        return Vector{DirectX::XMVectorSqrt(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::SqrtEst() const noexcept 
    {
        return Vector{DirectX::XMVectorSqrtEst(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::ReciprocalSqrt() const noexcept 
    {
        return Vector{DirectX::XMVectorReciprocalSqrt(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::ReciprocalSqrtEst() const noexcept 
    {
        return Vector{DirectX::XMVectorReciprocalSqrtEst(data)};
    }
    
    // Vector operations with template dimensionality parameter
    template<int N>
    DD_MATH_FORCEINLINE Vector Vector::Normalize() const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "Normalize dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return Vector{DirectX::XMVector2Normalize(data)};
        }
        else if constexpr (N == 3)
        {
            return Vector{DirectX::XMVector3Normalize(data)};
        }
        else if constexpr (N == 4)
        {
            return Vector{DirectX::XMVector4Normalize(data)};
        }
    }
    
    template<int N>
    DD_MATH_FORCEINLINE Vector Vector::NormalizeEst() const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "NormalizeEst dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return Vector{DirectX::XMVector2NormalizeEst(data)};
        }
        else if constexpr (N == 3)
        {
            return Vector{DirectX::XMVector3NormalizeEst(data)};
        }
        else if constexpr (N == 4)
        {
            return Vector{DirectX::XMVector4NormalizeEst(data)};
        }
    }
    
    template<int N>
    DD_MATH_FORCEINLINE float Vector::Length() const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "Length dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector2Length(data));
        }
        else if constexpr (N == 3)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector3Length(data));
        }
        else if constexpr (N == 4)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector4Length(data));
        }
    }
    
    template<int N>
    DD_MATH_FORCEINLINE float Vector::LengthSq() const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "LengthSq dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector2LengthSq(data));
        }
        else if constexpr (N == 3)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(data));
        }
        else if constexpr (N == 4)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(data));
        }
    }
    
    template<int N>
    DD_MATH_FORCEINLINE float Vector::LengthEst() const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "LengthEst dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector2LengthEst(data));
        }
        else if constexpr (N == 3)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector3LengthEst(data));
        }
        else if constexpr (N == 4)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector4LengthEst(data));
        }
    }
    
    // Cross product only makes sense for 3D vectors
    DD_MATH_FORCEINLINE Vector Vector::Cross(Vector other) const noexcept 
    {
        return Vector{DirectX::XMVector3Cross(data, other.data)};
    }
    
    template<int N>
    DD_MATH_FORCEINLINE float Vector::Dot(Vector other) const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "Dot dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector2Dot(data, other.data));
        }
        else if constexpr (N == 3)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector3Dot(data, other.data));
        }
        else if constexpr (N == 4)
        {
            return DirectX::XMVectorGetX(DirectX::XMVector4Dot(data, other.data));
        }
    }
    
    DD_MATH_FORCEINLINE Vector Vector::Lerp(Vector target, float t) const noexcept 
    {
        return Vector{DirectX::XMVectorLerp(data, target.data, t)};
    }
    
    template<int N>
    DD_MATH_FORCEINLINE Vector Vector::Reflect(Vector normal) const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "Reflect dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return Vector{DirectX::XMVector2Reflect(data, normal.data)};
        }
        else if constexpr (N == 3)
        {
            return Vector{DirectX::XMVector3Reflect(data, normal.data)};
        }
        else if constexpr (N == 4)
        {
            return Vector{DirectX::XMVector4Reflect(data, normal.data)};
        }
    }
    
    template<int N>
    DD_MATH_FORCEINLINE Vector Vector::Refract(Vector normal, float refractionIndex) const noexcept 
    {
        static_assert(N >= 2 && N <= 4, "Refract dimensionality must be 2, 3, or 4");
        if constexpr (N == 2)
        {
            return Vector{DirectX::XMVector2Refract(data, normal.data, refractionIndex)};
        }
        else if constexpr (N == 3)
        {
            return Vector{DirectX::XMVector3Refract(data, normal.data, refractionIndex)};
        }
        else if constexpr (N == 4)
        {
            return Vector{DirectX::XMVector4Refract(data, normal.data, refractionIndex)};
        }
    }
    
    DD_MATH_FORCEINLINE Vector Vector::Clamp(Vector min, Vector max) const noexcept 
    {
        return Vector{DirectX::XMVectorClamp(data, min.data, max.data)};
    }
    
    DD_MATH_FORCEINLINE Vector Vector::Saturate() const noexcept 
    {
        return Vector{DirectX::XMVectorSaturate(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Abs() const noexcept 
    {
        return Vector{DirectX::XMVectorAbs(data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Min(Vector other) const noexcept 
    {
        return Vector{DirectX::XMVectorMin(data, other.data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Max(Vector other) const noexcept 
    {
        return Vector{DirectX::XMVectorMax(data, other.data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Pow(float exponent) const noexcept 
    {
        return Vector{DirectX::XMVectorPow(data, DirectX::XMVectorReplicate(exponent))};
    }

    DD_MATH_FORCEINLINE Vector Vector::Pow(Vector exponent) const noexcept 
    {
        return Vector{DirectX::XMVectorPow(data, exponent.data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Abs(Vector vec) noexcept 
    {
        return Vector{DirectX::XMVectorAbs(vec.data)};
    }

    DD_MATH_FORCEINLINE Vector Vector::Pow(Vector base, float exponent) noexcept 
    {
        return Vector{DirectX::XMVectorPow(base.data, DirectX::XMVectorReplicate(exponent))};
    }

    DD_MATH_FORCEINLINE Vector Vector::Pow(Vector base, Vector exponent) noexcept 
    {
        return Vector{DirectX::XMVectorPow(base.data, exponent.data)};
    }

    // ================================
    // Free Function Implementations
    // ================================

    // Free function scalar multiplication (scalar * vector) for storage types
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

    // Mixed-type operators (Float + Integer types, promotes to Float)
    constexpr Float2 operator+(const Float2& lhs, const Int2& rhs) noexcept 
    {
        return Float2(lhs.x + static_cast<float>(rhs.storage.x), lhs.y + static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator-(const Float2& lhs, const Int2& rhs) noexcept 
    {
        return Float2(lhs.x - static_cast<float>(rhs.storage.x), lhs.y - static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator*(const Float2& lhs, const Int2& rhs) noexcept 
    {
        return Float2(lhs.x * static_cast<float>(rhs.storage.x), lhs.y * static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator/(const Float2& lhs, const Int2& rhs) noexcept 
    {
        return Float2(lhs.x / static_cast<float>(rhs.storage.x), lhs.y / static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator+(const Float2& lhs, const UInt2& rhs) noexcept 
    {
        return Float2(lhs.x + static_cast<float>(rhs.storage.x), lhs.y + static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator-(const Float2& lhs, const UInt2& rhs) noexcept 
    {
        return Float2(lhs.x - static_cast<float>(rhs.storage.x), lhs.y - static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator*(const Float2& lhs, const UInt2& rhs) noexcept 
    {
        return Float2(lhs.x * static_cast<float>(rhs.storage.x), lhs.y * static_cast<float>(rhs.storage.y));
    }

    constexpr Float2 operator/(const Float2& lhs, const UInt2& rhs) noexcept 
    {
        return Float2(lhs.x / static_cast<float>(rhs.storage.x), lhs.y / static_cast<float>(rhs.storage.y));
    }

    constexpr Float3 operator+(const Float3& lhs, const Int3& rhs) noexcept 
    {
        return Float3(lhs.x + static_cast<float>(rhs.storage.x), lhs.y + static_cast<float>(rhs.storage.y), lhs.z + static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator-(const Float3& lhs, const Int3& rhs) noexcept 
    {
        return Float3(lhs.x - static_cast<float>(rhs.storage.x), lhs.y - static_cast<float>(rhs.storage.y), lhs.z - static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator*(const Float3& lhs, const Int3& rhs) noexcept 
    {
        return Float3(lhs.x * static_cast<float>(rhs.storage.x), lhs.y * static_cast<float>(rhs.storage.y), lhs.z * static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator/(const Float3& lhs, const Int3& rhs) noexcept 
    {
        return Float3(lhs.x / static_cast<float>(rhs.storage.x), lhs.y / static_cast<float>(rhs.storage.y), lhs.z / static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator+(const Float3& lhs, const UInt3& rhs) noexcept 
    {
        return Float3(lhs.x + static_cast<float>(rhs.storage.x), lhs.y + static_cast<float>(rhs.storage.y), lhs.z + static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator-(const Float3& lhs, const UInt3& rhs) noexcept 
    {
        return Float3(lhs.x - static_cast<float>(rhs.storage.x), lhs.y - static_cast<float>(rhs.storage.y), lhs.z - static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator*(const Float3& lhs, const UInt3& rhs) noexcept 
    {
        return Float3(lhs.x * static_cast<float>(rhs.storage.x), lhs.y * static_cast<float>(rhs.storage.y), lhs.z * static_cast<float>(rhs.storage.z));
    }

    constexpr Float3 operator/(const Float3& lhs, const UInt3& rhs) noexcept 
    {
        return Float3(lhs.x / static_cast<float>(rhs.storage.x), lhs.y / static_cast<float>(rhs.storage.y), lhs.z / static_cast<float>(rhs.storage.z));
    }

    constexpr Float4 operator+(const Float4& lhs, const Int4& rhs) noexcept 
    {
        return Float4(lhs.x + static_cast<float>(rhs.storage.x), lhs.y + static_cast<float>(rhs.storage.y), lhs.z + static_cast<float>(rhs.storage.z), lhs.w + static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator-(const Float4& lhs, const Int4& rhs) noexcept 
    {
        return Float4(lhs.x - static_cast<float>(rhs.storage.x), lhs.y - static_cast<float>(rhs.storage.y), lhs.z - static_cast<float>(rhs.storage.z), lhs.w - static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator*(const Float4& lhs, const Int4& rhs) noexcept 
    {
        return Float4(lhs.x * static_cast<float>(rhs.storage.x), lhs.y * static_cast<float>(rhs.storage.y), lhs.z * static_cast<float>(rhs.storage.z), lhs.w * static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator/(const Float4& lhs, const Int4& rhs) noexcept 
    {
        return Float4(lhs.x / static_cast<float>(rhs.storage.x), lhs.y / static_cast<float>(rhs.storage.y), lhs.z / static_cast<float>(rhs.storage.z), lhs.w / static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator+(const Float4& lhs, const UInt4& rhs) noexcept 
    {
        return Float4(lhs.x + static_cast<float>(rhs.storage.x), lhs.y + static_cast<float>(rhs.storage.y), lhs.z + static_cast<float>(rhs.storage.z), lhs.w + static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator-(const Float4& lhs, const UInt4& rhs) noexcept 
    {
        return Float4(lhs.x - static_cast<float>(rhs.storage.x), lhs.y - static_cast<float>(rhs.storage.y), lhs.z - static_cast<float>(rhs.storage.z), lhs.w - static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator*(const Float4& lhs, const UInt4& rhs) noexcept 
    {
        return Float4(lhs.x * static_cast<float>(rhs.storage.x), lhs.y * static_cast<float>(rhs.storage.y), lhs.z * static_cast<float>(rhs.storage.z), lhs.w * static_cast<float>(rhs.storage.w));
    }

    constexpr Float4 operator/(const Float4& lhs, const UInt4& rhs) noexcept 
    {
        return Float4(lhs.x / static_cast<float>(rhs.storage.x), lhs.y / static_cast<float>(rhs.storage.y), lhs.z / static_cast<float>(rhs.storage.z), lhs.w / static_cast<float>(rhs.storage.w));
    }
    
    // Free function scalar multiplication for SIMD Vector
    inline Vector operator*(float scalar, Vector vec) noexcept 
    {
        return vec * scalar;
    }

    // Conversion functions
    inline Vector ToVector(const Float2& in_float2) noexcept
    {
        return Vector{DirectX::XMLoadFloat2(&in_float2.storage)};
    }

    inline Vector ToVector(const Float3& in_float3) noexcept
    {
        return Vector{DirectX::XMLoadFloat3(&in_float3.storage)};
    }

    inline Vector ToVector(const Float4& in_float4) noexcept
    {
        return Vector{DirectX::XMLoadFloat4(&in_float4.storage)};
    }

    template<>
    inline Float2 FromVector(Vector vec) noexcept
    {
        DirectX::XMFLOAT2 result;
        DirectX::XMStoreFloat2(&result, vec.Data());
        return Float2{result};
    }

    template<>
    inline Float3 FromVector(Vector vec) noexcept
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, vec.Data());
        return Float3{result};
    }

    template<>
    inline Float4 FromVector(Vector vec) noexcept
    {
        DirectX::XMFLOAT4 result;
        DirectX::XMStoreFloat4(&result, vec.Data());
        return Float4{result};
    }

    // ================================
    // Matrix3x3 Storage Implementation
    // ================================

    // Matrix conversion constructor - extracts upper-left 3x3 from 4x4 matrix
    constexpr Float3x3::Float3x3(const Float4x4& mat4x4) noexcept 
        : storage{
            mat4x4(0, 0), mat4x4(0, 1), mat4x4(0, 2),
            mat4x4(1, 0), mat4x4(1, 1), mat4x4(1, 2),
            mat4x4(2, 0), mat4x4(2, 1), mat4x4(2, 2)
        }
    {
    }

    constexpr Float3 Float3x3::Row(size_t index) const noexcept
    {
        return Float3(storage.m[index][0], storage.m[index][1], storage.m[index][2]);
    }

    constexpr void Float3x3::SetRow(size_t index, const Float3& row) noexcept
    {
        storage.m[index][0] = row.x;
        storage.m[index][1] = row.y;
        storage.m[index][2] = row.z;
    }

    constexpr Float3 Float3x3::Column(size_t index) const noexcept
    {
        return Float3(storage.m[0][index], storage.m[1][index], storage.m[2][index]);
    }

    constexpr void Float3x3::SetColumn(size_t index, const Float3& column) noexcept
    {
        storage.m[0][index] = column.x;
        storage.m[1][index] = column.y;
        storage.m[2][index] = column.z;
    }

    constexpr bool Float3x3::operator==(const Float3x3& rhs) const noexcept
    {
        return storage.m[0][0] == rhs.storage.m[0][0] && storage.m[0][1] == rhs.storage.m[0][1] && storage.m[0][2] == rhs.storage.m[0][2] &&
               storage.m[1][0] == rhs.storage.m[1][0] && storage.m[1][1] == rhs.storage.m[1][1] && storage.m[1][2] == rhs.storage.m[1][2] &&
               storage.m[2][0] == rhs.storage.m[2][0] && storage.m[2][1] == rhs.storage.m[2][1] && storage.m[2][2] == rhs.storage.m[2][2];
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
        return Float3(storage.m[index][0], storage.m[index][1], storage.m[index][2]);
    }

    constexpr void Float4x3::SetRow(size_t index, const Float3& row) noexcept
    {
        storage.m[index][0] = row.x;
        storage.m[index][1] = row.y;
        storage.m[index][2] = row.z;
    }

    constexpr Float4 Float4x3::Column(size_t index) const noexcept
    {
        return Float4(storage.m[0][index], storage.m[1][index], storage.m[2][index], storage.m[3][index]);
    }

    constexpr void Float4x3::SetColumn(size_t index, const Float4& column) noexcept
    {
        storage.m[0][index] = column.x;
        storage.m[1][index] = column.y;
        storage.m[2][index] = column.z;
        storage.m[3][index] = column.w;
    }

    constexpr bool Float4x3::operator==(const Float4x3& rhs) const noexcept
    {
        return storage.m[0][0] == rhs.storage.m[0][0] && storage.m[0][1] == rhs.storage.m[0][1] && storage.m[0][2] == rhs.storage.m[0][2] &&
               storage.m[1][0] == rhs.storage.m[1][0] && storage.m[1][1] == rhs.storage.m[1][1] && storage.m[1][2] == rhs.storage.m[1][2] &&
               storage.m[2][0] == rhs.storage.m[2][0] && storage.m[2][1] == rhs.storage.m[2][1] && storage.m[2][2] == rhs.storage.m[2][2] &&
               storage.m[3][0] == rhs.storage.m[3][0] && storage.m[3][1] == rhs.storage.m[3][1] && storage.m[3][2] == rhs.storage.m[3][2];
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

    // Matrix conversion constructor - expands 3x3 to 4x4 with identity in bottom-right
    constexpr Float4x4::Float4x4(const Float3x3& mat3x3) noexcept 
        : storage{
            mat3x3(0, 0), mat3x3(0, 1), mat3x3(0, 2), 0.0f,
            mat3x3(1, 0), mat3x3(1, 1), mat3x3(1, 2), 0.0f,
            mat3x3(2, 0), mat3x3(2, 1), mat3x3(2, 2), 0.0f,
            0.0f,        0.0f,        0.0f,        1.0f
        }
    {
    }

    constexpr Float4 Float4x4::Row(size_t index) const noexcept
    {
        return Float4(storage.m[index][0], storage.m[index][1], storage.m[index][2], storage.m[index][3]);
    }

    constexpr void Float4x4::SetRow(size_t index, const Float4& row) noexcept
    {
        storage.m[index][0] = row.x;
        storage.m[index][1] = row.y;
        storage.m[index][2] = row.z;
        storage.m[index][3] = row.w;
    }

    constexpr Float4 Float4x4::Column(size_t index) const noexcept
    {
        return Float4(storage.m[0][index], storage.m[1][index], storage.m[2][index], storage.m[3][index]);
    }

    constexpr void Float4x4::SetColumn(size_t index, const Float4& column) noexcept
    {
        storage.m[0][index] = column.x;
        storage.m[1][index] = column.y;
        storage.m[2][index] = column.z;
        storage.m[3][index] = column.w;
    }

    constexpr bool Float4x4::operator==(const Float4x4& rhs) const noexcept
    {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                if (storage.m[i][j] != rhs.storage.m[i][j]) {
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

    // ================================
    // SIMD Matrix Implementation
    // ================================

    DD_MATH_FORCEINLINE Matrix::Matrix() noexcept
        : data(DirectX::XMMatrixIdentity())
    {
    }

    DD_MATH_FORCEINLINE Matrix::Matrix(DirectX::XMMATRIX mat) noexcept : data{ std::move(mat) }
    {
    }

    DD_MATH_FORCEINLINE Matrix::Matrix(Vector row0, Vector row1, Vector row2, Vector row3) noexcept
        : data{ DirectX::XMMATRIX{row0.Data(), row1.Data(), row2.Data(), row3.Data()} }
    {
    }

    DD_MATH_FORCEINLINE Matrix::Matrix(const Matrix& other) noexcept : data{ other.data }
    {
    }

    DD_MATH_FORCEINLINE Matrix::Matrix(Matrix&& other) noexcept : data{ std::move(other.data) }
    {
    }

    DD_MATH_FORCEINLINE Matrix& Matrix::operator=(const Matrix& other) noexcept
    {
        if (this != &other)
        {
            data = other.data;
        }
        return *this;
    }

    DD_MATH_FORCEINLINE Matrix& Matrix::operator=(Matrix&& other) noexcept
    {
        if (this != &other)
        {
            data = std::move(other.data);
        }
        return *this;
    }

    DD_MATH_FORCEINLINE Vector Matrix::GetRow(size_t index) const noexcept
    {
        return Vector{data.r[index]};
    }

    DD_MATH_FORCEINLINE void Matrix::SetRow(size_t index, Vector row) noexcept
    {
        data.r[index] = row.Data();
    }

    DD_MATH_FORCEINLINE float Matrix::operator()(size_t row, size_t col) const noexcept
    {
        // Access matrix data as float array
        const float* matrix_data = reinterpret_cast<const float*>(&data);
        return matrix_data[row * 4 + col];
    }

    DD_MATH_FORCEINLINE void Matrix::SetElement(size_t row, size_t col, float value) noexcept
    {
        // Access matrix data as float array
        float* matrix_data = reinterpret_cast<float*>(&data);
        matrix_data[row * 4 + col] = value;
    }

    DD_MATH_FORCEINLINE Matrix Matrix::operator+(const Matrix& rhs) const noexcept
    {
        return Matrix{DirectX::XMMATRIX{
            DirectX::XMVectorAdd(data.r[0], rhs.data.r[0]),
            DirectX::XMVectorAdd(data.r[1], rhs.data.r[1]),
            DirectX::XMVectorAdd(data.r[2], rhs.data.r[2]),
            DirectX::XMVectorAdd(data.r[3], rhs.data.r[3])
        }};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::operator-(const Matrix& rhs) const noexcept
    {
        return Matrix{DirectX::XMMATRIX{
            DirectX::XMVectorSubtract(data.r[0], rhs.data.r[0]),
            DirectX::XMVectorSubtract(data.r[1], rhs.data.r[1]),
            DirectX::XMVectorSubtract(data.r[2], rhs.data.r[2]),
            DirectX::XMVectorSubtract(data.r[3], rhs.data.r[3])
        }};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::operator*(const Matrix& rhs) const noexcept
    {
        return Matrix{DirectX::XMMatrixMultiply(data, rhs.data)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::operator*(float scalar) const noexcept
    {
        DirectX::XMVECTOR scale_vec = DirectX::XMVectorReplicate(scalar);
        return Matrix{DirectX::XMMATRIX{
            DirectX::XMVectorScale(data.r[0], scalar),
            DirectX::XMVectorScale(data.r[1], scalar),
            DirectX::XMVectorScale(data.r[2], scalar),
            DirectX::XMVectorScale(data.r[3], scalar)
        }};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::operator-() const noexcept
    {
        return Matrix{DirectX::XMMATRIX{
            DirectX::XMVectorNegate(data.r[0]),
            DirectX::XMVectorNegate(data.r[1]),
            DirectX::XMVectorNegate(data.r[2]),
            DirectX::XMVectorNegate(data.r[3])
        }};
    }

    DD_MATH_FORCEINLINE Matrix& Matrix::operator+=(const Matrix& rhs) noexcept
    {
        data.r[0] = DirectX::XMVectorAdd(data.r[0], rhs.data.r[0]);
        data.r[1] = DirectX::XMVectorAdd(data.r[1], rhs.data.r[1]);
        data.r[2] = DirectX::XMVectorAdd(data.r[2], rhs.data.r[2]);
        data.r[3] = DirectX::XMVectorAdd(data.r[3], rhs.data.r[3]);
        return *this;
    }

    DD_MATH_FORCEINLINE Matrix& Matrix::operator-=(const Matrix& rhs) noexcept
    {
        data.r[0] = DirectX::XMVectorSubtract(data.r[0], rhs.data.r[0]);
        data.r[1] = DirectX::XMVectorSubtract(data.r[1], rhs.data.r[1]);
        data.r[2] = DirectX::XMVectorSubtract(data.r[2], rhs.data.r[2]);
        data.r[3] = DirectX::XMVectorSubtract(data.r[3], rhs.data.r[3]);
        return *this;
    }

    DD_MATH_FORCEINLINE Matrix& Matrix::operator*=(const Matrix& rhs) noexcept
    {
        data = DirectX::XMMatrixMultiply(data, rhs.data);
        return *this;
    }

    DD_MATH_FORCEINLINE Matrix& Matrix::operator*=(float scalar) noexcept
    {
        data.r[0] = DirectX::XMVectorScale(data.r[0], scalar);
        data.r[1] = DirectX::XMVectorScale(data.r[1], scalar);
        data.r[2] = DirectX::XMVectorScale(data.r[2], scalar);
        data.r[3] = DirectX::XMVectorScale(data.r[3], scalar);
        return *this;
    }

    DD_MATH_FORCEINLINE Vector Matrix::operator*(Vector vec) const noexcept
    {
        return Vector{DirectX::XMVector4Transform(vec.Data(), data)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Transpose() const noexcept
    {
        return Matrix{DirectX::XMMatrixTranspose(data)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Inverse() const noexcept
    {
        DirectX::XMVECTOR determinant;
        return Matrix{DirectX::XMMatrixInverse(&determinant, data)};
    }

    DD_MATH_FORCEINLINE float Matrix::Determinant() const noexcept
    {
        return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(data));
    }

    // Static factory methods for transformations
    DD_MATH_FORCEINLINE Matrix Matrix::Translation(Vector translation) noexcept
    {
        return Matrix{DirectX::XMMatrixTranslationFromVector(translation.Data())};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Translation(float x, float y, float z) noexcept
    {
        return Matrix{DirectX::XMMatrixTranslation(x, y, z)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Scale(Vector scale) noexcept
    {
        return Matrix{DirectX::XMMatrixScalingFromVector(scale.Data())};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Scale(float x, float y, float z) noexcept
    {
        return Matrix{DirectX::XMMatrixScaling(x, y, z)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Scale(float uniform_scale) noexcept
    {
        return Matrix{DirectX::XMMatrixScaling(uniform_scale, uniform_scale, uniform_scale)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::RotationX(float radians) noexcept
    {
        return Matrix{DirectX::XMMatrixRotationX(radians)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::RotationY(float radians) noexcept
    {
        return Matrix{DirectX::XMMatrixRotationY(radians)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::RotationZ(float radians) noexcept
    {
        return Matrix{DirectX::XMMatrixRotationZ(radians)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::RotationAxis(Vector axis, float radians) noexcept
    {
        return Matrix{DirectX::XMMatrixRotationAxis(axis.Data(), radians)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::RotationQuaternion(Vector quaternion) noexcept
    {
        return Matrix{DirectX::XMMatrixRotationQuaternion(quaternion.Data())};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::TRS(Vector translation, Vector rotation_quaternion, Vector scale) noexcept
    {
        DirectX::XMMATRIX scale_matrix = DirectX::XMMatrixScalingFromVector(scale.Data());
        DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationQuaternion(rotation_quaternion.Data());
        DirectX::XMMATRIX translation_matrix = DirectX::XMMatrixTranslationFromVector(translation.Data());
        
        // Apply transformations in Scale -> Rotation -> Translation order
        return Matrix{DirectX::XMMatrixMultiply(DirectX::XMMatrixMultiply(scale_matrix, rotation_matrix), translation_matrix)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::LookAt(Vector eye, Vector target, Vector up) noexcept
    {
        return Matrix{DirectX::XMMatrixLookAtLH(eye.Data(), target.Data(), up.Data())};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::LookTo(Vector eye, Vector direction, Vector up) noexcept
    {
        return Matrix{DirectX::XMMatrixLookToLH(eye.Data(), direction.Data(), up.Data())};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Perspective(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) noexcept
    {
        return Matrix{DirectX::XMMatrixPerspectiveFovLH(fov_y_radians, aspect_ratio, near_plane, far_plane)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::PerspectiveLH(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) noexcept
    {
        return Matrix{DirectX::XMMatrixPerspectiveFovLH(fov_y_radians, aspect_ratio, near_plane, far_plane)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::PerspectiveRH(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) noexcept
    {
        return Matrix{DirectX::XMMatrixPerspectiveFovRH(fov_y_radians, aspect_ratio, near_plane, far_plane)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Orthographic(float width, float height, float near_plane, float far_plane) noexcept
    {
        return Matrix{DirectX::XMMatrixOrthographicLH(width, height, near_plane, far_plane)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::OrthographicLH(float width, float height, float near_plane, float far_plane) noexcept
    {
        return Matrix{DirectX::XMMatrixOrthographicLH(width, height, near_plane, far_plane)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::OrthographicRH(float width, float height, float near_plane, float far_plane) noexcept
    {
        return Matrix{DirectX::XMMatrixOrthographicRH(width, height, near_plane, far_plane)};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Identity() noexcept
    {
        return Matrix{DirectX::XMMatrixIdentity()};
    }

    DD_MATH_FORCEINLINE Matrix Matrix::Zero() noexcept
    {
        return Matrix{DirectX::XMMATRIX{
            DirectX::XMVectorZero(),
            DirectX::XMVectorZero(),
            DirectX::XMVectorZero(),
            DirectX::XMVectorZero()
        }};
    }

    DD_MATH_FORCEINLINE bool Matrix::IsIdentity() const noexcept
    {
        return DirectX::XMMatrixIsIdentity(data);
    }

    DD_MATH_FORCEINLINE bool Matrix::IsNearlyEqual(const Matrix& other, float epsilon) const noexcept
    {
        DirectX::XMVECTOR epsilon_vec = DirectX::XMVectorReplicate(epsilon);
        return DirectX::XMVector4NearEqual(data.r[0], other.data.r[0], epsilon_vec) &&
               DirectX::XMVector4NearEqual(data.r[1], other.data.r[1], epsilon_vec) &&
               DirectX::XMVector4NearEqual(data.r[2], other.data.r[2], epsilon_vec) &&
               DirectX::XMVector4NearEqual(data.r[3], other.data.r[3], epsilon_vec);
    }

    // ================================
    // Matrix Free Functions
    // ================================

    template<int N>
    DD_MATH_FORCEINLINE Vector Transform(Vector vector, Matrix matrix) noexcept
    {
        if constexpr (N == 2)
        {
            return Vector{DirectX::XMVector2Transform(vector.Data(), matrix.Data())};
        }
        else if constexpr (N == 3)
        {
            return Vector{DirectX::XMVector3Transform(vector.Data(), matrix.Data())};
        }
        else if constexpr (N == 4)
        {
            return Vector{DirectX::XMVector4Transform(vector.Data(), matrix.Data())};
        }
    }

    DD_MATH_FORCEINLINE Vector TransformNormal(Vector normal, Matrix matrix) noexcept
    {
        return Vector{DirectX::XMVector3TransformNormal(normal.Data(), matrix.Data())};
    }

    DD_MATH_FORCEINLINE Matrix operator*(float scalar, const Matrix& mat) noexcept
    {
        return mat * scalar;
    }

    // Matrix conversion functions
    DD_MATH_FORCEINLINE Matrix ToMatrix(const Float3x3& storage) noexcept
    {
        return Matrix{DirectX::XMLoadFloat3x3(&storage.Data())};
    }

    DD_MATH_FORCEINLINE Matrix ToMatrix(const Float4x3& storage) noexcept
    {
        return Matrix{DirectX::XMLoadFloat4x3(&storage.Data())};
    }

    DD_MATH_FORCEINLINE Matrix ToMatrix(const Float4x4& storage) noexcept
    {
        return Matrix{DirectX::XMLoadFloat4x4(&storage.Data())};
    }

    template<>
    DD_MATH_FORCEINLINE Float3x3 FromMatrix(const Matrix& mat) noexcept
    {
        DirectX::XMFLOAT3X3 result;
        DirectX::XMStoreFloat3x3(&result, mat.Data());
        return Float3x3{result};
    }

    template<>
    DD_MATH_FORCEINLINE Float4x3 FromMatrix(const Matrix& mat) noexcept
    {
        DirectX::XMFLOAT4X3 result;
        DirectX::XMStoreFloat4x3(&result, mat.Data());
        return Float4x3{result};
    }

    template<>
    DD_MATH_FORCEINLINE Float4x4 FromMatrix(const Matrix& mat) noexcept
    {
        DirectX::XMFLOAT4X4 result;
        DirectX::XMStoreFloat4x4(&result, mat.Data());
        return Float4x4{result};
    }

} // namespace math
