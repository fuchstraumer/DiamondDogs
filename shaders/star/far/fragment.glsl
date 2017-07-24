#version 440

in vec4 vPos;
in vec2 vUV;

out vec4 fColor;

uniform int frame;
// Temperature value
uniform float temperature;
// Texture sampler
uniform sampler2D glowTex;

// Scales brightness to match temperature of the star
vec3 scaleBrightness(float temperature){
    return vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

void main() {
    vec4 color = texture(glowTex, vUV);
    fColor = color;
    fColor.a = 1.0f;
}