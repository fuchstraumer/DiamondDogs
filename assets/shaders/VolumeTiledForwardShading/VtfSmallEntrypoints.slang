implementing VolumeTiledForwardShading;
__include VtfTypes;

import ddCommon; // indirect arg structs
import glsl; // atomicAdd

// Small entrypoints for VTF shaders, also has the convenient side effect of making entry point naming easier: if you name an entrypoint
// the same name as the file, it will confuse the compiler if you're targeting specific entrypoints. This file collects a bunch of tiny
// things that don't need to be in their own files, and saves us from that entrypoint naming issue.

[SpecializationConstant]
internal const uint SmallEntrypointsNumThreadsPerGroup = 1024u;

[shader("compute")] [NumThreads(SmallEntrypointsNumThreadsPerGroup, 1, 1)]
void VtfFindUniqueClusters(
    uint localInvocationIndex: SV_GroupIndex,
    uniform uint8_t *clusterFlags,
    uniform uint32_t *uniqueClustersCounter,
    uniform uint32_t *uniqueClusters)
{
    if (clusterFlags[localInvocationIndex] != 0)
    {
        // This thread is responsible for a unique cluster
        uint32_t clusterIndex = atomicAdd(*uniqueClustersCounter, 1u);
        if (clusterIndex < SmallEntrypointsNumThreadsPerGroup)
        {
            uniqueClusters[clusterIndex] = localInvocationIndex;
        }
    }
}

internal struct UpdateLightsGlobalMatrices
{
    float4x4 *viewMatrix;
    float4x4 *projectionMatrix;
    float4x4 *viewProjectionMatrix;
}

[Shader("compute")] [NumThreads(SmallEntrypointsNumThreadsPerGroup, 1, 1)]
internal void VtfUpdateLights(
    uint localInvocationIndex: SV_GroupIndex,
    uniform PointLight *pointLights,
    uniform SpotLight *spotLights,
    uniform DirectionalLight *directionalLights,
    uniform LightCounts *lightCounts,
    uniform UpdateLightsGlobalMatrices *globalMatrices)
{
    // compute shader method of updating light view-space positions and directions each frame
    // becomes more efficient with larger numbers of lights
    bool3 exits = bool3(
        localInvocationIndex >= lightCounts.NumPointLights,
        localInvocationIndex >= lightCounts.NumSpotLights,
        localInvocationIndex >= lightCounts.NumDirectionalLights);

    if (all(exits))
    {
        return;
    }

    if (!exits.x)
    {
        // update point lights
        pointLights[localInvocationIndex].PositionViewSpace = mul(pointLights[localInvocationIndex].Position, *globalMatrices.viewMatrix);
    }

    if (!exits.y)
    {
        // update spot lights
        spotLights[localInvocationIndex].PositionViewSpace = mul(spotLights[localInvocationIndex].Position, *globalMatrices.viewMatrix);
        spotLights[localInvocationIndex].DirectionViewSpace = normalize(mul(spotLights[localInvocationIndex].Direction, *globalMatrices.viewMatrix));
    }

    if (!exits.z)
    {
        // update directional lights
        directionalLights[localInvocationIndex].DirectionViewSpace = normalize(mul(directionalLights[localInvocationIndex].Direction, *globalMatrices.viewMatrix));
    }
}

[shader("compute")] [NumThreads(1, 1, 1)]
public void VtfUpdateClusterIndirectArgs(
    uint localInvocationIndex: SV_GroupIndex,
    uniform uint *uniqueClustersCounter,
    uniform ComputeIndirectArgs *indirectArgs,
    uniform DrawIndirectArgs *debugClustersIndirectArgs,
    uniform bool updateUniqueClusters)
{
    indirectArgs.GroupCountX = *uniqueClustersCounter;
    indirectArgs.GroupCountY = 1;
    indirectArgs.GroupCountZ = 1;

    if (updateUniqueClusters)
    {
        // Update the indirect args for the debug draw of unique clusters
        debugClustersIndirectArgs.VertexCount = *uniqueClustersCounter;
        debugClustersIndirectArgs.InstanceCount = 1;
        debugClustersIndirectArgs.FirstVertex = 0;
        debugClustersIndirectArgs.FirstInstance = 0;
    }
}