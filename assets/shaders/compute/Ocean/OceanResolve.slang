implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;

// Unpacks the transformed slices of OceanTickCS into the Displacement and Derivatives textures. It also applies the
// checkerboard sign (the spectrum is centered at IFFT_SIZE / 2, which multiplies the output by (-1)^(x + y), the same
// as IfftPermuteCS), and the second IFFT_SIZE of the scale that OceanTickCS split between the two passes.

public struct OceanResolveParameters
{
    // the horizontal displacement multiplier, often called lambda. 0 gives a plain height field
    float Choppiness;
};

// dispatch (IFFT_SIZE / 8, IFFT_SIZE / 8, IFFT_NUM_WAVE_CASCADES)
[shader("compute")]
[numthreads(8, 8, 1)]
void OceanResolveCS(uniform OceanResolveParameters ResolveParams, uint3 id : SV_DispatchThreadID)
{
    const uint linearIndex = LinearizeThreadIndex(id, CascadeParams.Dimensions);
    const uint sliceStride = IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES;
    const float sign = ((id.x + id.y) & 1u) == 0u ? 1.0f : -1.0f;
    const float scale = sign * float(IFFT_SIZE);
    const float4 slice0 = float4(IfftInput[linearIndex]) * scale;
    const float4 slice1 = float4(IfftInput[linearIndex + sliceStride]) * scale;

    // the packing order of OceanTickCS
    const float dx = slice0.x;
    const float height = slice0.y;
    const float dz = slice0.z;
    const float dHeightDx = slice0.w;
    const float dHeightDz = slice1.x;
    const float dDxDx = slice1.y * ResolveParams.Choppiness;
    const float dDzDz = slice1.z * ResolveParams.Choppiness;
    const float dDxDz = slice1.w * ResolveParams.Choppiness;

    // the Jacobian of (x + Dx, z + Dz): below 1 the surface compresses, below 0 it folds over (where foam goes)
    const float jacobian = (1.0f + dDxDx) * (1.0f + dDzDz) - dDxDz * dDxDz;

    Displacement[id] = float4(dx * ResolveParams.Choppiness, height, dz * ResolveParams.Choppiness, jacobian);
    Derivatives[id] = float4(dHeightDx, dHeightDz, dDxDx, dDzDz);

    // now handle the whitecaps data
    // Dupuy 2012 per-cascade scalar kernel k_i
    static const float invN = 0.25f; // 1 / n for n = 4 cascades
    const float k_i = invN + dDxDx + dDzDz + (dDxDx * dDzDz) - (dDxDz * dDxDz);
    const float k_i_sq = k_i * k_i;
    Whitecaps[id] = float2(k_i, k_i_sq);
}
