#pragma once
#ifndef DIAMONDDOGS_FOUNDATION_SHADER_HOST_TYPES_HPP
#define DIAMONDDOGS_FOUNDATION_SHADER_HOST_TYPES_HPP
#include <cstdint>
#include "math/MathStorage.hpp"

// The host types that a Lodestone host header names (HOST_TYPES on lodestone_add_shader_cook). The header writes the
// Slang spellings unqualified inside its own namespace, so these aliases sit in the global namespace. Every type here
// is plain storage with 4-byte alignment or less: the generated header adds the padding and asserts each offset.
// Matrices are row-major (rows contiguous), which is the cooker's default.

namespace math::shader_host
{

    struct Int2
    {
        int32_t x;
        int32_t y;
    };

    struct Int3
    {
        int32_t x;
        int32_t y;
        int32_t z;
    };

    struct Int4
    {
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t w;
    };

    struct UInt2
    {
        uint32_t x;
        uint32_t y;
    };

    struct UInt3
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };

    struct UInt4
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    };

    // The bits of an IEEE half. No arithmetic: the CPU only stores and sends it.
    struct Half
    {
        uint16_t Bits;
    };

    struct Half2
    {
        Half x;
        Half y;
    };

    struct Half3
    {
        Half x;
        Half y;
        Half z;
    };

    struct Half4
    {
        Half x;
        Half y;
        Half z;
        Half w;
    };

}

using float2 = math::Float2;
using float3 = math::Float3;
using float4 = math::Float4;
using float3x3 = math::Float3x3;
using float4x3 = math::Float4x3;
using float4x4 = math::Float4x4;

using int2 = math::shader_host::Int2;
using int3 = math::shader_host::Int3;
using int4 = math::shader_host::Int4;
using uint2 = math::shader_host::UInt2;
using uint3 = math::shader_host::UInt3;
using uint4 = math::shader_host::UInt4;

using half = math::shader_host::Half;
using half2 = math::shader_host::Half2;
using half3 = math::shader_host::Half3;
using half4 = math::shader_host::Half4;

static_assert(sizeof(int3) == 12u && alignof(int3) == 4u);
static_assert(sizeof(uint4) == 16u && alignof(uint4) == 4u);
static_assert(sizeof(half) == 2u && sizeof(half3) == 6u && alignof(half3) == 2u);

#endif // !DIAMONDDOGS_FOUNDATION_SHADER_HOST_TYPES_HPP
