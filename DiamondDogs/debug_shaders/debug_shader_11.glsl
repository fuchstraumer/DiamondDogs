#version 450 core

uniform samplerCube skybox;

in vec3 texPos;
out vec4 frag_color;

void main(){
    frag_color = texture(skybox, texPos);
}