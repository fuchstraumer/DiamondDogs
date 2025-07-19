#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_MATH_VECTORTYPES_HPP
#define DIAMOND_DOGS_FOUNDATION_MATH_VECTORTYPES_HPP
#include <DirectXMath.h>

namespace math
{
    // Macro to generate swizzle accessors
    #define SWIZZLE_2D(x, y) \
        constexpr Float2 x##y() const noexcept \
        { \
            return Float2(storage.x, storage.y); \
        }

    #define SWIZZLE_3D(x, y, z) \
        constexpr Float3 x##y##z() const noexcept \
        { \
            return Float3(x(), y(), z()); \
        }

    #define SWIZZLE_4D(x, y, z, w) \
        constexpr Float4 x##y##z##w() const noexcept \
        { \
            return Float4(x(), y(), z(), w()); \
        }

    // Forward declarations
    struct Float3;
    struct Float4;
    struct Vector;

    /**
     * This file defines unoptimized vector types for 2D, 3D, and 4D vectors. These all use the DirectX math types as backing storage,
     * and define basic operators, ctors, and accessors for common vector operations. These types can be kept around and stored or transferred
     * as needed, unlike the optimized SIMD type. If you need to do a lot of repeated math operations in a hot loop, transform to the SIMD type
     */

    struct Float2
    {
    public:
        // Constructors
        constexpr Float2() noexcept : storage{0.0f, 0.0f} {}
        constexpr Float2(float x, float y) noexcept : storage{x, y} {}
        constexpr explicit Float2(float scalar) noexcept : storage{scalar, scalar} {}
        constexpr Float2(const DirectX::XMFloat2& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Float2(const Float2& other) noexcept = default;
        constexpr Float2(Float2&& other) noexcept = default;
        
        // Assignment operators
        constexpr Float2& operator=(const Float2& other) noexcept = default;
        constexpr Float2& operator=(Float2&& other) noexcept = default;
        
        // Component accessors
        constexpr float x() const noexcept 
        { 
            return storage.x; 
        }
        
        constexpr float y() const noexcept 
        { 
            return storage.y; 
        }
        
        constexpr float& x() noexcept 
        { 
            return storage.x; 
        }
        
        constexpr float& y() noexcept 
        { 
            return storage.y; 
        }
        
        // Array-style accessors
        constexpr float operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr float& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // DirectXMath interop
        constexpr const DirectX::XMFloat2& data() const noexcept 
        { 
            return storage; 
        }
        
        constexpr DirectX::XMFloat2& data() noexcept 
        { 
            return storage; 
        }
        
        // Arithmetic operators
        constexpr Float2 operator+(const Float2& rhs) const noexcept 
        {
            return Float2(storage.x + rhs.storage.x, storage.y + rhs.storage.y);
        }
        
        constexpr Float2 operator-(const Float2& rhs) const noexcept 
        {
            return Float2(storage.x - rhs.storage.x, storage.y - rhs.storage.y);
        }
        
        constexpr Float2 operator*(const Float2& rhs) const noexcept 
        {
            return Float2(storage.x * rhs.storage.x, storage.y * rhs.storage.y);
        }
        
        constexpr Float2 operator/(const Float2& rhs) const noexcept 
        {
            return Float2(storage.x / rhs.storage.x, storage.y / rhs.storage.y);
        }
        
        // Scalar operators
        constexpr Float2 operator*(float scalar) const noexcept 
        {
            return Float2(storage.x * scalar, storage.y * scalar);
        }
        
        constexpr Float2 operator/(float scalar) const noexcept 
        {
            return Float2(storage.x / scalar, storage.y / scalar);
        }
        
        // Unary operators
        constexpr Float2 operator-() const noexcept 
        {
            return Float2(-storage.x, -storage.y);
        }
        
        // Compound assignment operators
        constexpr Float2& operator+=(const Float2& rhs) noexcept 
        {
            storage.x += rhs.storage.x;
            storage.y += rhs.storage.y;
            return *this;
        }
        
        constexpr Float2& operator-=(const Float2& rhs) noexcept 
        {
            storage.x -= rhs.storage.x;
            storage.y -= rhs.storage.y;
            return *this;
        }
        
        constexpr Float2& operator*=(const Float2& rhs) noexcept 
        {
            storage.x *= rhs.storage.x;
            storage.y *= rhs.storage.y;
            return *this;
        }
        
        constexpr Float2& operator/=(const Float2& rhs) noexcept 
        {
            storage.x /= rhs.storage.x;
            storage.y /= rhs.storage.y;
            return *this;
        }
        
        constexpr Float2& operator*=(float scalar) noexcept 
        {
            storage.x *= scalar;
            storage.y *= scalar;
            return *this;
        }
        
        constexpr Float2& operator/=(float scalar) noexcept 
        {
            storage.x /= scalar;
            storage.y /= scalar;
            return *this;
        }
        
        // Comparison operators
        constexpr bool operator==(const Float2& rhs) const noexcept 
        {
            return storage.x == rhs.storage.x && storage.y == rhs.storage.y;
        }
        
        constexpr bool operator!=(const Float2& rhs) const noexcept 
        {
            return !(*this == rhs);
        }
        
        // Swizzle accessors - all 2D combinations
        SWIZZLE_2D(x, x) 
        SWIZZLE_2D(x, y)
        SWIZZLE_2D(y, x) 
        SWIZZLE_2D(y, y)

    private:
        DirectX::XMFloat2 storage;
    };

    struct Float3
    {
    public:
        // Constructors
        constexpr Float3() noexcept : storage{0.0f, 0.0f, 0.0f} {}
        constexpr Float3(float x, float y, float z) noexcept : storage{x, y, z} {}
        constexpr explicit Float3(float scalar) noexcept : storage{scalar, scalar, scalar} {}
        constexpr Float3(const Float2& xy, float z) noexcept : storage{xy.x(), xy.y(), z} {}
        constexpr Float3(float x, const Float2& yz) noexcept : storage{x, yz.x(), yz.y()} {}
        constexpr Float3(const DirectX::XMFLOAT3& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Float3(const Float3& other) noexcept = default;
        constexpr Float3(Float3&& other) noexcept = default;
        
        // Assignment operators
        constexpr Float3& operator=(const Float3& other) noexcept = default;
        constexpr Float3& operator=(Float3&& other) noexcept = default;
        
        // Component accessors
        constexpr float x() const noexcept 
        { 
            return storage.x; 
        }
        
        constexpr float y() const noexcept 
        { 
            return storage.y; 
        }
        
        constexpr float z() const noexcept 
        { 
            return storage.z; 
        }
        
        constexpr float& x() noexcept 
        { 
            return storage.x; 
        }
        
        constexpr float& y() noexcept 
        { 
            return storage.y; 
        }
        
        constexpr float& z() noexcept 
        { 
            return storage.z; 
        }
        
        // Array-style accessors
        constexpr float operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr float& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // DirectXMath interop
        constexpr const DirectX::XMFLOAT3& data() const noexcept 
        { 
            return storage; 
        }
        
        constexpr DirectX::XMFLOAT3& data() noexcept 
        { 
            return storage; 
        }
        
        // Float2 accessors
        constexpr Float2 xy() const noexcept 
        { 
            return Float2(storage.x, storage.y); 
        }
        
        constexpr Float2 xz() const noexcept 
        { 
            return Float2(storage.x, storage.z); 
        }
        
        constexpr Float2 yz() const noexcept 
        { 
            return Float2(storage.y, storage.z); 
        }
        
        // Arithmetic operators
        constexpr Float3 operator+(const Float3& rhs) const noexcept 
        {
            return Float3(storage.x + rhs.storage.x, storage.y + rhs.storage.y, storage.z + rhs.storage.z);
        }
        
        constexpr Float3 operator-(const Float3& rhs) const noexcept 
        {
            return Float3(storage.x - rhs.storage.x, storage.y - rhs.storage.y, storage.z - rhs.storage.z);
        }
        
        constexpr Float3 operator*(const Float3& rhs) const noexcept 
        {
            return Float3(storage.x * rhs.storage.x, storage.y * rhs.storage.y, storage.z * rhs.storage.z);
        }
        
        constexpr Float3 operator/(const Float3& rhs) const noexcept 
        {
            return Float3(storage.x / rhs.storage.x, storage.y / rhs.storage.y, storage.z / rhs.storage.z);
        }
        
        // Scalar operators
        constexpr Float3 operator*(float scalar) const noexcept 
        {
            return Float3(storage.x * scalar, storage.y * scalar, storage.z * scalar);
        }
        
        constexpr Float3 operator/(float scalar) const noexcept 
        {
            return Float3(storage.x / scalar, storage.y / scalar, storage.z / scalar);
        }
        
        // Unary operators
        constexpr Float3 operator-() const noexcept 
        {
            return Float3(-storage.x, -storage.y, -storage.z);
        }
        
        // Compound assignment operators
        constexpr Float3& operator+=(const Float3& rhs) noexcept 
        {
            storage.x += rhs.storage.x;
            storage.y += rhs.storage.y;
            storage.z += rhs.storage.z;
            return *this;
        }
        
        constexpr Float3& operator-=(const Float3& rhs) noexcept 
        {
            storage.x -= rhs.storage.x;
            storage.y -= rhs.storage.y;
            storage.z -= rhs.storage.z;
            return *this;
        }
        
        constexpr Float3& operator*=(const Float3& rhs) noexcept 
        {
            storage.x *= rhs.storage.x;
            storage.y *= rhs.storage.y;
            storage.z *= rhs.storage.z;
            return *this;
        }
        
        constexpr Float3& operator/=(const Float3& rhs) noexcept 
        {
            storage.x /= rhs.storage.x;
            storage.y /= rhs.storage.y;
            storage.z /= rhs.storage.z;
            return *this;
        }
        
        constexpr Float3& operator*=(float scalar) noexcept 
        {
            storage.x *= scalar;
            storage.y *= scalar;
            storage.z *= scalar;
            return *this;
        }
        
        constexpr Float3& operator/=(float scalar) noexcept 
        {
            storage.x /= scalar;
            storage.y /= scalar;
            storage.z /= scalar;
            return *this;
        }
        
        // Comparison operators
        constexpr bool operator==(const Float3& rhs) const noexcept 
        {
            return storage.x == rhs.storage.x && storage.y == rhs.storage.y && storage.z == rhs.storage.z;
        }
        
        constexpr bool operator!=(const Float3& rhs) const noexcept 
        {
            return !(*this == rhs);
        }
        
        // Swizzle accessors - common 3D combinations
        SWIZZLE_3D(x, y, z) 
        SWIZZLE_3D(x, z, y) 
        SWIZZLE_3D(y, x, z)
        SWIZZLE_3D(y, z, x) 
        SWIZZLE_3D(z, x, y) 
        SWIZZLE_3D(z, y, x)
        SWIZZLE_3D(x, x, x) 
        SWIZZLE_3D(y, y, y) 
        SWIZZLE_3D(z, z, z)

    private:
        DirectX::XMFLOAT3 storage;
    };

    struct Float4
    {
    public:
        // Constructors
        constexpr Float4() noexcept : storage{0.0f, 0.0f, 0.0f, 0.0f} {}
        constexpr Float4(float x, float y, float z, float w) noexcept : storage{x, y, z, w} {}
        constexpr explicit Float4(float scalar) noexcept : storage{scalar, scalar, scalar, scalar} {}
        constexpr Float4(const Float3& xyz, float w) noexcept : storage{xyz.x(), xyz.y(), xyz.z(), w} {}
        constexpr Float4(float x, const Float3& yzw) noexcept : storage{x, yzw.x(), yzw.y(), yzw.z()} {}
        constexpr Float4(const Float2& xy, const Float2& zw) noexcept : storage{xy.x(), xy.y(), zw.x(), zw.y()} {}
        constexpr Float4(const Float2& xy, float z, float w) noexcept : storage{xy.x(), xy.y(), z, w} {}
        constexpr Float4(float x, const Float2& yz, float w) noexcept : storage{x, yz.x(), yz.y(), w} {}
        constexpr Float4(float x, float y, const Float2& zw) noexcept : storage{x, y, zw.x(), zw.y()} {}
        constexpr Float4(const DirectX::XMFLOAT4& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Float4(const Float4& other) noexcept = default;
        constexpr Float4(Float4&& other) noexcept = default;
        
        // Assignment operators
        constexpr Float4& operator=(const Float4& other) noexcept = default;
        constexpr Float4& operator=(Float4&& other) noexcept = default;
        
        // Component accessors
        constexpr float x() const noexcept 
        { 
            return storage.x; 
        }
        
        constexpr float y() const noexcept 
        { 
            return storage.y; 
        }
        
        constexpr float z() const noexcept 
        { 
            return storage.z; 
        }
        
        constexpr float w() const noexcept 
        { 
            return storage.w; 
        }
        
        constexpr float& x() noexcept 
        { 
            return storage.x; 
        }
        
        constexpr float& y() noexcept 
        { 
            return storage.y; 
        }
        
        constexpr float& z() noexcept 
        { 
            return storage.z; 
        }
        
        constexpr float& w() noexcept 
        { 
            return storage.w; 
        }
        
        // Array-style accessors
        constexpr float operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr float& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // DirectXMath interop
        constexpr const DirectX::XMFLOAT4& data() const noexcept 
        { 
            return storage; 
        }
        
        constexpr DirectX::XMFLOAT4& data() noexcept 
        { 
            return storage; 
        }
        
        // Sub-vector accessors
        constexpr Float2 xy() const noexcept 
        { 
            return Float2(storage.x, storage.y); 
        }
        
        constexpr Float2 xz() const noexcept 
        { 
            return Float2(storage.x, storage.z); 
        }
        
        constexpr Float2 xw() const noexcept 
        { 
            return Float2(storage.x, storage.w); 
        }
        
        constexpr Float2 yz() const noexcept 
        { 
            return Float2(storage.y, storage.z); 
        }
        
        constexpr Float2 yw() const noexcept 
        { 
            return Float2(storage.y, storage.w); 
        }
        
        constexpr Float2 zw() const noexcept 
        { 
            return Float2(storage.z, storage.w); 
        }
        
        constexpr Float3 xyz() const noexcept 
        { 
            return Float3(storage.x, storage.y, storage.z); 
        }
        
        constexpr Float3 xyw() const noexcept 
        { 
            return Float3(storage.x, storage.y, storage.w); 
        }
        
        constexpr Float3 xzw() const noexcept 
        { 
            return Float3(storage.x, storage.z, storage.w); 
        }
        
        constexpr Float3 yzw() const noexcept 
        { 
            return Float3(storage.y, storage.z, storage.w); 
        }
        
        // Arithmetic operators
        constexpr Float4 operator+(const Float4& rhs) const noexcept 
        {
            return Float4(storage.x + rhs.storage.x, storage.y + rhs.storage.y, 
                        storage.z + rhs.storage.z, storage.w + rhs.storage.w);
        }
        
        constexpr Float4 operator-(const Float4& rhs) const noexcept 
        {
            return Float4(storage.x - rhs.storage.x, storage.y - rhs.storage.y, 
                        storage.z - rhs.storage.z, storage.w - rhs.storage.w);
        }
        
        constexpr Float4 operator*(const Float4& rhs) const noexcept 
        {
            return Float4(storage.x * rhs.storage.x, storage.y * rhs.storage.y, 
                        storage.z * rhs.storage.z, storage.w * rhs.storage.w);
        }
        
        constexpr Float4 operator/(const Float4& rhs) const noexcept 
        {
            return Float4(storage.x / rhs.storage.x, storage.y / rhs.storage.y, 
                        storage.z / rhs.storage.z, storage.w / rhs.storage.w);
        }
        
        // Scalar operators
        constexpr Float4 operator*(float scalar) const noexcept 
        {
            return Float4(storage.x * scalar, storage.y * scalar, 
                        storage.z * scalar, storage.w * scalar);
        }
        
        constexpr Float4 operator/(float scalar) const noexcept 
        {
            return Float4(storage.x / scalar, storage.y / scalar, 
                        storage.z / scalar, storage.w / scalar);
        }
        
        // Unary operators
        constexpr Float4 operator-() const noexcept 
        {
            return Float4(-storage.x, -storage.y, -storage.z, -storage.w);
        }
        
        // Compound assignment operators
        constexpr Float4& operator+=(const Float4& rhs) noexcept 
        {
            storage.x += rhs.storage.x;
            storage.y += rhs.storage.y;
            storage.z += rhs.storage.z;
            storage.w += rhs.storage.w;
            return *this;
        }
        
        constexpr Float4& operator-=(const Float4& rhs) noexcept 
        {
            storage.x -= rhs.storage.x;
            storage.y -= rhs.storage.y;
            storage.z -= rhs.storage.z;
            storage.w -= rhs.storage.w;
            return *this;
        }
        
        constexpr Float4& operator*=(const Float4& rhs) noexcept 
        {
            storage.x *= rhs.storage.x;
            storage.y *= rhs.storage.y;
            storage.z *= rhs.storage.z;
            storage.w *= rhs.storage.w;
            return *this;
        }
        
        constexpr Float4& operator/=(const Float4& rhs) noexcept 
        {
            storage.x /= rhs.storage.x;
            storage.y /= rhs.storage.y;
            storage.z /= rhs.storage.z;
            storage.w /= rhs.storage.w;
            return *this;
        }
        
        constexpr Float4& operator*=(float scalar) noexcept 
        {
            storage.x *= scalar;
            storage.y *= scalar;
            storage.z *= scalar;
            storage.w *= scalar;
            return *this;
        }
        
        constexpr Float4& operator/=(float scalar) noexcept 
        {
            storage.x /= scalar;
            storage.y /= scalar;
            storage.z /= scalar;
            storage.w /= scalar;
            return *this;
        }
        
        // Comparison operators
        constexpr bool operator==(const Float4& rhs) const noexcept 
        {
            return storage.x == rhs.storage.x && storage.y == rhs.storage.y && 
                storage.z == rhs.storage.z && storage.w == rhs.storage.w;
        }
        
        constexpr bool operator!=(const Float4& rhs) const noexcept 
        {
            return !(*this == rhs);
        }
        
        // Swizzle accessors - common 4D combinations
        SWIZZLE_4D(x, y, z, w) 
        SWIZZLE_4D(x, y, w, z) 
        SWIZZLE_4D(x, z, y, w)
        SWIZZLE_4D(x, z, w, y) 
        SWIZZLE_4D(x, w, y, z) 
        SWIZZLE_4D(x, w, z, y)
        SWIZZLE_4D(x, x, x, x) 
        SWIZZLE_4D(y, y, y, y) 
        SWIZZLE_4D(z, z, z, z) 
        SWIZZLE_4D(w, w, w, w)

    private:
        DirectX::XMFLOAT4 storage;
    };

    /**
     * SIMD Vector type - optimized for mathematical operations using DirectXMath.
     * This type should NOT be stored or persisted. Convert to storage types (Float2/3/4) 
     * at the end of mathematical operations and from storage types at the beginning.
     * 
     * All operations pass and return by value for optimal SIMD performance.
     */
    struct Vector
    {
    public:
        // Constructors
        Vector() noexcept : data{DirectX::XMVectorZero()} {}
        Vector(float x, float y, float z, float w) noexcept : data{DirectX::XMVectorSet(x, y, z, w)} {}
        Vector(float x, float y, float z) noexcept : data{DirectX::XMVectorSet(x, y, z, 0.0f)} {}
        Vector(float x, float y) noexcept : data{DirectX::XMVectorSet(x, y, 0.0f, 0.0f)} {}
        explicit Vector(float scalar) noexcept : data{DirectX::XMVectorReplicate(scalar)} {}
        Vector(DirectX::XMVECTOR vec) noexcept : data{vec} {}
        
        // Copy and move constructors
        Vector(const Vector& other) noexcept = default;
        Vector(Vector&& other) noexcept = default;
        
        // Assignment operators
        Vector& operator=(const Vector& other) noexcept = default;
        Vector& operator=(Vector&& other) noexcept = default;
        
        // DirectXMath interop
        DirectX::XMVECTOR raw() const noexcept 
        { 
            return data; 
        }
        
        // Component accessors
        float x() const noexcept 
        { 
            return DirectX::XMVectorGetX(data); 
        }
        
        float y() const noexcept 
        { 
            return DirectX::XMVectorGetY(data); 
        }
        
        float z() const noexcept 
        { 
            return DirectX::XMVectorGetZ(data); 
        }
        
        float w() const noexcept 
        { 
            return DirectX::XMVectorGetW(data); 
        }
        
        // Arithmetic operators
        Vector operator+(Vector rhs) const noexcept 
        {
            return Vector{DirectX::XMVectorAdd(data, rhs.data)};
        }
        
        Vector operator-(Vector rhs) const noexcept 
        {
            return Vector{DirectX::XMVectorSubtract(data, rhs.data)};
        }
        
        Vector operator*(Vector rhs) const noexcept 
        {
            return Vector{DirectX::XMVectorMultiply(data, rhs.data)};
        }
        
        Vector operator/(Vector rhs) const noexcept 
        {
            return Vector{DirectX::XMVectorDivide(data, rhs.data)};
        }
        
        Vector operator*(float scalar) const noexcept 
        {
            return Vector{DirectX::XMVectorScale(data, scalar)};
        }
        
        Vector operator/(float scalar) const noexcept 
        {
            return Vector{DirectX::XMVectorScale(data, 1.0f / scalar)};
        }
        
        Vector operator-() const noexcept 
        {
            return Vector{DirectX::XMVectorNegate(data)};
        }
        
        // Compound assignment operators
        Vector& operator+=(Vector rhs) noexcept 
        {
            data = DirectX::XMVectorAdd(data, rhs.data);
            return *this;
        }
        
        Vector& operator-=(Vector rhs) noexcept 
        {
            data = DirectX::XMVectorSubtract(data, rhs.data);
            return *this;
        }
        
        Vector& operator*=(Vector rhs) noexcept 
        {
            data = DirectX::XMVectorMultiply(data, rhs.data);
            return *this;
        }
        
        Vector& operator/=(Vector rhs) noexcept 
        {
            data = DirectX::XMVectorDivide(data, rhs.data);
            return *this;
        }
        
        Vector& operator*=(float scalar) noexcept 
        {
            data = DirectX::XMVectorScale(data, scalar);
            return *this;
        }
        
        Vector& operator/=(float scalar) noexcept 
        {
            data = DirectX::XMVectorScale(data, 1.0f / scalar);
            return *this;
        }
        
        // Vector operations with template dimensionality parameter
        template<int N>
        Vector Normalize() const noexcept 
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
        Vector NormalizeEst() const noexcept 
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
        float Length() const noexcept 
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
        float LengthSq() const noexcept 
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
        float LengthEst() const noexcept 
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
        Vector Cross(Vector other) const noexcept 
        {
            return Vector{DirectX::XMVector3Cross(data, other.data)};
        }
        
        template<int N>
        float Dot(Vector other) const noexcept 
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
        
        Vector Lerp(Vector target, float t) const noexcept 
        {
            return Vector{DirectX::XMVectorLerp(data, target.data, t)};
        }
        
        template<int N>
        Vector Reflect(Vector normal) const noexcept 
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
        Vector Refract(Vector normal, float refractionIndex) const noexcept 
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
        
        Vector Clamp(Vector min, Vector max) const noexcept 
        {
            return Vector{DirectX::XMVectorClamp(data, min.data, max.data)};
        }
        
        Vector Saturate() const noexcept 
        {
            return Vector{DirectX::XMVectorSaturate(data)};
        }
        
        Vector Abs() const noexcept 
        {
            return Vector{DirectX::XMVectorAbs(data)};
        }
        
        Vector Min(Vector other) const noexcept 
        {
            return Vector{DirectX::XMVectorMin(data, other.data)};
        }
        
        Vector Max(Vector other) const noexcept 
        {
            return Vector{DirectX::XMVectorMax(data, other.data)};
        }

    private:
        DirectX::XMVECTOR data;
    };

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
    
    // Free function scalar multiplication for SIMD Vector
    Vector operator*(float scalar, Vector vec) noexcept 
    {
        return vec * scalar;
    }

    /**
     * @brief Convert Float2 storage type to SIMD Vector
     * Use this function at the START of mathematical operations to convert from storage format
     * @param storage The Float2 to convert
     * @return SIMD Vector for mathematical operations
     */
    Vector ToVector(const Float2& storage) noexcept
    {
        return Vector{DirectX::XMLoadFloat2(&storage.data())};
    }

    /**
     * @brief Convert Float3 storage type to SIMD Vector  
     * Use this function at the START of mathematical operations to convert from storage format
     * @param storage The Float3 to convert
     * @return SIMD Vector for mathematical operations
     */
    Vector ToVector(const Float3& storage) noexcept
    {
        return Vector{DirectX::XMLoadFloat3(&storage.data())};
    }

    /**
     * @brief Convert Float4 storage type to SIMD Vector
     * Use this function at the START of mathematical operations to convert from storage format  
     * @param storage The Float4 to convert
     * @return SIMD Vector for mathematical operations
     */
    Vector ToVector(const Float4& storage) noexcept
    {
        return Vector{DirectX::XMLoadFloat4(&storage.data())};
    }

    /**
     * @brief Convert SIMD Vector to Float2 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param vec The SIMD Vector to convert
     * @return Float2 for storage/persistence
     */
    Float2 ToFloat2(Vector vec) noexcept
    {
        DirectX::XMFLOAT2 result;
        DirectX::XMStoreFloat2(&result, vec.raw());
        return Float2{result};
    }

    /**
     * @brief Convert SIMD Vector to Float3 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param vec The SIMD Vector to convert
     * @return Float3 for storage/persistence
     */
    Float3 ToFloat3(Vector vec) noexcept
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, vec.raw());
        return Float3{result};
    }

    /**
     * @brief Convert SIMD Vector to Float4 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param vec The SIMD Vector to convert  
     * @return Float4 for storage/persistence
     */
    Float4 ToFloat4(Vector vec) noexcept
    {
        DirectX::XMFLOAT4 result;
        DirectX::XMStoreFloat4(&result, vec.raw());
        return Float4{result};
    }

    #undef SWIZZLE_2D
    #undef SWIZZLE_3D
    #undef SWIZZLE_4D
    
} // namespace math

#endif // !DIAMOND_DOGS_FOUNDATION_MATH_VECTORTYPES_HPP
