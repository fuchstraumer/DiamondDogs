module PlanetPresent;

// Fullscreen triangle presentation pass for PlanetGenLoom.
// Fits the equirectangular MapInput texture into the swapchain aspect ratio with pan/zoom
// and decodes sRGB-authored map texels to linear when presenting into an sRGB swapchain target.

struct PresentMapParams
{
    float2 ViewCenter;
    float ViewZoom;
    float MapAspect;
    float ScreenAspect;
    uint OutputIsSrgb;
    uint Pad0;
    uint Pad1;
};

ConstantBuffer<PresentMapParams> PresentConstants;
Texture2D<float4> MapInput;
SamplerState MapSampler;

struct PresentVertexOut
{
    float4 Position : SV_Position;
    float2 Uv : TEXCOORD0;
};

float3 SrgbToLinear(float3 srgb)
{
    const float3 low = srgb / 12.92f;
    const float3 high = pow((srgb + 0.055f) / 1.055f, 2.4f);
    return select(srgb <= 0.04045f, low, high);
}

[shader("vertex")]
PresentVertexOut PresentMapVS(uint vertexId : SV_VertexID)
{
    const float2 corner = float2(float((vertexId << 1u) & 2u), float(vertexId & 2u));
    PresentVertexOut result;
    result.Position = float4(corner * 2.0f - 1.0f, 0.0f, 1.0f);
    result.Uv = float2(corner.x, 1.0f - corner.y);
    return result;
}

[shader("fragment")]
float4 PresentMapPS(PresentVertexOut input) : SV_Target0
{
    float2 centered = input.Uv - 0.5f;
    const float mapAspect = max(PresentConstants.MapAspect, 0.001f);
    const float screenAspect = max(PresentConstants.ScreenAspect, 0.001f);

    if (screenAspect > mapAspect)
    {
        centered.x *= screenAspect / mapAspect;
    }
    else
    {
        centered.y *= mapAspect / screenAspect;
    }

    const float zoom = max(PresentConstants.ViewZoom, 0.25f);
    float2 mapUv = PresentConstants.ViewCenter + centered / zoom;

    float3 color = float3(0.02f, 0.03f, 0.05f);

    if (mapUv.y >= 0.0f && mapUv.y <= 1.0f)
    {
        mapUv.x = frac(mapUv.x);
        color = MapInput.Sample(MapSampler, mapUv).rgb;
    }

    if (PresentConstants.OutputIsSrgb != 0u)
    {
        color = SrgbToLinear(saturate(color));
    }

    return float4(color, 1.0f);
}
