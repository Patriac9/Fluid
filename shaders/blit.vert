#version 450
layout(location=0) out vec2 uv;
layout(push_constant) uniform Push {
    vec2 screen;
    vec2 _pad;
    vec4 rect;
} pc;
void main() {
    vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    uv = corners[gl_VertexIndex];
    vec2 pos = pc.rect.xy + uv * pc.rect.zw;
    gl_Position = vec4(pos / pc.screen * 2.0 - 1.0, 0.0, 1.0);
}
