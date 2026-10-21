implementing OceanFft;
__include OceanFftCommon;

// apply checkerboard to the final output of the IFFT, to recenter it for rendering
[numthreads(32, 32, 1)]
void IfftPermuteCS(
    in uint3 dispatchThreadId: SV_DispatchThreadID,
    in uint3 groupId: SV_GroupID)
{
    uint linearizedIndex = IFFT_SIZE * IFFT_SIZE * dispatchThreadId.z + IFFT_SIZE * dispatchThreadId.y + dispatchThreadId.x;
    half4 value = IfftInput[linearizedIndex];
    value *= 1.0h - 2.0h * half4((dispatchThreadId.x + dispatchThreadId.y) % 2);
    IfftInput[linearizedIndex] = value;
}