#include "simplex.glsl"
// Used to find color in texture map
uniform int temperature;
// Used to shift color to brighter based on temp
uniform vec3 colorShift;
// Color texture, for color-to-temp map
uniform sampler1D blackbody;
// Texture texture, for getting better surface definition.
// Frame counter
uniform int frame;
// Camera position
uniform vec3 cameraPos;
// Allows for changing transparency of the star.
uniform float opacity;

in vec3 fPos;

out vec4 fragColor;


void main(){
    // Get base surface texture
    vec4 noisePos = vec4(fPos,frame * 0.008f);

    float n = noise(noisePos, 3, 0.25f, 0.70f) * 0.50f;
    
    // Get texture coordinate from temperature
    float u = (temperature - 800.0f)/29200.0f;
    vec4 color = texture(blackbody, u);

    vec3 viewDir = normalize(cameraPos - fPos);

    // Color edges slightly based on distance between ray from viewer and fragment ray
    float theta = 1.0f - dot(fPos, viewDir);
    theta *= 0.5f;
    color *= 1.5f;
    // Total value ot use for colormapping
    float total = n * 2.2f;
    fragColor = (vec4(colorShift,1.0f) + color - theta) * total;
    fragColor.a = 1.0f * opacity;

}