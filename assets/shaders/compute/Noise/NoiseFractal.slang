// One fractal loop for every octave rule. The loop owns the frequency, the per-octave seed and the per-octave
// rotation; the rule (NoiseRules) owns the sum, the amplitude and the warp.
module NoiseFractal;
__exported import NoiseField;
__exported import NoiseLattice;

// A rotation by the golden angle (2.39996 rad) about the unit axis (0.36, 0.48, 0.8). The angle and the axis are
// irrational with respect to the lattice, so the lattices of successive octaves do not line up.
static const float3x3 k_OctaveRotation =
{
    float3(-0.512205871, -0.240174893, 0.824597578),
    float3(0.840609578, -0.337079089, 0.423973143),
    float3(0.176126896, 0.910326155, 0.374547204),
};

static const float3x3 k_Identity =
{
    float3(1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.0, 1.0),
};

public struct Fractal<B : IBasis3, R : IOctaveRule> : IField3
{
    public R rule;
    /// The frequency of the first octave.
    public float frequency;
    /// The frequency ratio of successive octaves.
    public float lacunarity;
    /// The octave count, clamped to [1, k_MaxOctaves].
    public int octaveCount;
    /// Octave i uses the seed seed + i.
    public uint seed;
    /// 1 rotates each octave by k_OctaveRotation relative to the one before it. 0 keeps every octave aligned, as the
    /// old code did.
    public int rotateOctaves;

    [Differentiable, NoDiffThis]
    public float eval(float3 p)
    {
        const B basis = {};
        const int clampedCount = clamp(octaveCount, 1, k_MaxOctaves);
        const float3x3 stepRotation = rotateOctaves != 0 ? k_OctaveRotation : k_Identity;

        // octave 0 is rotated too: an unrotated octave on the plane z = 0 samples a lattice plane, where Perlin's edge
        // gradients (±1, 0, ±1) have no y part. Two such corners make n exactly 0 along a whole cell edge, which ridged
        // rules turn into straight seams
        OctaveState state = rule.begin();
        float3x3 rotation = stepRotation;
        float octaveFrequency = frequency;

        [MaxIters(16)]
        for (int octave = 0; octave < clampedCount; ++octave)
        {
            const float3 scaledPosition = p * octaveFrequency + state.sampleOffset;
            const float3 samplePosition = mul(scaledPosition, rotation);
            const float4 noise = basis.evalGrad(MakeLatticePoint(samplePosition), seed + uint(octave));

            // the rules want the gradient with respect to the scaled position, before the rotation
            const float3 gradient = mul(rotation, noise.yzw);

            OctaveContext context;
            context.octave = octave;
            context.frequency = octaveFrequency;
            context.lacunarity = lacunarity;
            rule.step(state, context, float4(noise.x, gradient));

            octaveFrequency *= lacunarity;
            rotation = mul(rotation, stepRotation);
        }

        return rule.finish(state);
    }

    public float bound()
    {
        return 1.0;
    }
}
