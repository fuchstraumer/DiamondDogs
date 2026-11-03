// Copyright 2021 NVIDIA CORPORATION
// SPDX-License-Identifier: Apache-2.0
//
// NvproPyramid: compute shaders that fill the mip chain (the "image pyramid") of a 2D array texture.
//
// This is NOT our algorithm. It is a transcription of nvpro_pyramid (nvpro_pyramid.glsl) from NVIDIA's
// vk_compute_mipmaps sample, https://github.com/nvpro-samples/vk_compute_mipmaps. The schedule, the tiling, the
// subgroup shuffles and the energy-conserving 3x3 kernel of the general pipeline are NVIDIA's work. It is licensed
// under the Apache License, Version 2.0; a copy is in LICENSE.nvpro_pyramid, next to this file. We are building on
// the work of the people who came before us, and we thank them for it.
//
// Changes from the original (DiamondDogs, 2026), as Apache 2.0 section 4(b) asks:
// - Transcribed from GLSL to Slang. The two pipelines are two entry points, NvproPyramidFastCS and
//   NvproPyramidGeneralCS, in place of one source compiled twice with NVPRO_PYRAMID_IS_FAST_PIPELINE.
// - The configuration macros are gone. Lodestone permutation axes select the variant instead:
//   - NVPRO_PYRAMID_TYPE, NVPRO_PYRAMID_REDUCE, NVPRO_PYRAMID_REDUCE2 and NVPRO_PYRAMID_REDUCE4 are the
//     PYRAMID_REDUCTION interface axis (PyramidReductions.slang);
//   - NVPRO_PYRAMID_SHARED_TYPE, NVPRO_PYRAMID_SHARED_LOAD and NVPRO_PYRAMID_SHARED_STORE are the PYRAMID_SHARED_F16
//     axis;
//   - NVPRO_PYRAMID_LOAD, NVPRO_PYRAMID_LOAD_REDUCE4, NVPRO_PYRAMID_STORE and NVPRO_PYRAMID_LEVEL_SIZE read and
//     write the bindings below. LOAD_REDUCE4 uses a bilinear fetch when the reduction says it is exact (an
//     average), as the sample's sRGB preamble does.
// - The levels are one binding array, PyramidLevels[16], indexed by the level, as the sample's imageMipLevels[16] is.
// - A layer index: SV_GroupID.z is the array layer, so one dispatch covers every layer of a 2D array texture. The
//   original is 2D only and dispatches with y and z of 1.
// - The fallthrough switches of reduceStoreSample_ are if-chains with the same loads and the same arithmetic.
// - The push constant is the same 32 bits: input level << 5 | level count.
module NvproPyramid;
import LodestoneAttributes;

__include PyramidReductions;
__include PyramidFast;
__include PyramidGeneral;

// ************************************************************************************************
// Axes

// the reduction of each 2x2 (or up to 3x3) block, and the sample type (see PyramidReductions.slang)
[ls_axis_interface]
public extern struct PYRAMID_REDUCTION : IPyramidReduction = PyramidAverage;

// keep the samples that later levels read in groupshared memory as fp16 (NVPRO_PYRAMID_SHARED_TYPE f16vec4 in the
// sample). Halves the groupshared memory, at one fp16 rounding per cached level
[ls_axis_boolean][ls_axis_kind("tuning")]
extern static const bool PYRAMID_SHARED_F16 = false;

// ************************************************************************************************
// Bindings

// the whole texture, every level, sampled. Loads (and the bilinear LOAD_REDUCE4) read the input level from it
uniform Texture2DArray<float4> PyramidInput;
// linear filtering, clamp to edge: only the bilinear LOAD_REDUCE4 reads through it
uniform SamplerState PyramidSampler;

// PyramidLevels[n] views level n of the same texture, all layers (the original's imageMipLevels[n]). A dispatch never
// writes the level it reads. The host binds a view of the last level to every element past the texture's level count.
// todo: C11. The storage format is "unknown", because a [format] attribute takes a literal and an axis cannot pick it.
// Without the attribute Slang guesses rgba32f from float4, and that is wrong for any other format. Writes without a
// format need shaderStorageImageWriteWithoutFormat, so the WGSL target cannot cook this module until C11 lands.
static const uint PYRAMID_MAX_LEVELS = 16u;
[format("unknown")] uniform RWTexture2DArray<float4> PyramidLevels[PYRAMID_MAX_LEVELS];

// `level` is dynamically uniform, so every thread reads the same element. This needs
// shaderStorageImageArrayDynamicIndexing
void PyramidLevelStore(int level, uint3 texel, float4 value)
{
    PyramidLevels[level][texel] = value;
}

uint3 PyramidLevelDimensions(int level)
{
    uint3 size;
    PyramidLevels[level].GetDimensions(size.x, size.y, size.z);
    return size;
}

public struct NvproPyramidParameters
{
    // NVPRO_PYRAMID_PUSH_CONSTANT: input level << 5 | the number of levels to fill
    uint LevelsPacked;
};

// ************************************************************************************************
// The macros of the original, as functions

// the layer this workgroup works on (SV_GroupID.z), set once by each entry point
static uint s_PyramidLayer;

int PyramidInputLevel(NvproPyramidParameters parameters)
{
    return int(parameters.LevelsPacked >> 5u);
}

int PyramidLevelCount(NvproPyramidParameters parameters)
{
    return int(parameters.LevelsPacked & 31u);
}

// NVPRO_PYRAMID_LEVEL_SIZE
int2 PyramidLevelSize(int level)
{
    return int2(PyramidLevelDimensions(level).xy);
}

// NVPRO_PYRAMID_LOAD
float4 PyramidLoad(int2 coord, int level)
{
    return PyramidInput.Load(int4(coord, int(s_PyramidLayer), level));
}

// NVPRO_PYRAMID_STORE
void PyramidStore(int2 coord, int level, float4 value)
{
    PyramidLevelStore(level, uint3(uint2(coord), s_PyramidLayer), value);
}

float4 PyramidReduce(float a0, float4 v0, float a1, float4 v1, float a2, float4 v2)
{
    PYRAMID_REDUCTION reduction = {};
    return reduction.Reduce(a0, v0, a1, v1, a2, v2);
}

float4 PyramidReduce2(float4 v0, float4 v1)
{
    PYRAMID_REDUCTION reduction = {};
    return reduction.Reduce2(v0, v1);
}

float4 PyramidReduce4(float4 v00, float4 v01, float4 v10, float4 v11)
{
    PYRAMID_REDUCTION reduction = {};
    return reduction.Reduce4(v00, v01, v10, v11);
}

// NVPRO_PYRAMID_LOAD_REDUCE4: the 2x2 block from srcCoord to srcCoord + (1, 1), reduced
float4 PyramidLoadReduce4(int2 srcCoord, int srcLevel)
{
    PYRAMID_REDUCTION reduction = {};

    if (reduction.BilinearIsExact())
    {
        // the sample's loadReduce4: a normalized coordinate at the exact center of the four texels
        const float2 normCoord = (float2(srcCoord) + float2(1.0f)) / float2(PyramidLevelSize(srcLevel));
        return PyramidInput.SampleLevel(PyramidSampler, float3(normCoord, float(s_PyramidLayer)), float(srcLevel));
    }

    const float4 v00 = PyramidLoad(srcCoord + int2(0, 0), srcLevel);
    const float4 v01 = PyramidLoad(srcCoord + int2(0, 1), srcLevel);
    const float4 v10 = PyramidLoad(srcCoord + int2(1, 0), srcLevel);
    const float4 v11 = PyramidLoad(srcCoord + int2(1, 1), srcLevel);
    return reduction.Reduce4(v00, v01, v10, v11);
}

// NVPRO_PYRAMID_SHUFFLE_XOR: subgroupShuffleXor
float4 PyramidShuffleXor(float4 value, uint mask)
{
    return WaveReadLaneAt(value, WaveGetLaneIndex() ^ mask);
}

// NVPRO_PYRAMID_SHARED_STORE and NVPRO_PYRAMID_SHARED_LOAD for PYRAMID_SHARED_F16: two halves per uint. Each
// pipeline declares a float4 cache and a uint2 cache, and uses one of them; the other is never read, and the
// compiler removes it
uint2 PyramidPackHalves(float4 value)
{
    return uint2(f32tof16(value.x) | (f32tof16(value.y) << 16u), f32tof16(value.z) | (f32tof16(value.w) << 16u));
}

float4 PyramidUnpackHalves(uint2 stored)
{
    return float4(f16tof32(stored.x & 0xFFFFu), f16tof32(stored.x >> 16u), f16tof32(stored.y & 0xFFFFu), f16tof32(stored.y >> 16u));
}
