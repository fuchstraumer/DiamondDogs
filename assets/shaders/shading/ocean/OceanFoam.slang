implementing OceanRender;

// Persistent foam: a world-space clipmap of FOAM_LEVELS square levels (array layers) of N x N texels around the
// orbit target. Each level is a toroidal window in the drift frame: texel a (absolute, in texels) holds the foam of
// drift-frame point (a + 0.5) * texelSize, stored at a mod N, and world = drift frame + View.FoamDrift.xy. The window
// scrolls with the target, and drift moves the frame with the wind, but no texel is ever resampled: a texel that just
// entered the window starts from 0.
//
// The foam integrates the same coverage the fragment stage draws (EvaluateWhitecapCoverage, unbiased), as
// df/dt = production * coverage - f / tau. The fragment stage reads it back twice: as a raise of the breaking
// threshold, so foam near a crest keeps the whitecaps' detail, and as residual foam that dissolves through a noise

public static const uint FOAM_LEVELS = 2u;

public struct OceanFoamStepParameters
{
    // the window origin of each level in absolute texels: xy level 0, zw level 1
    public int4 WindowOrigins;
    // the origins of the step before. A texel outside that window is new, and starts from 0
    public int4 PreviousOrigins;
    // exp(-dt / tau)
    public float DecayFactor;
    // production rate per second times dt: coverage 1 for 1 / rate seconds fills a texel
    public float ProductionStep;
    // 1 clears every texel this step (the replay sets it on its first step)
    public uint Reset;
    public uint Pad0;
    // xy: the drift-frame offset in world meters, the same value as View.FoamDrift.xy for this step. zw unused
    public float4 Drift;
};

internal float FoamTexelSize(uint level)
{
    return level == 0u ? View.PersistentFoamParameters.z : View.PersistentFoamParameters.w;
}

// the absolute texel in [origin, origin + N) that storage texel `stored` holds
internal int2 FoamAbsoluteTexel(int2 stored, int2 origin, int size)
{
    const int2 wrapped = ((stored - origin) % size + size) % size;
    return origin + wrapped;
}

// dispatch (N / 8, N / 8, FOAM_LEVELS), after the simulation tick and its mips
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanFoamStepCS(uniform OceanFoamStepParameters StepParams, uint3 id : SV_DispatchThreadID)
{
    uint width;
    uint height;
    uint levels;
    FoamClipmapStorage.GetDimensions(width, height, levels);
    const int size = int(width);

    const uint level = id.z;
    const int2 origin = level == 0u ? StepParams.WindowOrigins.xy : StepParams.WindowOrigins.zw;
    const int2 previousOrigin = level == 0u ? StepParams.PreviousOrigins.xy : StepParams.PreviousOrigins.zw;
    const int2 absolute = FoamAbsoluteTexel(int2(id.xy), origin, size);
    const bool entered = any(absolute < previousOrigin) || any(absolute >= previousOrigin + size);

    const float texelSize = FoamTexelSize(level);
    const float2 surfaceXZ = (float2(absolute) + 0.5f) * texelSize + StepParams.Drift.xy;
    // unbiased: biasing the injection by the foam itself would feed back and run away
    const float coverage = EvaluateWhitecapCoverage(surfaceXZ, float2(texelSize, 0.0f), float2(0.0f, texelSize), 0.0f);

    const float previous = (StepParams.Reset != 0u || entered) ? 0.0f : FoamClipmapStorage[id];
    FoamClipmapStorage[id] = saturate(previous * StepParams.DecayFactor + StepParams.ProductionStep * coverage);
}

// a cubic B-spline of one level from four bilinear taps (Sigg and Hadwiger, GPU Gems 2 ch. 20). The residual cuts
// the foam at a level, and the contours of a bilinear texture kink at every texel edge; the B-spline's are smooth
internal float SampleFoamLevelSmooth(float2 uv, uint level, float size)
{
    const float2 texel = uv * size - 0.5f;
    const float2 base = floor(texel);
    const float2 f = texel - base;
    const float2 f2 = f * f;
    const float2 f3 = f2 * f;
    const float2 w0 = (1.0f - 3.0f * f + 3.0f * f2 - f3) / 6.0f;
    const float2 w1 = (4.0f - 6.0f * f2 + 3.0f * f3) / 6.0f;
    const float2 w2 = (1.0f + 3.0f * f + 3.0f * f2 - 3.0f * f3) / 6.0f;
    const float2 w3 = f3 / 6.0f;
    const float2 g0 = w0 + w1;
    const float2 g1 = w2 + w3;
    // tap positions in texels: the bilinear weights between each pair of texels give the four B-spline weights
    const float2 h0 = (base - 0.5f + w1 / g0) / size;
    const float2 h1 = (base + 1.5f + w3 / g1) / size;
    const float layer = float(level);

    const float s00 = FoamClipmap.SampleLevel(OceanSampler, float3(h0.x, h0.y, layer), 0.0f);
    const float s10 = FoamClipmap.SampleLevel(OceanSampler, float3(h1.x, h0.y, layer), 0.0f);
    const float s01 = FoamClipmap.SampleLevel(OceanSampler, float3(h0.x, h1.y, layer), 0.0f);
    const float s11 = FoamClipmap.SampleLevel(OceanSampler, float3(h1.x, h1.y, layer), 0.0f);
    return g0.y * (g0.x * s00 + g1.x * s10) + g1.y * (g0.x * s01 + g1.x * s11);
}

// the persistent foam at a world position, [0, 1]: level 0 near the window center, level 1 beyond, 0 past level 1.
// Each level fades out over the outer part of its window, where filtering would reach across the wrap
internal float SamplePersistentFoam(float2 surface_xz)
{
    uint width;
    uint height;
    uint layers;
    FoamClipmap.GetDimensions(width, height, layers);
    const float size = float(width);

    const float2 framePosition = surface_xz - View.FoamDrift.xy;
    const float2 centerFrame = View.FoamDrift.zw - View.FoamDrift.xy;
    float foam = 0.0f;
    float remaining = 1.0f;

    [unroll]
    for (uint level = 0u; level < FOAM_LEVELS; ++level)
    {
        const float extent = FoamTexelSize(level) * size;
        const float2 offset = abs(framePosition - centerFrame) / (0.5f * extent);
        const float weight = remaining * (1.0f - smoothstep(0.7f, 0.9f, max(offset.x, offset.y)));
        foam += weight * SampleFoamLevelSmooth(framePosition / extent, level, size);
        remaining -= weight;
    }

    return foam;
}

// an integer hash of a lattice cell, [0, 1). Exact at any distance: the sin() hash loses its low bits once the
// argument is large, which showed as straight-edged shards a few hundred meters out
internal float FoamHash(int2 cell)
{
    uint2 v = uint2(cell) * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v ^= v >> 16u;
    return float(v.x) * (1.0f / 4294967296.0f);
}

// value noise, three octaves, [0, 1]. It is evaluated in the drift frame, so its pattern travels with the stored foam
// and does not tile
internal float FoamResidualNoise(float2 frame_position, float cell_size)
{
    float2 position = frame_position / cell_size;
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;

    [unroll]
    for (uint octave = 0u; octave < 3u; ++octave)
    {
        const float2 floored = floor(position);
        const int2 cell = int2(floored);
        const float2 t = position - floored;
        const float2 s = t * t * (3.0f - 2.0f * t);
        const float a = FoamHash(cell);
        const float b = FoamHash(cell + int2(1, 0));
        const float c = FoamHash(cell + int2(0, 1));
        const float d = FoamHash(cell + int2(1, 1));
        sum += amplitude * lerp(lerp(a, b, s.x), lerp(c, d, s.x), s.y);
        total += amplitude;
        amplitude *= 0.5f;
        // a rotation and a doubling between octaves keeps their lattices from lining up
        position = float2(1.6f * position.x - 1.2f * position.y, 1.2f * position.x + 1.6f * position.y);
    }

    return sum / total;
}

// the old foam on its own: as the stored foam decays it dissolves through the noise, so it breaks into patches
// that drift instead of showing as a veil. The stored foam is an averaged coverage, so the dissolve keeps it as an
// area: with uniform noise u, P(u > 1 - a) = a, and amount * stored of the surface is foam. Once a pixel covers
// several noise cells, the dissolve would alias, so it blends into its own filtered value, the area itself
internal float EvaluateResidualFoam(float2 surface_xz, float2 grad_x, float2 grad_y, float stored_foam)
{
    const float area = saturate(max(0.0f, View.FoamResidualParameters.x) * stored_foam);
    const float cellSize = max(0.05f, View.FoamResidualParameters.z);
    const float footprint = max(length(grad_x), length(grad_y));
    const float detail = 1.0f - smoothstep(0.25f, 1.0f, footprint / cellSize);

    if (detail <= 0.0f)
    {
        return area;
    }

    // the three octaves sum to a bell around 0.5 with a standard deviation of 0.14 (measured): its Gaussian CDF makes
    // it close to uniform. 5.05 = 1 / (0.14 * sqrt(2))
    const float noise = FoamResidualNoise(surface_xz - View.FoamDrift.xy, cellSize);
    const float uniformNoise = 0.5f + 0.5f * FastErf((noise - 0.5f) * 5.05f);
    const float softness = max(0.01f, View.FoamResidualParameters.y);
    const float dissolved = saturate((area - (1.0f - uniformNoise)) / softness);
    return lerp(area, dissolved, detail);
}
