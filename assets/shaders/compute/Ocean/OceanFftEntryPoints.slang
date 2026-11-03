implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;
__include OceanFftStages;

public struct OceanTickParameters
{
    float Time;
};

// dispatch (IFFT_SIZE / 8, IFFT_SIZE / 8, 1)
[shader("compute")]
[numthreads(8, 8, IFFT_NUM_WAVE_CASCADES)]
void OceanTickCS(uniform OceanTickParameters TickParams, uint3 id : SV_DispatchThreadID)
{
    const uint linearIndex = LinearizeThreadIndex(id, CascadeParams.Dimensions);
    const uint sliceStride = IFFT_SIZE * IFFT_SIZE * IFFT_NUM_WAVE_CASCADES;
    const TickSlices slices = TickSpectrumElement(id, linearIndex, TickParams.Time);

    IfftInput[linearIndex] = slices.Slice0;
    IfftInput[linearIndex + sliceStride] = slices.Slice1;
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
    const uint sliceBase = IFFT_SIZE * IFFT_SIZE * groupID.z;
    const uint laneStride = transpose ? IFFT_UNFUSED_SLOTS : 1u;
    const Radix4LineThread line = Radix4MapThread(transpose, IFFT_UNFUSED_SLOTS, dispatchThreadID.x, groupID.y);

    half4 v0;
    half4 v1;
    half4 v2;
    half4 v3;
    Radix4LoadLine(transpose, sliceBase, line, v0, v1, v2, v3);
    Radix4TransformLine(transpose, IFFT_UNFUSED_SLOTS, laneStride, dispatchThreadID.x, line, v0, v1, v2, v3);
    Radix4StoreLine(transpose, IFFT_UNFUSED_SLOTS, sliceBase, line);
}

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
    const ResolvedTexel texel = ResolveTexel(IfftInput[linearIndex],
                                             IfftInput[linearIndex + sliceStride],
                                             id.xy,
                                             ResolveParams.Choppiness);

    WriteResolvedTexel(id, texel);
}

// the tick fused into the row pass: one group transforms the same 4 rows of both slices of one cascade, with
// 2 * IFFT_SIZE threads, and computes its input instead of reading what OceanTickCS wrote. it still writes the
// transformed rows to IfftInput. dispatch (1, IFFT_SIZE / 4, IFFT_NUM_WAVE_CASCADES)
[shader("compute")]
[numthreads(IFFT_FUSED_THREADS, 1, 1)]
void OceanTickRowsCS(
    uniform OceanTickParameters TickParams,
    in uint3 dispatchThreadID : SV_DispatchThreadID,
    in uint3 groupID : SV_GroupID)
{
    const Radix4LineThread line = Radix4MapThread(false, IFFT_FUSED_SLOTS, dispatchThreadID.x, groupID.y);
    const uint slice = line.slot / IFFT_LINES_PER_GROUP;
    const uint sliceBase = IFFT_SIZE * IFFT_SIZE * (slice * IFFT_NUM_WAVE_CASCADES + groupID.z);

    half4 v0;
    half4 v1;
    half4 v2;
    half4 v3;
    Radix4LoadTickedLine(slice, groupID.z, TickParams.Time, line, v0, v1, v2, v3);
    Radix4TransformLine(false, IFFT_FUSED_SLOTS, 1u, dispatchThreadID.x, line, v0, v1, v2, v3);
    Radix4StoreLine(false, IFFT_FUSED_SLOTS, sliceBase, line);
}

// the column pass fused with the resolve: one group transforms the same 4 columns of both slices of one cascade
// (slot = slice * 4 + column, so each run of 4 lanes still reads one 32B sector), then resolves from LDS instead of
// writing IfftInput. dispatch (1, IFFT_SIZE / 4, IFFT_NUM_WAVE_CASCADES)
[shader("compute")]
[numthreads(IFFT_FUSED_THREADS, 1, 1)]
void OceanColumnsResolveCS(
    uniform OceanResolveParameters ResolveParams,
    in uint3 dispatchThreadID : SV_DispatchThreadID,
    in uint3 groupID : SV_GroupID)
{
    const Radix4LineThread line = Radix4MapThread(true, IFFT_FUSED_SLOTS, dispatchThreadID.x, groupID.y);
    const uint slice = line.slot / IFFT_LINES_PER_GROUP;
    const uint sliceBase = IFFT_SIZE * IFFT_SIZE * (slice * IFFT_NUM_WAVE_CASCADES + groupID.z);

    half4 v0;
    half4 v1;
    half4 v2;
    half4 v3;
    Radix4LoadLine(true, sliceBase, line, v0, v1, v2, v3);
    Radix4TransformLine(true, IFFT_FUSED_SLOTS, IFFT_FUSED_SLOTS, dispatchThreadID.x, line, v0, v1, v2, v3);
    Radix4ResolveColumns(dispatchThreadID.x, groupID.y, groupID.z, ResolveParams.Choppiness);
}
