#include "fluid/ui.h"
#include "utf8.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fluid {
namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr int key_space = 32, key_a = 65, key_escape = 256, key_enter = 257;
constexpr int key_tab = 258, key_backspace = 259, key_delete = 261;
constexpr int key_right = 262, key_left = 263, key_down = 264, key_up = 265;
constexpr int key_home = 268, key_end = 269, key_left_shift = 340, key_left_control = 341;
constexpr int key_right_shift = 344, key_right_control = 345, key_keypad_enter = 335;

Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 subtract(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 multiply(Vec2 a, float scale) { return {a.x * scale, a.y * scale}; }
float length(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
Color mix(Color a, Color b, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount, a.b + (b.b - a.b) * amount,
            a.a + (b.a - a.a) * amount};
}
Rect intersect(Rect a, Rect b) {
    const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0.0f, std::min(a.x + a.w, b.x + b.w) - x),
            std::max(0.0f, std::min(a.y + a.h, b.y + b.h) - y)};
}
bool same_rect(Rect a, Rect b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
Rect inset(Rect a, float amount) { return {a.x + amount, a.y + amount, a.w - amount * 2, a.h - amount * 2}; }

std::vector<Vec2> rounded_points(Rect bounds, float radius, int segments = 0) {
    radius = std::clamp(radius, 0.0f, std::min(bounds.w, bounds.h) * 0.5f);
    if (!segments)
        segments = std::clamp(static_cast<int>(std::ceil(radius * 0.5f)), 3, 16);
    if (radius <= 0 && !segments)
        segments = 1;
    std::vector<Vec2> points;
    points.reserve(static_cast<std::size_t>(segments + 1) * 4);
    const Vec2 centers[] = {{bounds.x + bounds.w - radius, bounds.y + radius},
                            {bounds.x + bounds.w - radius, bounds.y + bounds.h - radius},
                            {bounds.x + radius, bounds.y + bounds.h - radius},
                            {bounds.x + radius, bounds.y + radius}};
    for (int corner = 0; corner < 4; ++corner) {
        for (int segment = 0; segment <= segments; ++segment) {
            const float angle = (-0.5f + corner * 0.5f + 0.5f * segment / segments) * pi;
            const Vec2 point{centers[corner].x + std::cos(angle) * radius,
                             centers[corner].y + std::sin(angle) * radius};
            // Keep corresponding outline points even for square inner corners.
            points.push_back(point);
        }
    }
    return points;
}

std::vector<Vec2> outward_normals(const std::vector<Vec2> &points) {
    std::vector<Vec2> normals(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto previous = i;
        auto next = i;
        for (std::size_t step = 0; step < points.size(); ++step) {
            previous = (previous + points.size() - 1) % points.size();
            if (length(subtract(points[i], points[previous])) > 0.0001f)
                break;
        }
        for (std::size_t step = 0; step < points.size(); ++step) {
            next = (next + 1) % points.size();
            if (length(subtract(points[next], points[i])) > 0.0001f)
                break;
        }
        Vec2 before = subtract(points[i], points[previous]);
        Vec2 after = subtract(points[next], points[i]);
        before = multiply(before, 1.0f / std::max(length(before), 0.0001f));
        after = multiply(after, 1.0f / std::max(length(after), 0.0001f));
        Vec2 normal{(before.y + after.y) * 0.5f, -(before.x + after.x) * 0.5f};
        const float square = normal.x * normal.x + normal.y * normal.y;
        normals[i] = multiply(normal, std::min(1.0f / std::max(square, 0.0001f), 4.0f));
    }
    return normals;
}
} // namespace

void DrawList::reset(float width, float height, const FontAtlas &font) {
    vertices.clear();
    commands.clear();
    scenes.clear();
    clips_.clear();
    font_ = &font;
    clips_.push_back({0, 0, std::max(width, 0.0f), std::max(height, 0.0f)});
}

Rect DrawList::current_clip() const { return clips_.empty() ? Rect{} : clips_.back(); }
void DrawList::push_clip(Rect clip) { clips_.push_back(intersect(current_clip(), clip)); }
void DrawList::pop_clip() {
    if (clips_.size() > 1)
        clips_.pop_back();
}

void DrawList::append_command(std::uint32_t count) {
    const auto first = static_cast<std::uint32_t>(vertices.size()) - count;
    if (!commands.empty()) {
        auto &previous = commands.back();
        if (previous.kind == DrawCommand::Kind::triangles && previous.first + previous.count == first &&
            same_rect(previous.clip, current_clip())) {
            previous.count += count;
            return;
        }
    }
    commands.push_back({DrawCommand::Kind::triangles, first, count, current_clip(), 0});
}

void DrawList::triangle(Vec2 a, Vec2 b, Vec2 c, Color ca, Color cb, Color cc) {
    const Rect clip = current_clip();
    if (!font_ || clip.w <= 0 || clip.h <= 0)
        return;
    const Vec2 white = font_->white_uv();
    vertices.push_back({a, white, ca});
    vertices.push_back({b, white, cb});
    vertices.push_back({c, white, cc});
    append_command(3);
}

void DrawList::textured_quad(Rect bounds, Vec2 uv0, Vec2 uv1, Color color) {
    const Rect visible = intersect(bounds, current_clip());
    if (!font_ || visible.w <= 0 || visible.h <= 0)
        return;
    const Vec2 a{bounds.x, bounds.y}, b{bounds.x + bounds.w, bounds.y};
    const Vec2 c{bounds.x + bounds.w, bounds.y + bounds.h}, d{bounds.x, bounds.y + bounds.h};
    vertices.push_back({a, uv0, color});
    vertices.push_back({b, {uv1.x, uv0.y}, color});
    vertices.push_back({c, uv1, color});
    vertices.push_back({a, uv0, color});
    vertices.push_back({c, uv1, color});
    vertices.push_back({d, {uv0.x, uv1.y}, color});
    append_command(6);
}

void DrawList::polygon(const std::vector<Vec2> &points, Rect bounds, Color top, Color bottom) {
    if (points.size() < 3 || bounds.w <= 0 || bounds.h <= 0 || (top.a <= 0 && bottom.a <= 0))
        return;
    const auto normals = outward_normals(points);
    const Vec2 center{bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f};
    const auto color_at = [&](Vec2 point) { return mix(top, bottom, (point.y - bounds.y) / bounds.h); };
    const Color middle = color_at(center);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto next = (i + 1) % points.size();
        const Vec2 inside_a = subtract(points[i], multiply(normals[i], 0.5f));
        const Vec2 inside_b = subtract(points[next], multiply(normals[next], 0.5f));
        const Vec2 outside_a = add(points[i], multiply(normals[i], 0.5f));
        const Vec2 outside_b = add(points[next], multiply(normals[next], 0.5f));
        const Color color_a = color_at(points[i]), color_b = color_at(points[next]);
        triangle(center, inside_a, inside_b, middle, color_a, color_b);
        triangle(inside_a, outside_a, outside_b, color_a, color_a.opacity(0), color_b.opacity(0));
        triangle(inside_a, outside_b, inside_b, color_a, color_b.opacity(0), color_b);
    }
}

void DrawList::rect(Rect bounds, Color color, float radius) { gradient(bounds, color, color, radius); }
void DrawList::gradient(Rect bounds, Color top, Color bottom, float radius) {
    if (bounds.w <= 0 || bounds.h <= 0)
        return;
    const Rect visible = intersect(inset(bounds, -1), current_clip());
    if (visible.w <= 0 || visible.h <= 0)
        return;
    if (radius <= 0) {
        polygon({{bounds.x, bounds.y},
                 {bounds.x + bounds.w, bounds.y},
                 {bounds.x + bounds.w, bounds.y + bounds.h},
                 {bounds.x, bounds.y + bounds.h}},
                bounds, top, bottom);
    } else
        polygon(rounded_points(bounds, radius), bounds, top, bottom);
}

void DrawList::outline(Rect bounds, Color color, float radius, float thickness) {
    if (bounds.w <= 0 || bounds.h <= 0 || thickness <= 0 || color.a <= 0)
        return;
    thickness = std::min(thickness, std::min(bounds.w, bounds.h) * 0.5f);
    if (thickness * 2 >= std::min(bounds.w, bounds.h)) {
        rect(bounds, color, radius);
        return;
    }
    const int segments = radius > 0 ? std::clamp(static_cast<int>(std::ceil(radius * 0.5f)), 3, 16) : 1;
    const auto outer = rounded_points(bounds, radius, segments);
    const auto inner = rounded_points(inset(bounds, thickness), std::max(0.0f, radius - thickness), segments);
    const auto outer_normals = outward_normals(outer), inner_normals = outward_normals(inner);
    const float aa = std::min(0.5f, thickness * 0.5f);
    const Color transparent = color.opacity(0);
    auto quad = [&](Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color ca, Color cb, Color cc, Color cd) {
        triangle(a, b, c, ca, cb, cc);
        triangle(a, c, d, ca, cc, cd);
    };
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const auto next = (i + 1) % outer.size();
        const Vec2 oa = subtract(outer[i], multiply(outer_normals[i], aa));
        const Vec2 ob = subtract(outer[next], multiply(outer_normals[next], aa));
        const Vec2 ia = add(inner[i], multiply(inner_normals[i], aa));
        const Vec2 ib = add(inner[next], multiply(inner_normals[next], aa));
        quad(oa, ob, ib, ia, color, color, color, color);
        quad(add(outer[i], multiply(outer_normals[i], aa)),
             add(outer[next], multiply(outer_normals[next], aa)), ob, oa, transparent, transparent, color,
             color);
        quad(ia, ib, subtract(inner[next], multiply(inner_normals[next], aa)),
             subtract(inner[i], multiply(inner_normals[i], aa)), color, color, transparent, transparent);
    }
}

void DrawList::line(Vec2 from, Vec2 to, Color color, float thickness) {
    if (thickness <= 0)
        return;
    const Vec2 delta = subtract(to, from);
    if (length(delta) < 0.001f) {
        circle(from, thickness * 0.5f, color);
        return;
    }
    const float angle = std::atan2(delta.y, delta.x);
    const float radius = thickness * 0.5f;
    std::vector<Vec2> points;
    constexpr int segments = 8;
    for (int end = 0; end < 2; ++end) {
        const Vec2 center = end == 0 ? to : from;
        for (int i = 0; i <= segments; ++i) {
            const float theta = angle - pi * 0.5f + end * pi + pi * i / segments;
            points.push_back({center.x + std::cos(theta) * radius, center.y + std::sin(theta) * radius});
        }
    }
    const Rect bounds{std::min(from.x, to.x) - radius, std::min(from.y, to.y) - radius,
                      std::abs(delta.x) + thickness, std::abs(delta.y) + thickness};
    polygon(points, bounds, color, color);
}

void DrawList::circle(Vec2 center, float radius, Color color) {
    if (radius <= 0)
        return;
    const int count = std::clamp(static_cast<int>(std::ceil(radius * 1.25f)), 12, 80);
    std::vector<Vec2> points;
    points.reserve(count);
    for (int i = 0; i < count; ++i) {
        const float angle = 2 * pi * i / count;
        points.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
    }
    polygon(points, {center.x - radius, center.y - radius, radius * 2, radius * 2}, color, color);
}

void DrawList::text(Vec2 position, std::string_view text, float size, Color color, bool bold) {
    if (!font_ || size <= 0 || color.a <= 0)
        return;
    const float start = position.x;
    const float scale = size / font_->base_size();
    for (std::size_t cursor = 0; cursor < text.size();) {
        const auto scalar = detail::decode_utf8(text, cursor);
        if (scalar == '\n') {
            position.x = start;
            position.y += size * 1.35f;
            continue;
        }
        if (scalar == '\r')
            continue;
        if (scalar == '\t') {
            position.x += font_->glyph(' ', bold).advance * scale * 4;
            continue;
        }
        const auto glyph = font_->glyph(scalar, bold);
        textured_quad({position.x + glyph.x0 * scale, position.y + glyph.y0 * scale,
                       (glyph.x1 - glyph.x0) * scale, (glyph.y1 - glyph.y0) * scale},
                      {glyph.u0, glyph.v0}, {glyph.u1, glyph.v1}, color);
        position.x += glyph.advance * scale;
    }
}

float DrawList::text_width(std::string_view text, float size, bool bold) const {
    return font_ ? font_->measure(text, size, bold) : 0;
}

void DrawList::scene(const SceneView &view) {
    const Rect clip = intersect(current_clip(), view.bounds);
    if (clip.w <= 0 || clip.h <= 0 || !view.mesh)
        return;
    scenes.push_back(view);
    commands.push_back({DrawCommand::Kind::scene, 0, 0, clip, static_cast<std::uint32_t>(scenes.size() - 1)});
}

Ui::Ui() = default;

void Ui::begin_frame(const InputState &input, float width, float height, float dt) {
    input_ = input;
    time_ += std::clamp(dt, 0.0f, 0.25f);
    draw_.reset(width, height, font_);
    focus_order_.clear();
    if (input.mouse_pressed) {
        active_.clear();
        focused_.clear();
        keyboard_focus_ = false;
    }
    if (input.keys_pressed[key_escape]) {
        active_.clear();
        focused_.clear();
    }
    if (input.keys_pressed[key_tab] && !previous_focus_order_.empty()) {
        const bool reverse = input.keys_down[key_left_shift] || input.keys_down[key_right_shift];
        const auto found = std::find(previous_focus_order_.begin(), previous_focus_order_.end(), focused_);
        auto index = found == previous_focus_order_.end()
                         ? (reverse ? 0 : -1)
                         : static_cast<int>(found - previous_focus_order_.begin());
        const int count = static_cast<int>(previous_focus_order_.size());
        index = (index + (reverse ? -1 : 1) + count) % count;
        focused_ = previous_focus_order_[index];
        keyboard_focus_ = true;
    }
}

void Ui::end_frame() {
    if (input_.mouse_released || !input_.mouse_down)
        active_.clear();
    if (std::find(focus_order_.begin(), focus_order_.end(), focused_) == focus_order_.end())
        focused_.clear();
    previous_focus_order_ = focus_order_;
}

void Ui::register_widget(std::string_view id) {
    if (id.empty())
        return;
    const std::string key(id);
    if (std::find(focus_order_.begin(), focus_order_.end(), key) == focus_order_.end())
        focus_order_.push_back(key);
}

bool Ui::hovered(Rect bounds) const {
    return bounds.contains(input_.mouse) && draw_.current_clip().contains(input_.mouse);
}

bool Ui::hit(std::string_view id, Rect bounds) {
    if (id.empty())
        return false;
    register_widget(id);
    const bool hover = hovered(bounds);
    if (input_.mouse_pressed && hover && active_.empty()) {
        active_ = id;
        focused_ = id;
    }
    const bool click = input_.mouse_released && active_ == id && hover;
    const bool keyboard =
        focused_ == id && (input_.keys_pressed[key_enter] || input_.keys_pressed[key_keypad_enter] ||
                           input_.keys_pressed[key_space]);
    return click || keyboard;
}

void Ui::focus_outline(std::string_view id, Rect bounds, float radius) {
    if (focused_ == id && keyboard_focus_)
        draw_.outline(inset(bounds, -3), theme_.accent.opacity(0.8f), radius + 3, 1.5f);
}

bool Ui::button(std::string_view id, Rect bounds, std::string_view label, bool primary, bool selected) {
    const bool clicked = hit(id, bounds);
    Color fill = primary ? theme_.accent : selected ? theme_.accent.opacity(0.16f) : theme_.elevated;
    if (hovered(bounds))
        fill =
            mix(fill, primary ? Color::hex(0xFFFFFF) : theme_.text.opacity(fill.a), primary ? 0.08f : 0.035f);
    if (active_ == id && input_.mouse_down)
        fill = mix(fill, theme_.background, 0.16f);
    draw_.rect(bounds, fill, 8);
    if (!primary)
        draw_.outline(bounds, selected ? theme_.accent.opacity(0.38f) : theme_.border.opacity(0.8f), 8);
    const float size = 13;
    draw_.push_clip(inset(bounds, 5));
    draw_.text({bounds.x + (bounds.w - draw_.text_width(label, size, true)) * 0.5f,
                bounds.y + (bounds.h - size) * 0.5f - 0.5f},
               label, size,
               primary    ? theme_.accent_text
               : selected ? theme_.accent
                          : theme_.text,
               true);
    draw_.pop_clip();
    focus_outline(id, bounds, 8);
    return clicked;
}

bool Ui::toggle(std::string_view id, Rect bounds, bool &value) {
    const bool clicked = hit(id, bounds);
    if (clicked)
        value = !value;
    const float height = std::min(bounds.h, 22.0f), width = std::min(bounds.w, 40.0f);
    const Rect track{bounds.x, bounds.y + (bounds.h - height) * 0.5f, width, height};
    draw_.rect(track,
               value             ? theme_.accent
               : hovered(bounds) ? mix(theme_.border, theme_.muted, 0.18f)
                                 : theme_.border,
               height * 0.5f);
    const Vec2 center{track.x + height * 0.5f + (value ? width - height : 0), track.y + height * 0.5f};
    draw_.circle({center.x, center.y + 1}, std::max(0.0f, height * 0.5f - 3), Color::hex(0x000000, 0.10f));
    draw_.circle(center, std::max(0.0f, height * 0.5f - 3), value ? theme_.accent_text : theme_.text);
    focus_outline(id, track, height * 0.5f);
    return clicked;
}

bool Ui::slider(std::string_view id, Rect bounds, float &value, float minimum, float maximum) {
    if (id.empty() || bounds.w <= 0 || bounds.h <= 0)
        return false;
    register_widget(id);
    if (maximum < minimum)
        std::swap(maximum, minimum);
    const float old = value;
    value = std::isfinite(value) ? std::clamp(value, minimum, maximum) : minimum;
    const float left = bounds.x + 7, width = std::max(1.0f, bounds.w - 14);
    if (input_.mouse_pressed && hovered(bounds) && active_.empty()) {
        active_ = id;
        focused_ = id;
    }
    if (active_ == id && (input_.mouse_down || input_.mouse_pressed || input_.mouse_released))
        value = minimum + std::clamp((input_.mouse.x - left) / width, 0.0f, 1.0f) * (maximum - minimum);
    if (focused_ == id) {
        const float step =
            (maximum - minimum) *
            ((input_.keys_down[key_left_shift] || input_.keys_down[key_right_shift]) ? 0.1f : 0.01f);
        if (input_.keys_pressed[key_left] || input_.keys_pressed[key_down])
            value -= step;
        if (input_.keys_pressed[key_right] || input_.keys_pressed[key_up])
            value += step;
        if (input_.keys_pressed[key_home])
            value = minimum;
        if (input_.keys_pressed[key_end])
            value = maximum;
        value = std::clamp(value, minimum, maximum);
    }
    const float amount = maximum > minimum ? (value - minimum) / (maximum - minimum) : 0;
    const float y = bounds.y + bounds.h * 0.5f;
    draw_.rect({left, y - 2, width, 4}, theme_.border, 2);
    if (amount > 0)
        draw_.rect({left, y - 2, width * amount, 4}, theme_.accent, 2);
    const Vec2 knob{left + width * amount, y};
    if (hovered(bounds) || active_ == id)
        draw_.circle(knob, 10, theme_.accent.opacity(0.13f));
    draw_.circle({knob.x, knob.y + 1}, 6, Color::hex(0x000000, 0.18f));
    draw_.circle(knob, 6, theme_.accent);
    draw_.circle(knob, 2, theme_.accent_text.opacity(0.5f));
    focus_outline(id, {bounds.x, y - 10, bounds.w, 20}, 6);
    return value != old;
}

bool Ui::text_field(std::string_view id, Rect bounds, std::string &value, std::string_view placeholder) {
    if (id.empty())
        return false;
    register_widget(id);
    auto &state = fields_[std::string(id)];
    if (!state.initialized) {
        state.cursor = value.size();
        state.initialized = true;
    }
    state.cursor = std::min(state.cursor, value.size());
    while (state.cursor < value.size() && state.cursor > 0 &&
           (static_cast<unsigned char>(value[state.cursor]) & 0xC0) == 0x80)
        --state.cursor;
    constexpr float size = 13;
    constexpr float padding = 12;
    if (input_.mouse_pressed && hovered(bounds) && active_.empty()) {
        active_ = id;
        focused_ = id;
        state.select_all = false;
        const float offset = input_.mouse.x - bounds.x - padding + state.scroll;
        state.cursor = 0;
        while (state.cursor < value.size()) {
            const auto next = detail::next_utf8(value, state.cursor);
            const float before = draw_.text_width(std::string_view(value).substr(0, state.cursor), size);
            const float after = draw_.text_width(std::string_view(value).substr(0, next), size);
            if (offset < (before + after) * 0.5f)
                break;
            state.cursor = next;
        }
    }
    const bool focused = focused_ == id;
    bool changed = false;
    if (focused) {
        const bool control = input_.keys_down[key_left_control] || input_.keys_down[key_right_control];
        if (control && input_.keys_pressed[key_a]) {
            state.select_all = true;
            state.cursor = value.size();
        }
        if (input_.keys_pressed[key_home]) {
            state.cursor = 0;
            state.select_all = false;
        }
        if (input_.keys_pressed[key_end]) {
            state.cursor = value.size();
            state.select_all = false;
        }
        if (input_.keys_pressed[key_left]) {
            state.cursor = state.select_all ? 0 : detail::previous_utf8(value, state.cursor);
            state.select_all = false;
        }
        if (input_.keys_pressed[key_right]) {
            state.cursor = state.select_all ? value.size() : detail::next_utf8(value, state.cursor);
            state.select_all = false;
        }
        const auto clear_selection = [&]() {
            if (!state.select_all)
                return false;
            changed = changed || !value.empty();
            value.clear();
            state.cursor = 0;
            state.select_all = false;
            return true;
        };
        if (input_.keys_pressed[key_backspace] && !clear_selection() && state.cursor > 0) {
            const auto previous = detail::previous_utf8(value, state.cursor);
            value.erase(previous, state.cursor - previous);
            state.cursor = previous;
            changed = true;
        }
        if (input_.keys_pressed[key_delete] && !clear_selection() && state.cursor < value.size()) {
            value.erase(state.cursor, detail::next_utf8(value, state.cursor) - state.cursor);
            changed = true;
        }
        if (!control && !input_.text.empty()) {
            std::string insertion;
            for (std::size_t cursor = 0; cursor < input_.text.size();) {
                const auto start = cursor;
                const auto scalar = detail::decode_utf8(input_.text, cursor);
                if (scalar >= 0x20 && scalar != 0x7F && scalar != 0xFFFD)
                    insertion.append(input_.text, start, cursor - start);
            }
            if (!insertion.empty()) {
                clear_selection();
                value.insert(state.cursor, insertion);
                state.cursor += insertion.size();
                changed = true;
            }
        }
    } else {
        state.select_all = false;
        state.scroll = 0;
    }
    const float available = std::max(0.0f, bounds.w - padding * 2);
    const float cursor_x = draw_.text_width(std::string_view(value).substr(0, state.cursor), size);
    if (focused) {
        state.scroll = std::max(state.scroll, cursor_x - available + 2);
        state.scroll = std::min(state.scroll, cursor_x);
        state.scroll = std::max(0.0f, state.scroll);
    }
    draw_.rect(bounds, theme_.background.opacity(0.72f), 8);
    draw_.outline(bounds,
                  focused           ? theme_.accent.opacity(0.85f)
                  : hovered(bounds) ? theme_.muted.opacity(0.5f)
                                    : theme_.border,
                  8);
    const float y = bounds.y + (bounds.h - size) * 0.5f - 0.5f;
    draw_.push_clip({bounds.x + padding - 1, bounds.y + 3, available + 2, std::max(0.0f, bounds.h - 6)});
    if (focused && state.select_all)
        draw_.rect({bounds.x + padding - state.scroll, y - 2, draw_.text_width(value, size), size + 4},
                   theme_.accent.opacity(0.25f), 2);
    draw_.text({bounds.x + padding - state.scroll, y}, value.empty() ? placeholder : std::string_view(value),
               size, value.empty() ? theme_.muted : theme_.text);
    if (focused && !state.select_all && (changed || std::fmod(time_, 1.0f) < 0.6f)) {
        const float caret = bounds.x + padding + cursor_x - state.scroll;
        draw_.rect({caret, y - 1, 1.25f, size + 2}, theme_.accent, 0.5f);
    }
    draw_.pop_clip();
    focus_outline(id, bounds, 8);
    return changed;
}

void Ui::label(Vec2 position, std::string_view text, float size, bool bold) {
    draw_.text(position, text, size, theme_.text, bold);
}
void Ui::panel(Rect bounds, float radius) {
    draw_.rect({bounds.x, bounds.y + 3, bounds.w, bounds.h}, Color::hex(0x000000, 0.06f), radius);
    draw_.rect(bounds, theme_.panel, radius);
    draw_.outline(bounds, theme_.border.opacity(0.65f), radius);
}
void Ui::progress(Rect bounds, float value, Color color) {
    const float radius = std::min(bounds.w, bounds.h) * 0.5f;
    draw_.rect(bounds, theme_.border, radius);
    value = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0;
    if (value > 0)
        draw_.rect({bounds.x, bounds.y, bounds.w * value, bounds.h}, color, radius);
}

} // namespace Fluid
