#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=0) out vec3 world_position;
layout(location=1) out vec3 world_normal;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 tint;
    vec4 material;
    vec4 eye;
    vec4 rotation;
} pc;
void main() {
    float c=pc.rotation.x, s=pc.rotation.y;
    mat3 model=mat3(c,0,-s, 0,1,0, s,0,c);
    world_position=model*position;
    world_normal=model*normal;
    gl_Position=pc.mvp*vec4(position,1);
}
