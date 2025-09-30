#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_MATH_HPP
#define DIAMOND_DOGS_FOUNDATION_MATH_HPP
#include <DirectXMath.h>

namespace math
{
    // Swizzle generation macros - these create combinations from storage members
    // 2-component swizzles returning Float2
    #define SWIZZLE_2(a, b) \
        constexpr Float2 a##b() const noexcept { return Float2(a, b); }
    
    // 3-component swizzles returning Float3
    #define SWIZZLE_3(a, b, c) \
        constexpr Float3 a##b##c() const noexcept { return Float3(a, b, c); }
    
    // 4-component swizzles returning Float4
    #define SWIZZLE_4(a, b, c, d) \
        constexpr Float4 a##b##c##d() const noexcept { return Float4(a, b, c, d); }

    // Forward declarations
    struct Float3;
    struct Float4;
    struct Int2;
    struct Int3;
    struct Int4;
    struct UInt2;
    struct UInt3;
    struct UInt4;
    struct Vector;
    struct Float3x3;
    struct Float4x3;
    struct Float4x4;
    struct Matrix;

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
        constexpr Float2(const DirectX::XMFLOAT2& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Float2(const Float2& other) noexcept = default;
        constexpr Float2(Float2&& other) noexcept = default;
        
        // Assignment operators
        constexpr Float2& operator=(const Float2& other) noexcept = default;
        constexpr Float2& operator=(Float2&& other) noexcept = default;
         
        // Array-style accessors
        constexpr float operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr float& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
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
            DirectX::XMFLOAT2 storage;
            struct
            {
                float x, y;
            };
        };
    };

    struct Float3
    {
    public:
        // Constructors
        constexpr Float3() noexcept : storage{0.0f, 0.0f, 0.0f} {}
        constexpr Float3(float x, float y, float z) noexcept : storage{x, y, z} {}
        constexpr explicit Float3(float scalar) noexcept : storage{scalar, scalar, scalar} {}
        constexpr Float3(const Float2& xy, float z) noexcept : storage{xy.x, xy.y, z} {}
        constexpr Float3(float x, const Float2& yz) noexcept : storage{x, yz.x, yz.y} {}
        constexpr Float3(const DirectX::XMFLOAT3& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Float3(const Float3& other) noexcept = default;
        constexpr Float3(Float3&& other) noexcept = default;
        
        // Assignment operators
        constexpr Float3& operator=(const Float3& other) noexcept = default;
        constexpr Float3& operator=(Float3&& other) noexcept = default;
        
        // Array-style accessors
        constexpr float operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr float& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
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
        
        // Swizzle accessors - 3D permutations
        SWIZZLE_3(x, y, z)  // identity
        SWIZZLE_3(x, z, y)
        SWIZZLE_3(y, x, z)
        SWIZZLE_3(y, z, x)
        SWIZZLE_3(z, x, y)
        SWIZZLE_3(z, y, x)
        
        // Broadcast swizzles
        SWIZZLE_3(x, x, x)
        SWIZZLE_3(y, y, y)
        SWIZZLE_3(z, z, z)
        
        // 2D extractions
        SWIZZLE_2(x, y)
        SWIZZLE_2(x, z)
        SWIZZLE_2(y, z)
        
        // gross, but necessary for interop and simple accessors
        union
        {
            DirectX::XMFLOAT3 storage;
            struct
            {
                float x, y, z;
            };
            struct
            {
                float r, g, b;
            };
            struct
            {
                float u, v, w;
            };
        };

    };

    struct Float4
    {
    public:
        // Constructors
        constexpr Float4() noexcept : storage{0.0f, 0.0f, 0.0f, 0.0f} {}
        constexpr Float4(float x, float y, float z, float w) noexcept : storage{x, y, z, w} {}
        constexpr explicit Float4(float scalar) noexcept : storage{scalar, scalar, scalar, scalar} {}
        constexpr Float4(const Float3& xyz, float w) noexcept : storage{xyz.x, xyz.y, xyz.z, w} {}
        constexpr Float4(float x, const Float3& yzw) noexcept : storage{x, yzw.x, yzw.y, yzw.z} {}
        constexpr Float4(const Float2& xy, const Float2& zw) noexcept : storage{xy.x, xy.y, zw.x, zw.y} {}
        constexpr Float4(const Float2& xy, float z, float w) noexcept : storage{xy.x, xy.y, z, w} {}
        constexpr Float4(float x, const Float2& yz, float w) noexcept : storage{x, yz.x, yz.y, w} {}
        constexpr Float4(float x, float y, const Float2& zw) noexcept : storage{x, y, zw.x, zw.y} {}
        constexpr Float4(const DirectX::XMFLOAT4& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Float4(const Float4& other) noexcept = default;
        constexpr Float4(Float4&& other) noexcept = default;
        
        // Assignment operators
        constexpr Float4& operator=(const Float4& other) noexcept = default;
        constexpr Float4& operator=(Float4&& other) noexcept = default;
    
        // Array-style accessors
        constexpr float operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr float& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
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
        
        // Swizzle accessors - 4D permutations (common ones)
        SWIZZLE_4(x, y, z, w)  // identity
        SWIZZLE_4(x, y, w, z)
        SWIZZLE_4(x, z, y, w)
        SWIZZLE_4(x, z, w, y)
        SWIZZLE_4(x, w, y, z)
        SWIZZLE_4(x, w, z, y)
        SWIZZLE_4(w, x, y, z)
        
        // Broadcast swizzles
        SWIZZLE_4(x, x, x, x)
        SWIZZLE_4(y, y, y, y)
        SWIZZLE_4(z, z, z, z)
        SWIZZLE_4(w, w, w, w)
        
        // 3D extractions
        SWIZZLE_3(x, y, z)
        SWIZZLE_3(r, g, b)  // color accessor
        
        // 2D extractions  
        SWIZZLE_2(x, y)
        SWIZZLE_2(z, w)

        union
        {
            DirectX::XMFLOAT4 storage;
            struct
            {
                float x, y, z, w;
            };
            struct
            {
                float r, g, b, a;
            };
            struct
            {
                float u, v, w, t;
            };
        };

    };

    /**
     * Signed integer vector types using DirectXMath XMINT storage.
     * These provide similar operations to float types but with integer semantics.
     * Mixed operations with float types will promote to float results.
     */
    struct Int2
    {
    public:
        // Constructors
        constexpr Int2() noexcept : storage{0, 0} {}
        constexpr Int2(int32_t x, int32_t y) noexcept : storage{x, y} {}
        constexpr explicit Int2(int32_t scalar) noexcept : storage{scalar, scalar} {}
        constexpr Int2(const DirectX::XMINT2& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Int2(const Int2& other) noexcept = default;
        constexpr Int2(Int2&& other) noexcept = default;
        
        // Assignment operators
        constexpr Int2& operator=(const Int2& other) noexcept = default;
        constexpr Int2& operator=(Int2&& other) noexcept = default;
        
        // Array-style accessors
        constexpr int32_t operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr int32_t& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // Arithmetic operators (integer)
        constexpr Int2 operator+(const Int2& rhs) const noexcept;
        constexpr Int2 operator-(const Int2& rhs) const noexcept;
        constexpr Int2 operator*(const Int2& rhs) const noexcept;
        constexpr Int2 operator/(const Int2& rhs) const noexcept;
        
        // Mixed operations with Float2 (promote to float)
        constexpr Float2 operator+(const Float2& rhs) const noexcept;
        constexpr Float2 operator-(const Float2& rhs) const noexcept;
        constexpr Float2 operator*(const Float2& rhs) const noexcept;
        constexpr Float2 operator/(const Float2& rhs) const noexcept;
        
        // Scalar operators
        constexpr Int2 operator*(int32_t scalar) const noexcept;
        constexpr Int2 operator/(int32_t scalar) const noexcept;
        
        // Mixed scalar operations (promote to float)
        constexpr Float2 operator*(float scalar) const noexcept;
        constexpr Float2 operator/(float scalar) const noexcept;
        
        // Unary operators
        constexpr Int2 operator-() const noexcept;
        
        // Compound assignment operators
        constexpr Int2& operator+=(const Int2& rhs) noexcept;
        constexpr Int2& operator-=(const Int2& rhs) noexcept;
        constexpr Int2& operator*=(const Int2& rhs) noexcept;
        constexpr Int2& operator/=(const Int2& rhs) noexcept;
        constexpr Int2& operator*=(int32_t scalar) noexcept;
        constexpr Int2& operator/=(int32_t scalar) noexcept;
        
        // Comparison operators
        constexpr bool operator==(const Int2& rhs) const noexcept;
        constexpr bool operator!=(const Int2& rhs) const noexcept;

        // Swizzle accessors - all 2D combinations
        constexpr Int2 xx() const noexcept;
        constexpr Int2 xy() const noexcept;
        constexpr Int2 yx() const noexcept;
        constexpr Int2 yy() const noexcept;

        union
        {
            DirectX::XMINT2 storage;
            struct
            {
                int32_t x, y;
            };
        };

    };

    struct UInt2
    {
    public:
        // Constructors
        constexpr UInt2() noexcept : storage{0u, 0u} {}
        constexpr UInt2(uint32_t x, uint32_t y) noexcept : storage{x, y} {}
        constexpr explicit UInt2(uint32_t scalar) noexcept : storage{scalar, scalar} {}
        constexpr UInt2(const DirectX::XMUINT2& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr UInt2(const UInt2& other) noexcept = default;
        constexpr UInt2(UInt2&& other) noexcept = default;
        
        // Assignment operators
        constexpr UInt2& operator=(const UInt2& other) noexcept = default;
        constexpr UInt2& operator=(UInt2&& other) noexcept = default;

        // Array-style accessors
        constexpr uint32_t operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr uint32_t& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // Arithmetic operators (unsigned integer)
        constexpr UInt2 operator+(const UInt2& rhs) const noexcept;
        constexpr UInt2 operator-(const UInt2& rhs) const noexcept;
        constexpr UInt2 operator*(const UInt2& rhs) const noexcept;
        constexpr UInt2 operator/(const UInt2& rhs) const noexcept;
        
        // Mixed operations with Float2 (promote to float)
        constexpr Float2 operator+(const Float2& rhs) const noexcept;
        constexpr Float2 operator-(const Float2& rhs) const noexcept;
        constexpr Float2 operator*(const Float2& rhs) const noexcept;
        constexpr Float2 operator/(const Float2& rhs) const noexcept;
        
        // Scalar operators
        constexpr UInt2 operator*(uint32_t scalar) const noexcept;
        constexpr UInt2 operator/(uint32_t scalar) const noexcept;
        
        // Mixed scalar operations (promote to float)
        constexpr Float2 operator*(float scalar) const noexcept;
        constexpr Float2 operator/(float scalar) const noexcept;
        
        // Compound assignment operators
        constexpr UInt2& operator+=(const UInt2& rhs) noexcept;
        constexpr UInt2& operator-=(const UInt2& rhs) noexcept;
        constexpr UInt2& operator*=(const UInt2& rhs) noexcept;
        constexpr UInt2& operator/=(const UInt2& rhs) noexcept;
        constexpr UInt2& operator*=(uint32_t scalar) noexcept;
        constexpr UInt2& operator/=(uint32_t scalar) noexcept;
        
        // Comparison operators
        constexpr bool operator==(const UInt2& rhs) const noexcept;
        constexpr bool operator!=(const UInt2& rhs) const noexcept;

        // Swizzle accessors - all 2D combinations
        constexpr UInt2 xx() const noexcept;
        constexpr UInt2 xy() const noexcept;
        constexpr UInt2 yx() const noexcept;
        constexpr UInt2 yy() const noexcept;

        union
        {
            DirectX::XMUINT2 storage;
            struct
            {
                uint32_t x, y;
            };
        };

    };

    struct Int3
    {
    public:
        // Constructors
        constexpr Int3() noexcept : storage{0, 0, 0} {}
        constexpr Int3(int32_t x, int32_t y, int32_t z) noexcept : storage{x, y, z} {}
        constexpr explicit Int3(int32_t scalar) noexcept : storage{scalar, scalar, scalar} {}
        constexpr Int3(const DirectX::XMINT3& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Int3(const Int3& other) noexcept = default;
        constexpr Int3(Int3&& other) noexcept = default;
        
        // Assignment operators
        constexpr Int3& operator=(const Int3& other) noexcept = default;
        constexpr Int3& operator=(Int3&& other) noexcept = default;
        
        // Array-style accessors
        constexpr int32_t operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr int32_t& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // Arithmetic operators (integer)
        constexpr Int3 operator+(const Int3& rhs) const noexcept;
        constexpr Int3 operator-(const Int3& rhs) const noexcept;
        constexpr Int3 operator*(const Int3& rhs) const noexcept;
        constexpr Int3 operator/(const Int3& rhs) const noexcept;
        
        // Mixed operations with Float3 (promote to float)
        constexpr Float3 operator+(const Float3& rhs) const noexcept;
        constexpr Float3 operator-(const Float3& rhs) const noexcept;
        constexpr Float3 operator*(const Float3& rhs) const noexcept;
        constexpr Float3 operator/(const Float3& rhs) const noexcept;
        
        // Scalar operators
        constexpr Int3 operator*(int32_t scalar) const noexcept;
        constexpr Int3 operator/(int32_t scalar) const noexcept;
        
        // Mixed scalar operations (promote to float)
        constexpr Float3 operator*(float scalar) const noexcept;
        constexpr Float3 operator/(float scalar) const noexcept;
        
        // Unary operators
        constexpr Int3 operator-() const noexcept;
        
        // Compound assignment operators
        constexpr Int3& operator+=(const Int3& rhs) noexcept;
        constexpr Int3& operator-=(const Int3& rhs) noexcept;
        constexpr Int3& operator*=(const Int3& rhs) noexcept;
        constexpr Int3& operator/=(const Int3& rhs) noexcept;
        constexpr Int3& operator*=(int32_t scalar) noexcept;
        constexpr Int3& operator/=(int32_t scalar) noexcept;
        
        // Comparison operators
        constexpr bool operator==(const Int3& rhs) const noexcept;
        constexpr bool operator!=(const Int3& rhs) const noexcept;

        // Swizzle accessors - common 3D combinations
        constexpr Int3 xyz() const noexcept;
        constexpr Int3 xzy() const noexcept;
        constexpr Int3 yxz() const noexcept;
        constexpr Int3 yzx() const noexcept;
        constexpr Int3 zxy() const noexcept;
        constexpr Int3 zyx() const noexcept;
        constexpr Int3 xxx() const noexcept;
        constexpr Int3 yyy() const noexcept;
        constexpr Int3 zzz() const noexcept;

        // 2D swizzles from 3D
        constexpr Int2 xy() const noexcept;
        constexpr Int2 xz() const noexcept;
        constexpr Int2 yz() const noexcept;

        union
        {
            DirectX::XMINT3 storage;
            struct
            {
                int32_t x, y, z;
            };
        };
        
    };

    struct UInt3
    {
    public:
        // Constructors
        constexpr UInt3() noexcept : storage{0u, 0u, 0u} {}
        constexpr UInt3(uint32_t x, uint32_t y, uint32_t z) noexcept : storage{x, y, z} {}
        constexpr explicit UInt3(uint32_t scalar) noexcept : storage{scalar, scalar, scalar} {}
        constexpr UInt3(const DirectX::XMUINT3& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr UInt3(const UInt3& other) noexcept = default;
        constexpr UInt3(UInt3&& other) noexcept = default;
        
        // Assignment operators
        constexpr UInt3& operator=(const UInt3& other) noexcept = default;
        constexpr UInt3& operator=(UInt3&& other) noexcept = default;
        
        // Array-style accessors
        constexpr uint32_t operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr uint32_t& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        

        // Arithmetic operators (unsigned integer)
        constexpr UInt3 operator+(const UInt3& rhs) const noexcept;
        constexpr UInt3 operator-(const UInt3& rhs) const noexcept;
        constexpr UInt3 operator*(const UInt3& rhs) const noexcept;
        constexpr UInt3 operator/(const UInt3& rhs) const noexcept;
        
        // Mixed operations with Float3 (promote to float)
        constexpr Float3 operator+(const Float3& rhs) const noexcept;
        constexpr Float3 operator-(const Float3& rhs) const noexcept;
        constexpr Float3 operator*(const Float3& rhs) const noexcept;
        constexpr Float3 operator/(const Float3& rhs) const noexcept;
        
        // Scalar operators
        constexpr UInt3 operator*(uint32_t scalar) const noexcept;
        constexpr UInt3 operator/(uint32_t scalar) const noexcept;
        
        // Mixed scalar operations (promote to float)
        constexpr Float3 operator*(float scalar) const noexcept;
        constexpr Float3 operator/(float scalar) const noexcept;
        
        // Compound assignment operators
        constexpr UInt3& operator+=(const UInt3& rhs) noexcept;
        constexpr UInt3& operator-=(const UInt3& rhs) noexcept;
        constexpr UInt3& operator*=(const UInt3& rhs) noexcept;
        constexpr UInt3& operator/=(const UInt3& rhs) noexcept;
        constexpr UInt3& operator*=(uint32_t scalar) noexcept;
        constexpr UInt3& operator/=(uint32_t scalar) noexcept;
        
        // Comparison operators
        constexpr bool operator==(const UInt3& rhs) const noexcept;
        constexpr bool operator!=(const UInt3& rhs) const noexcept;

        // Swizzle accessors - common 3D combinations
        constexpr UInt3 xyz() const noexcept;
        constexpr UInt3 xzy() const noexcept;
        constexpr UInt3 yxz() const noexcept;
        constexpr UInt3 yzx() const noexcept;
        constexpr UInt3 zxy() const noexcept;
        constexpr UInt3 zyx() const noexcept;
        constexpr UInt3 xxx() const noexcept;
        constexpr UInt3 yyy() const noexcept;
        constexpr UInt3 zzz() const noexcept;

        // 2D swizzles from 3D
        constexpr UInt2 xy() const noexcept;
        constexpr UInt2 xz() const noexcept;
        constexpr UInt2 yz() const noexcept;

        union
        {
            DirectX::XMUINT3 storage;
            struct
            {
                uint32_t x, y, z;
            };
        };
    };

    struct Int4
    {
    public:
        // Constructors
        constexpr Int4() noexcept : storage{0, 0, 0, 0} {}
        constexpr Int4(int32_t x, int32_t y, int32_t z, int32_t w) noexcept : storage{x, y, z, w} {}
        constexpr explicit Int4(int32_t scalar) noexcept : storage{scalar, scalar, scalar, scalar} {}
        constexpr Int4(const DirectX::XMINT4& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr Int4(const Int4& other) noexcept = default;
        constexpr Int4(Int4&& other) noexcept = default;
        
        // Assignment operators
        constexpr Int4& operator=(const Int4& other) noexcept = default;
        constexpr Int4& operator=(Int4&& other) noexcept = default;
        
        // Array-style accessors
        constexpr int32_t operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr int32_t& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // Arithmetic operators (integer)
        constexpr Int4 operator+(const Int4& rhs) const noexcept;
        constexpr Int4 operator-(const Int4& rhs) const noexcept;
        constexpr Int4 operator*(const Int4& rhs) const noexcept;
        constexpr Int4 operator/(const Int4& rhs) const noexcept;
        
        // Mixed operations with Float4 (promote to float)
        constexpr Float4 operator+(const Float4& rhs) const noexcept;
        constexpr Float4 operator-(const Float4& rhs) const noexcept;
        constexpr Float4 operator*(const Float4& rhs) const noexcept;
        constexpr Float4 operator/(const Float4& rhs) const noexcept;
        
        // Scalar operators
        constexpr Int4 operator*(int32_t scalar) const noexcept;
        constexpr Int4 operator/(int32_t scalar) const noexcept;
        
        // Mixed scalar operations (promote to float)
        constexpr Float4 operator*(float scalar) const noexcept;
        constexpr Float4 operator/(float scalar) const noexcept;
        
        // Unary operators
        constexpr Int4 operator-() const noexcept;
        
        // Compound assignment operators
        constexpr Int4& operator+=(const Int4& rhs) noexcept;
        constexpr Int4& operator-=(const Int4& rhs) noexcept;
        constexpr Int4& operator*=(const Int4& rhs) noexcept;
        constexpr Int4& operator/=(const Int4& rhs) noexcept;
        constexpr Int4& operator*=(int32_t scalar) noexcept;
        constexpr Int4& operator/=(int32_t scalar) noexcept;
        
        // Comparison operators
        constexpr bool operator==(const Int4& rhs) const noexcept;
        constexpr bool operator!=(const Int4& rhs) const noexcept;

        // Swizzle accessors - common 4D combinations
        constexpr Int4 xyzw() const noexcept;
        constexpr Int4 wxyz() const noexcept;
        constexpr Int4 zwxy() const noexcept;
        constexpr Int4 yzwx() const noexcept;
        constexpr Int4 wwww() const noexcept;

        // 3D swizzles from 4D
        constexpr Int3 xyz() const noexcept;
        constexpr Int3 rgb() const noexcept;

        // 2D swizzles from 4D
        constexpr Int2 xy() const noexcept;
        constexpr Int2 zw() const noexcept;

        union
        {
            DirectX::XMINT4 storage;
            struct
            {
                int32_t x, y, z, w;
            };
        };
        
    };

    struct UInt4
    {
    public:
        // Constructors
        constexpr UInt4() noexcept : storage{0u, 0u, 0u, 0u} {}
        constexpr UInt4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) noexcept : storage{x, y, z, w} {}
        constexpr explicit UInt4(uint32_t scalar) noexcept : storage{scalar, scalar, scalar, scalar} {}
        constexpr UInt4(const DirectX::XMUINT4& xm) noexcept : storage{xm} {}
        
        // Copy and move constructors
        constexpr UInt4(const UInt4& other) noexcept = default;
        constexpr UInt4(UInt4&& other) noexcept = default;
        
        // Assignment operators
        constexpr UInt4& operator=(const UInt4& other) noexcept = default;
        constexpr UInt4& operator=(UInt4&& other) noexcept = default;
        
        // Array-style accessors
        constexpr uint32_t operator[](size_t index) const noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        constexpr uint32_t& operator[](size_t index) noexcept 
        { 
            return (&storage.x)[index]; 
        }
        
        // Arithmetic operators (unsigned integer)
        constexpr UInt4 operator+(const UInt4& rhs) const noexcept;
        constexpr UInt4 operator-(const UInt4& rhs) const noexcept;
        constexpr UInt4 operator*(const UInt4& rhs) const noexcept;
        constexpr UInt4 operator/(const UInt4& rhs) const noexcept;
        
        // Mixed operations with Float4 (promote to float)
        constexpr Float4 operator+(const Float4& rhs) const noexcept;
        constexpr Float4 operator-(const Float4& rhs) const noexcept;
        constexpr Float4 operator*(const Float4& rhs) const noexcept;
        constexpr Float4 operator/(const Float4& rhs) const noexcept;
        
        // Scalar operators
        constexpr UInt4 operator*(uint32_t scalar) const noexcept;
        constexpr UInt4 operator/(uint32_t scalar) const noexcept;
        
        // Mixed scalar operations (promote to float)
        constexpr Float4 operator*(float scalar) const noexcept;
        constexpr Float4 operator/(float scalar) const noexcept;
        
        // Compound assignment operators
        constexpr UInt4& operator+=(const UInt4& rhs) noexcept;
        constexpr UInt4& operator-=(const UInt4& rhs) noexcept;
        constexpr UInt4& operator*=(const UInt4& rhs) noexcept;
        constexpr UInt4& operator/=(const UInt4& rhs) noexcept;
        constexpr UInt4& operator*=(uint32_t scalar) noexcept;
        constexpr UInt4& operator/=(uint32_t scalar) noexcept;
        
        // Comparison operators
        constexpr bool operator==(const UInt4& rhs) const noexcept;
        constexpr bool operator!=(const UInt4& rhs) const noexcept;

        // Swizzle accessors - common 4D combinations
        constexpr UInt4 xyzw() const noexcept;
        constexpr UInt4 wxyz() const noexcept;
        constexpr UInt4 zwxy() const noexcept;
        constexpr UInt4 yzwx() const noexcept;
        constexpr UInt4 wwww() const noexcept;

        // 3D swizzles from 4D
        constexpr UInt3 xyz() const noexcept;
        constexpr UInt3 rgb() const noexcept;

        // 2D swizzles from 4D
        constexpr UInt2 xy() const noexcept;
        constexpr UInt2 zw() const noexcept;

        union 
        {
            DirectX::XMUINT4 storage;
            struct
            {
                uint32_t x, y, z, w;
            };
        };

    };

    /**
     * SIMD Vector type - optimized for mathematical operations using DirectXMath.
     * This type should NOT be stored or persisted. Convert to storage types (Float2/3/4) 
     * at the end of mathematical operations and from storage types at the beginning.
     * 
     * All operations pass and return by value for optimal SIMD performance.
     */
    struct alignas(16) Vector
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
        DirectX::XMVECTOR Data() const noexcept 
        { 
            return data; 
        }
        
        /** @note Accessing a single scalar value in this vector type is very slow */
        float x() const noexcept;
        /** @note Accessing a single scalar value in this vector type is very slow */
        float y() const noexcept;
        /** @note Accessing a single scalar value in this vector type is very slow */
        float z() const noexcept;
        /** @note Accessing a single scalar value in this vector type is very slow */
        float w() const noexcept;
        
        // Arithmetic operators
        Vector operator+(Vector rhs) const noexcept;
        Vector operator-(Vector rhs) const noexcept;
        Vector operator*(Vector rhs) const noexcept;
        Vector operator/(Vector rhs) const noexcept;
        Vector operator*(float scalar) const noexcept;
        Vector operator/(float scalar) const noexcept;
        Vector operator-() const noexcept;

        Vector MultiplyAdd(Vector Factor, Vector Addend) const noexcept;
        
        // Compound assignment operators
        Vector& operator+=(Vector rhs) noexcept;
        Vector& operator-=(Vector rhs) noexcept;
        Vector& operator*=(Vector rhs) noexcept;
        Vector& operator/=(Vector rhs) noexcept;
        Vector& operator*=(float scalar) noexcept;
        Vector& operator/=(float scalar) noexcept;
        
        Vector Reciprocal() const noexcept;
        Vector ReciprocalEst() const noexcept;
        Vector Sqrt() const noexcept;
        Vector SqrtEst() const noexcept;
        Vector ReciprocalSqrt() const noexcept;
        Vector ReciprocalSqrtEst() const noexcept;

        template<int N>
        Vector Normalize() const noexcept;
        
        template<int N>
        Vector NormalizeEst() const noexcept;
        
        template<int N>
        float Length() const noexcept;
        
        template<int N>
        float LengthSq() const noexcept;
        
        template<int N>
        float LengthEst() const noexcept;
        
        // Cross product only makes sense for 3D vectors
        Vector Cross(Vector other) const noexcept;
        
        template<int N>
        float Dot(Vector other) const noexcept;
        
        Vector Lerp(Vector target, float t) const noexcept;
        
        template<int N>
        Vector Reflect(Vector normal) const noexcept;
        
        template<int N>
        Vector Refract(Vector normal, float refractionIndex) const noexcept;
        
        Vector Clamp(Vector min, Vector max) const noexcept;
        Vector Saturate() const noexcept;
        Vector Abs() const noexcept;
        Vector Min(Vector other) const noexcept;
        Vector Max(Vector other) const noexcept;
        Vector Pow(float exponent) const noexcept;
        Vector Pow(Vector exponent) const noexcept;

        static Vector Replicate(float scalar) noexcept;
        static Vector Zero() noexcept;
        static Vector Epsilon() noexcept;
        static Vector Identity() noexcept;
        static Vector Abs(Vector vec) noexcept;
        static Vector Pow(Vector base, float exponent) noexcept;
        static Vector Pow(Vector base, Vector exponent) noexcept;

        static Vector AlmostEqual(Vector other) noexcept;
        static Vector AlmostEqual(Vector other, float epsilon) noexcept;
        static Vector AlmostZero() noexcept;

    private:
        DirectX::XMVECTOR data;
    };

    /**
     * Matrix3x3 storage type for persistence and interop with DirectXMath.
     * These types can be stored, transferred, and used for data persistence.
     * @note You cannot perform mathematical operations directly on these types, you 
     * must convert them to the SIMD Matrix type first. This is an intentional design choice!
     */
    struct Float3x3
    {
    public:
        // Constructors
        constexpr Float3x3() noexcept : storage{
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        } {}
        
        constexpr Float3x3(
            float m00, float m01, float m02,
            float m10, float m11, float m12,
            float m20, float m21, float m22
        ) noexcept : storage{
            m00, m01, m02,
            m10, m11, m12,
            m20, m21, m22
        } {}
        
        constexpr Float3x3(const Float3& row0, const Float3& row1, const Float3& row2) noexcept 
            : storage{
                row0.x, row0.y, row0.z,
                row1.x, row1.y, row1.z,
                row2.x, row2.y, row2.z
            } {}
        
        constexpr Float3x3(const DirectX::XMFLOAT3X3& xm) noexcept : storage{xm} {}
        
        // Matrix conversion constructors
        // Extracts upper-left 3x3 portion from 4x4 matrix (useful for removing translation from view matrices for skybox rendering)
        explicit constexpr Float3x3(const Float4x4& mat4x4) noexcept;
        
        constexpr Float3x3(const Float3x3& other) noexcept = default;
        constexpr Float3x3(Float3x3&& other) noexcept = default;
        
        constexpr Float3x3& operator=(const Float3x3& other) noexcept = default;
        constexpr Float3x3& operator=(Float3x3&& other) noexcept = default;

        constexpr float operator()(size_t row, size_t col) const noexcept 
        { 
            return storage.m[row][col]; 
        }
        
        constexpr float& operator()(size_t row, size_t col) noexcept 
        { 
            return storage.m[row][col]; 
        }
        
        constexpr Float3 Row(size_t index) const noexcept;
        constexpr void SetRow(size_t index, const Float3& row) noexcept;
        
        constexpr Float3 Column(size_t index) const noexcept;
        constexpr void SetColumn(size_t index, const Float3& column) noexcept;
        
        constexpr const DirectX::XMFLOAT3X3& Data() const noexcept 
        { 
            return storage; 
        }
        
        constexpr DirectX::XMFLOAT3X3& Data() noexcept 
        { 
            return storage; 
        }
        

        constexpr bool operator==(const Float3x3& rhs) const noexcept;
        constexpr bool operator!=(const Float3x3& rhs) const noexcept;
        
        // Static factory methods
        static constexpr Float3x3 Identity() noexcept;
        static constexpr Float3x3 Zero() noexcept;

    private:
        DirectX::XMFLOAT3X3 storage;
    };
    
    /**
     * Matrix4x3 storage type for persistence and interop with DirectXMath.
     * These types can be stored, transferred, and used for data persistence.
     * 4x3 matrices are commonly used for affine transformations, those without perspective.
     * @note You cannot perform mathematical operations directly on these types, you 
     * must convert them to the SIMD Matrix type first. This is an intentional design choice!
     */
    struct Float4x3
    {
    public:

        constexpr Float4x3() noexcept : storage{
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f
        } {}
        
        constexpr Float4x3(
            float m00, float m01, float m02,
            float m10, float m11, float m12,
            float m20, float m21, float m22,
            float m30, float m31, float m32
        ) noexcept : storage{
            m00, m01, m02,
            m10, m11, m12,
            m20, m21, m22,
            m30, m31, m32
        } {}
        
        constexpr Float4x3(const Float3& row0, const Float3& row1, const Float3& row2, const Float3& row3) noexcept 
            : storage{
                row0.x, row0.y, row0.z,
                row1.x, row1.y, row1.z,
                row2.x, row2.y, row2.z,
                row3.x, row3.y, row3.z
            } {}
        
        constexpr Float4x3(const DirectX::XMFLOAT4X3& xm) noexcept : storage{xm} {}
        
        constexpr Float4x3(const Float4x3& other) noexcept = default;
        constexpr Float4x3(Float4x3&& other) noexcept = default;
        
        constexpr Float4x3& operator=(const Float4x3& other) noexcept = default;
        constexpr Float4x3& operator=(Float4x3&& other) noexcept = default;
        
        constexpr float operator()(size_t row, size_t col) const noexcept 
        {
            assert(row < 4 && col < 3);
            return storage.m[row][col]; 
        }
        
        constexpr float& operator()(size_t row, size_t col) noexcept 
        {
            assert(row < 4 && col < 3);
            return storage.m[row][col]; 
        }
        
        constexpr Float3 Row(size_t index) const noexcept;
        constexpr void SetRow(size_t index, const Float3& row) noexcept;
        
        constexpr Float4 Column(size_t index) const noexcept;
        constexpr void SetColumn(size_t index, const Float4& column) noexcept;
        
        constexpr const DirectX::XMFLOAT4X3& Data() const noexcept 
        { 
            return storage; 
        }
        
        constexpr DirectX::XMFLOAT4X3& Data() noexcept 
        { 
            return storage; 
        }
        
        constexpr bool operator==(const Float4x3& rhs) const noexcept;
        constexpr bool operator!=(const Float4x3& rhs) const noexcept;

        static constexpr Float4x3 Identity() noexcept;
        static constexpr Float4x3 Zero() noexcept;

    private:
        DirectX::XMFLOAT4X3 storage;
    };

    /**
     * Matrix4x4 storage type for persistence and interop with DirectXMath.
     * These types can be stored, transferred, and used for data persistence.
     * @note You cannot perform mathematical operations directly on these types, you
     * must convert them to the SIMD Matrix type first. This is an intentional design choice!
     */
    struct Float4x4
    {
    public:
        // Constructors
        constexpr Float4x4() noexcept : storage{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        } {}
        
        constexpr Float4x4(
            float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33
        ) noexcept : storage{
            m00, m01, m02, m03,
            m10, m11, m12, m13,
            m20, m21, m22, m23,
            m30, m31, m32, m33
        } {}
        
        constexpr Float4x4(const Float4& row0, const Float4& row1, const Float4& row2, const Float4& row3) noexcept 
            : storage{
                row0.x, row0.y, row0.z, row0.w,
                row1.x, row1.y, row1.z, row1.w,
                row2.x, row2.y, row2.z, row2.w,
                row3.x, row3.y, row3.z, row3.w
            } {}
        
        constexpr Float4x4(const DirectX::XMFLOAT4X4& xm) noexcept : storage{xm} {}
        
        // Matrix conversion constructors
        // Expands 3x3 matrix to 4x4 with identity translation and w component (useful for converting rotation/scale matrices to full transforms)
        explicit constexpr Float4x4(const Float3x3& mat3x3) noexcept;
        
        constexpr Float4x4(const Float4x4& other) noexcept = default;
        constexpr Float4x4(Float4x4&& other) noexcept = default;
        
        constexpr Float4x4& operator=(const Float4x4& other) noexcept = default;
        constexpr Float4x4& operator=(Float4x4&& other) noexcept = default;
        
        constexpr float operator()(size_t row, size_t col) const noexcept 
        { 
            return storage.m[row][col]; 
        }
        
        constexpr float& operator()(size_t row, size_t col) noexcept 
        { 
            return storage.m[row][col]; 
        }

        constexpr Float4 Row(size_t index) const noexcept;
        constexpr void SetRow(size_t index, const Float4& row) noexcept;

        constexpr Float4 Column(size_t index) const noexcept;
        constexpr void SetColumn(size_t index, const Float4& column) noexcept;
        
        constexpr const DirectX::XMFLOAT4X4& Data() const noexcept 
        { 
            return storage; 
        }
        
        constexpr DirectX::XMFLOAT4X4& Data() noexcept 
        { 
            return storage; 
        }

        constexpr bool operator==(const Float4x4& rhs) const noexcept;
        constexpr bool operator!=(const Float4x4& rhs) const noexcept;
        
        static constexpr Float4x4 Identity() noexcept;
        static constexpr Float4x4 Zero() noexcept;

    private:
        DirectX::XMFLOAT4X4 storage;
    };

    /**
     * SIMD Matrix type - optimized for mathematical operations using DirectXMath.
     * This type should NOT be stored or persisted. Convert to storage types (Matrix3x3/4x3/4x4)
     * at the end of mathematical operations and from storage types at the beginning.
     * 
     * All operations pass and return by value for optimal SIMD performance.
     * Supports both 3x3 and 4x4 operations through template parameters.
     */
    struct alignas(16) Matrix
    {
    public:
        // Constructors
        Matrix() noexcept;
        Matrix(DirectX::XMMATRIX mat) noexcept;
        
        // Construct from individual elements (4x4)
        Matrix(
            float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33
        ) noexcept : data{DirectX::XMMatrixSet(
            m00, m01, m02, m03,
            m10, m11, m12, m13,
            m20, m21, m22, m23,
            m30, m31, m32, m33
        )} {}
        
        // Construct from row vectors
        Matrix(Vector row0, Vector row1, Vector row2, Vector row3) noexcept;
        
        // Copy and move constructors
        Matrix(const Matrix& other) noexcept;
        Matrix(Matrix&& other) noexcept;
        
        // Assignment operators
        Matrix& operator=(const Matrix& other) noexcept;
        Matrix& operator=(Matrix&& other) noexcept;
        
        // DirectXMath interop
        DirectX::XMMATRIX Data() const noexcept 
        { 
            return data; 
        }
        
        // Row access
        Vector GetRow(size_t index) const noexcept;
        void SetRow(size_t index, Vector row) noexcept;
        
        // Column access  
        Vector GetColumn(size_t index) const noexcept;
        void SetColumn(size_t index, Vector column) noexcept;
        
        // Element access
        float operator()(size_t row, size_t col) const noexcept;
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
        static Matrix RotationQuaternion(Vector quaternion) noexcept;
        
        // Combined transformations
        static Matrix TRS(Vector translation, Vector rotation_quaternion, Vector scale) noexcept;
        static Matrix LookAt(Vector eye, Vector target, Vector up) noexcept;
        static Matrix LookTo(Vector eye, Vector direction, Vector up) noexcept;
        
        // Projection matrices
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
        DirectX::XMMATRIX data;
    };

    // Vector transformation functionst that use matrix and vector types together
    template<int N>
    Vector Transform(Vector vector, Matrix matrix) noexcept;
    Vector TransformNormal(Vector vector, Matrix matrix) noexcept;

    // Free function scalar multiplication (scalar * vector) for storage types
    constexpr Float2 operator*(float scalar, const Float2& vec) noexcept;
    constexpr Float3 operator*(float scalar, const Float3& vec) noexcept;
    constexpr Float4 operator*(float scalar, const Float4& vec) noexcept;

    // Mixed-type operators (Float + Integer types, promotes to Float)
    constexpr Float2 operator+(const Float2& lhs, const Int2& rhs) noexcept;
    constexpr Float2 operator-(const Float2& lhs, const Int2& rhs) noexcept;
    constexpr Float2 operator*(const Float2& lhs, const Int2& rhs) noexcept;
    constexpr Float2 operator/(const Float2& lhs, const Int2& rhs) noexcept;

    constexpr Float2 operator+(const Float2& lhs, const UInt2& rhs) noexcept;
    constexpr Float2 operator-(const Float2& lhs, const UInt2& rhs) noexcept;
    constexpr Float2 operator*(const Float2& lhs, const UInt2& rhs) noexcept;
    constexpr Float2 operator/(const Float2& lhs, const UInt2& rhs) noexcept;

    constexpr Float3 operator+(const Float3& lhs, const Int3& rhs) noexcept;
    constexpr Float3 operator-(const Float3& lhs, const Int3& rhs) noexcept;
    constexpr Float3 operator*(const Float3& lhs, const Int3& rhs) noexcept;
    constexpr Float3 operator/(const Float3& lhs, const Int3& rhs) noexcept;

    constexpr Float3 operator+(const Float3& lhs, const UInt3& rhs) noexcept;
    constexpr Float3 operator-(const Float3& lhs, const UInt3& rhs) noexcept;
    constexpr Float3 operator*(const Float3& lhs, const UInt3& rhs) noexcept;
    constexpr Float3 operator/(const Float3& lhs, const UInt3& rhs) noexcept;

    constexpr Float4 operator+(const Float4& lhs, const Int4& rhs) noexcept;
    constexpr Float4 operator-(const Float4& lhs, const Int4& rhs) noexcept;
    constexpr Float4 operator*(const Float4& lhs, const Int4& rhs) noexcept;
    constexpr Float4 operator/(const Float4& lhs, const Int4& rhs) noexcept;

    constexpr Float4 operator+(const Float4& lhs, const UInt4& rhs) noexcept;
    constexpr Float4 operator-(const Float4& lhs, const UInt4& rhs) noexcept;
    constexpr Float4 operator*(const Float4& lhs, const UInt4& rhs) noexcept;
    constexpr Float4 operator/(const Float4& lhs, const UInt4& rhs) noexcept;
    
    // Free function scalar multiplication for SIMD Vector and Matrix
    Vector operator*(float scalar, Vector vec) noexcept;
    Matrix operator*(float scalar, const Matrix& mat) noexcept;

    // Matrix-Vector operations (mixing storage and SIMD types)
    constexpr Float3 operator*(const Float3x3& mat, const Float3& vec) noexcept;
    constexpr Float4 operator*(const Float4x3& mat, const Float3& vec) noexcept;
    constexpr Float4 operator*(const Float4x3& mat, const Float4& vec) noexcept;
    constexpr Float4 operator*(const Float4x4& mat, const Float4& vec) noexcept;
    constexpr Float3 operator*(const Float4x4& mat, const Float3& vec) noexcept;
    
    // Note: Matrix * Vector is handled by Matrix::operator*(Vector) member function

    /**
     * @brief Convert Matrix3x3 storage type to SIMD Matrix
     * Use this function at the START of mathematical operations to convert from storage format
     * @param storage The Matrix3x3 to convert
     * @return SIMD Matrix for mathematical operations
     */
    Matrix ToMatrix(const Float3x3& storage) noexcept;

    /**
     * @brief Convert Matrix4x3 storage type to SIMD Matrix  
     * Use this function at the START of mathematical operations to convert from storage format
     * @param storage The Matrix4x3 to convert
     * @return SIMD Matrix for mathematical operations
     */
    Matrix ToMatrix(const Float4x3& storage) noexcept;

    /**
     * @brief Convert Matrix4x4 storage type to SIMD Matrix
     * Use this function at the START of mathematical operations to convert from storage format  
     * @param storage The Matrix4x4 to convert
     * @return SIMD Matrix for mathematical operations
     */
    Matrix ToMatrix(const Float4x4& storage) noexcept;

    template<typename T>
    T FromMatrix(const Matrix& mat) noexcept;

    /**
     * @brief Convert SIMD Matrix to Matrix3x3 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param mat The SIMD Matrix to convert
     * @return Matrix3x3 for storage/persistence
     */
    template<>
    Float3x3 FromMatrix(const Matrix& mat) noexcept;

    /**
     * @brief Convert SIMD Matrix to Matrix4x3 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param mat The SIMD Matrix to convert
     * @return Matrix4x3 for storage/persistence
     */
    template<>
    Float4x3 FromMatrix(const Matrix& mat) noexcept;

    /**
     * @brief Convert SIMD Matrix to Matrix4x4 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param mat The SIMD Matrix to convert  
     * @return Matrix4x4 for storage/persistence
     */
    template<>
    Float4x4 FromMatrix(const Matrix& mat) noexcept;

    /**
     * @brief Convert Float2 storage type to SIMD Vector
     * Use this function at the START of mathematical operations to convert from storage format
     * @param storage The Float2 to convert
     * @return SIMD Vector for mathematical operations
     */
    Vector ToVector(const Float2& storage) noexcept;

    /**
     * @brief Convert Float3 storage type to SIMD Vector  
     * Use this function at the START of mathematical operations to convert from storage format
     * @param storage The Float3 to convert
     * @return SIMD Vector for mathematical operations
     */
    Vector ToVector(const Float3& storage) noexcept;

    /**
     * @brief Convert Float4 storage type to SIMD Vector
     * Use this function at the START of mathematical operations to convert from storage format  
     * @param storage The Float4 to convert
     * @return SIMD Vector for mathematical operations
     */
    Vector ToVector(const Float4& storage) noexcept;

    template<typename T>
    T FromVector(Vector vec) noexcept;

    /**
     * @brief Convert SIMD Vector to Float2 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param vec The SIMD Vector to convert
     * @return Float2 for storage/persistence
     */
    template<>
    Float2 FromVector(Vector vec) noexcept;

    /**
     * @brief Convert SIMD Vector to Float3 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param vec The SIMD Vector to convert
     * @return Float3 for storage/persistence
     */
    template<>
    Float3 FromVector(Vector vec) noexcept;

    /**
     * @brief Convert SIMD Vector to Float4 storage type
     * Use this function at the END of mathematical operations to convert back to storage format
     * @param vec The SIMD Vector to convert  
     * @return Float4 for storage/persistence
     */
    template<>
    Float4 FromVector(Vector vec) noexcept;

    #undef SWIZZLE_2D
    #undef SWIZZLE_3D
    #undef SWIZZLE_4D
    
} // namespace math

#include "math/Math.inl"

#endif // !DIAMOND_DOGS_FOUNDATION_MATH_HPP
