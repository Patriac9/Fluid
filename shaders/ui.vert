#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=0) out vec2 frag_uv;
layout(location=1) out vec4 frag_color;
layout(push_constant) uniform Push { vec2 screen; } pc;
void main() {
    gl_Position = vec4(position / pc.screen * 2.0 - 1.0, 0.0, 1.0);
    frag_uv = uv;
    frag_color = color;
}
