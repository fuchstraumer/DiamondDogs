#include "Structures.glsl"
#include "Functions.glsl"

#pragma USE_RESOURCES GlobalResources
#pragma USE_RESOURCES VolumetricForward
#pragma USE_RESOURCES VolumetricForwardLights
#pragma USE_RESOURCES Material

// just a vertex shader that uses a height map
void main() {
    float offset_amt = float(texture(sampler2D(HeightMap, AnisotropicSampler), uv).r);
    vec3 offset_position = position + (normal * offset_amt);
    gl_Position = matrices.projection * matrices.view * matrices.model * vec4(offset_position, 1.0f);
    vPosition = matrices.model * vec4(offset_position, 1.0f);
    vNormal = mat3(matrices.inverseTransposeModel) * normal;
    vTangent = mat3(matrices.inverseTransposeModel) * tangent;
    vUV = uv;
}
