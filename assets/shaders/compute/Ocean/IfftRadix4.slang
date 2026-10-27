implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;



// renaming these slightly from radix-2 impl, as either the linter is paranoid and thinks we have
// ambiguous names, or its going to actually be a problem. not worth the risk so removing this
namespace Radix4
{
    // Single shared memory buffer: each thread owns an entire butterfly (4 elements),
    // eliminating the need for a ping-pong buffer pair.
    internal groupshared half4 s_Data[IFFT_SIZE];
}


// Base-4 digit-reversal of the low FFT_LOG2_SIZE bits of idx.
internal uint DigitReverse4(uint idx)
{
    if (IFFT_LOG4_REMAINDER == 0)
    {
        // Unchanged - this is correct as-is for even FFT_LOG2_SIZE.
        uint x = idx;
        x = ((x >> 2) & 0x33333333) | ((x & 0x33333333) << 2);
        x = ((x & 0x0F0F0F0F) << 4) | ((x & 0xF0F0F0F0) >> 4);
        x = ((x & 0x00FF00FF) << 8) | ((x & 0xFF00FF00) >> 8);
        x = (x << 16) | (x >> 16);
        return x >> (32u - IFFT_LOG2_SIZE);
    }
    else
    {
        // Odd FFT_LOG2_SIZE (e.g. 512): bit-position permutation pi(i), verified
        // against the actual radix-4 x4 + radix-2 stage order in DoIFFT_Radix4.
        const uint L = IFFT_LOG2_SIZE;
        uint result = 0u;
        [unroll(IFFT_LOG2_SIZE)]
        for (uint i = 0; i < L; ++i)
        {
            if (bool((idx >> i) & 1u))
            {
                uint pi;
                if (i == 0)
                {
                    pi = L - 1u;
                }
                else if ((i & 1u) == 0u)
                {
                    pi = L - i;
                }
                else
                {
                    pi = L - 2u - i;
                }
                result |= (1u << pi);
            }
        }
        return result;
    }
}

// Inverse base-4 digit-reversal: exact inverse permutation of DigitReverse4
internal uint InvDigitReverse4(uint idx)
{
    if (IFFT_LOG4_REMAINDER == 0)
    {
        // Even FFT_LOG2_SIZE is an involution: reverse is its own inverse.
        uint x = idx;
        x = ((x >> 2) & 0x33333333) | ((x & 0x33333333) << 2);
        x = ((x & 0x0F0F0F0F) << 4) | ((x & 0xF0F0F0F0) >> 4);
        x = ((x & 0x00FF00FF) << 8) | ((x & 0xFF00FF00) >> 8);
        x = (x << 16) | (x >> 16);
        return x >> (32u - IFFT_LOG2_SIZE);
    }
    else
    {
        // Odd FFT_LOG2_SIZE: exact inverse bit permutation of DigitReverse4
        const uint L = IFFT_LOG2_SIZE;
        uint result = 0u;
        [unroll(IFFT_LOG2_SIZE)]
        for (uint j = 0; j < L; ++j)
        {
            if (bool((idx >> j) & 1u))
            {
                uint inv_pi;
                if (j == L - 1u)
                {
                    inv_pi = 0u;
                }
                else if (j == 0u)
                {
                    inv_pi = L - 2u;
                }
                else if ((j & 1u) == 1u)
                {
                    inv_pi = L - j;
                }
                else
                {
                    inv_pi = L - 2u - j;
                }
                result |= (1u << inv_pi);
            }
        }
        return result;
    }
}

internal void Radix4Twiddles(uint logStep,
                    uint outputIndex,
                    out half2 twiddle1,
                    out half2 twiddle2,
                    out half2 twiddle3)
{
    const uint groupSize = logStep << 2; // 4 * logStep
    const uint j = outputIndex & (logStep - 1); // outputIndex % logStep
    if (j == 0u)
    {
        twiddle1 = half2(1.0h, 0.0h);
        twiddle2 = half2(1.0h, 0.0h);
        twiddle3 = half2(1.0h, 0.0h);
        return;
    }
    const float theta = (-float(OceanFft_2PI) / float(groupSize)) * float(j);
    float s, c;
    sincos(theta, s, c);
    twiddle1 = half2(half(c), half(-s));
    twiddle2 = half2(half(c * c - s * s), half(-2.0f * s * c));
    const float c3 = c * (c * c - 3.0f * s * s);
    const float s3 = s * (3.0f * c * c - s * s);
    twiddle3 = half2(half(c3), half(-s3));
}

internal void ButterflyValues_Radix4(uint logStep,
                            uint outputIndex,
                            out uint4 inputIndices,
                            out half2 twiddle1,
                            out half2 twiddle2,
                            out half2 twiddle3)
{
    Radix4Twiddles(logStep, outputIndex, twiddle1, twiddle2, twiddle3);
    const uint groupSize = logStep << 2;
    const uint j = outputIndex & (logStep - 1); // outputIndex % logStep
    // outputIndex - (outputIndex % groupSize)
    const uint groupBase = outputIndex & ~(groupSize - 1); 
    inputIndices = uint4(
        groupBase + j,
        groupBase + j + logStep,
        groupBase + j + 2 * logStep,
        groupBase + j + 3 * logStep);
}

internal void Radix4Combine(half4 a0,
                   half4 a1,
                   half4 a2,
                   half4 a3,
                   out half4 y0,
                   out half4 y1,
                   out half4 y2,
                   out half4 y3)
{
    const half4 s02p = a0 + a2;
    const half4 s02m = a0 - a2;
    const half4 s13p = a1 + a3;
    const half4 s13m = a1 - a3;

    y0 = s02p + s13p;
    y2 = s02p - s13p;

    const half4 rotP = half4(ComplexMulI(s13m.xy),    ComplexMulI(s13m.zw));
    const half4 rotM = half4(ComplexMulNegI(s13m.xy), ComplexMulNegI(s13m.zw));

    y1 = s02m + rotP;
    y3 = s02m + rotM;
}

// one row of one slice per workgroup: dispatch (1, IFFT_SIZE, IFFT_NUM_WAVE_CASCADES * IFFT_NUM_SLICES), once with
// Transpose false (rows) and once with it true (columns).
// Each thread processes 4 elements (one full radix-4 butterfly per thread), reducing workgroup thread count by 4x.
[shader("compute")]
[numthreads(IFFT_RADIX4_THREADS, 1, 1)]
void IfftRadix4CS(
    uniform IfftParameters IfftParams,
    in uint3 dispatchThreadID : SV_DispatchThreadID,
    in uint3 groupID : SV_GroupID)
{
    using namespace Radix4;
    const uint threadIdx = dispatchThreadID.x;
    const uint baseLinearIndex = IFFT_SIZE * IFFT_SIZE * groupID.z;

    // 1. Gather 4 inputs from global memory into thread registers
    half4 in0, in1, in2, in3;
    [unroll]
    for (uint k = 0u; k < 4u; ++k)
    {
        const uint coord = 4u * threadIdx + k;
        const uint gatherCoord = InvDigitReverse4(coord);
        uint readIndex;
        if (IfftParams.Transpose)
        {
            readIndex = baseLinearIndex + IFFT_SIZE * gatherCoord + dispatchThreadID.y;
        }
        else
        {
            readIndex = baseLinearIndex + IFFT_SIZE * dispatchThreadID.y + gatherCoord;
        }

        const half4 val = IfftInput[readIndex];
        if (k == 0u) in0 = val;
        else if (k == 1u) in1 = val;
        else if (k == 2u) in2 = val;
        else in3 = val;
    }

    // 2. Stage 0: 100% in registers! Twiddles are identically (1.0h, 0.0h)
    half4 v0, v1, v2, v3;
    Radix4Combine(in0, in1, in2, in3, v0, v1, v2, v3);
    v0 *= IFFT_RADIX4_SCALE_FACTOR;
    v1 *= IFFT_RADIX4_SCALE_FACTOR;
    v2 *= IFFT_RADIX4_SCALE_FACTOR;
    v3 *= IFFT_RADIX4_SCALE_FACTOR;

    // 3. Store Stage 0 outputs to single LDS buffer
    s_Data[4u * threadIdx + 0u] = v0;
    s_Data[4u * threadIdx + 1u] = v1;
    s_Data[4u * threadIdx + 2u] = v2;
    s_Data[4u * threadIdx + 3u] = v3;
    GroupMemoryBarrierWithGroupSync();

    // 4. Remaining radix-4 stages via LDS (stages 1 to IFFT_LOG4_SIZE - 1)
    for (uint bitOffset = 2u; bitOffset + 2u <= IFFT_LOG2_SIZE; bitOffset += 2u)
    {
        const uint logStep = 1u << bitOffset;
        const uint groupSize = logStep << 2;
        const uint p = threadIdx / logStep;
        const uint j = threadIdx % logStep;
        const uint groupBase = p * groupSize;

        const uint idx0 = groupBase + j;
        const uint idx1 = idx0 + logStep;
        const uint idx2 = idx0 + 2u * logStep;
        const uint idx3 = idx0 + 3u * logStep;

        half4 a0 = s_Data[idx0];
        half4 a1 = s_Data[idx1];
        half4 a2 = s_Data[idx2];
        half4 a3 = s_Data[idx3];

        half2 tw1, tw2, tw3;
        Radix4Twiddles(logStep, j, tw1, tw2, tw3);

        a1 = half4(ComplexMul(tw1, a1.xy), ComplexMul(tw1, a1.zw));
        a2 = half4(ComplexMul(tw2, a2.xy), ComplexMul(tw2, a2.zw));
        a3 = half4(ComplexMul(tw3, a3.xy), ComplexMul(tw3, a3.zw));

        half4 y0, y1, y2, y3;
        Radix4Combine(a0, a1, a2, a3, y0, y1, y2, y3);

        s_Data[idx0] = y0 * IFFT_RADIX4_SCALE_FACTOR;
        s_Data[idx1] = y1 * IFFT_RADIX4_SCALE_FACTOR;
        s_Data[idx2] = y2 * IFFT_RADIX4_SCALE_FACTOR;
        s_Data[idx3] = y3 * IFFT_RADIX4_SCALE_FACTOR;

        GroupMemoryBarrierWithGroupSync();
    }

    // 5. Trailing radix-2 stage for odd IFFT_LOG2_SIZE (e.g. 512, 128)
    if (IFFT_LOG4_REMAINDER != 0u)
    {
        const uint halfSize = IFFT_SIZE >> 1u;
        const uint quarterSize = IFFT_SIZE >> 2u;

        const uint j0 = threadIdx;
        const uint j1 = threadIdx + quarterSize;

        half4 a0 = s_Data[j0];
        half4 b0 = s_Data[j0 + halfSize];
        half4 a1 = s_Data[j1];
        half4 b1 = s_Data[j1 + halfSize];

        half2 rot0;
        if (j0 == 0u)
        {
            rot0 = half2(1.0h, 0.0h);
        }
        else
        {
            const float theta0 = (-float(OceanFft_2PI) / float(IFFT_SIZE)) * float(j0);
            float s0, c0;
            sincos(theta0, s0, c0);
            rot0 = half2(half(c0), half(-s0));
        }
        const half2 rot1 = half2(-rot0.y, rot0.x);

        half4 rot_b0 = half4(ComplexMul(rot0, b0.xy), ComplexMul(rot0, b0.zw));
        half4 rot_b1 = half4(ComplexMul(rot1, b1.xy), ComplexMul(rot1, b1.zw));

        s_Data[j0] = (a0 + rot_b0) * IFFT_RADIX2_SCALE_FACTOR;
        s_Data[j0 + halfSize] = (a0 - rot_b0) * IFFT_RADIX2_SCALE_FACTOR;
        s_Data[j1] = (a1 + rot_b1) * IFFT_RADIX2_SCALE_FACTOR;
        s_Data[j1 + halfSize] = (a1 - rot_b1) * IFFT_RADIX2_SCALE_FACTOR;

        GroupMemoryBarrierWithGroupSync();
    }

    // 6. Write 4 outputs to global memory in natural order
    [unroll]
    for (uint k = 0u; k < 4u; ++k)
    {
        const uint coord = 4u * threadIdx + k;
        uint writeIndex;
        if (IfftParams.Transpose)
        {
            writeIndex = baseLinearIndex + IFFT_SIZE * coord + dispatchThreadID.y;
        }
        else
        {
            writeIndex = baseLinearIndex + IFFT_SIZE * dispatchThreadID.y + coord;
        }

        IfftInput[writeIndex] = s_Data[coord];
    }
}
