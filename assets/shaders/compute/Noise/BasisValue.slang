// Value noise: a hashed value in [-1, 1] at each lattice corner, interpolated with the quintic fade of Perlin's
// "Improving Noise" (SIGGRAPH 2002). The gradient is the derivative of the interpolation weights only.
implementing NoiseBases;

[Differentiable]
float CornerValue(no_diff int3 cell, no_diff uint seed)
{
    const uint4 hashes = HashCell(cell, seed);
    return no_diff HashToSignedUnit(hashes.y);
}

[ls_axis_interface_impl("IBasis3")]
public struct Value3 : IBasis3
{
    [Differentiable, NoDiffThis]
    public float eval(LatticePoint p, uint seed)
    {
        return evalGrad(p, seed).x;
    }

    [Differentiable, NoDiffThis]
    public float4 evalGrad(LatticePoint p, uint seed)
    {
        const int3 c = p.cell;

        // corner order: a = 000, b = 100, c = 010, d = 110, e = 001, f = 101, g = 011, h = 111
        const float va = CornerValue(c + int3(0, 0, 0), seed);
        const float vb = CornerValue(c + int3(1, 0, 0), seed);
        const float vc = CornerValue(c + int3(0, 1, 0), seed);
        const float vd = CornerValue(c + int3(1, 1, 0), seed);
        const float ve = CornerValue(c + int3(0, 0, 1), seed);
        const float vf = CornerValue(c + int3(1, 0, 1), seed);
        const float vg = CornerValue(c + int3(0, 1, 1), seed);
        const float vh = CornerValue(c + int3(1, 1, 1), seed);

        const float3 u = QuinticFade(p.frac);
        const float3 du = QuinticFadeDerivative(p.frac);

        const float k1 = vb - va;
        const float k2 = vc - va;
        const float k3 = ve - va;
        const float k4 = va - vb - vc + vd;
        const float k5 = va - vc - ve + vg;
        const float k6 = va - vb - ve + vf;
        const float k7 = -va + vb + vc - vd + ve - vf - vg + vh;

        const float value = va + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z
                            + k6 * u.z * u.x + k7 * u.x * u.y * u.z;

        const float3 gradient = du * float3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                                            k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                                            k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
        return float4(value, gradient);
    }
}
