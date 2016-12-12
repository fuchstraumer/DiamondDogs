#version 430 core

uniform vec3 lightDir;
uniform float g;
uniform float g2;

in vec3 dir;
in vec4 frontColor;
in vec4 frontSecondaryColor;
out vec4 color;

void main(){
    float Cos = dot(lightDir, dir) / length(dir);
    float rayleighPhase = 1.0f + (Cos * Cos);
    float miePhase = (1.0f - g2) / (2.0f + g2) * (1.0f + Cos * Cos) / pow(1.0f + g2 - 2.0f * g * Cos, 1.50f);
    color = vec4(1.0f - exp(-1.50f * (rayleighPhase * frontColor.rgb + miePhase * frontSecondaryColor.rgb)),1.0f);
    color.a = color.b;
}