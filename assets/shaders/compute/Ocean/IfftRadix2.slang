implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;

namespace Radix2
{
    // these declarations end up at module scope, so we need to use a namespace
    // so that each declaration is unique across each IFFT entrypoint
    internal groupshared half4 s_DataA[IFFT_SIZE];
    internal groupshared half4 s_DataB[IFFT_SIZE];
}

internal half4 DoIfftRadix2(in const uint threadIdx, in const half4 input)
{
    using namespace Radix2;
    const uint bitReversedIdx = reversebits(threadIdx) >> (32 - IFFT_LOG2_SIZE);
    s_DataA[bitReversedIdx] = input;
    s_DataB[bitReversedIdx] = 0.0h; // zero out the other
    GroupMemoryBarrierWithGroupSync();
    uint startStage = 0;
    bool pingPong = false;

    if (IFFT_USE_WAVE_OPS)
    {
        half4 threadData = s_DataA[threadIdx];

        [unroll]
        for (uint stage = 0; stage < IFFT_LOG2_WAVE_SIZE; ++stage)
        {
            threadData = IfftRadix2WaveOpsStep(threadData, threadIdx, stage);
        }

        // write wave data back to the LDS
        s_DataA[threadIdx] = half4(threadData);
        GroupMemoryBarrierWithGroupSync();
        startStage = IFFT_LOG2_WAVE_SIZE;
    }

    [unroll]
    for (uint stage = startStage; stage < IFFT_LOG2_SIZE; ++stage)
    {
        const uint logStep = 1u << stage;
        const uint groupSize = logStep << 1;
        const uint j = threadIdx & (logStep - 1);
        const half theta = -half(OceanFft_2PI) * half(j) / half(groupSize);
        half2 phaseRotation = ComplexExp(theta);
        phaseRotation.y *= -1.0h;

        const uint partnerIdx = threadIdx ^ logStep;
        half4 sourceData = pingPong ? s_DataB[threadIdx] : s_DataA[threadIdx];
        half4 partnerData = pingPong ? s_DataB[partnerIdx] : s_DataA[partnerIdx];
        
        const bool isLow = (threadIdx & logStep) == 0;
        half4 a = isLow ? sourceData : partnerData;
        half4 b = isLow ? partnerData : sourceData;
        half4 rotated = half4(ComplexMul(phaseRotation, b.xy),
                              ComplexMul(phaseRotation, b.zw));
        
        if (pingPong)
        {
            s_DataA[threadIdx] = isLow ? half4(a + rotated) : half4(a - rotated) ;
            s_DataA[threadIdx] *= IFFT_RADIX2_SCALE_FACTOR;
        }
        else
        {
            s_DataB[threadIdx] = isLow ? half4(a + rotated) : half4(a - rotated);
            s_DataB[threadIdx] *= IFFT_RADIX2_SCALE_FACTOR;
        }
        pingPong = !pingPong;

        GroupMemoryBarrierWithGroupSync();
    }

    return pingPong ? s_DataB[threadIdx] : s_DataA[threadIdx];
}

[numthreads(IFFT_SIZE, 1, 1)]
void IfftRadix2CS(
    in uint3 dispatchThreadId : SV_DispatchThreadID,
    in uint3 groupId : SV_GroupID)
{
    uint threadId = dispatchThreadId.x;
    uint2 targetIndex;
    if (IfftParams.Transpose)
    {
        targetIndex = dispatchThreadId.yx;
    }
    else
    {
        targetIndex = dispatchThreadId.xy;
    }

    uint linearizedIndex = IFFT_SIZE * IFFT_SIZE * groupId.z + IFFT_SIZE * targetIndex.y + targetIndex.x;

    half4 inputValue = IfftInput[linearizedIndex];
    IfftInput[linearizedIndex] = DoIfftRadix2(threadId, inputValue);
}
