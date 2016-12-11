#version 440

uniform vec3 lightPos;
uniform float g;
uniform float g2;

in vec3 dir;

out vec4 color;

void main(){
    float Cos = dot(lightPos, dir) / length(dir);
    float rayleighPhase = 1.0 + (Cos * Cos);
    float miePhase = (1.0f - g2) / (2.0f + g2) * (1.0f + Cos * Cos) / pow(1.0f + g2 - 2.0f * g * Cos * 1.50f);
    color = vec4(1.0f - exp(-1.50f * (rayleighPhase * gl_Color.rgb + miePhase * gl_SecondaryColor.rgb)),1.0f);
    color.a = color.b;
}