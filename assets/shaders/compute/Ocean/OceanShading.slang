implementing OceanFft;
__include OceanFftCommon;
__include OceanFftBindings;
__include OceanFftDims;

struct ShadingParameters
{
    float4 WaterColor;
    float4 FoamColor;
    float Roughness;
    float Reflectance;
    float WaveHeight;
    float Fresnel;
};