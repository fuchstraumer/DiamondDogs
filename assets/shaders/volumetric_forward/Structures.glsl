
struct PointLight {
    vec4 Position;
    vec4 PositionViewSpace;
    vec3 Color;
    float Range;
    float Intensity;
    bool Enabled;
    vec2 Padding;
}; // 64 bytes

struct SpotLight {
    vec4 Position;
    vec4 PositionViewSpace;
    vec4 Direction;
    vec4 DirectionViewSpace;
    vec3 Color;
    float SpotLightAngle;
    float Range;
    float Intensity;
    bool Enabled;
    float Padding;
}; // 96 bytes

struct DirectionalLight {
    vec4 Direction;
    vec4 DirectionViewSpace;
    vec3 Color;
    float Intensity;
    bool Enabled;
    vec3 Padding;
}; // 64 bytes

struct AABB {
    vec4 Min;
    vec4 Max;
};

struct Plane {
    vec3 N;
    float d;
};

struct Frustum {
    Plane planes[4];
};

struct Sphere {
    vec3 c;
    float r;
};

struct Cone {
    vec3 T;
    float h;
    vec3 d;
    float r;
};

struct LightingResult {
    vec3 Diffuse;
    vec3 Specular;
};

struct SurfaceOutput {
    vec3 Albedo;
    vec3 Normal; // Tangent-space normal
    vec3 Emission;
    float Metallic;
    float Smoothness;
    float Occlusion;
    float Alpha;
};
