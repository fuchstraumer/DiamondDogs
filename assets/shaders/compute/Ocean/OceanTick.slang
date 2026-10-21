implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;

// Per-frame time evolution of the spectrum (Tessendorf): h(k, t) = h0(k) e^(i w t) + conj(h0(-k)) e^(-i w t),
// then every field the renderer needs, as a spectrum for the IFFT. All of them are real in space, so two real
// fields pack into one complex value: IFFT(A + iB) = a + ib. With D = -i (k / |k|) h the horizontal (choppy)
// displacement, and a derivative along x being a multiply by i k.x:
//   slice 0: .xy = Dx + i Dy,           .zw = Dz + i dDy/dx
//   slice 1: .xy = dDy/dz + i dDx/dx,   .zw = dDz/dz + i dDx/dz
// dDy is the height, Dy = h. OceanResolveCS unpacks them in the same order.

public struct OceanTickParameters
{
    float Time;
};

// The IFFT kernels scale every stage, so one 1D pass returns (1/IFFT_SIZE) * the sum and the 2D transform returns
// (1/IFFT_SIZE^2) * the sum. Computing in half precision, that pushes small heights into denormals. This pass
// multiplies by IFFT_SIZE, and OceanResolveCS multiplies by the other IFFT_SIZE, so the values stay in range for half:
// 2 m waves are ~0.004 after the transform (normal), and the inputs stay far below the half maximum.
internal const static float OCEAN_TICK_PRESCALE = float(IFFT_SIZE);

// dispatch (IFFT_SIZE / 8, IFFT_SIZE / 8, 1)
[shader("compute")]
[numthreads(8, 8, IFFT_NUM_WAVE_CASCADES)]
void OceanTickCS(uniform OceanTickParameters TickParams, uint3 id : SV_DispatchThreadID)
{
    const uint linearIndex = LinearizeThreadIndex(id, CascadeParams.Dimensions);
    const half4 wave = WaveData[linearIndex];
    const float2 k = float2(wave.x, wave.z);
    const float omega = float(wave.w);

    // the phase stays in float: t grows without bound, and half has 11 bits of mantissa
    const float2 phase = ComplexExp(omega * TickParams.Time);
    const float2 h0 = float2(h0K[linearIndex]);
    const float2 h0Conjugate = float2(h0KConjugate[linearIndex]);
    const float2 h = ComplexAdd(ComplexMul(h0, phase), ComplexMul(h0Conjugate, ComplexConj(phase))) * OCEAN_TICK_PRESCALE;

    const float kLength = length(k);
    const float inverseKLength = kLength > 1e-6f ? 1.0f / kLength : 0.0f;
    // D = -i (k / |k|) h. Its x derivative is i k.x D = (k.x^2 / |k|) h
    const float2 dx = ComplexMulNegI(h) * (k.x * inverseKLength);
    const float2 dz = ComplexMulNegI(h) * (k.y * inverseKLength);
    const float2 dHeightDx = ComplexMulI(h) * k.x;
    const float2 dHeightDz = ComplexMulI(h) * k.y;
    const float2 dDxDx = h * (k.x * k.x * inverseKLength);
    const float2 dDzDz = h * (k.y * k.y * inverseKLength);
    const float2 dDxDz = h * (k.x * k.y * inverseKLength);

    const uint sliceStride = IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES;
    IfftInput[linearIndex] = half4(half2(dx + ComplexMulI(h)), half2(dz + ComplexMulI(dHeightDx)));
    IfftInput[linearIndex + sliceStride] = half4(half2(dHeightDz + ComplexMulI(dDxDx)), half2(dDzDz + ComplexMulI(dDxDz)));
}
