implementing ddMaterials;

struct SurfaceOutput
{
    float3 Albedo;
    float Metallic;
    float3 Normal;
    float Roughness;
    float3 Emissive;
    float Occlusion;
    float Alpha;
    float3 Padding; // Padding to align to 16 bytes
}

struct LightingResult
{
    float3 Diffuse;
    float Pad0; // Padding to align to 16 bytes
    float3 Specular;
    float Pad1; // Padding to align to 16 bytes
}

struct LightingInput
{
    float3 LightColor;
    float LightRange;
    float3 LightPosition;
    float LightIntensity;

    float3 ViewDirection;
    float3 ViewPosition;
    float2 UV;
    float3 DiffuseColor;
    float3 SpecularColor;

    float Roughness;
    float Metallic;

    float3 WorldPosition;
    float3 FragmentPosition;

    float3x3 TBN;
}