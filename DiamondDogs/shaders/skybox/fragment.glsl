#version 400 core

in vec3 texPos;
out vec4 frag_color;

uniform samplerCube skybox;

void main(){
    frag_color = texture(skybox, texPos);
}