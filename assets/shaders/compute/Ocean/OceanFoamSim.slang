implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;

// Persistent foam, one layer per cascade. The foam image does not move: it is fixed in a frame that drifts with the
// wind, so the drift never resamples it (no ping-pong, no blur from repeated bilinear filtering). The host adds
// wind * drift * dt / L to each cascade's offset each frame and wraps it. This pass injects at uv + offset, and the
// render pass reads the foam at uv - offset
public struct OceanFoamSimParameters
{
    // exp(-dt / tau), from the host. 1 holds the foam (paused), 0 clears it
    float DecayFactor;
    // Jacobian below which the cascade injects foam. 1 is the rest state, below 0 the surface folds
    float InjectThreshold;
    // how fast injection goes from 0 to 1 as the Jacobian falls below the threshold, per unit of Jacobian
    float InjectSharpness;
    // the injected value, [0, 1]. 0 with DecayFactor 0 clears the foam in one dispatch
    float InjectRate;
    // xy: the drift offset of cascade 2i, zw: of cascade 2i + 1, in uv (already wrapped to [0, 1))
    float4 DriftOffsets[4];
};

internal float2 FoamDriftOffset(in uniform OceanFoamSimParameters params, uint cascade)
{
    const float4 pair = params.DriftOffsets[cascade >> 1u];
    return (cascade & 1u) == 0u ? pair.xy : pair.zw;
}

// bilinear filter of Displacement.w at a uv that wraps, from four loads: the compute side binds Displacement as a
// storage image, which has no sampler
internal float LoadJacobianBilinear(float2 uv, uint cascade)
{
    const float2 texel = uv * float(IFFT_SIZE) - 0.5f;
    const float2 base = floor(texel);
    const float2 t = texel - base;
    const uint mask = IFFT_SIZE - 1u;
    const uint2 p0 = uint2(int2(base)) & mask;
    const uint2 p1 = (p0 + 1u) & mask;

    const float j00 = Displacement[uint3(p0.x, p0.y, cascade)].w;
    const float j10 = Displacement[uint3(p1.x, p0.y, cascade)].w;
    const float j01 = Displacement[uint3(p0.x, p1.y, cascade)].w;
    const float j11 = Displacement[uint3(p1.x, p1.y, cascade)].w;
    return lerp(lerp(j00, j10, t.x), lerp(j01, j11, t.x), t.y);
}

// runs after the resolve and before the mips. dispatch (IFFT_SIZE / 8, IFFT_SIZE / 8, IFFT_NUM_WAVE_CASCADES)
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanFoamSimCS(uniform OceanFoamSimParameters FoamParams, uint3 id : SV_DispatchThreadID)
{
    const uint cascade = id.z;
    const float2 uv = (float2(id.xy) + 0.5f) / float(IFFT_SIZE);
    const float jacobian = LoadJacobianBilinear(frac(uv + FoamDriftOffset(FoamParams, cascade)), cascade);
    const float inject = saturate((FoamParams.InjectThreshold - jacobian) * FoamParams.InjectSharpness);

    // max, not a sum: it cannot run away, and the decay time stays the time a crest's foam takes to fade
    const float previous = Foam[id];
    Foam[id] = max(previous * FoamParams.DecayFactor, inject * FoamParams.InjectRate);
}
