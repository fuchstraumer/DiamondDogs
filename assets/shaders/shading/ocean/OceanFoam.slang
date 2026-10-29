implementing OceanRender;

// Persistent foam: a world-space clipmap of FOAM_LEVELS square levels (array layers) of N x N texels around the
// orbit target. Each level is a toroidal window in the drift frame: texel a (absolute, in texels) holds the foam of
// drift-frame point (a + 0.5) * texelSize, stored at a mod N, and world = drift frame + View.FoamDrift.xy. The window
// scrolls with the target, and drift moves the frame with the wind, but no texel is ever resampled: a texel that just
// entered the window starts from 0.
//
// The foam integrates the same coverage the fragment stage draws (EvaluateWhitecapCoverage, unbiased), as
// df/dt = production * coverage - f / tau. The fragment stage reads it back twice: as a raise of the breaking
// threshold, so foam near a crest keeps the whitecaps' detail, and as residual foam that dissolves through the foam
// material's mask

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

    float previous = 0.0f;
    if (StepParams.Reset == 0u)
    {
        if (!entered)
        {
            previous = FoamClipmapStorage[id];
        }
        else if (level == 0u)
        {
            const float texelSize1 = FoamTexelSize(1u);
            const float2 framePos = (float2(absolute) + 0.5f) * texelSize;
            const float2 texel1 = framePos / texelSize1 - 0.5f;
            const int2 baseTexel1 = int2(floor(texel1));
            const int2 prevOrigin1 = StepParams.PreviousOrigins.zw;

            if (all(baseTexel1 >= prevOrigin1) && all(baseTexel1 + 1 < prevOrigin1 + size))
            {
                const float2 f = texel1 - float2(baseTexel1);

                const int2 c00 = ((baseTexel1 % size) + size) % size;
                const int2 c10 = (((baseTexel1 + int2(1, 0)) % size) + size) % size;
                const int2 c01 = (((baseTexel1 + int2(0, 1)) % size) + size) % size;
                const int2 c11 = (((baseTexel1 + int2(1, 1)) % size) + size) % size;

                const float s00 = FoamClipmapStorage[uint3(uint2(c00), 1u)];
                const float s10 = FoamClipmapStorage[uint3(uint2(c10), 1u)];
                const float s01 = FoamClipmapStorage[uint3(uint2(c01), 1u)];
                const float s11 = FoamClipmapStorage[uint3(uint2(c11), 1u)];

                previous = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
            }
        }
    }

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
    if (View.PersistentFoamParameters.x <= 0.0f)
    {
        return 0.0f;
    }

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
        const float weight = remaining * (1.0f - smoothstep(0.55f, 0.92f, max(offset.x, offset.y)));
        if (weight > 0.0f)
        {
            const float2 uv = framePosition / extent;
            const float sampleVal = (level == 0u)
                ? SampleFoamLevelSmooth(uv, 0u, size)
                : FoamClipmap.SampleLevel(OceanSampler, float3(uv, float(level)), 0.0f);
            foam += weight * sampleVal;
            remaining -= weight;
        }
    }

    return foam;
}


internal struct FoamMaterial
{
    // the opacity mask, uniform on [0, 1]
    float Mask;
    float3 Albedo;
    float2 NormalXY;
    float Roughness;
};

// the foam material at a world position, in the drift frame so it travels with the stored foam
internal FoamMaterial SampleFoamMaterial(float2 surface_xz)
{
    const float tileSize = max(0.1f, View.FoamResidualParameters.z);
    const float2 uv = (surface_xz - View.FoamDrift.xy) / tileSize;
    const float4 surface = FoamSurface.Sample(OceanSampler, uv);

    FoamMaterial material;
    material.Mask = surface.r;
    material.Albedo = saturate(surface.a * View.FoamTextureParameters.x * View.FoamTextureTint.rgb);
    material.NormalXY = FoamNormal.Sample(OceanSampler, uv) * 2.0f - 1.0f;
    material.Roughness = clamp(surface.b * Frame.FoamRoughnessScale, 0.0f, 1.0f);
    return material;
}

// the old foam on its own: as the stored foam decays it dissolves through the material's mask, so it breaks into the
// texture's patches and drifts with them. The stored foam is an averaged coverage, and the packer equalized the mask
// to uniform, so the dissolve keeps area: P(mask > 1 - a) = a, and amount * stored of the surface is foam. Once a
// pixel covers a few of the mask's features, its mips have averaged them toward 0.5 and a cut would be wrong, so the
// dissolve blends into its filtered value, the area itself
internal float EvaluateResidualFoam(float2 surface_xz, float2 grad_x, float2 grad_y, float stored_foam, float mask)
{
    const float area = saturate(max(0.0f, View.FoamResidualParameters.x) * stored_foam);

    uint width;
    uint height;
    uint levels;
    FoamSurface.GetDimensions(0u, width, height, levels);
    const float texelSize = max(0.1f, View.FoamResidualParameters.z) / float(width);
    const float footprint = max(length(grad_x), length(grad_y));
    // Foam002's patches are tens of texels across: from 8 texels per pixel up, the mask stops resolving them
    const float lod = log2(max(footprint / texelSize, 1.0f));
    const float detail = 1.0f - smoothstep(2.5f, 5.0f, lod);

    const float softness = max(0.01f, View.FoamResidualParameters.y);
    const float dissolved = saturate((area - (1.0f - mask)) / softness);
    return lerp(area, dissolved, detail);
}
