#version 450
layout(constant_id=0) const bool target_srgb=false;
layout(set=0,binding=0) uniform sampler2D atlas;
layout(location=0) in vec2 frag_uv;
layout(location=1) in vec4 frag_color;
layout(location=0) out vec4 out_color;
vec3 target_color(vec3 color) {
    // UNORM attachments store authored sRGB values directly. sRGB attachments
    // perform encoding themselves, so supply the inverse transfer function.
    return target_srgb ? mix(color/12.92, pow((color+0.055)/1.055,vec3(2.4)), greaterThan(color,vec3(0.04045))) : color;
}
void main() {
    out_color = frag_color * texture(atlas, frag_uv);
    out_color.rgb = target_color(out_color.rgb);
}
