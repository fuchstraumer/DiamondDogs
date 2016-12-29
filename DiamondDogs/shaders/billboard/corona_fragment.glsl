#version 430

in vec3 vPos;
in vec3 vUV;

out vec4 fColor;

void main() {
    float dist = length(vPos) * 3.0f;
    float brightness = ((1.0f / dist * dist) - 0.10f) * 0.70f;
    fColor = vec4(brightness,brightness,brightness,brightness);
}