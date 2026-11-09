// The lattice coordinate that every basis takes, and the gradient set of the gradient bases.
//
// The 12 edge gradients are Ken Perlin's, from "Improving Noise" (SIGGRAPH 2002).
module NoiseLattice;
import NoiseHash;

/// A position split into an integer lattice cell and the fraction inside it. A basis reads only the fraction as a float,
/// so its precision does not fall as the cell index grows. Today MakeLatticePoint builds it from a float3; a per-tile
/// origin table can build it later with no change to any basis.
public struct LatticePoint : IDifferentiable
{
    public no_diff int3 cell;
    public float3 frac;
}

[Differentiable]
public LatticePoint MakeLatticePoint(float3 position)
{
    const float3 cellOrigin = floor(position);

    LatticePoint point;
    point.cell = int3(detach(cellOrigin));
    point.frac = position - cellOrigin;
    return point;
}

static const float3 k_EdgeGradients[12] =
{
    float3(1.0, 1.0, 0.0),
    float3(-1.0, 1.0, 0.0),
    float3(1.0, -1.0, 0.0),
    float3(-1.0, -1.0, 0.0),
    float3(1.0, 0.0, 1.0),
    float3(-1.0, 0.0, 1.0),
    float3(1.0, 0.0, -1.0),
    float3(-1.0, 0.0, -1.0),
    float3(0.0, 1.0, 1.0),
    float3(0.0, -1.0, 1.0),
    float3(0.0, 1.0, -1.0),
    float3(0.0, -1.0, -1.0),
};

/// Returns one of Perlin's 12 edge gradients for a lattice cell. The pick uses the high bits of the hash, which mix
/// every bit of the cell and the seed.
public float3 EdgeGradient(int3 cell, uint seed)
{
    const uint4 hashes = HashCell(cell, seed);
    return k_EdgeGradients[HashToRange(hashes.x, 12u)];
}

/// Returns the quintic fade 6t^5 - 15t^4 + 10t^3 (Perlin 2002).
[Differentiable]
public float3 QuinticFade(float3 t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/// Returns the derivative of QuinticFade: 30t^4 - 60t^3 + 30t^2.
[Differentiable]
public float3 QuinticFadeDerivative(float3 t)
{
    return 30.0 * t * t * (t * (t - 2.0) + 1.0);
}
