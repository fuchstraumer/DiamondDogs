in vec3 fragPos;
in vec3 gTriDist;
in vec3 gNormal;

float fade(){
    vec3 d = fwidth(gTriDist);
    vec3 a3 = smoothstep(vec3(0.0f),d*1.50f,gTriDist);
    return min(min(a3.x, a3.y),a3.z);
}

out vec4 fColor;

void main(){
    vec3 norm = normalize(gNormal);
    vec4 color = vec4(0.1f, 0.8f, 0.3f, 1.0f);
    fColor = mix(vec3(0.0f),vec3(0.50f),fade());
}