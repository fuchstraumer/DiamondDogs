implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfTypes;
__include VtfBindings;

import ddCommon;

// Draws one proxy sphere for each light, at the range of that light. The mesh is a unit sphere, and one
// instance covers one light.

public struct VtfDebugLightsVertexInput
{
    float3 Position: POSITION;
}

// nointerpolation parameters can get wonky, crossing my fingers this stays portable and behaves as expected
public struct VtfDebugLightsVertexOutput
{
    float4 ClipPosition: SV_Position;
    float4 Color: COLOR0;
    nointerpolation uint InstanceID: TEXCOORD0;
}

[shader("vertex")]
public VtfDebugLightsVertexOutput VtfDebugLightsVS(
    VtfDebugLightsVertexInput input,
    uint instanceID: SV_InstanceID,
    uniform ParameterBlock<VtfDebugLightsResources> resources)
{
    float3 lightPosition;
    float lightRange;
    float3 lightColor;

    // VTF_DEBUG_POINT_LIGHTS used to be used as a switch, but now we make it compile time
    // and if it's false, we are debugging spot lights. the actual toggling of this debug pass
    // is left to clients, not the shader itself.
    if (VTF_DEBUG_POINT_LIGHTS)
    {
        const PointLight light = resources.PointLights[instanceID];
        lightPosition = light.Position.xyz;
        lightRange = light.Range;
        lightColor = light.Color;
    }
    else
    {
        const SpotLight light = resources.SpotLights[instanceID];
        lightPosition = light.Position.xyz;
        lightRange = light.Range;
        lightColor = light.Color;
    }

    // fix: glsl was scaling the position of the light, not the proxy size
    const float4 worldPosition = float4(lightPosition + (lightRange * input.Position), 1.0f);

    VtfDebugLightsVertexOutput output;
    output.ClipPosition = mul(resources.Matrices.viewProjectionMatrix, worldPosition);
    output.InstanceID = instanceID;
    output.Color = float4(lightColor, 1.0f);

    return output;
}

[shader("fragment")]
public float4 VtfDebugLightsFS(VtfDebugLightsVertexOutput input): SV_Target
{
    return float4(input.Color.rgb, 0.3f);
}
