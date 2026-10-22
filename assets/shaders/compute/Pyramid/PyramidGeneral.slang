// Copyright 2021 NVIDIA CORPORATION
// SPDX-License-Identifier: Apache-2.0
//
// Part of NvproPyramid, a Slang transcription of NVIDIA's nvpro_pyramid (see NvproPyramid.slang for the
// attribution and the list of changes). This file is the general pipeline (NVPRO_PYRAMID_IS_FAST_PIPELINE == 0).
// The comments are the original's, apart from the notes marked "DiamondDogs".
implementing NvproPyramid;

// General-case shader for generating 1 or 2 levels of the mip pyramid.
// When generating 1 level, each workgroup handles up to 128 samples of the
// output mip level. When generating 2 levels, each workgroup handles
// a 8x8 tile of the last (2nd) output mip level, generating up to
// 17x17 samples of the intermediate (1st) output mip level along the way.
//
// Dispatch with y = 1, and z = the layer count (DiamondDogs). It has no special hardware requirements.

// When generating 2 levels, the results of generating the intermediate
// level (first level generated) are cached here; this is the input tile
// needed to generate the 8x8 tile of the second level generated.
groupshared float4 s_GeneralLevel[17][17]; // [y][x]
// DiamondDogs: the PYRAMID_SHARED_F16 copy of the same cache
groupshared uint2 s_GeneralLevelHalves[17][17];

void GeneralSharedStore(int2 coord, float4 value)
{
    if (PYRAMID_SHARED_F16)
    {
        s_GeneralLevelHalves[coord.y][coord.x] = PyramidPackHalves(value);
    }
    else
    {
        s_GeneralLevel[coord.y][coord.x] = value;
    }
}

float4 GeneralSharedLoad(int2 coord)
{
    if (PYRAMID_SHARED_F16)
    {
        return PyramidUnpackHalves(s_GeneralLevelHalves[coord.y][coord.x]);
    }

    return s_GeneralLevel[coord.y][coord.x];
}

int2 GeneralKernelSizeFromInputSize(int2 inputSize)
{
    return int2(inputSize.x == 1 ? 1 : (2 | (inputSize.x & 1)), inputSize.y == 1 ? 1 : (2 | (inputSize.y & 1)));
}

float4 GeneralLoadSample(int2 srcCoord, int srcLevel, bool loadFromShared)
{
    if (loadFromShared)
    {
        return GeneralSharedLoad(srcCoord);
    }

    return PyramidLoad(srcCoord, srcLevel);
}

// DiamondDogs: one column of reduceStoreSample_'s vertical pass, kernelHeight samples from the top at srcCoord.
// The original writes it as a fallthrough switch; this is the same loads and the same reduction
float4 GeneralReduceColumn(int2 srcCoord, int srcLevel, bool loadFromShared, int kernelHeight, float w0, float w1, float w2)
{
    const float4 v0 = GeneralLoadSample(srcCoord, srcLevel, loadFromShared);

    if (kernelHeight == 1)
    {
        return v0;
    }

    const float4 v1 = GeneralLoadSample(srcCoord + int2(0, 1), srcLevel, loadFromShared);

    if (kernelHeight == 2)
    {
        return PyramidReduce2(v0, v1);
    }

    const float4 v2 = GeneralLoadSample(srcCoord + int2(0, 2), srcLevel, loadFromShared);
    return PyramidReduce(w0, v0, w1, v1, w2, v2);
}

// Handle loading and reducing a rectangle of size kernelSize
// with the given upper-left coordinate srcCoord. Samples read from
// mip level srcLevel if !loadFromShared, the shared level otherwise.
//
// kernelSize must range from 1x1 to 3x3.
//
// Once computed, the sample is written to the given coordinate of the
// specified destination mip level, and returned. The destination
// image size is needed to compute the kernel weights.
float4 GeneralReduceStoreSample(int2 srcCoord, int srcLevel, bool loadFromShared, int2 kernelSize, int2 dstImageSize, int2 dstCoord, int dstLevel)
{
    float n = float(dstImageSize.y);
    float rcp = 1.0f / (2.0f * n + 1.0f);
    float w0 = rcp * (n - float(dstCoord.y));
    float w1 = rcp * n;
    float w2 = 1.0f - w0 - w1;

    // Reduce vertically up to 3 times (depending on kernel horizontal size)
    const float4 h0 = GeneralReduceColumn(srcCoord, srcLevel, loadFromShared, kernelSize.y, w0, w1, w2);
    float4 h1 = h0;
    float4 h2 = h0;

    if (kernelSize.x >= 2)
    {
        h1 = GeneralReduceColumn(srcCoord + int2(1, 0), srcLevel, loadFromShared, kernelSize.y, w0, w1, w2);
    }

    if (kernelSize.x == 3)
    {
        h2 = GeneralReduceColumn(srcCoord + int2(2, 0), srcLevel, loadFromShared, kernelSize.y, w0, w1, w2);
    }

    // Reduce up to 3 samples horizontally.
    float4 result = h0;

    if (kernelSize.x == 3)
    {
        n = float(dstImageSize.x);
        rcp = 1.0f / (2.0f * n + 1.0f);
        w0 = rcp * (n - float(dstCoord.x));
        w1 = rcp * n;
        w2 = 1.0f - w0 - w1;
        result = PyramidReduce(w0, h0, w1, h1, w2, h2);
    }
    else if (kernelSize.x == 2)
    {
        result = PyramidReduce2(h0, h1);
    }

    // Write out sample.
    PyramidStore(dstCoord, dstLevel, result);
    return result;
}

// Compute and write out (to the 1st mip level generated) the samples
// at coordinates
//     initDstCoord,
//     initDstCoord + step, ...
//     initDstCoord + (iterations-1) * step
// and cache them at in the shared level tile at coordinates
//     initSharedCoord,
//     initSharedCoord + step, ...
//     initSharedCoord + (iterations-1) * step
// If boundsCheck is true, skip coordinates that are out of bounds.
void GeneralIntermediateLevelLoop(int inputLevel, int2 initDstCoord, int2 initSharedCoord, int2 step, int iterations, bool boundsCheck)
{
    int2 dstCoord = initDstCoord;
    int2 sharedCoord = initSharedCoord;
    const int srcLevel = inputLevel;
    const int dstLevel = srcLevel + 1;
    const int2 srcImageSize = PyramidLevelSize(srcLevel);
    const int2 dstImageSize = PyramidLevelSize(dstLevel);
    const int2 kernelSize = GeneralKernelSizeFromInputSize(srcImageSize);

    for (int i = 0; i < iterations; ++i)
    {
        const int2 srcCoord = dstCoord * 2;
        // Optional bounds check. As in the original, `continue` also skips the step below; the coordinates only
        // grow, so every later sample of this thread is out of bounds too
        const bool inBounds = !boundsCheck || (uint(dstCoord.x) < uint(dstImageSize.x) && uint(dstCoord.y) < uint(dstImageSize.y));

        if (!inBounds)
        {
            continue;
        }

        const float4 sample = GeneralReduceStoreSample(srcCoord, srcLevel, false, kernelSize, dstImageSize, dstCoord, dstLevel);

        // Above function handles writing to the actual output; manually
        // cache into shared memory here.
        GeneralSharedStore(sharedCoord, sample);
        dstCoord += step;
        sharedCoord += step;
    }
}

// Function for the workgroup that handles filling the intermediate level
// (caching it in shared memory as well).
//
// We need somewhere from 16x16 to 17x17 samples, depending
// on what the kernel size for the 2nd mip level generation will be.
//
// dstTileCoord : upper left coordinate of the tile to generate.
// boundsCheck  : whether to skip samples that are out-of-bounds.
void GeneralFillIntermediateTile(int inputLevel, int2 dstTileCoord, bool boundsCheck, uint localIdx)
{
    int2 initThreadOffset;
    int2 step;
    int iterations;

    const int2 dstImageSize = PyramidLevelSize(inputLevel + 1);
    const int2 futureKernelSize = GeneralKernelSizeFromInputSize(dstImageSize);

    if (futureKernelSize.x == 3)
    {
        if (futureKernelSize.y == 3)
        {
            // Fill in 2 17x7 steps and 1 17x3 step (9 idle threads)
            initThreadOffset = int2(int(localIdx % 17u), int(localIdx / 17u));
            step = int2(0, 7);
            iterations = localIdx >= 7u * 17u ? 0 : localIdx < 3u * 17u ? 3 : 2;
        }
        else  // Future 3x[2,1] kernel
        {
            // Fill in 2 8x16 steps and 1 1x16 step
            initThreadOffset = int2(int(localIdx / 16u), int(localIdx % 16u));
            step = int2(8, 0);
            iterations = localIdx < 1u * 16u ? 3 : 2;
        }
    }
    else
    {
        if (futureKernelSize.y == 3)
        {
            // Fill in 2 16x8 steps and 1 16x1 step
            initThreadOffset = int2(int(localIdx % 16u), int(localIdx / 16u));
            step = int2(0, 8);
            iterations = localIdx < 1u * 16u ? 3 : 2;
        }
        else
        {
            // Fill in 2 16x8 steps
            initThreadOffset = int2(int(localIdx % 16u), int(localIdx / 16u));
            step = int2(0, 8);
            iterations = 2;
        }
    }

    GeneralIntermediateLevelLoop(inputLevel, dstTileCoord + initThreadOffset, initThreadOffset, step, iterations, boundsCheck);
}

// Function for the workgroup that handles filling the last level tile
// (2nd level after the original input level), using as input the
// tile in shared memory.
//
// dstTileCoord : upper left coordinate of the tile to generate.
// boundsCheck  : whether to skip samples that are out-of-bounds.
void GeneralFillLastTile(int inputLevel, int2 dstTileCoord, bool boundsCheck, uint localIdx)
{
    if (localIdx < 8u * 8u)
    {
        const int2 threadOffset = int2(int(localIdx % 8u), int(localIdx / 8u));
        const int srcLevel = inputLevel + 1;
        const int dstLevel = inputLevel + 2;
        const int2 srcImageSize = PyramidLevelSize(srcLevel);
        const int2 dstImageSize = PyramidLevelSize(dstLevel);

        const int2 srcSharedCoord = threadOffset * 2;
        const int2 kernelSize = GeneralKernelSizeFromInputSize(srcImageSize);
        const int2 dstCoord = threadOffset + dstTileCoord;
        bool inBounds = true;

        if (boundsCheck)
        {
            inBounds = (uint(dstCoord.x) < uint(dstImageSize.x)) && (uint(dstCoord.y) < uint(dstImageSize.y));
        }

        if (inBounds)
        {
            GeneralReduceStoreSample(srcSharedCoord, 0, true, kernelSize, dstImageSize, dstCoord, dstLevel);
        }
    }
}

// dispatch (workgroups, 1, layers). One level: ceil(output samples / 128) workgroups. Two levels: one workgroup
// per 8x8 tile of the second output level
[shader("compute")]
[numthreads(128, 1, 1)]
void NvproPyramidGeneralCS(
    uniform NvproPyramidParameters PyramidParams,
    uint3 globalId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint localIdx : SV_GroupIndex)
{
    s_PyramidLayer = groupId.z;
    const int inputLevel = PyramidInputLevel(PyramidParams);

    if (PyramidLevelCount(PyramidParams) == 1)
    {
        const int2 kernelSize = GeneralKernelSizeFromInputSize(PyramidLevelSize(inputLevel));
        const int2 dstImageSize = PyramidLevelSize(inputLevel + 1);
        const int2 dstCoord = int2(int(globalId.x) % dstImageSize.x, int(globalId.x) / dstImageSize.x);
        const int2 srcCoord = dstCoord * 2;

        if (dstCoord.y < dstImageSize.y)
        {
            GeneralReduceStoreSample(srcCoord, inputLevel, false, kernelSize, dstImageSize, dstCoord, inputLevel + 1);
        }
    }
    else  // Handling two levels.
    {
        // Assign a 8x8 tile of mip level inputLevel + 2 to this workgroup.
        const int level2 = inputLevel + 2;
        const int2 level2Size = PyramidLevelSize(level2);
        int2 tileCount;
        tileCount.x = int(uint(level2Size.x + 7) / 8u);
        tileCount.y = int(uint(level2Size.y + 7) / 8u);
        const int2 tileIdx = int2(int(groupId.x % uint(tileCount.x)), int(groupId.x / uint(tileCount.x)));

        // Determine if bounds checking is needed; this is only the case
        // for tiles at the right or bottom fringe that might be cut off
        // by the image border. Note that later, I use if statements rather
        // than passing boundsCheck directly to convince the compiler
        // to inline everything.
        const bool boundsCheck = tileIdx.x >= tileCount.x - 1 || tileIdx.y >= tileCount.y - 1;

        if (boundsCheck)
        {
            // Compute the tile in level inputLevel + 1 that's needed to
            // compute the above 8x8 tile.
            GeneralFillIntermediateTile(inputLevel, tileIdx * 2 * int2(8, 8), true, localIdx);
            GroupMemoryBarrierWithGroupSync();

            // Compute the inputLevel + 2 tile of size 8x8, loading
            // inupts from shared memory.
            GeneralFillLastTile(inputLevel, tileIdx * int2(8, 8), true, localIdx);
        }
        else
        {
            // Same with no bounds checking.
            GeneralFillIntermediateTile(inputLevel, tileIdx * 2 * int2(8, 8), false, localIdx);
            GroupMemoryBarrierWithGroupSync();
            GeneralFillLastTile(inputLevel, tileIdx * int2(8, 8), false, localIdx);
        }
    }
}
