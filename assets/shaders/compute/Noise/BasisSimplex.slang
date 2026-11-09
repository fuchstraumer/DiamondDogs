// Simplex noise, after Ken Perlin ("Noise hardware", SIGGRAPH 2001 course notes) as Stefan Gustavson explains it in
// "Simplex noise demystified" (2005). The analytic gradient follows the structure of Gustavson's sdnoise (2003). The
// corner ordering keeps the branch structure of the author's CUDA_Noise simplex3d. The code is written from the math.
//
// The kernel radius is r^2 = 0.5, so that the kernel of a vertex ends inside the simplices that contain it. The old code
// used 0.6, which cut each kernel at the simplex boundary and left small value steps and larger gradient steps.
implementing NoiseBases;

// measured with r^2 = 0.5: max |n| = 0.013007 over 3 seeds with a local search
static const float k_Simplex3Scale = 76.881376;
static const float k_Simplex3RadiusSquared = 0.5;
static const float k_SkewFactor = 1.0 / 3.0;
static const float k_UnskewFactor = 1.0 / 6.0;

/// Returns floor(value / 3) for a negative value too. Slang integer division truncates toward zero.
int FloorDivideByThree(int value)
{
    if (value >= 0)
    {
        return value / 3;
    }

    return -((2 - value) / 3);
}

/// Returns the offsets of the second and third simplex corners, from the order of the components of x0.
void SimplexCornerOffsets(float3 x0, out int3 second, out int3 third)
{
    if (x0.x >= x0.y)
    {
        if (x0.y >= x0.z)
        {
            second = int3(1, 0, 0);
            third = int3(1, 1, 0);
        }
        else if (x0.x >= x0.z)
        {
            second = int3(1, 0, 0);
            third = int3(1, 0, 1);
        }
        else
        {
            second = int3(0, 0, 1);
            third = int3(1, 0, 1);
        }
    }
    else
    {
        if (x0.y < x0.z)
        {
            second = int3(0, 0, 1);
            third = int3(0, 1, 1);
        }
        else if (x0.x < x0.z)
        {
            second = int3(0, 1, 0);
            third = int3(0, 1, 1);
        }
        else
        {
            second = int3(0, 1, 0);
            third = int3(1, 1, 0);
        }
    }
}

/// Adds the value and the gradient of one corner kernel: (r^2 - |x|^2)^4 * dot(g, x).
[Differentiable]
float4 SimplexCornerContribution(float3 offset, no_diff float3 gradient)
{
    const float falloff = k_Simplex3RadiusSquared - dot(offset, offset);

    if (falloff <= 0.0)
    {
        return float4(0.0);
    }

    const float falloff2 = falloff * falloff;
    const float falloff4 = falloff2 * falloff2;
    const float ramp = dot(gradient, offset);
    const float3 cornerGradient = falloff4 * gradient - 8.0 * falloff2 * falloff * ramp * offset;
    return float4(falloff4 * ramp, cornerGradient);
}

[ls_axis_interface_impl("IBasis3")]
public struct Simplex3 : IBasis3
{
    [Differentiable, NoDiffThis]
    public float eval(LatticePoint p, uint seed)
    {
        return evalGrad(p, seed).x;
    }

    [Differentiable, NoDiffThis]
    public float4 evalGrad(LatticePoint p, uint seed)
    {
        // Skew with the integer part kept as an integer. With k = sum(cell) = 3q + r, the skewed position is
        // (cell + q) + frac + (r + sum(frac)) / 3, and q cancels again in the unskew. Every float below is small,
        // whatever the cell index.
        const int3 cell = p.cell;
        const int cellSum = cell.x + cell.y + cell.z;
        const int quotient = FloorDivideByThree(cellSum);
        const int remainder = cellSum - 3 * quotient;

        const float3 skewedFrac = p.frac + (float(remainder) + p.frac.x + p.frac.y + p.frac.z) * k_SkewFactor;
        const int3 skewStep = int3(detach(floor(skewedFrac)));
        const int3 baseCorner = cell + quotient + skewStep;

        const int stepSum = remainder + skewStep.x + skewStep.y + skewStep.z;
        const float3 x0 = p.frac - float3(skewStep) + float(stepSum) * k_UnskewFactor;

        int3 second;
        int3 third;
        SimplexCornerOffsets(detach(x0), second, third);

        const float3 x1 = x0 - float3(second) + k_UnskewFactor;
        const float3 x2 = x0 - float3(third) + 2.0 * k_UnskewFactor;
        const float3 x3 = x0 - 1.0 + 3.0 * k_UnskewFactor;

        const float3 g0 = EdgeGradient(baseCorner, seed);
        const float3 g1 = EdgeGradient(baseCorner + second, seed);
        const float3 g2 = EdgeGradient(baseCorner + third, seed);
        const float3 g3 = EdgeGradient(baseCorner + int3(1, 1, 1), seed);

        const float4 sum = SimplexCornerContribution(x0, g0) +
                           SimplexCornerContribution(x1, g1) +
                           SimplexCornerContribution(x2, g2) +
                           SimplexCornerContribution(x3, g3);

        return k_Simplex3Scale * sum;
    }
}
