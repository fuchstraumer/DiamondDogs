module SharingBenchmark;

// The SharingBenchmark cooks this module with Lodestone. The raster entry points draw one full-screen triangle
// into the target. The compute entry point reads every texel of the target and writes one sum per 8x8 workgroup,
// so that the reads are used.

struct RasterOutput
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

// PCG hash, from Jarzynski and Olano, "Hash Functions for GPU Rendering" (JCGT, 2020)
uint PcgHash(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Returns noise in [-0.5, 0.5) for one pixel
float PixelNoise(uint2 pixel)
{
    const uint hashed = PcgHash(pixel.x | (pixel.y << 16u));
    return float(hashed) * 2.3283064e-10 - 0.5;
}

float SmoothRamp(float2 uv)
{
    return uv.x * 0.5 + uv.y * 0.25;
}

[shader("vertex")]
RasterOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 uv = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    RasterOutput output;
    // the depth pass writes this plane, so that depth compression sees real geometry and not per-pixel depth
    output.Position = float4(uv * 2.0 - 1.0, SmoothRamp(uv), 1.0);
    output.Uv = uv;
    return output;
}

[shader("fragment")]
float4 ColorFS(RasterOutput input) : SV_Target
{
    const float ramp = SmoothRamp(input.Uv);
    const float noise = PixelNoise(uint2(input.Position.xy)) * (1.0 / 512.0);
    return float4(ramp + noise, ramp * 0.5 + noise, 1.0 - ramp + noise, 1.0);
}

[shader("fragment")]
void DepthFS(RasterOutput input)
{
}

Texture2D<float4> Source;
RWStructuredBuffer<float> Sums;

// a group is 16x16 threads, and each thread reads 2x2 texels, so a group covers 32x32 texels. large groups and a wave
// reduction keep the pass limited by texture reads, not by group launches or a serial sum
static const uint k_GroupThreads = 256u;
static const uint k_TileSize = 32u;

// one partial sum per wave
groupshared float waveSums[k_GroupThreads];

[shader("compute")]
[numthreads(16, 16, 1)]
void SumCS(uint3 threadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint localIndex : SV_GroupIndex)
{
    uint width;
    uint height;
    Source.GetDimensions(width, height);

    const uint2 base = threadId.xy * 2u;
    float texel = 0.0;

    for (uint y = 0u; y < 2u; ++y)
    {
        for (uint x = 0u; x < 2u; ++x)
        {
            const uint2 pixel = base + uint2(x, y);

            if (pixel.x < width && pixel.y < height)
            {
                texel += dot(Source.Load(int3(int2(pixel), 0)), float4(1.0));
            }
        }
    }

    const uint laneCount = WaveGetLaneCount();
    const uint waveIndex = localIndex / laneCount;
    const float waveSum = WaveActiveSum(texel);

    if (WaveIsFirstLane())
    {
        waveSums[waveIndex] = waveSum;
    }

    GroupMemoryBarrierWithGroupSync();

    if (localIndex == 0)
    {
        const uint waveCount = (k_GroupThreads + laneCount - 1u) / laneCount;
        float tileSum = 0.0;

        for (uint i = 0; i < waveCount; ++i)
        {
            tileSum += waveSums[i];
        }

        const uint groupsPerRow = (width + k_TileSize - 1u) / k_TileSize;
        Sums[groupId.y * groupsPerRow + groupId.x] = tileSum;
    }
}
