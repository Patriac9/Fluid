#version 450
// Spatial reconstruction at DLSS Quality scale (render ~67%, display 100%).
layout(set=0, binding=0) uniform sampler2D src;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(push_constant) uniform Push { vec2 texel; } pc;

vec4 cubic(float v) {
    vec4 n = vec4(1.0, 2.0, 3.0, 4.0) - v;
    vec4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return vec4(x, y, z, w) * (1.0 / 6.0);
}

vec4 bicubic(vec2 coord) {
    vec2 tex = coord / pc.texel - 0.5;
    vec2 fxy = fract(tex);
    tex -= fxy;
    vec4 xcubic = cubic(fxy.x);
    vec4 ycubic = cubic(fxy.y);
    vec4 c = tex.xxyy + vec2(-0.5, 1.5).xyxy;
    vec4 s = vec4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
    vec4 offset = c + vec4(xcubic.yw, ycubic.yw) / s;
    offset *= pc.texel.xxyy;
    vec4 sample0 = textureLod(src, offset.xz, 0.0);
    vec4 sample1 = textureLod(src, offset.yz, 0.0);
    vec4 sample2 = textureLod(src, offset.xw, 0.0);
    vec4 sample3 = textureLod(src, offset.yw, 0.0);
    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);
    return mix(mix(sample3, sample2, sx), mix(sample1, sample0, sx), sy);
}

void main() {
    vec4 e = bicubic(uv);
    vec4 d = textureLod(src, uv + vec2(-pc.texel.x, 0), 0.0);
    vec4 f = textureLod(src, uv + vec2(pc.texel.x, 0), 0.0);
    vec4 b = textureLod(src, uv + vec2(0, -pc.texel.y), 0.0);
    vec4 h = textureLod(src, uv + vec2(0, pc.texel.y), 0.0);
    vec4 sharp = e + (e - 0.25 * (d + f + b + h)) * 0.42;
    vec3 lo = min(e.rgb, min(min(d.rgb, f.rgb), min(b.rgb, h.rgb)));
    vec3 hi = max(e.rgb, max(max(d.rgb, f.rgb), max(b.rgb, h.rgb)));
    out_color = vec4(clamp(sharp.rgb, lo, hi), e.a);
}
