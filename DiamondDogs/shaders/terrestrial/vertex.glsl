#version 450 core

// Input data
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

// Core uniform matrices
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform mat4 normTransform;

// Lighting uniforms
uniform vec4 lightPosition;

// Atmospheric uniforms
uniform vec3 cameraPos;			// The camera's current position
uniform vec3 lightDir;			// The direction vector to the light source
uniform vec3 invWavelength;		// 1 / pow(wavelength, 4) for the red, green, and blue channels
uniform float cameraHeight;		// The camera's current height
uniform float cameraHeight2;		// fCameraHeight^2
uniform float outerRadius;			// The outer (atmosphere) radius
uniform float outerRadius2;		// fOuterRadius^2
uniform float innerRadius;			// The inner (planetary) radius
uniform float innerRadius2;		// fInnerRadius^2
uniform float KrESun;				// Kr * ESun
uniform float KmESun;				// Km * ESun
uniform float Kr4PI;				// Kr * 4 * PI
uniform float Km4PI;				// Km * 4 * PI
uniform float scale;				// 1 / (fOuterRadius - fInnerRadius)
uniform float scaleDepth;			// The scale depth (i.e. the altitude at which the atmosphere's average density is found)
uniform float scaleOverScaleDepth;	// fScale / fScaleDepth
uniform int samples;

// Output data
// "fragment" position and "fragment" norm, respectively
out vec3 fPos;
out vec3 fNorm;
out vec4 frontColor;
out vec4 frontSecondaryColor;
out float cameraDistance;
out vec3 f_lightDir;

// Scaling function for correctly applying bias
float scalefunc(float Cos){
	float x = 1.0f - Cos;
	return scaleDepth * exp(-0.00287f + x * (0.459f + x * (3.83f + x * (-6.80f + x * 5.25f))));
}

// Main function
void main(){
    // setting fragment position
    mat4 modelView =  model * view;
    // Norm transform is calculated on the CPU
    mat3 normalMatrix = mat3(normTransform);
    // normal vector normalized for use later (fragment shader)
    vec3 n = normalize(normalMatrix * normal);
    fNorm = n;
    // Vertex in view-space
    vec4 vertViewSpace = modelView * vec4(position, 1.0f);
    // Nothing special - usual model-view transformation for fragment position
    fPos = vertViewSpace.xyz;

    // Light direction
    f_lightDir = normalize(lightDir.xyz - vertViewSpace.xyz);

    // Scattering portion    
    vec3 vertexPosition = position;
    vec3 vertexRay = vertexPosition - cameraPos;
    float rayMag = length(vertexRay);
    vertexRay /= rayMag;

    // Get closest intersection of ray to outer atmosphere
    float B = 2.0f * dot(cameraPos, vertexRay);
    float C = cameraHeight2 - outerRadius2;
    float det = max(0.0f, B*B - 4.0f * C);
    float near = 0.50f * (-B - sqrt(det));

    // Less "off of the surface", more "not in the atmosphere"
    bool offSurface;
    if(cameraHeight > outerRadius){
        offSurface = true;
    }
    else{
        offSurface = false;
    }

    // Get rays starting position, then get scattering coeff
    vec3 rayStart;
    if(offSurface){
        rayStart = cameraPos + vertexRay * near;
    }
    else{
        rayStart = cameraPos;
    }

    if(offSurface){
        rayMag -= near;
    }

    float Depth;
    // If we're outside the atmosphere, this is the depth value (static)
    if(offSurface){
        Depth = exp((innerRadius - outerRadius) / scaleDepth);
    }
    // If we're in the atmosphere, our depth will change as the camera's height from the
    // surface changes
    else{
        Depth = exp((innerRadius - cameraHeight) / scaleDepth);
    }

    // Initialize sampling loop variables
    float cameraAngle = dot(-vertexRay, vertexPosition) / length(vertexPosition);
    float lightAngle = dot(lightDir, vertexPosition) / length(vertexPosition);
    float cameraScale = scalefunc(cameraAngle);
    float lightScale = scalefunc(lightAngle);
    float cameraOffset = Depth * cameraScale;
    float tmp = (lightScale + cameraScale);
    float sampleLength = rayMag / samples;
    float scaledLength = sampleLength * scale;
    vec3 sampleRay = vertexRay * sampleLength;
    vec3 samplePoint = rayStart + sampleRay * 0.50f;
    // Now loop through all the sample rays
    vec3 attenuate = vec3(0.0f);
    vec3 tmpColor = vec3(0.0f);
    for(int i = 0; i < samples; ++i){
        float Height = length(samplePoint);
        float Depth = exp(scaleOverScaleDepth * (innerRadius - Height));
        float Scatter = Depth*tmp - cameraOffset;
        attenuate = exp(-Scatter * (invWavelength * Kr4PI + Km4PI));
        tmpColor += attenuate * (Depth * scaledLength);
        samplePoint += sampleRay;
    }

    // Get the surface's brightness based on the camera's position and the atmosphere's radius
    cameraDistance = clamp(abs(outerRadius - cameraHeight), 0.10f, 0.50f);

    frontColor.rgb = tmpColor * (invWavelength * KrESun + KmESun);
    // Attentuation factor for the ground
    frontSecondaryColor.rgb = attenuate;

    gl_Position = projection * modelView * vec4(position, 1.0f); 
}

