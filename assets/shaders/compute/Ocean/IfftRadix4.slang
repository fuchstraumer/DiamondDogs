implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;

// radix-4 decimation-in-time IFFT. each workgroup transforms 4 lines (rows, or columns when Transpose is
// set) with IFFT_SIZE threads, so a line gets IFFT_RADIX4_THREADS threads, and each thread does one whole
// butterfly per stage: 4 elements in, the same 4 slots out. no two threads share a slot within a stage,
// so every stage runs in place in one LDS buffer with one barrier. (the first version gave each element
// its own thread, so all 4 threads of a butterfly computed it and kept one output each, and it needed
// ping-pong buffers)
//
// stage 0 runs in registers, stage 1 on wave shuffles, the rest through LDS. odd log2 sizes (128, 512)
// finish with one radix-2 stage
//
// a stage with stride S splits a line into groups of 4S. butterfly j of a group reads the elements at
// base + j + k * S, multiplies element k by w^k with w = e^(+2*pi*i * j / 4S), and runs a 4-point DFT on
// the result. each half4 packs two complex values (xy and zw), so this is two transforms at once

// unique name rather than a namespace: groupshared declarations land at module scope, next to the radix-2
// kernel's
internal groupshared half4 s_Radix4Data[4u * IFFT_SIZE];

// rows sit row-major. columns interleave the 4 lines, because the column pass puts the 4 lines on
// adjacent lanes, and this keeps those lanes on adjacent addresses
internal uint Radix4LdsIndex(bool transpose, uint lineIdx, uint elemIdx)
{
    if (transpose)
    {
        return (elemIdx << 2u) | lineIdx;
    }

    return lineIdx * IFFT_SIZE + elemIdx;
}

internal uint Radix4GlobalIndex(bool transpose, uint sliceBase, uint lineCoord, uint elemIdx)
{
    if (transpose)
    {
        return sliceBase + IFFT_SIZE * elemIdx + lineCoord;
    }

    return sliceBase + IFFT_SIZE * lineCoord + elemIdx;
}

// returns the input index that DIT wants at `position`. for 512 = 2 * 4^4, the position
// d4 + 4*d3 + 16*d2 + 64*d1 + 256*b reads input b + 2*d1 + 8*d2 + 32*d3 + 128*d4: the radix-4 digits
// reverse, and the radix-2 digit b (odd sizes only) moves from the bottom to the top, because the radix-2
// stage runs last
internal uint InvDigitReverse4(uint position)
{
    const uint radix4Bits = IFFT_LOG2_SIZE - IFFT_LOG4_REMAINDER;
    const uint radix4Digits = position & ((1u << radix4Bits) - 1u);

    // reversebits() reverses the digit order but also flips the two bits inside each digit, so swap each
    // bit pair back
    const uint flipped = reversebits(radix4Digits) >> (32u - radix4Bits);
    const uint reversed = ((flipped & 0x55555555u) << 1u) | ((flipped >> 1u) & 0x55555555u);

    return (reversed << IFFT_LOG4_REMAINDER) | (position >> radix4Bits);
}

// w^1, w^2 and w^3 for butterfly j of the stage with this stride. one sincos in float, then the double-
// and triple-angle identities. float matters here: a theta in half is off by up to ~0.004 rad near 2*pi
internal void Radix4Twiddles(uint stride, uint j, out half2 twiddle1, out half2 twiddle2, out half2 twiddle3)
{
    const float theta = (OceanFft_2PI / float(4u * stride)) * float(j);
    float s;
    float c;
    sincos(theta, s, c);

    const float c2 = c * c - s * s;
    const float s2 = 2.0f * s * c;
    const float c3 = c * (c * c - 3.0f * s * s);
    const float s3 = s * (3.0f * c * c - s * s);

    twiddle1 = half2(half(c), half(s));
    twiddle2 = half2(half(c2), half(s2));
    twiddle3 = half2(half(c3), half(s3));
}

internal half4 Radix4Rotate(half4 value, half2 twiddle)
{
    return half4(ComplexMul(twiddle, value.xy), ComplexMul(twiddle, value.zw));
}

// the 4-point inverse DFT y_m = sum_k a_k * i^(mk), scaled by 1/4. i^(mk) is only ever +-1 or +-i, so
// it's adds and swizzles, no multiplies
internal void Radix4Combine(inout half4 a0, inout half4 a1, inout half4 a2, inout half4 a3)
{
    const half4 sum02 = a0 + a2;
    const half4 diff02 = a0 - a2;
    const half4 sum13 = a1 + a3;
    const half4 diff13 = a1 - a3;
    const half4 diff13TimesI = half4(ComplexMulI(diff13.xy), ComplexMulI(diff13.zw));

    a0 = (sum02 + sum13) * IFFT_RADIX4_SCALE_FACTOR;
    a1 = (diff02 + diff13TimesI) * IFFT_RADIX4_SCALE_FACTOR;
    a2 = (sum02 - sum13) * IFFT_RADIX4_SCALE_FACTOR;
    a3 = (diff02 - diff13TimesI) * IFFT_RADIX4_SCALE_FACTOR;
}

internal void Radix4Butterfly(uint stride, uint j,
                              inout half4 a0, inout half4 a1, inout half4 a2, inout half4 a3)
{
    half2 twiddle1;
    half2 twiddle2;
    half2 twiddle3;
    Radix4Twiddles(stride, j, twiddle1, twiddle2, twiddle3);

    a1 = Radix4Rotate(a1, twiddle1);
    a2 = Radix4Rotate(a2, twiddle2);
    a3 = Radix4Rotate(a3, twiddle3);
    Radix4Combine(a0, a1, a2, a3);
}

// reads the input element that DIT wants at `position`. the digit reversal happens here, on the global
// read: the old LDS scatter sent all 32 lanes of a warp to one bank pair. a row is 4KB, so the scattered
// reads stay in L1
internal half4 Radix4LoadInput(bool transpose, uint sliceBase, uint lineCoord, uint position)
{
    return IfftInput[Radix4GlobalIndex(transpose, sliceBase, lineCoord, InvDigitReverse4(position))];
}

internal void Radix4LoadLds(bool transpose, uint lineIdx, uint firstElem, uint stride,
                            out half4 a0, out half4 a1, out half4 a2, out half4 a3)
{
    a0 = s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem)];
    a1 = s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem + stride)];
    a2 = s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem + 2u * stride)];
    a3 = s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem + 3u * stride)];
}

internal void Radix4StoreLds(bool transpose, uint lineIdx, uint firstElem, uint stride,
                             half4 a0, half4 a1, half4 a2, half4 a3)
{
    s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem)] = a0;
    s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem + stride)] = a1;
    s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem + 2u * stride)] = a2;
    s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, firstElem + 3u * stride)] = a3;
}

// one round of the stage 1 transpose: the lower lane of each pair swaps its (hi0, hi1) for the upper
// lane's (lo0, lo1). each lane sends only the pair that its partner wants, so 2 shuffles instead of 4.
// (the previous version shuffled all 4 values and threw half of them away)
internal void Radix4ExchangePairs(bool isUpper, uint partnerLane,
                                  inout half4 lo0, inout half4 lo1, inout half4 hi0, inout half4 hi1)
{
    const half4 received0 = WaveReadLaneAt(isUpper ? lo0 : hi0, partnerLane);
    const half4 received1 = WaveReadLaneAt(isUpper ? lo1 : hi1, partnerLane);

    lo0 = isUpper ? received0 : lo0;
    lo1 = isUpper ? received1 : lo1;
    hi0 = isUpper ? hi0 : received0;
    hi1 = isUpper ? hi1 : received1;
}

// the radix-2 butterfly on elements j and j + N/2 of a line
internal void Radix4TailButterfly(bool transpose, uint lineIdx, uint j, half2 twiddle)
{
    const uint lowIdx = Radix4LdsIndex(transpose, lineIdx, j);
    const uint highIdx = Radix4LdsIndex(transpose, lineIdx, j + IFFT_SIZE / 2u);
    const half4 low = s_Radix4Data[lowIdx];
    const half4 high = Radix4Rotate(s_Radix4Data[highIdx], twiddle);

    s_Radix4Data[lowIdx] = (low + high) * IFFT_RADIX2_SCALE_FACTOR;
    s_Radix4Data[highIdx] = (low - high) * IFFT_RADIX2_SCALE_FACTOR;
}

// dispatch (1, IFFT_SIZE / 4, IFFT_NUM_WAVE_CASCADES * IFFT_NUM_SLICES) twice: Transpose false for the
// rows, then true for the columns. the parameters are push constants on SPIR-V
[shader("compute")]
[numthreads(IFFT_SIZE, 1, 1)]
void IfftRadix4CS(
    uniform IfftParameters IfftParams,
    in uint3 dispatchThreadID : SV_DispatchThreadID,
    in uint3 groupID : SV_GroupID)
{
    const bool transpose = IfftParams.Transpose;
    const uint threadIdx = dispatchThreadID.x;
    const uint sliceBase = IFFT_SIZE * IFFT_SIZE * groupID.z;

    // rows: a line's threads are contiguous, so a wave walks along one row. columns: the 4 lines
    // interleave, so lanes 0-3 read 4 adjacent columns of one row, a full 32B sector (one column per lane
    // used 8B of each sector)
    const uint lineIdx = transpose ? (threadIdx & 3u) : (threadIdx / IFFT_RADIX4_THREADS);
    const uint lineThread = transpose ? (threadIdx >> 2u) : (threadIdx % IFFT_RADIX4_THREADS);
    const uint lineCoord = 4u * groupID.y + lineIdx;

    // stage 0: DIT positions 4 * lineThread + k are one whole butterfly with every twiddle at 1, so it
    // needs no LDS
    const uint firstPosition = 4u * lineThread;
    half4 v0 = Radix4LoadInput(transpose, sliceBase, lineCoord, firstPosition);
    half4 v1 = Radix4LoadInput(transpose, sliceBase, lineCoord, firstPosition + 1u);
    half4 v2 = Radix4LoadInput(transpose, sliceBase, lineCoord, firstPosition + 2u);
    half4 v3 = Radix4LoadInput(transpose, sliceBase, lineCoord, firstPosition + 3u);
    Radix4Combine(v0, v1, v2, v3);

    uint firstLdsBitOffset = 2u;

    // the column pass spreads 4 neighbor threads over lanes 4 apart, so stage 1 needs 16 lanes
    if (IFFT_USE_WAVE_OPS && IFFT_WAVE_SIZE >= 16u)
    {
        // stage 1 groups 4 neighbor threads. thread m of a group has row m of a 4x4 tile (its v_k is group
        // element 4m + k), and butterfly m wants column m. so transpose the tile: transpose each 2x2 block
        // between threads 1 apart, then swap the off-diagonal blocks between threads 2 apart.
        // waves are runs of consecutive threads in a 1D group, so this is the lane index
        const uint lane = threadIdx & (IFFT_WAVE_SIZE - 1u);
        const uint laneStride = transpose ? 4u : 1u;
        Radix4ExchangePairs(bool(lineThread & 1u), lane ^ laneStride, v0, v2, v1, v3);
        Radix4ExchangePairs(bool(lineThread & 2u), lane ^ (2u * laneStride), v0, v1, v2, v3);

        const uint j = lineThread & 3u;
        Radix4Butterfly(4u, j, v0, v1, v2, v3);

        // in the row pass this store is 4-way bank conflicted (4 contiguous lanes, then a 128B jump). the
        // column pass is clean
        Radix4StoreLds(transpose, lineIdx, ((lineThread >> 2u) << 4u) + j, 4u, v0, v1, v2, v3);
        firstLdsBitOffset = 4u;
    }
    else
    {
        Radix4StoreLds(transpose, lineIdx, firstPosition, 1u, v0, v1, v2, v3);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint bitOffset = firstLdsBitOffset; bitOffset + 2u <= IFFT_LOG2_SIZE; bitOffset += 2u)
    {
        const uint stride = 1u << bitOffset;
        const uint j = lineThread & (stride - 1u);
        const uint firstElem = ((lineThread >> bitOffset) << (bitOffset + 2u)) + j;

        half4 a0;
        half4 a1;
        half4 a2;
        half4 a3;
        Radix4LoadLds(transpose, lineIdx, firstElem, stride, a0, a1, a2, a3);
        Radix4Butterfly(stride, j, a0, a1, a2, a3);
        Radix4StoreLds(transpose, lineIdx, firstElem, stride, a0, a1, a2, a3);
        GroupMemoryBarrierWithGroupSync();
    }

    // odd log2 sizes: one radix-2 stage with stride N/2. each thread does butterflies j and j + N/4, and
    // the second twiddle is the first times e^(i*pi/2) = i, so one sincos covers both
    if (IFFT_LOG4_REMAINDER != 0u)
    {
        float s;
        float c;
        sincos((OceanFft_2PI / float(IFFT_SIZE)) * float(lineThread), s, c);
        const half2 twiddle = half2(half(c), half(s));

        Radix4TailButterfly(transpose, lineIdx, lineThread, twiddle);
        Radix4TailButterfly(transpose, lineIdx, lineThread + IFFT_RADIX4_THREADS, ComplexMulI(twiddle));
        GroupMemoryBarrierWithGroupSync();
    }

    // natural order out, in place: every read of these lines happened before the first barrier.
    // element k * N/4 + lineThread rather than 4 * lineThread + k, so that adjacent lanes touch adjacent
    // elements. that keeps the LDS read conflict-free and the row pass's global store coalesced
    // (4 * lineThread + k put lanes 32B apart: a 4-way bank conflict, and 8B written per 32B sector)
    [unroll]
    for (uint k = 0u; k < 4u; ++k)
    {
        const uint elemIdx = k * IFFT_RADIX4_THREADS + lineThread;
        const half4 result = s_Radix4Data[Radix4LdsIndex(transpose, lineIdx, elemIdx)];
        IfftInput[Radix4GlobalIndex(transpose, sliceBase, lineCoord, elemIdx)] = result;
    }
}
