#include "Structures.glsl"
#pragma USE_RESOURCES GlobalResources
#pragma INTERFACE_OVERRIDE
layout (location = 0) in vec4 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vTangent;
layout (location = 3) in vec2 vUV;
#pragma END_INTERFACE_OVERRIDE

layout (push_constant) uniform push_consts {
    layout (offset = 0) bool opaque;
};

void main() {

    if (!opaque) {
        discard;
    }

}
