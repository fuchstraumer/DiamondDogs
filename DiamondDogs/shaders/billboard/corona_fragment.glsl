#include "simplex.glsl"

in vec4 vPos;

out vec4 fColor;

uniform int frame;
// Temperature value
uniform float temperature;

vec3 getStarColor(float temperature) {
    return vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

void main() {
    vec3 fPos = vPos.xyz;
    // Time component of vector is length of vPos, or distance from center.
    // Makes corona appear to radiate outwards.
    float t = (frame * 0.0001f) - length(vPos);
    // Get three noise samples.
    float frequency = 1.5f;
    float ox = snoise(vec4(fPos, t) * frequency);
    float oy = snoise(vec4(fPos + 2000.0f, t) * frequency);
    float oz = snoise(vec4(fPos + 4000.0f, t) * frequency);
    // Store offset since we'll use it again shortly.
    vec3 offset = vec3(ox, oy, oz) * 0.10f;
    // Get distance from center.
    vec3 dist = normalize(fPos + offset);

    // Get noise with normalized position ot offset original position.
    vec3 noisePosition = fPos + noise(vec4(dist, t), 5, 2.0f, 0.70f) * 0.10f;

    // Using position found above, calculate brightness 
    float cdist = length(noisePosition) * 3.0f;
    float brightness = (1.0f / (cdist * cdist) - 0.10f) * 0.80f;
    
    // Placeholder color until I use parent star's texture color for this too
    float u;
    if(temperature < 26000){
        u = temperature + 3000.0f;
    }
    else{
        u = 29200.0f;
    }
    vec3 color = getStarColor(u);
    fColor = vec4(color,0.6f) * brightness;
    float transparency = (1.0f / (cdist * cdist)) * 0.09f;
    fColor.a = transparency;
    if(fColor.a < 0.04f){
      discard;
    }
}