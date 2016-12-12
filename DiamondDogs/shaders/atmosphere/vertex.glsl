#version 450 core 

// input variables
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

// Baseline uniforms
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normTransform;

// Uniforms from the camera's location
uniform vec3 cameraPos;
uniform vec3 lightDir;
uniform float cameraHeight;
uniform float cameraHeightSq;

// Atmospheric uniforms

uniform vec3 invWavelength;
uniform float innerRadius;
uniform float innerRadiusSq;
uniform float outerRadius;
uniform float outerRadiusSq;
uniform float KrESun;
uniform float KmESun;
uniform float Kr4PI;
uniform float Km4PI;
uniform float scale;
uniform float scaleDepth;
uniform float scaleOverScaleDepth;
uniform int samples;

// Output to fragment shader

out vec3 dir;
out vec4 frontColor;
out vec4 frontSecondaryColor;

// Scaling function for correctly applying bias
float scalefunc(float Cos){
	float x = 1.0f - Cos;
	return scaleDepth * exp(-0.00287f + x * (0.459f + x * (3.83f + x * (-6.80f + x * 5.25f))));
}

void main(){
    // Get the ray from camera to this vertex
    // Get vertex position, discarding extra parts of model matrix
    vec3 vertexPos = mat3(model) * position;
    // Get ray direction
    vec3 vertexRay = vertexPos - cameraPos;
    // Normalize the ray and turn it into a unit vector
    float rayMag = length(vertexRay);
    vertexRay /= rayMag;

    // Find closest intersection between the ray and the atmosphere
    float B = 2.0f * dot(cameraPos, vertexRay);
    // Distance from camera to top of atmosphere
    float C = cameraHeightSq - outerRadiusSq;
    float determinant = max(0.0f, B*B - 4.0f * C);
    float near = 0.50f * (-B - sqrt(determinant));

    // Are we within the atmosphere?
    bool offSurface;
    if(cameraHeight > outerRadius){
        offSurface = true;
    }
    else{
        offSurface = false;
    }

    // Get the ray's starting position
    vec3 rayStart;
    if(offSurface){
        rayStart = cameraPos + (vertexRay * near);
    }
    else{
        rayStart = cameraPos;
    }

    float height = length(rayStart);
    float depth = exp(scaleOverScaleDepth * (innerRadius - cameraHeight));

    rayMag -= near;

    float startAngle;
    if(offSurface){
        startAngle = dot(vertexRay, rayStart) / outerRadius;
    }
    else{
        startAngle = dot(vertexPos, rayStart) / height;
    }

    float startDepth = exp(-1.0f / scaleDepth);

    float startOffset;
    if(offSurface){
        startOffset = startDepth * scalefunc(startAngle);
    }
    else{
        startOffset = depth * scalefunc(startAngle);
    }

    // Scattering loop variables
    // Length of a sample ray
    float sampleLength = rayMag / samples;
    // sample ray length scaled to the right length (from unit length)
    float scaledLength = sampleLength * scale;
    // Sample ray
    vec3 sampleRay = vertexRay * sampleLength;
    // Sample point (init)
    vec3 samplePoint = rayStart + sampleRay * 0.50f;

    // init color vec3
    vec3 fColor = vec3(0.0f);
    for(int i = 0; i < samples; ++i){
        float Height = length(samplePoint);
        float Depth = exp(scaleOverScaleDepth * (innerRadius - Height));
        float lightAngle = dot(lightDir, samplePoint) / Height;
        float cameraAngle = dot(vertexRay, samplePoint) / Height;
        float scatter = (startOffset + Depth * (scalefunc(lightAngle) - scalefunc(cameraAngle)));
        vec3 attenuate = exp(-scatter * (invWavelength * Kr4PI + Km4PI));
        fColor += attenuate * (Depth * scaledLength);
        samplePoint += sampleRay;
    }

    // Scale colors and set the corresponding values for the fragment shader
    frontSecondaryColor.rgb = fColor * KmESun;
    frontColor.rgb = fColor * (invWavelength * KrESun);
    gl_Position = projection * view * model * vec4(position, 1.0f);

    // Set direction vector (using vertices model position)
    dir = cameraPos - vertexPos;
}