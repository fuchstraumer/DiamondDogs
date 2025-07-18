#pragma BEGIN_CUSTOM_INTERFACE
layout (location = 0) in vec4 gColor;
layout (location = 0) out vec4 backbuffer;
#pragma END_CUSTOM_INTERFACE

void main() {
    backbuffer = vec4(gColor.rgb, 0.2f);
}
