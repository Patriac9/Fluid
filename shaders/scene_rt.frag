#version 460
#extension GL_EXT_ray_query : require
layout(constant_id=0) const bool target_srgb=false;
layout(set=0, binding=0) uniform accelerationStructureEXT tlas;
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_normal;
layout(location=0) out vec4 out_color;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 tint;
    vec4 material;
    vec4 eye;
    vec4 rotation;
} pc;
const float PI = 3.14159265359;
vec3 target_color(vec3 color) {
    return target_srgb ? mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)),
                             greaterThan(color, vec3(0.04045)))
                       : color;
}
vec3 light(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    float nl = max(dot(N, L), 0.0), nv = max(dot(N, V), 0.001), nh = max(dot(N, H), 0.0),
          hv = max(dot(H, V), 0.0);
    float a = roughness * roughness, a2 = a * a;
    float denom = nh * nh * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom + 0.0001);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - hv, 5.0);
    vec3 spec = D * G * F / (4.0 * nv * nl + 0.0001);
    return ((1.0 - F) * (1.0 - metallic) * albedo / PI + spec) * radiance * nl;
}
bool shadowed(vec3 origin, vec3 dir) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF,
                          origin, 0.03, dir, 48.0);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
vec3 sky(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.07, 0.075, 0.10), vec3(0.42, 0.52, 0.72), t);
}
vec3 reflection(vec3 origin, vec3 dir, vec3 albedo, float metallic) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, tlas, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.03, dir, 24.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
        return sky(dir);
    return mix(sky(dir) * 0.25, albedo, mix(0.18, 0.62, metallic));
}
void main() {
    if (pc.material.w > 0.5) {
        out_color = vec4(target_color(pc.tint.rgb), pc.tint.a);
        return;
    }
    vec3 N = normalize(world_normal);
    if (!gl_FrontFacing)
        N = -N;
    vec3 V = normalize(pc.eye.xyz - world_position);
    vec3 albedo = pow(pc.tint.rgb, vec3(2.2));
    float metallic = clamp(pc.material.x, 0, 1), roughness = clamp(pc.material.y, 0.08, 1);
    vec3 origin = world_position + N * 0.03;
    vec3 color = albedo * (0.10 + 0.13 * max(N.y, 0.0));
    vec3 L0 = normalize(vec3(-3, 5, 4));
    vec3 L1 = normalize(vec3(4, 2, -2));
    vec3 L2 = normalize(vec3(-2, 1, -4));
    if (!shadowed(origin, L0))
        color += light(L0, vec3(4.0, 3.8, 3.4), N, V, albedo, metallic, roughness);
    if (!shadowed(origin, L1))
        color += light(L1, vec3(1.5, 2.2, 3.5), N, V, albedo, metallic, roughness);
    if (!shadowed(origin, L2))
        color += light(L2, vec3(2.0, 1.1, 2.5), N, V, albedo, metallic, roughness);
    vec3 R = reflect(-V, N);
    color += reflection(origin, R, albedo, metallic) * mix(0.04, 0.85, metallic) * (1.0 - roughness * 0.85);
    color += mix(vec3(0.02), albedo, metallic) * pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.7;
    color = vec3(1.0) - exp(-color * pc.material.z);
    out_color = vec4(target_color(pow(color, vec3(1.0 / 2.2))), pc.tint.a);
}
