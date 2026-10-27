implementing OceanFft;
__include OceanFftDims;
__include OceanFftBindings;
__include OceanFftCommon;

public struct OceanFoamSimParams
{
    float DeltaTime;
    float FoamDecayTime;
    float InjectionRate;
    uint FrameIndex; // 0 or 1 indicating which buffer to read/write for ping-ponging
    float FoamWindPower; // should be kept low to avoid excessive foam drift
};

[shader("compute")][numthreads(8, 8, 1)]
void OceanFoamSimCS(uint3 dispatchThreadID : SV_DispatchThreadID,
                    uniform OceanFoamSimParams Params)
{
    const float2 uv = (float2(dispatchThreadID.xy) + 0.50f) / float2(IFFT_SIZE, IFFT_SIZE);
    const uint cascade = dispatchThreadID.z;
    const uint pingPongIndex = Params.FrameIndex % 2;
    // wind direction is given as an angle: convert it to a 2D unit vector
    const float2 windDir = float2(cos(SpectrumParams.LocalWindDirection), sin(SpectrumParams.LocalWindDirection));
    // get advection offset in uv space
    const float2 worldDrift = windDir * (SpectrumParams.WindSpeed * Params.FoamWindPower) * Params.DeltaTime;
    const float2 advectedUv = frac(uv - (worldDrift / CascadeParams.LengthScales[cascade]));
    // read previous foam value
    const float prevFoam = 

}
