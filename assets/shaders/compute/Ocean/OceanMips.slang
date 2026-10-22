implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;

// Fills the mip chains of Displacement and Derivatives, up to OCEAN_MIPS_MAX_LEVELS levels per dispatch. A workgroup
// reads a 32x32 tile of MipSource and writes the 16x16, 8x8, 4x4, 2x2 and 1x1 tiles of the next five levels. It
// keeps each level in groupshared memory, so the next level never goes back to the texture. Each texel is the
// average of the 2x2 block above it, the same as a linear blit of an exact halving.
//
// The host binds one image per dispatch: MipSource is one level of it, and MipTarget0 to MipTarget4 are the levels
// below that one. Two dispatches cover every size up to 1024: the first reads mip 0 and writes mips 1 to 5, the
// second reads mip 5 (at most 32x32, so one workgroup per layer) and writes the rest.

static const uint OCEAN_MIPS_MAX_LEVELS = 5u;
// the first target level of one workgroup is 16x16, one texel per thread
static const uint OCEAN_MIPS_GROUP_SIZE = 16u;

public struct OceanMipsParameters
{
    // the width and height of the level that MipSource views
    uint SourceSize;
    // the number of target levels to write, 1 to OCEAN_MIPS_MAX_LEVELS
    uint LevelCount;
};

groupshared float4 MipTile[OCEAN_MIPS_GROUP_SIZE][OCEAN_MIPS_GROUP_SIZE];

void StoreMip(uint level, uint3 texel, float4 value)
{
    switch (level)
    {
    case 0u: MipTarget0[texel] = value; break;
    case 1u: MipTarget1[texel] = value; break;
    case 2u: MipTarget2[texel] = value; break;
    case 3u: MipTarget3[texel] = value; break;
    default: MipTarget4[texel] = value; break;
    }
}

// dispatch (max(SourceSize / 32, 1), max(SourceSize / 32, 1), IFFT_NUM_WAVE_CASCADES)
[shader("compute")]
[numthreads(OCEAN_MIPS_GROUP_SIZE, OCEAN_MIPS_GROUP_SIZE, 1)]
void OceanMipsCS(uniform OceanMipsParameters MipParams, uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    const uint layer = groupId.z;
    uint levelSize = MipParams.SourceSize / 2u;
    uint tileSize = OCEAN_MIPS_GROUP_SIZE;
    float4 value = float4(0.0f);

    // a source smaller than 32x32 leaves part of the one workgroup without a texel. Those threads keep zero in
    // groupshared memory: no texel that is written reads it
    const uint2 texel = groupId.xy * tileSize + localId.xy;

    if (all(texel < levelSize))
    {
        const int2 source = int2(texel * 2u);
        value = 0.25f * (
            MipSource.Load(int4(source, layer, 0)) + MipSource.Load(int4(source + int2(1, 0), layer, 0)) +
            MipSource.Load(int4(source + int2(0, 1), layer, 0)) + MipSource.Load(int4(source + int2(1, 1), layer, 0)));
        StoreMip(0u, uint3(texel, layer), value);
    }

    MipTile[localId.y][localId.x] = value;

    // LevelCount is a push constant, so every thread takes the same trip count and reaches every barrier
    for (uint level = 1u; level < MipParams.LevelCount; ++level)
    {
        tileSize /= 2u;
        levelSize /= 2u;
        const bool active = all(localId.xy < tileSize);
        GroupMemoryBarrierWithGroupSync();

        if (active)
        {
            const uint2 above = localId.xy * 2u;
            value = 0.25f * (
                MipTile[above.y][above.x] + MipTile[above.y][above.x + 1u] +
                MipTile[above.y + 1u][above.x] + MipTile[above.y + 1u][above.x + 1u]);
        }

        // every read of the level above is done before this level overwrites the front of the tile
        GroupMemoryBarrierWithGroupSync();

        if (active)
        {
            MipTile[localId.y][localId.x] = value;
            const uint2 levelTexel = groupId.xy * tileSize + localId.xy;

            if (all(levelTexel < levelSize))
            {
                StoreMip(level, uint3(levelTexel, layer), value);
            }
        }
    }
}
