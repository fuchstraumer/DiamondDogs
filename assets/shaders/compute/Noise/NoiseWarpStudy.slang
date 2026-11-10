// A study kernel, not part of the library: it measures how a rule's domain warp distorts the sample positions of each
// octave. The scaled sample position of octave k is s_k(p) = p * f_k + offset_k(p), where offset_k is the rule's
// sampleOffset after k octaves. Its Jacobian over the plane (two columns, d s_k / dx and d s_k / dy, each in 3D)
// comes from forward-mode autodiff through the earlier octaves. Divided by f_k, an unwarped octave gives the
// identity. A singular value near 0 means the octave is stretched without limit there; a 2D determinant <= 0 means
// the warp folds the plane over.
module NoiseWarpStudy;
import LodestoneAttributes;
import NoiseBases;
import NoiseFractal;
import NoiseRules;
import NoiseGrids;

[ls_axis_interface]
public extern struct BASIS : IBasis3 = Simplex3;

[ls_axis_interface]
public extern struct RULE : IOctaveRule = Fbm;

struct FractalNodeParameters
{
    PlaneGrid grid;
    Fractal<BASIS, RULE> fractal;
}

ParameterBlock<FractalNodeParameters> gFractalNode;
RWStructuredBuffer<float4> gNodeOutput;

[Differentiable]
float3 SampleOffsetAt(no_diff Fractal<BASIS, RULE> fractal, float3 p, no_diff int stopOctave)
{
    return fractal.runOctaves(p, stopOctave).sampleOffset;
}

/// Writes, over octaves 1 to octaveCount - 1: x, the smallest singular value of the Jacobian divided by f_k; y, the
/// smallest 2D determinant (the xy part) divided by f_k^2; z, the largest anisotropy (largest over smallest singular
/// value); w, the octave of the smallest singular value.
[shader("compute")]
[numthreads(8, 8, 1)]
void WarpJacobianCS(uint3 id : SV_DispatchThreadID)
{
    const int2 element = int2(id.xy);

    if (!gFractalNode.grid.contains(element))
    {
        return;
    }

    const Fractal<BASIS, RULE> fractal = gFractalNode.fractal;
    const float3 position = gFractalNode.grid.position(element);
    const int octaveCount = clamp(fractal.octaveCount, 1, k_MaxOctaves);

    float smallestSingular = 1.0e30;
    float smallestDeterminant = 1.0e30;
    float largestAnisotropy = 1.0;
    float smallestOctave = 0.0;
    float frequency = fractal.frequency;

    for (int octave = 1; octave < octaveCount; ++octave)
    {
        frequency *= fractal.lacunarity;

        // octave k samples after k octaves have updated the offset
        const DifferentialPair<float3> alongX = fwd_diff(SampleOffsetAt)(fractal, diffPair(position, float3(1.0, 0.0, 0.0)), octave);
        const DifferentialPair<float3> alongY = fwd_diff(SampleOffsetAt)(fractal, diffPair(position, float3(0.0, 1.0, 0.0)), octave);
        const float3 columnX = float3(1.0, 0.0, 0.0) + alongX.d / frequency;
        const float3 columnY = float3(0.0, 1.0, 0.0) + alongY.d / frequency;

        // the singular values are the square roots of the eigenvalues of the 2x2 Gram matrix
        const float gramXX = dot(columnX, columnX);
        const float gramYY = dot(columnY, columnY);
        const float gramXY = dot(columnX, columnY);
        const float halfTrace = 0.5 * (gramXX + gramYY);
        const float spread = sqrt(max(halfTrace * halfTrace - (gramXX * gramYY - gramXY * gramXY), 0.0));
        const float singularSmall = sqrt(max(halfTrace - spread, 0.0));
        const float singularLarge = sqrt(halfTrace + spread);
        const float determinant = columnX.x * columnY.y - columnX.y * columnY.x;

        if (singularSmall < smallestSingular)
        {
            smallestSingular = singularSmall;
            smallestOctave = float(octave);
        }

        smallestDeterminant = min(smallestDeterminant, determinant);
        largestAnisotropy = max(largestAnisotropy, singularLarge / max(singularSmall, 1.0e-6));
    }

    gNodeOutput[gFractalNode.grid.linearIndex(element)] = float4(smallestSingular, smallestDeterminant, largestAnisotropy, smallestOctave);
}
