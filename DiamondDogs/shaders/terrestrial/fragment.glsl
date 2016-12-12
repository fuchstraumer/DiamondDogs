#version 430 core

in vec3 fPos;
in vec3 fNorm;
in vec3 f_lightDir;
in vec4 frontColor;
in vec4 frontSecondaryColor;

in float cameraDistance;

out vec4 Color;

void main(){
    // Temp body color until texturing implemented
    vec3 bodyColor = vec3(0.1f,0.8f,0.3f);
    vec3 normal = normalize(fNorm);

    // Diffuse component
    float diffuse = max(dot(normal, f_lightDir), 0.0f);
   
    // Atmo components
    vec4 frontAtmo = mix(frontColor * 0.01f, frontSecondaryColor * 0.30f, (diffuse + 0.1f) * 5.0f);
    vec4 secondaryAtmo = mix(frontSecondaryColor * 0.01f, frontSecondaryColor * 0.30f, (diffuse + 0.1f) * 5.0f);

    // Final color
    Color = frontAtmo + secondaryAtmo + vec4(bodyColor, 1.0f);
}