implementing OceanFft;
__include OceanFftCommon;

namespace Radix2_f32
{
    internal groupshared float4 s_DataA[FFT_SIZE];
    internal groupshared float4 s_DataB[FFT_SIZE];
}

internal float4 DoIfftRadix2_f32(in const uint threadIdx, in const float4 input)
{
    using namespace Radix2_f32;
    const uint bitReversedIdx = reversebits(threadIdx) >> (32 - FFT_LOG2_SIZE);
    s_DataA[bitReversedIdx] = input;
    s_DataB[bitReversedIdx] = 0.0f; // zero out the other
    GroupMemoryBarrierWithGroupSync();
    uint startStage = 0;
    bool pingPong = false;

    if (FFT_USE_WAVE_OPS)
    {
        float4 threadData = s_DataA[threadIdx];

        [unroll(FFT_LOG2_WAVE_SIZE)]
        for (uint stage = 0; stage < FFT_LOG2_WAVE_SIZE; ++stage)
        {
            // one spot we still convert to half4
            threadData = IfftRadix2WaveOpsStep((half4)threadData, threadIdx, stage);
        }

        // write wave data back to the LDS
        s_DataA[threadIdx] = float4(threadData);
        GroupMemoryBarrierWithGroupSync();
        startStage = FFT_LOG2_WAVE_SIZE;
    }

    for (uint stage = startStage; stage < FFT_LOG2_SIZE; ++stage)
    {
        const uint logStep = 1u << stage;
        const uint groupSize = logStep << 1;
        const uint j = threadIdx & (logStep - 1);
        const float theta = -float(OceanFft_2PI) * float(j) / float(groupSize);
        float2 phaseRotation = ComplexExp(theta);
        phaseRotation.y *= -1.0f;

        const uint partnerIdx = threadIdx ^ logStep;
        float4 sourceData = pingPong ? s_DataB[threadIdx] : s_DataA[threadIdx];
        float4 partnerData = pingPong ? s_DataB[partnerIdx] : s_DataA[partnerIdx];
        
        const bool isLow = (threadIdx & logStep) == 0;
        float4 a = isLow ? sourceData : partnerData;
        float4 b = isLow ? partnerData : sourceData;
        float4 rotated = float4(ComplexMul(phaseRotation, b.xy),
                                ComplexMul(phaseRotation, b.zw));
        
        if (pingPong)
        {
            s_DataA[threadIdx] = isLow ? float4(a + rotated) : float4(a - rotated) ;
            s_DataA[threadIdx] *= FFT_RADIX2_SCALE_FACTOR;
        }
        else
        {
            s_DataB[threadIdx] = isLow ? float4(a + rotated) : float4(a - rotated);
            s_DataB[threadIdx] *= FFT_RADIX2_SCALE_FACTOR;
        }
        pingPong = !pingPong;

        GroupMemoryBarrierWithGroupSync();
    }

    return pingPong ? s_DataB[threadIdx] : s_DataA[threadIdx];
}

[[vk::binding(0, 0)]] RWStructuredBuffer<float4> Radix2_IFFT_Input_f32;
[[vk::binding(0, 1)]] Texture1D<float2> Radix2_IFFT_PhaseRotationLUT_f32;
[[vk::binding(0, 2)]] bool Radix2_IFFT_Transpose_f32;

[numthreads(FFT_SIZE, 1, 1)]
void IfftRadix2CS_f32(
    in uint3 dispatchThreadId : SV_DispatchThreadID,
    in uint3 groupId : SV_GroupID)
{
    uint threadId = dispatchThreadId.x;
    uint2 targetIndex;
    if (Radix2_IFFT_Transpose_f32)
    {
        targetIndex = dispatchThreadId.yx;
    }
    else
    {
        targetIndex = dispatchThreadId.xy;
    }

    uint linearizedIndex = FFT_SIZE * FFT_SIZE * groupId.z + FFT_SIZE * targetIndex.y + targetIndex.x;

    float4 inputValue = Radix2_IFFT_Input_f32[linearizedIndex];
    Radix2_IFFT_Input_f32[linearizedIndex] = DoIfftRadix2_f32(threadId, inputValue);
}

