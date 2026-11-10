// The interfaces of the noise library: fields, bases, and octave rules.
module NoiseField;
import NoiseLattice;

/// The largest octave count of any fractal. The count itself is a runtime uniform, clamped to this.
public static const int k_MaxOctaves = 16;

/// A scalar field over 3D space, pulled at a position that the caller chooses.
public interface IField3
{
    [Differentiable]
    float eval(float3 p);

    /// Returns an upper bound of |eval|. The scheduler uses it to size the aprons of domain warps.
    float bound();

    /// Returns the value in x and the gradient in yzw. The default differentiates eval with forward-mode autodiff, in
    /// three passes. A field with a cheaper analytic gradient overrides it.
    float4 evalGrad(float3 p)
    {
        return ForwardGradientOf(this, p);
    }
}

/// A vector field over 3D space, such as the offset of a domain warp.
public interface IVecField3
{
    [Differentiable]
    float3 eval(float3 p);

    /// Returns an upper bound of the length of eval.
    float bound();
}

/// One octave of coherent noise. A basis has no parameters, so `B basis = {};` makes one.
public interface IBasis3
{
    /// Returns a value in about [-1, 1].
    [Differentiable]
    float eval(LatticePoint p, uint seed);

    /// Returns the value in x and the analytic gradient with respect to the position in yzw.
    [Differentiable]
    float4 evalGrad(LatticePoint p, uint seed);
}

/// The state that an octave rule carries from one octave to the next. All rules share this one struct; each rule
/// reads and writes the fields it needs.
public struct OctaveState : IDifferentiable
{
    /// The running sum.
    public float sum;
    /// The amplitude of the next octave, with any damping applied.
    public float amplitude;
    /// The amplitude of the next octave with no damping (gain^i), or the spectral weight of RidgedMulti.
    public float envelope;
    /// The sum of the largest contribution of each octave, so that finish can normalize the sum.
    public float range;
    /// The feedback weight of RidgedMulti, or the undamped amplitude of the next Jordan octave.
    public float weight;
    /// The offset added to the scaled position of the next octave, before the octave rotation.
    public float3 sampleOffset;
    /// The accumulated warp gradient (Swiss dsum, Jordan dsum_warp).
    public float3 warpSum;
    /// The accumulated damping gradient (Jordan dsum_damp).
    public float3 dampSum;
}

/// What the fractal loop tells a rule about the current octave.
public struct OctaveContext
{
    public int octave;
    public float frequency;
    public float lacunarity;
}

/// The part of a fractal that changes between FBM, Billow, RidgedMulti, Swiss and Jordan. The fractal loop owns the
/// frequency, the seed and the rotation. The rule owns the sum, the amplitude and the warp.
public interface IOctaveRule
{
    [Differentiable]
    OctaveState begin();

    /// Adds one octave. noise is the basis value and gradient (with respect to the scaled, unrotated position) at
    /// this octave.
    [Differentiable]
    void step(inout OctaveState state, no_diff OctaveContext context, float4 noise);

    /// Returns the sum, normalized to about [-1, 1].
    [Differentiable]
    float finish(OctaveState state);
}

// fwd_diff(field.eval) on a value of a generic type fails inside Slang v2026.19 (an internal assert in
// slang-ir-constexpr.cpp), so autodiff of a field goes through this free function, with the field as no_diff
[Differentiable]
public float EvalFieldAt<F : IField3>(no_diff F field, float3 p)
{
    return field.eval(p);
}

/// Returns the value and the gradient of a field from three forward-mode passes, one for each axis.
public float4 ForwardGradientOf<F : IField3>(F field, float3 p)
{
    const DifferentialPair<float> alongX = fwd_diff(EvalFieldAt<F>)(field, diffPair(p, float3(1.0, 0.0, 0.0)));
    const DifferentialPair<float> alongY = fwd_diff(EvalFieldAt<F>)(field, diffPair(p, float3(0.0, 1.0, 0.0)));
    const DifferentialPair<float> alongZ = fwd_diff(EvalFieldAt<F>)(field, diffPair(p, float3(0.0, 0.0, 1.0)));
    return float4(alongX.p, alongX.d, alongY.d, alongZ.d);
}
