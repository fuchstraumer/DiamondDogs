implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;
__include IfftRadix4;

// Per-frame time evolution of the spectrum (Tessendorf): h(k, t) = h0(k) e^(i w t) + conj(h0(-k)) e^(-i w t),
// then every field the renderer needs, as a spectrum for the IFFT. All of them are real in space, so two real
// fields pack into one complex value: IFFT(A + iB) = a + ib. With D = -i (k / |k|) h the horizontal (choppy)
// displacement, and a derivative along x being a multiply by i k.x:
//   slice 0: .xy = Dx + i Dy,           .zw = Dz + i dDy/dx
//   slice 1: .xy = dDy/dz + i dDx/dx,   .zw = dDz/dz + i dDx/dz
// dDy is the height, Dy = h. ResolveTexel unpacks them in the same order.

// The IFFT kernels scale every stage, so one 1D pass returns (1/IFFT_SIZE) * the sum and the 2D transform returns
// (1/IFFT_SIZE^2) * the sum. Computing in half precision, that pushes small heights into denormals. This pass
// multiplies by IFFT_SIZE, and ResolveTexel multiplies by the other IFFT_SIZE, so the values stay in range for half:
// 2 m waves are ~0.004 after the transform (normal), and the inputs stay far below the half maximum.
internal const static float OCEAN_TICK_PRESCALE = float(IFFT_SIZE);

internal struct TickSlices
{
    half4 Slice0;
    half4 Slice1;
};

internal TickSlices TickSpectrumElement(uint3 id, uint linearIndex, float time)
{
    const int2 coordinates = int2(id.xy) - int(IFFT_SIZE / 2);
    const float2 k = WaveSpacing(id.z) * float2(coordinates);
    const float omega = float(WaveData[linearIndex]);

    // the phase stays in float: t grows without bound, and half has 11 bits of mantissa
    const float2 phase = ComplexExp(omega * time);
    const float2 h0 = float2(h0K[linearIndex]);
    const float2 h0Conjugate = float2(h0KConjugate[linearIndex]);
    const float2 h = ComplexAdd(ComplexMul(h0, phase), ComplexMul(h0Conjugate, ComplexConj(phase))) * OCEAN_TICK_PRESCALE;

    const float kLength = length(k);
    const float inverseKLength = kLength > 1e-6f ? 1.0f / kLength : 0.0f;
    // Horizontal displacement D pulls vertices towards crests and away from troughs:
    // D = +i (k / |k|) h. In our +i IFFT convention, its spatial derivative is i * k * D:
    // dDx/dx = (i k.x) * (i k.x / |k|) h = -(k.x^2 / |k|) h
    const float2 dx = ComplexMulI(h) * (k.x * inverseKLength);
    const float2 dz = ComplexMulI(h) * (k.y * inverseKLength);
    const float2 dHeightDx = ComplexMulI(h) * k.x;
    const float2 dHeightDz = ComplexMulI(h) * k.y;
    const float2 dDxDx = -h * (k.x * k.x * inverseKLength);
    const float2 dDzDz = -h * (k.y * k.y * inverseKLength);
    const float2 dDxDz = -h * (k.x * k.y * inverseKLength);

    TickSlices slices;
    slices.Slice0 = half4(half2(dx + ComplexMulI(h)), half2(dz + ComplexMulI(dHeightDx)));
    slices.Slice1 = half4(half2(dHeightDz + ComplexMulI(dDxDx)), half2(dDzDz + ComplexMulI(dDxDz)));

    return slices;
}

internal struct ResolvedTexel
{
    float4 Displacement;
    float4 Derivatives;
    // x: k_i^2, y: dDx/dz (the off-diagonal of the Jacobian tensor, for the crest eigenvalue)
    float2 Whitecaps;
};

// Unpacks the transformed slices of the tick into the Displacement and Derivatives textures. It also applies the
// checkerboard sign (the spectrum is centered at IFFT_SIZE / 2, which multiplies the output by (-1)^(x + y), the same
// as IfftPermuteCS), and the second IFFT_SIZE of the scale that the tick split between the two passes.
internal ResolvedTexel ResolveTexel(half4 slice0Input, half4 slice1Input, uint2 xy, float choppiness)
{
    const float sign = ((xy.x + xy.y) & 1u) == 0u ? 1.0f : -1.0f;
    const float scale = sign * float(IFFT_SIZE);
    const float4 slice0 = float4(slice0Input) * scale;
    const float4 slice1 = float4(slice1Input) * scale;

    // the packing order of TickSpectrumElement
    const float dx = slice0.x;
    const float height = slice0.y;
    const float dz = slice0.z;
    const float dHeightDx = slice0.w;
    const float dHeightDz = slice1.x;
    const float dDxDx = slice1.y * choppiness;
    const float dDzDz = slice1.z * choppiness;
    const float dDxDz = slice1.w * choppiness;

    // the Jacobian of (x + Dx, z + Dz): below 1 the surface compresses, below 0 it folds over (where foam goes)
    const float jacobian = (1.0f + dDxDx) * (1.0f + dDzDz) - dDxDz * dDxDz;

    // now handle the whitecaps data
    // Dupuy 2012 per-cascade scalar kernel k_i
    static const float invN = 1.0f / float(IFFT_NUM_WAVE_CASCADES);
    const float k_i = invN + dDxDx + dDzDz + (dDxDx * dDzDz) - (dDxDz * dDxDz);
    const float k_i_sq = k_i * k_i;

    ResolvedTexel texel;
    texel.Displacement = float4(dx * choppiness, height, dz * choppiness, jacobian);
    texel.Derivatives = float4(dHeightDx, dHeightDz, dDxDx, dDzDz);
    texel.Whitecaps = float2(k_i_sq, dDxDz);

    return texel;
}

internal void WriteResolvedTexel(uint3 id, ResolvedTexel texel)
{
    Displacement[id] = texel.Displacement;
    Derivatives[id] = texel.Derivatives;
    Whitecaps[id] = texel.Whitecaps;
}

internal struct Radix4LineThread
{
    uint slot;
    uint lineThread;
    uint lineCoord;
};

// slotCount is IFFT_UNFUSED_SLOTS (one slice per group) or IFFT_FUSED_SLOTS (both slices, slot = slice * 4 + line)
internal Radix4LineThread Radix4MapThread(bool transpose, uint slotCount, uint threadIdx, uint groupY)
{
    // rows: a line's threads are contiguous, so a wave walks along one row. columns: the slots
    // interleave, so each run of 4 lanes (one slice) reads 4 adjacent columns of one row, a full 32B sector (one
    // column per lane used 8B of each sector)
    const uint slot = transpose ? (threadIdx % slotCount) : (threadIdx / IFFT_RADIX4_THREADS);
    const uint lineThread = transpose ? (threadIdx / slotCount) : (threadIdx % IFFT_RADIX4_THREADS);
    const uint lineCoord = IFFT_LINES_PER_GROUP * groupY + (slot % IFFT_LINES_PER_GROUP);

    Radix4LineThread line;
    line.slot = slot;
    line.lineThread = lineThread;
    line.lineCoord = lineCoord;

    return line;
}

internal void Radix4LoadLine(bool transpose, uint sliceBase, Radix4LineThread line,
                             out half4 v0, out half4 v1, out half4 v2, out half4 v3)
{
    const uint firstPosition = 4u * line.lineThread;
    v0 = Radix4LoadInput(transpose, sliceBase, line.lineCoord, firstPosition);
    v1 = Radix4LoadInput(transpose, sliceBase, line.lineCoord, firstPosition + 1u);
    v2 = Radix4LoadInput(transpose, sliceBase, line.lineCoord, firstPosition + 2u);
    v3 = Radix4LoadInput(transpose, sliceBase, line.lineCoord, firstPosition + 3u);
}

// laneStride is the distance between the lanes of neighboring line threads: 1 in the row pass, and the slot count
// in the column pass. slotCount and the other arguments of a call are constants, except transpose and laneStride in
// the unfused kernel, which reads them from push constants
internal void Radix4TransformLine(bool transpose, uint slotCount, uint laneStride, uint threadIdx, Radix4LineThread line,
                                  inout half4 v0, inout half4 v1, inout half4 v2, inout half4 v3)
{
    // stage 0: DIT positions 4 * lineThread + k are one whole butterfly with every twiddle at 1, so it
    // needs no LDS
    Radix4Combine(v0, v1, v2, v3);

    uint firstLdsBitOffset = 2u;

    // the column pass spreads 4 neighbor threads over lanes laneStride apart, so stage 1 needs 4 * slotCount lanes
    // (16 unfused, 32 fused: a wave of 16 stages through LDS in the fused kernels)
    if (IFFT_USE_WAVE_OPS && IFFT_WAVE_SIZE >= 4u * slotCount)
    {
        // stage 1 groups 4 neighbor threads. thread m of a group has row m of a 4x4 tile (its v_k is group
        // element 4m + k), and butterfly m wants column m. so transpose the tile: transpose each 2x2 block
        // between threads 1 apart, then swap the off-diagonal blocks between threads 2 apart.
        // waves are runs of consecutive threads in a 1D group, so this is the lane index
        const uint lane = threadIdx & (IFFT_WAVE_SIZE - 1u);
        Radix4ExchangePairs(bool(line.lineThread & 1u), lane ^ laneStride, v0, v2, v1, v3);
        Radix4ExchangePairs(bool(line.lineThread & 2u), lane ^ (2u * laneStride), v0, v1, v2, v3);

        const uint j = line.lineThread & 3u;
        Radix4Butterfly(4u, j, v0, v1, v2, v3);

        // in the row pass this store is 4-way bank conflicted (4 contiguous lanes, then a 128B jump). the
        // column pass is clean
        Radix4StoreLds(transpose, slotCount, line.slot, ((line.lineThread >> 2u) << 4u) + j, 4u, v0, v1, v2, v3);
        firstLdsBitOffset = 4u;
    }
    else
    {
        Radix4StoreLds(transpose, slotCount, line.slot, 4u * line.lineThread, 1u, v0, v1, v2, v3);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint bitOffset = firstLdsBitOffset; bitOffset + 2u <= IFFT_LOG2_SIZE; bitOffset += 2u)
    {
        const uint stride = 1u << bitOffset;
        const uint j = line.lineThread & (stride - 1u);
        const uint firstElem = ((line.lineThread >> bitOffset) << (bitOffset + 2u)) + j;

        half4 a0;
        half4 a1;
        half4 a2;
        half4 a3;
        Radix4LoadLds(transpose, slotCount, line.slot, firstElem, stride, a0, a1, a2, a3);
        Radix4Butterfly(stride, j, a0, a1, a2, a3);
        Radix4StoreLds(transpose, slotCount, line.slot, firstElem, stride, a0, a1, a2, a3);
        GroupMemoryBarrierWithGroupSync();
    }

    // odd log2 sizes: one radix-2 stage with stride N/2. each thread does butterflies j and j + N/4, and
    // the second twiddle is the first times e^(i*pi/2) = i, so one sincos covers both
    if (IFFT_LOG4_REMAINDER != 0u)
    {
        float s;
        float c;
        sincos((OceanFft_2PI / float(IFFT_SIZE)) * float(line.lineThread), s, c);
        const half2 twiddle = half2(half(c), half(s));

        Radix4TailButterfly(transpose, slotCount, line.slot, line.lineThread, twiddle);
        Radix4TailButterfly(transpose, slotCount, line.slot, line.lineThread + IFFT_RADIX4_THREADS, ComplexMulI(twiddle));
        GroupMemoryBarrierWithGroupSync();
    }
}

internal void Radix4StoreLine(bool transpose, uint slotCount, uint sliceBase, Radix4LineThread line)
{
    // natural order out, in place: every read of these lines happened before the first barrier.
    // element k * N/4 + lineThread rather than 4 * lineThread + k, so that adjacent lanes touch adjacent
    // elements. that keeps the LDS read conflict-free and the row pass's global store coalesced
    // (4 * lineThread + k put lanes 32B apart: a 4-way bank conflict, and 8B written per 32B sector)
    [unroll]
    for (uint k = 0u; k < 4u; ++k)
    {
        const uint elemIdx = k * IFFT_RADIX4_THREADS + line.lineThread;
        const half4 result = Radix4LdsRead(slotCount, Radix4LdsIndex(transpose, slotCount, line.slot, elemIdx));
        IfftInput[Radix4GlobalIndex(transpose, sliceBase, line.lineCoord, elemIdx)] = result;
    }
}

// the tick element that Radix4LoadInput would read at `position` of a row, computed instead of read from IfftInput.
// the fused row pass runs a thread per slice and element, so the other slice's half is computed and dropped. the
// h0K, h0KConjugate and WaveData reads of the two slice threads hit the same lines, so they stay in L1
internal half4 Radix4TickInput(uint slice, uint cascade, float time, uint row, uint position)
{
    const uint3 id = uint3(InvDigitReverse4(position), row, cascade);
    const uint linearIndex = LinearizeThreadIndex(id, CascadeParams.Dimensions);
    const TickSlices slices = TickSpectrumElement(id, linearIndex, time);

    return slice == 0u ? slices.Slice0 : slices.Slice1;
}

internal void Radix4LoadTickedLine(uint slice, uint cascade, float time, Radix4LineThread line,
                                   out half4 v0, out half4 v1, out half4 v2, out half4 v3)
{
    const uint firstPosition = 4u * line.lineThread;
    v0 = Radix4TickInput(slice, cascade, time, line.lineCoord, firstPosition);
    v1 = Radix4TickInput(slice, cascade, time, line.lineCoord, firstPosition + 1u);
    v2 = Radix4TickInput(slice, cascade, time, line.lineCoord, firstPosition + 2u);
    v3 = Radix4TickInput(slice, cascade, time, line.lineCoord, firstPosition + 3u);
}

// resolves the group's 4 columns from LDS, after the last barrier of the column transform. the 2 * IFFT_SIZE
// threads take 2 pixels each, with the 4 columns of a row on neighboring lanes so that a row's texel writes make one
// 32B run. (the LDS reads are 2-way conflicted: a wave covers 8 rows, which are 64B apart)
internal void Radix4ResolveColumns(uint threadIdx, uint groupY, uint cascade, float choppiness)
{
    const uint pixelsPerThread = IFFT_LINES_PER_GROUP * IFFT_SIZE / IFFT_FUSED_THREADS;

    [unroll]
    for (uint pass = 0u; pass < pixelsPerThread; ++pass)
    {
        const uint pixel = threadIdx + pass * IFFT_FUSED_THREADS;
        const uint lineInGroup = pixel % IFFT_LINES_PER_GROUP;
        const uint row = pixel / IFFT_LINES_PER_GROUP;
        const uint column = IFFT_LINES_PER_GROUP * groupY + lineInGroup;

        const uint slice0Index = Radix4LdsIndex(true, IFFT_FUSED_SLOTS, lineInGroup, row);
        const uint slice1Index = Radix4LdsIndex(true, IFFT_FUSED_SLOTS, lineInGroup + IFFT_LINES_PER_GROUP, row);
        const half4 slice0 = Radix4LdsRead(IFFT_FUSED_SLOTS, slice0Index);
        const half4 slice1 = Radix4LdsRead(IFFT_FUSED_SLOTS, slice1Index);
        const ResolvedTexel texel = ResolveTexel(slice0, slice1, uint2(column, row), choppiness);

        WriteResolvedTexel(uint3(column, row, cascade), texel);
    }
}
