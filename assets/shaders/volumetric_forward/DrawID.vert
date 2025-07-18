// also used for clustered.vert. Simple pass-through shader, really.
#pragma USE_INTERFACE SharedVertexInputs
#pragma USE_INTERFACE SharedVertexOutputs
#pragma BEGIN_CUSTOM_INTERFACE
flat out int drawIdx;
#pragma END_CUSTOM_INTERFACE
#pragma INTERFACE_OVERRIDE
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;
layout (location = 0) out vec4 vPosition;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec3 vTangent;
layout (location = 3) out vec2 vUV;
layout (location = 4) flat out int drawIdx;
#pragma END_INTERFACE_OVERRIDE
#include "Structures.glsl"
#pragma USE_RESOURCES GlobalResources

void main() {
    gl_Position = matrices.modelViewProjection * vec4(position, 1.0f);
    vPosition = matrices.modelView * vec4(position, 1.0f);
    vNormal = mat3(inverse(transpose(matrices.model))) * normal;
    vTangent = mat3(inverse(transpose(matrices.model))) * tangent;
    vUV = uv;
    drawIdx = gl_DrawID;
}
