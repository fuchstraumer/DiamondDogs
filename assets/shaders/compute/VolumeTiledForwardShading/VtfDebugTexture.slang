implementing VolumeTiledForwardShading;
__include VtfConfig;
__include VtfBindings;

import ddCommon;

// Draws one texture over the viewport, so that an intermediate target can be inspected.
//
// Both GLSL files for this pass held zero bytes, so there was nothing to port. The pass draws three
// vertices with no vertex buffer and reads the shared fullscreen triangle from ddFullscreenQuad.slang.

[shader("vertex")]
public FullscreenQuadOutput VtfDebugTextureVS(uint vertexID: SV_VertexID)
{
    return GetFullscreenQuadVertex(vertexID);
}

[shader("fragment")]
public float4 VtfDebugTextureFS(
    FullscreenQuadOutput input,
    uniform ParameterBlock<VtfDebugTextureResources> resources): SV_Target
{
    return resources.DebugTexture.Sample(resources.DebugSampler, input.UV);
}
