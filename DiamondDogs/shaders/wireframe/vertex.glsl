#version 430

uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
uniform mat4 normTransform;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
<<<<<<< HEAD

=======
layout(location = 2) in vec2 uv;
>>>>>>> a13f8af3fadbc3153e6c23b04bd9d5c84982e86b

out vec3 fPos;
out vec4 vPosition;
out vec3 vNormal;
<<<<<<< HEAD
=======
out vec2 vUV;
>>>>>>> a13f8af3fadbc3153e6c23b04bd9d5c84982e86b

void main(){
    vec4 Position = vec4(position,1.0f);
    vNormal = mat3(normTransform) * normal;
    vPosition = projection * view * model * Position;
<<<<<<< HEAD

=======
	vUV = uv;
>>>>>>> a13f8af3fadbc3153e6c23b04bd9d5c84982e86b
    fPos = (model * Position).xyz;
}