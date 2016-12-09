#version 440

in vec3 fPos;
in vec3 vNormal;

out vec4 fColor;

void main(){
    vec3 norm = normalize(vNormal);
    vec4 color = vec4(0.1f, 0.8f, 0.3f, 1.0f);
    fColor = color;
}