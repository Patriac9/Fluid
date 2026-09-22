#version 450
layout(constant_id=0) const bool target_srgb=false;
layout(set=0, binding=0) uniform sampler2D scene;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
vec3 target_color(vec3 color) {
    return target_srgb ? mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)),
                             greaterThan(color, vec3(0.04045)))
                       : color;
}
void main() {
    vec4 c = texture(scene, uv);
    c.rgb = target_color(c.rgb);
    out_color = c;
}
