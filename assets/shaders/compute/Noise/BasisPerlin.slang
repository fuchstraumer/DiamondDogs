// Gradient noise, after Ken Perlin: "An Image Synthesizer" (SIGGRAPH 1985) and "Improving Noise" (SIGGRAPH 2002),
// with the quintic fade and the 12 edge gradients of the 2002 paper. The analytic gradient expands the trilinear
// interpolation into its eight polynomial terms, in the form that Inigo Quilez presents in his article on gradient
// noise derivatives (iquilezles.org). The code is written from the math.
implementing NoiseBases;

// measured: max |n| = 1.01547 over 3 seeds with a local search
static const float k_Perlin3Scale = 0.984766;

[ls_axis_interface_impl("IBasis3")]
public struct Perlin3 : IBasis3
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
        const float3 f = p.frac;

        // corner order: a = 000, b = 100, c = 010, d = 110, e = 001, f = 101, g = 011, h = 111
        const float3 ga = EdgeGradient(c + int3(0, 0, 0), seed);
        const float3 gb = EdgeGradient(c + int3(1, 0, 0), seed);
        const float3 gc = EdgeGradient(c + int3(0, 1, 0), seed);
        const float3 gd = EdgeGradient(c + int3(1, 1, 0), seed);
        const float3 ge = EdgeGradient(c + int3(0, 0, 1), seed);
        const float3 gf = EdgeGradient(c + int3(1, 0, 1), seed);
        const float3 gg = EdgeGradient(c + int3(0, 1, 1), seed);
        const float3 gh = EdgeGradient(c + int3(1, 1, 1), seed);

        const float va = dot(ga, f - float3(0.0, 0.0, 0.0));
        const float vb = dot(gb, f - float3(1.0, 0.0, 0.0));
        const float vc = dot(gc, f - float3(0.0, 1.0, 0.0));
        const float vd = dot(gd, f - float3(1.0, 1.0, 0.0));
        const float ve = dot(ge, f - float3(0.0, 0.0, 1.0));
        const float vf = dot(gf, f - float3(1.0, 0.0, 1.0));
        const float vg = dot(gg, f - float3(0.0, 1.0, 1.0));
        const float vh = dot(gh, f - float3(1.0, 1.0, 1.0));

        const float3 u = QuinticFade(f);
        const float3 du = QuinticFadeDerivative(f);

        const float k0 = va;
        const float k1 = vb - va;
        const float k2 = vc - va;
        const float k3 = ve - va;
        const float k4 = va - vb - vc + vd;
        const float k5 = va - vc - ve + vg;
        const float k6 = va - vb - ve + vf;
        const float k7 = -va + vb + vc - vd + ve - vf - vg + vh;

        const float value = k0 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z
                            + k6 * u.z * u.x + k7 * u.x * u.y * u.z;

        // the corner gradients, interpolated with the same weights as the value
        const float3 interpolatedGradient = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga)
                                            + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg)
                                            + u.z * u.x * (ga - gb - ge + gf)
                                            + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh);

        // the change of the interpolation weights
        const float3 weightGradient = du * float3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                                                  k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                                                  k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);

        return k_Perlin3Scale * float4(value, interpolatedGradient + weightGradient);
    }
}
