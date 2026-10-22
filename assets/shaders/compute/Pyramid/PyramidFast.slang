// Copyright 2021 NVIDIA CORPORATION
// SPDX-License-Identifier: Apache-2.0
//
// Part of NvproPyramid, a Slang transcription of NVIDIA's nvpro_pyramid (see NvproPyramid.slang for the
// attribution and the list of changes). This file is the fast pipeline (NVPRO_PYRAMID_IS_FAST_PIPELINE != 0).
// The comments are the original's, apart from the notes marked "DiamondDogs".
implementing NvproPyramid;

// Efficient special case image pyramid generation kernel.  Generates
// up to 6 levels at once: each workgroup reads up to N samples (see below)
// of the input mip level and generates the resulting samples for the next
// up to 6 levels. Dispatch with y = 1, and z = the layer count (DiamondDogs).
//
// N = 1024 if the level count <= 5; 4096 otherwise.
//
// This only works when the input mip level has edges divisible by 2
// to the power of the level count, and the level count can be at most 6.
//
// Requires subgroup shuffles and a subgroup size of at least 16. DiamondDogs: the original notes that it was not
// tested with a subgroup size other than 32, so the host only picks it with full subgroups of exactly 32.

// Cache for the tile generated for level input level + N
// used only when the level count >= 4.
// N is 3 if said level count is 4 or 5, 4 if level count is 6.
// If level count is 5 or higher, the layout of the tile is:
// +---+---++---+---+
// | 0 | 1 || 4 | 5 |
// +---+---++---+---+
// | 2 | 3 || 6 | 7 |
// +===+===++===+===+
// | 8 | 9 ||12 |13 |
// +---+---++---+---+
// |10 |11 ||14 |15 |
// +---+---++---+---+
//
// If level count is 4, there are up to 4 consecutive tiles of layout:
// +---+---+
// | 0 | 1 |
// +---+---+
// | 2 | 3 |
// +---+---+
//
// These diagrams are referenced later.
groupshared float4 s_FastTile[16];
// DiamondDogs: the PYRAMID_SHARED_F16 copy of the same cache
groupshared uint2 s_FastTileHalves[16];

void FastSharedStore(uint index, float4 value)
{
    if (PYRAMID_SHARED_F16)
    {
        s_FastTileHalves[index] = PyramidPackHalves(value);
    }
    else
    {
        s_FastTile[index] = value;
    }
}

float4 FastSharedLoad(uint index)
{
    if (PYRAMID_SHARED_F16)
    {
        return PyramidUnpackHalves(s_FastTileHalves[index]);
    }

    return s_FastTile[index];
}

// Handle the tile at the given input level and offset (position
// of upper-left corner), and write out the resulting minified tiles
// in the next 1 to 4 mip levels (depending on levelCount).
// Tiles are squares with edge length 1 << levelCount
//
// Only 1 to 3 levels are supported if !sharedMemoryWrite;
// Only 3 to 4 levels are supported otherwise (micro-optimization).
//
// Must be executed by N consecutive threads with same inputs, with
// the lowest thread number being a multiple of N; N given by
//
// levelCount  1   2   3   4
// N           1   4  16  16 [NOT 64]
//
// If sharedMemoryWrite == true, then the 1x1 sample generated for
// the final output level is written to the shared tile at sharedMemoryIdx.
void FastHandleTile(int2 srcTileOffset, int inputLevel, uint levelCount, bool sharedMemoryWrite, uint sharedMemoryIdx, uint localIndex)
{
    // Discussion for levelCount == 3
    //
    // Break the input tile into 16 2x2 sub-tiles.  Each thread in the team
    // generates 1 sample (from 4 inputs), writes them to inputLevel + 1.
    // +---+---++---+---+
    // | 0 | 1 || 4 | 5 |
    // +---+---++---+---+
    // | 2 | 3 || 6 | 7 |
    // +===+===++===+===+
    // | 8 | 9 ||12 |13 |
    // +---+---++---+---+
    // |10 |11 ||14 |15 |
    // +---+---++---+---+
    //
    // For levelCount < 3, just cut out the parts of the 8x8 tile
    // that are not applicable.
    //
    // For levelCount == 4, the input 16x16 tile is broken into 4x4
    // sub-tiles in the same pattern. Each thread generates the
    // corresponding 2x2 sub-tile of the inputLevel + 1 8x8 tile.
    // The problem then reduces to the levelCount == 3 case.
    float4 sample00;
    float4 sample01;
    float4 sample10;
    float4 sample11;
    float4 result;
    int dstLevel = inputLevel + 1;
    int2 dstSubTile;

    // Calculate the index of this thread within the team.
    const uint teamMask = levelCount >= 3u ? 15u : levelCount == 2u ? 3u : 0u;
    const uint idxInTeam = localIndex & teamMask;

    // NOTE the extra sharedMemoryWrite requirement!!!
    if (sharedMemoryWrite && levelCount == 4u)
    {
        // The location of the sub-tile assigned to this thread in level inputLevel
        const uint xOffset = (idxInTeam & 1u) << 2u | (idxInTeam & 4u) << 1u;
        const uint yOffset = (idxInTeam & 2u) << 1u | (idxInTeam & 8u);
        const int2 srcSubTile = srcTileOffset + int2(int(xOffset), int(yOffset));
        dstSubTile = srcSubTile >> 1;

        // Thread calculates upper-left sample of 2x2 output sub-tile.
        sample00 = PyramidLoadReduce4(srcSubTile, inputLevel);
        PyramidStore(dstSubTile, dstLevel, sample00);

        // Thread calculates lower-left sample of 2x2 output sub-tile.
        sample01 = PyramidLoadReduce4(srcSubTile + int2(0, 2), inputLevel);
        PyramidStore(dstSubTile + int2(0, 1), dstLevel, sample01);

        // Thread calculates upper-right sample of 2x2 output sub-tile.
        sample10 = PyramidLoadReduce4(srcSubTile + int2(2, 0), inputLevel);
        PyramidStore(dstSubTile + int2(1, 0), dstLevel, sample10);

        // Thread calculates lower-right sample of 2x2 output sub-tile.
        sample11 = PyramidLoadReduce4(srcSubTile + int2(2, 2), inputLevel);
        PyramidStore(dstSubTile + int2(1, 1), dstLevel, sample11);

        // Now the full assigned 2x2 subtile has been filled, move on to
        // the 1x1 sample of the next level assigned to this thread.
        dstLevel++;
        dstSubTile >>= 1;
        result = PyramidReduce4(sample00, sample01, sample10, sample11);
        PyramidStore(dstSubTile, dstLevel, result);
    }
    else  // levelCount != 4
    {
        // The location of the sub-tile assigned to this thread in the
        // level after level inputLevel.
        const uint xOffset = (idxInTeam & 1u) << 1u | (idxInTeam & 4u);
        const uint yOffset = (idxInTeam & 2u) | (idxInTeam & 8u) >> 1u;
        const int2 srcSubTile = srcTileOffset + int2(int(xOffset), int(yOffset));
        dstSubTile = srcSubTile >> 1;

        // Thread calculates the sample in that sub-tile.
        result = PyramidLoadReduce4(srcSubTile, inputLevel);
        PyramidStore(dstSubTile, dstLevel, result);
    }

    if (!sharedMemoryWrite && levelCount == 1u)
    {
        return;
    }

    // The whole team computes the 2x2 tile in the next level; only 1
    // out of every 4 threads does this. Use shuffle to get the needed
    // data from the other three threads.
    dstLevel++;
    dstSubTile >>= 1;
    sample00 = result;
    sample01 = PyramidShuffleXor(result, 1u);
    sample10 = PyramidShuffleXor(result, 2u);
    sample11 = PyramidShuffleXor(result, 3u);

    if (0u == (WaveGetLaneIndex() & 3u))
    {
        result = PyramidReduce4(sample00, sample01, sample10, sample11);
        PyramidStore(dstSubTile, dstLevel, result);
    }

    if (!sharedMemoryWrite && levelCount == 2u)
    {
        return;
    }

    // Compute 1x1 "tile" in the last level handled by this function;
    // only 1 thread per 16 does this. Shuffle again.
    // This is also the thread that does the optional shared memory write.
    dstLevel++;
    dstSubTile >>= 1;
    sample00 = result;
    sample01 = PyramidShuffleXor(result, 4u);
    sample10 = PyramidShuffleXor(result, 8u);
    sample11 = PyramidShuffleXor(result, 12u);

    if (0u == (WaveGetLaneIndex() & 15u))
    {
        result = PyramidReduce4(sample00, sample01, sample10, sample11);
        PyramidStore(dstSubTile, dstLevel, result);

        if (sharedMemoryWrite)
        {
            FastSharedStore(sharedMemoryIdx, result);
        }
    }
}

// dispatch (ceil(input width * input height / N), 1, layers), N above
[shader("compute")]
[numthreads(256, 1, 1)]
void NvproPyramidFastCS(
    uniform NvproPyramidParameters PyramidParams,
    uint3 globalId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint localIndex : SV_GroupIndex)
{
    s_PyramidLayer = groupId.z;

    // Cut the input mip level into square tiles of edge length
    // 2 to the power of the level count.
    int levelCount = PyramidLevelCount(PyramidParams);
    int inputLevel = PyramidInputLevel(PyramidParams);
    const int2 srcImageSize = PyramidLevelSize(inputLevel);
    const uint horizontalTiles = uint(srcImageSize.x) >> uint(levelCount);
    const uint verticalTiles = uint(srcImageSize.y) >> uint(levelCount);

    // Calculate the team size from the level count.  Each thread
    // handles 4 inupt samples, except when the level count == 6, then each
    // handles 16 samples.
    const uint teamSizeLog2 = min(8u, uint(levelCount) * 2u - 2u);

    // Assign tiles to each team.
    uint tileIndex = globalId.x >> teamSizeLog2;
    uint horizontalIndex = tileIndex % horizontalTiles;
    uint verticalIndex = tileIndex / horizontalTiles;
    int2 tileOffset = int2(int(horizontalIndex), int(verticalIndex)) << levelCount;

    if (levelCount <= 3)
    {
        if (verticalIndex < verticalTiles)
        {
            // Reminder to self: can't handle 4 level case when
            // sharedMemoryWrite is false.
            FastHandleTile(tileOffset, inputLevel, uint(levelCount), false, 0u, localIndex);
        }

        return;
    }

    // For 4 or more levels, team size is too big for shuffle communication.
    // Need to split the tile into sub-tiles and teams into 16 thread sub-teams.
    // Each sub-team writes one sample to shared memory.
    // Refer to the shared tile diagram for details.
    if (verticalIndex < verticalTiles)
    {
        // Number of levels to fill for now.
        const int subLevelCount = levelCount == 6 ? 4 : 3;

        // Calculate the index of the sub-team within the team.
        const int subTeamMask = levelCount == 4 ? 3 : 15;
        const int subTeamIdx = int(globalId.x >> 4u) & subTeamMask;

        // Location of sub-tile; they are 8x8 or 16x16 depending on subLevelCount
        int2 subTeamOffset;
        subTeamOffset.x = (subTeamIdx & 1) << 3 | (subTeamIdx & 4) << 2;
        subTeamOffset.y = (subTeamIdx & 2) << 2 | (subTeamIdx & 8) << 1;

        if (subLevelCount == 4)
        {
            subTeamOffset <<= 1;
        }

        // Index in shared memory that this sub-team will write to.
        const uint sharedMemoryIndex = (globalId.x >> 4u) & 15u;

        // Handle the sub-tile and write the last level 1x1 sample to shared memory.
        FastHandleTile(tileOffset + subTeamOffset, inputLevel, uint(subLevelCount), true, sharedMemoryIndex, localIndex);

        // Problem reduces to handling 1 or 2 remaining levels.
        inputLevel += subLevelCount;
        levelCount -= subLevelCount;
    }

    // Wait for shared memory to fill.
    GroupMemoryBarrierWithGroupSync();

    // Handle the remaining 1 or 2 levels.
    // Only 4 threads have to do this per workgroup (NOT per team)
    if (localIndex < 4u)
    {
        float4 in00;
        float4 in01;
        float4 in10;
        float4 in11;
        float4 result;

        if (levelCount == 1)
        {
            // Handle up to 4 2x2 tiles in shared memory, 1 tile per thread.
            // Tile location calculated for the output level (final level).
            tileIndex = groupId.x * 4u + localIndex;
            horizontalIndex = tileIndex % horizontalTiles;
            verticalIndex = tileIndex / horizontalTiles;
            tileOffset = int2(int(horizontalIndex), int(verticalIndex));
            const uint smemOffset = localIndex * 4u;

            if (verticalIndex < verticalTiles)
            {
                in00 = FastSharedLoad(smemOffset + 0u);
                in10 = FastSharedLoad(smemOffset + 1u);
                in01 = FastSharedLoad(smemOffset + 2u);
                in11 = FastSharedLoad(smemOffset + 3u);
                result = PyramidReduce4(in00, in01, in10, in11);
                PyramidStore(tileOffset, inputLevel + 1, result);
            }
        }
        else  // levelCount == 2
        {
            // Handle the 4x4 tile in shared memory, 1 2x2 sub-tile per
            // thread.  Tile location calculated for the final output level
            // (here we first calculate an intermediate level).
            tileIndex = groupId.x;
            horizontalIndex = tileIndex % horizontalTiles;
            verticalIndex = tileIndex / horizontalTiles;
            tileOffset = int2(int(horizontalIndex), int(verticalIndex));
            const uint smemOffset = localIndex * 4u;

            if (verticalIndex < verticalTiles)
            {
                // Handle first level after inputLevel
                in00 = FastSharedLoad(smemOffset + 0u);
                in10 = FastSharedLoad(smemOffset + 1u);
                in01 = FastSharedLoad(smemOffset + 2u);
                in11 = FastSharedLoad(smemOffset + 3u);
                result = PyramidReduce4(in00, in01, in10, in11);
                const int2 threadOffset = int2(int(localIndex & 1u), int((localIndex & 2u) >> 1u));
                PyramidStore(tileOffset * 2 + threadOffset, inputLevel + 1, result);

                // Shuffle 4 samples and produce sole last level sample.
                in00 = result;
                in10 = PyramidShuffleXor(result, 1u);
                in01 = PyramidShuffleXor(result, 2u);
                in11 = PyramidShuffleXor(result, 3u);

                if (localIndex == 0u)
                {
                    result = PyramidReduce4(in00, in01, in10, in11);
                    PyramidStore(tileOffset, inputLevel + 2, result);
                }
            }
        }
    }
}
