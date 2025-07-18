
vec3 ExpandNormal(vec3 n) {
    return n * 2.0f - 1.0f;
}

vec4 NormalMapping(mat3 tbn, vec3 _sample) {
    vec3 normal = ExpandNormal(_sample);
    normal *= tbn;
    return normalize(vec4(normal, 0.0f));
}

float Attenuation(float range, float d) {
    return 1.0f - smoothstep(range * 0.75f, range, d);
}

float GetSpotLightCone(in const SpotLight light, in const vec3 L) {
    float minCos = cos(radians(light.SpotLightAngle));
    float maxCos = mix(minCos, 1.0f, 0.0f);
    float cosAngle = dot(light.Direction.xyz, -L);
    return smoothstep(minCos, maxCos, cosAngle);
}

float DistributionGGX(in const vec3 N, in const vec3 H, in const float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float numerator = a2;
    float denominator = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denominator = 3.14159f * denominator * denominator;

    return numerator / denominator;
}

float GeometrySchlickGGX(in const float NdotV, in const float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    float denominator = NdotV * (1.0f - k) + k;

    return NdotV / denominator;
}

float GeometrySmith(in vec3 N, in vec3 V, in vec3 L, in float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(const in float cosTheta, const in vec3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

vec4 ClipToView(in vec4 clip_pos, in mat4 projection_matrix) {
    vec4 view = inverse(projection_matrix) * clip_pos;
    view.xyz /= view.w;
    return view;
}

vec4 ScreenToView(vec4 screen, in vec2 screen_dim, in mat4 projection_matrix) {
    vec2 tex_coord = screen.xy / screen_dim;
    vec4 clip = vec4(tex_coord * 2.0f - 1.0f, screen.z, screen.w);
    return ClipToView(clip, projection_matrix);
}

Plane ComputePlane(in vec3 p0, in vec3 p1, in vec3 p2) {
    Plane result;
    result.N = normalize(cross(p1 - p0, p2 - p0));
    result.d = dot(result.N, p0);
    return result;
}

bool SphereInsidePlane(in Sphere sph, in Plane p) {
    return dot(p.N, sph.c) - p.d < -sph.r;
}

bool PointInsidePlane(in vec3 pt, in Plane pl) {
    return dot(pl.N, pt) - pl.d < 0.0f;
}

bool ConeInsidePlane(in Cone cone, in Plane plane) {
    vec3 m = cross(cross(plane.N, cone.d), cone.d);
    vec3 Q = cone.T + cone.d * cone.h - m * cone.r;
    return PointInsidePlane(cone.T, plane) && PointInsidePlane(Q, plane);
}

bool SphereInsideFrustum(in Sphere sphere, in Frustum frustum, in vec2 depth_range) {
    vec2 sphere_zz = vec2(sphere.c.z - sphere.r, sphere.c.z + sphere.r);
    if (sphere.c.z - sphere.r > depth_range.x || sphere.c.z + sphere.r < depth_range.y) {
        return false;
    }

    bvec4 sphere_inside_planes = bvec4(
        SphereInsidePlane(sphere, frustum.planes[0]),
        SphereInsidePlane(sphere, frustum.planes[1]),
        SphereInsidePlane(sphere, frustum.planes[2]),
        SphereInsidePlane(sphere, frustum.planes[3])
    );

    return any(sphere_inside_planes) ? false : true;
}

vec3 when_lt(in const vec3 x, in const vec3 y) {
    return max(sign(y - x), vec3(0.0f));
}

vec3 when_gt(in const vec3 x, in const vec3 y) {
    return max(sign(x - y), vec3(0.0f));
}

vec3 when_ge(in const vec3 x, in const vec3 y) {
    return 1.0f - when_lt(x, y);
}

vec3 when_le(in const vec3 x, in const vec3 y) {
    return 1.0f - when_gt(x, y);
}

bool SphereInsideAABB_Fast(in Sphere sphere, in AABB b) {
    vec3 result = vec3(0.0f);
    result += when_lt(sphere.c, b.Min.xyz) * (sphere.c - b.Min.xyz) * (sphere.c - b.Min.xyz);
    result += when_gt(sphere.c, b.Max.xyz) * (sphere.c - b.Max.xyz) * (sphere.c - b.Max.xyz);
    return (result.x + result.y + result.z) <= (sphere.r * sphere.r);
}

bool SphereInsideAABB(in Sphere sphere, in const AABB b) {
    float result = 0.0f;
    for (uint i = 0; i < 3; ++i) {
        if (sphere.c[i] < b.Min[i]) {
            result += (sphere.c[i] - b.Min[i]) * (sphere.c[i] - b.Min[i]);
        }
        else if (sphere.c[i] > b.Max[i]) {
            result += (sphere.c[i] - b.Max[i]) * (sphere.c[i] - b.Max[i]);
        }
    }
    return (result <= sphere.r * sphere.r);
}

bool AABBIntersectAABB(in const AABB a, in const AABB b) {
    return all(greaterThanEqual(a.Max,b.Min)) && all(lessThanEqual(a.Min,b.Max));
}

bool ConeInsideFrustum(in Cone cone, in Frustum frustum, in vec2 depth_range) {
    Plane near_plane;
    near_plane.N = vec3(0.0f, 0.0f,-1.0f);
    near_plane.d = -depth_range.x;
    Plane far_plane;
    far_plane.N = vec3(0.0f, 0.0f, 1.0f);
    far_plane.d = depth_range.y;

    if (ConeInsidePlane(cone, near_plane) || ConeInsidePlane(cone, far_plane)) {
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        if (ConeInsidePlane(cone, frustum.planes[i])) {
            return false;
        }
    }

    return true;
}

bool IntersectLinePlane(in vec3 a, in vec3 b, in Plane plane, out vec3 q) {
    vec3 ab = b - a;
    float t  = (plane.d - dot(plane.N, a)) / dot(plane.N, ab);
    bool intersect = ( t >= 0.0f && t <= 1.0f);
    q = vec3(0.0f);
    if (intersect) {
        q = a + t * ab;
    }
    return intersect;
}
