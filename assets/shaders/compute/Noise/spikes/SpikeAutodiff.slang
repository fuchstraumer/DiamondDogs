// Spike S1: forward-mode autodiff call shapes on Slang v2026.19-87, and whether evalGrad can be an interface method
// with a default implementation that differentiates eval.
module SpikeAutodiff;

public interface ISpikeField
{
    [Differentiable]
    float eval(float3 p);

    // the probe: a default body in the interface, built from fwd_diff of the requirement above
    float4 evalGrad(float3 p)
    {
        return SpikeGradientOf(this, p);
    }
}

// fwd_diff(field.eval) on a value of generic type F fails inside Slang (assert slang-ir-constexpr.cpp(536),
// paramCount == callArgCount), so both modes go through this free function, with the field as no_diff
[Differentiable]
public float SpikeEvalAt<F : ISpikeField>(no_diff F field, float3 p)
{
    return field.eval(p);
}

public float4 SpikeGradientOf<F : ISpikeField>(F field, float3 p)
{
    const DifferentialPair<float> alongX = fwd_diff(SpikeEvalAt<F>)(field, diffPair(p, float3(1.0, 0.0, 0.0)));
    const DifferentialPair<float> alongY = fwd_diff(SpikeEvalAt<F>)(field, diffPair(p, float3(0.0, 1.0, 0.0)));
    const DifferentialPair<float> alongZ = fwd_diff(SpikeEvalAt<F>)(field, diffPair(p, float3(0.0, 0.0, 1.0)));
    return float4(alongX.p, alongX.d, alongY.d, alongZ.d);
}

public float3 SpikeReverseGradientOf<F : ISpikeField>(F field, float3 p)
{
    var pair = diffPair(p);
    bwd_diff(SpikeEvalAt<F>)(field, pair, 1.0);
    return pair.d;
}

// uses the default evalGrad
public struct SpikeQuadratic : ISpikeField
{
    public float scale;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return scale * dot(p, p);
    }
}

// overrides evalGrad with an analytic gradient, as a noise basis does
public struct SpikeAnalytic : ISpikeField
{
    public float scale;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return scale * dot(p, p);
    }

    override public float4 evalGrad(float3 p)
    {
        return float4(scale * dot(p, p), 2.0 * scale * p);
    }
}

// a field that composes another through a generic, as Warp does
public struct SpikeShifted<S : ISpikeField> : ISpikeField
{
    public S source;
    public float3 offset;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        return source.eval(p + offset);
    }
}

struct SpikeParameters
{
    SpikeShifted<SpikeQuadratic> composed;
    SpikeAnalytic analytic;
}

ParameterBlock<SpikeParameters> gSpike;
RWStructuredBuffer<float4> gSpikeOut;

[shader("compute")]
[numthreads(64, 1, 1)]
void SpikeAutodiffCS(uint3 id : SV_DispatchThreadID)
{
    const float3 p = float3(id) * 0.01;
    const float4 composed = gSpike.composed.evalGrad(p);
    const float4 analytic = gSpike.analytic.evalGrad(p);
    const float3 reverse = SpikeReverseGradientOf(gSpike.composed, p);
    gSpikeOut[id.x] = composed + analytic + float4(0.0, reverse);
}
