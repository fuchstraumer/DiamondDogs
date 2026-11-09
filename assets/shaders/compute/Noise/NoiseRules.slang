// The octave rules of Fractal<B, R>. Each rule follows its published source; the README of this folder names each
// source and lists what changed from the author's CUDA_Noise port. Every rule returns about [-1, 1] from finish.
module NoiseRules;
import LodestoneAttributes;
__exported import NoiseField;

[Differentiable]
OctaveState MakeOctaveState()
{
    OctaveState state;
    state.sum = 0.0;
    state.amplitude = 1.0;
    state.envelope = 1.0;
    state.range = 0.0;
    state.weight = 1.0;
    state.sampleOffset = float3(0.0);
    state.warpSum = float3(0.0);
    state.dampSum = float3(0.0);
    return state;
}

/// Fractional Brownian motion: sum of amplitude * n, with the amplitude scaled by gain each octave (Musgrave,
/// Texturing and Modeling, chapter "Procedural Fractal Terrains").
[ls_axis_interface_impl("IOctaveRule")]
public struct Fbm : IOctaveRule
{
    public float gain;

    [Differentiable, NoDiffThis]
    public OctaveState begin()
    {
        return MakeOctaveState();
    }

    [Differentiable, NoDiffThis]
    public void step(inout OctaveState state, no_diff OctaveContext context, float4 noise)
    {
        state.sum += state.amplitude * noise.x;
        state.range += state.amplitude;
        state.amplitude *= gain;
    }

    [Differentiable, NoDiffThis]
    public float finish(OctaveState state)
    {
        return state.sum / state.range;
    }
}

/// Billow: FBM of 2|n| - 1, the absolute-value turbulence of Perlin's "An Image Synthesizer" (SIGGRAPH 1985), moved
/// back to [-1, 1]. The old port summed |n|, which changed the range.
[ls_axis_interface_impl("IOctaveRule")]
public struct Billow : IOctaveRule
{
    public float gain;

    [Differentiable, NoDiffThis]
    public OctaveState begin()
    {
        return MakeOctaveState();
    }

    [Differentiable, NoDiffThis]
    public void step(inout OctaveState state, no_diff OctaveContext context, float4 noise)
    {
        state.sum += state.amplitude * (2.0 * abs(noise.x) - 1.0);
        state.range += state.amplitude;
        state.amplitude *= gain;
    }

    [Differentiable, NoDiffThis]
    public float finish(OctaveState state)
    {
        return state.sum / state.range;
    }
}

/// Musgrave's ridged multifractal (Texturing and Modeling, chapter "Procedural Fractal Terrains"). The signal is
/// (offset - |n|)^2. Each octave's signal is multiplied by a weight from the octave before it,
/// weight = saturate(signal * gain), so ridges stay sharp at peaks and smooth in valleys. Each octave also has the
/// spectral weight lacunarity^(-H * i). The old port summed 1 - |n| with no feedback, which is ridged turbulence.
[ls_axis_interface_impl("IOctaveRule")]
public struct RidgedMulti : IOctaveRule
{
    /// Musgrave's default is 1.0.
    public float offset;
    /// The feedback gain. Musgrave's default is 2.0.
    public float gain;
    /// The spectral exponent H. Musgrave's default is 1.0.
    public float spectralExponent;

    [Differentiable, NoDiffThis]
    public OctaveState begin()
    {
        return MakeOctaveState();
    }

    [Differentiable, NoDiffThis]
    public void step(inout OctaveState state, no_diff OctaveContext context, float4 noise)
    {
        const float ridge = offset - abs(noise.x);
        const float signal = ridge * ridge * state.weight;
        const float largestRidge = max(abs(offset), abs(offset - 1.0));

        state.sum += signal * state.envelope;
        state.range += largestRidge * largestRidge * state.envelope;
        state.weight = saturate(signal * gain);
        state.envelope *= pow(context.lacunarity, -spectralExponent);
    }

    [Differentiable, NoDiffThis]
    public float finish(OctaveState state)
    {
        return 2.0 * state.sum / state.range - 1.0;
    }
}

/// Swiss turbulence (Giliam de Carpentier, "Scape: 3. Procedural extensions", 2012). Ridged turbulence, with two
/// additions: each octave samples at a position moved along the accumulated noise gradient, weighted by -n, and the
/// amplitude is scaled by the clamped running sum, so detail stays near peaks and fades in valleys.
[ls_axis_interface_impl("IOctaveRule")]
public struct Swiss : IOctaveRule
{
    /// de Carpentier's default is 0.5.
    public float gain;
    /// de Carpentier's default is 0.15.
    public float warpStrength;
    /// 0 is the original warp: octave i samples at (p + warp * dsum) * frequency_i. 1 is the warp of the CUDA_Noise
    /// port, where the offset is scaled by lacunarity again each octave (about 0.01 to 0.02 there).
    public int compoundingWarp;

    [Differentiable, NoDiffThis]
    public OctaveState begin()
    {
        return MakeOctaveState();
    }

    [Differentiable, NoDiffThis]
    public void step(inout OctaveState state, no_diff OctaveContext context, float4 noise)
    {
        state.sum += state.amplitude * (1.0 - abs(noise.x));
        state.range += state.envelope;
        state.warpSum += state.amplitude * noise.yzw * -noise.x;

        if (compoundingWarp != 0)
        {
            state.sampleOffset = state.sampleOffset * context.lacunarity + warpStrength * state.warpSum;
        }
        else
        {
            const float nextFrequency = context.frequency * context.lacunarity;
            state.sampleOffset = warpStrength * state.warpSum * nextFrequency;
        }

        state.amplitude *= gain * saturate(state.sum);
        state.envelope *= gain;
    }

    [Differentiable, NoDiffThis]
    public float finish(OctaveState state)
    {
        return 2.0 * state.sum / state.range - 1.0;
    }
}

/// Jordan turbulence (Giliam de Carpentier, "Scape: 3. Procedural extensions", 2012). It sums n^2, warps each octave
/// by the accumulated n * gradient in sample space, and damps the amplitude by the accumulated gradient, so flat
/// areas lose detail at any height. The first octave has its own gain, warp and damping.
[ls_axis_interface_impl("IOctaveRule")]
public struct Jordan : IOctaveRule
{
    /// de Carpentier's defaults: gain1 0.8, gain 0.5, warp0 0.4, warp 0.35, damp0 1.0, damp 0.8, dampScale 1.0.
    public float gain1;
    public float gain;
    public float warp0;
    public float warp;
    public float damp0;
    public float damp;
    public float dampScale;

    [Differentiable, NoDiffThis]
    public OctaveState begin()
    {
        return MakeOctaveState();
    }

    [Differentiable, NoDiffThis]
    public void step(inout OctaveState state, no_diff OctaveContext context, float4 noise)
    {
        const float squared = noise.x * noise.x;
        const float3 squaredGradient = noise.x * noise.yzw;

        if (context.octave == 0)
        {
            state.sum = squared;
            state.range = 1.0;
            state.warpSum = warp0 * squaredGradient;
            state.dampSum = damp0 * squaredGradient;
            state.envelope = gain1;
            state.amplitude = gain1 * gain;
            state.weight = gain1 * gain;
        }
        else
        {
            state.sum += state.amplitude * squared;
            state.range += state.weight;
            state.warpSum += warp * squaredGradient;
            state.dampSum += damp * squaredGradient;
            state.envelope *= gain;
            state.amplitude = state.envelope * (1.0 - dampScale / (1.0 + dot(state.dampSum, state.dampSum)));
            state.weight = state.envelope;
        }

        state.sampleOffset = state.warpSum;
    }

    [Differentiable, NoDiffThis]
    public float finish(OctaveState state)
    {
        return 2.0 * state.sum / state.range - 1.0;
    }
}
