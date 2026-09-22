#include "fluid_app.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using namespace Fluid;
constexpr float pi = 3.14159265359f;
const std::array<Color, 5> swatches{Color::hex(0xB7A6EF), Color::hex(0x83CABC), Color::hex(0xEBC494),
                                    Color::hex(0xE293AD), Color::hex(0xA9C8EA)};
struct Painter {
    Ui &ui;
    void rect(Rect bounds, Color color, float radius = 0) {
        BackgroundedTextStyle style;
        style.background = color;
        style.shape = radius > 0 ? BackgroundShape::rounded_rectangle : BackgroundShape::rectangle;
        style.radius = radius;
        ui.backgrounded_text(bounds, "", style);
    }
    void gradient(Rect bounds, Color top, Color bottom, float radius = 0) {
        BackgroundedTextStyle style;
        style.background = top;
        style.gradient = bottom;
        style.shape = radius > 0 ? BackgroundShape::rounded_rectangle : BackgroundShape::rectangle;
        style.radius = radius;
        ui.backgrounded_text(bounds, "", style);
    }
    void outline(Rect bounds, Color color, float radius = 0) {
        BackgroundedTextStyle style;
        style.border = color;
        style.shape = radius > 0 ? BackgroundShape::rounded_rectangle : BackgroundShape::rectangle;
        style.radius = radius;
        ui.backgrounded_text(bounds, "", style);
    }
    void line(Vec2 from, Vec2 to, Color color, float thickness = 1) { ui.line(from, to, color, thickness); }
    void circle(Vec2 center, float radius, Color color) {
        BackgroundedTextStyle style;
        style.background = color;
        style.shape = BackgroundShape::circle;
        ui.backgrounded_text({center.x - radius, center.y - radius, radius * 2, radius * 2}, "", style);
    }
    void text(Vec2 position, std::string_view value, float size, Color color, bool bold = false) {
        ui.label(position, value, size, color, bold);
    }
    void scene(const SceneView &view) { ui.scene(view); }
    void push_clip(Rect clip) { ui.push_clip(clip); }
    void pop_clip() { ui.pop_clip(); }
    float text_width(std::string_view value, float size, bool bold = false) const {
        return ui.font().measure(value, size, bold);
    }
};
std::string decimal(float value, int precision = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}
window_cfg config(const DemoOptions &options) {
    window_cfg cfg;
    cfg.width = 1440;
    cfg.height = 960;
    cfg.window_title = "Fluid Studio | GUI & 3D playground";
    cfg.resizable = true;
    cfg.visible = !options.hidden;
    return cfg;
}
Color blend(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
std::filesystem::path utf8_path(const std::string &value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
} // namespace

fluid_test::fluid_test(DemoOptions options) : Fluid::window(config(options)), options_(std::move(options)) {
    meshes_ = {Mesh::knot(), Mesh::sphere(), Mesh::torus(), Mesh::cube()};
    camera_.distance = 3.6f;
}
void fluid_test::init() {
    application_cfg cfg;
    cfg.app_name = "Fluid Studio";
    cfg.engine_name = "Fluid";
    Init(cfg);
    if (options_.smoke_test) {
        options_.scene_aa = true;
        options_.ray_tracing = true;
        options_.dlss = true;
    }
    set_scene_aa(options_.scene_aa);
    set_ray_tracing(options_.ray_tracing);
    set_dlss(options_.dlss);
    glfwSetWindowSizeLimits(native_handle(), 1120, 820, GLFW_DONT_CARE, GLFW_DONT_CARE);
    set_frame_limit(options_.frames);
    if (!options_.model_path.empty()) {
        // A requested CLI model is required input; let main report a failure.
        // Interactive imports below keep the currently displayed scene intact.
        imported_ = Mesh::load_obj(utf8_path(options_.model_path));
        select_model(4);
        toast("Model loaded: " + imported_.name);
    }
    if (options_.page == "components")
        page_ = 1;
    if (options_.page == "typography")
        page_ = 2;
    if (options_.page == "motion")
        page_ = 3;
    std::cout << "Fluid Studio | " << device_name() << '\n';
}
void fluid_test::run() { loop(); }
void fluid_test::toast(std::string message) {
    toast_message_ = std::move(message);
    toast_until_ = float(elapsed_time()) + 4.5f;
}
void fluid_test::select_model(int index) {
    model_index_ = index;
    const char *names[] = {"Torus knot", "UV sphere", "Torus", "Cube"};
    model_name_ = index < 4 ? names[index] : imported_.name;
    camera_.reset();
    camera_.distance = 3.6f;
    rotation_ = 0;
}
void fluid_test::import_model(const std::string &path) {
    try {
        auto mesh = Mesh::load_obj(utf8_path(path));
        imported_ = std::move(mesh);
        select_model(4);
        page_ = 0;
        toast("Model loaded: " + imported_.name);
    } catch (const std::exception &e) {
        toast(e.what());
        std::cerr << "Model import: " << e.what() << '\n';
    }
}
void fluid_test::icon(Vec2 p, int kind, Color c, float s) {
    Painter d{ui()};
    float x = p.x, y = p.y;
    if (kind == 0) {
        d.outline({x, y, s * .42f, s * .42f}, c, 2);
        d.outline({x + s * .58f, y, s * .42f, s * .42f}, c, 2);
        d.outline({x, y + s * .58f, s * .42f, s * .42f}, c, 2);
        d.outline({x + s * .58f, y + s * .58f, s * .42f, s * .42f}, c, 2);
    } else if (kind == 1) {
        d.outline({x, y + 1, s, s * .38f}, c, 3);
        d.outline({x, y + s * .6f, s * .42f, s * .4f}, c, 3);
        d.circle({x + s * .8f, y + s * .8f}, s * .2f, c);
    } else if (kind == 2) {
        d.line({x, y + 1}, {x + s, y + 1}, c, 1.5f);
        d.line({x + s * .5f, y + 1}, {x + s * .5f, y + s}, c, 1.5f);
        d.line({x + s * .25f, y + s}, {x + s * .75f, y + s}, c, 1.5f);
    } else if (kind == 3) {
        d.circle({x + s * .23f, y + s * .5f}, s * .16f, c.opacity(.3f));
        d.circle({x + s * .5f, y + s * .5f}, s * .22f, c.opacity(.6f));
        d.circle({x + s * .8f, y + s * .5f}, s * .28f, c);
    } else if (kind == 4) {
        Vec2 a{x + s * .5f, y}, b{x + s, y + s * .25f}, cc{x + s * .5f, y + s * .5f}, e{x, y + s * .25f};
        d.line(a, b, c, 1.3f);
        d.line(b, cc, c, 1.3f);
        d.line(cc, e, c, 1.3f);
        d.line(e, a, c, 1.3f);
        d.line(e, {x, y + s * .75f}, c, 1.3f);
        d.line(b, {x + s, y + s * .75f}, c, 1.3f);
        d.line(cc, {x + s * .5f, y + s}, c, 1.3f);
        d.line({x, y + s * .75f}, {x + s * .5f, y + s}, c, 1.3f);
        d.line({x + s, y + s * .75f}, {x + s * .5f, y + s}, c, 1.3f);
    } else if (kind == 5) {
        d.circle({x + s * .5f, y + s * .5f}, s * .22f, c);
        for (int i = 0; i < 8; ++i) {
            float a = float(i) * pi / 4;
            d.line({x + s * .5f + std::cos(a) * s * .35f, y + s * .5f + std::sin(a) * s * .35f},
                   {x + s * .5f + std::cos(a) * s * .48f, y + s * .5f + std::sin(a) * s * .48f}, c, 1.4f);
        }
    }
}
void fluid_test::section_title(Vec2 p, const std::string &title, const std::string &description) {
    Painter d{ui()};
    const auto &t = ui().theme();
    d.text(p, title, 29, t.text, true);
    d.text({p.x, p.y + 42}, description, 14, t.muted);
}
void fluid_test::topbar(float side) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    d.rect({side, 0, width() - side, 72}, t.background);
    d.line({side, 71}, {width(), 71}, t.border.opacity(.65f));
    d.text({side + 32, 26}, "Workspace", 14, t.muted);
    d.text({side + 118, 26}, "/", 14, t.border);
    const char *pages[] = {"Overview", "Components", "Typography", "Motion"};
    d.text({side + 139, 26}, pages[page_], 14, t.text);
    float right = width() - 32;
    if (u.button("capture", {right - 151, 18, 151, 36}, "Save snapshot", true)) {
        std::filesystem::create_directories("snapshots");
        const auto path =
            "snapshots/fluid-" + std::to_string(static_cast<long long>(elapsed_time() * 1000)) + ".png";
        screenshot(path);
        toast("Snapshot saved to " + path);
    }
    if (u.button("theme", {right - 201, 18, 36, 36}, ""))
        dark_ = !dark_;
    icon({right - 192, 27}, 5, t.muted, 18);
    BackgroundedTextStyle vulkan;
    vulkan.background = t.elevated;
    vulkan.text = t.muted;
    vulkan.align = TextAlign::left;
    vulkan.padding = 25;
    vulkan.radius = 13;
    vulkan.font_size = 12;
    u.backgrounded_text({right - 302, 23, 87, 26}, "Vulkan", vulkan);
    d.circle({right - 287, 36}, 3, t.positive);
}
void fluid_test::sidebar(float side) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    d.rect({0, 0, side, height()}, t.panel);
    d.line({side - 1, 0}, {side - 1, height()}, t.border.opacity(.55f));
    d.rect({24, 24, 31, 31}, t.accent, 10);
    d.line({32, 43}, {43, 32}, t.accent_text, 4);
    d.line({39, 48}, {48, 39}, t.accent_text, 4);
    d.text({65, 22}, "fluid", 27, t.text, true);
    d.text({125, 32}, "STUDIO", 9, t.muted, true);
    d.text({26, 103}, "PLAYGROUND", 10, t.muted, true);
    const std::string_view pages[]{"Overview", "Components", "Typography", "Motion"};
    const float nav_y = 130, nav_h = 40, nav_gap = 6;
    SelectionListStyle nav;
    nav.gap = nav_gap;
    nav.item.align = TextAlign::left;
    nav.item.padding = 43;
    nav.item.font_size = 14;
    nav.item.text = t.muted;
    nav.item.radius = 8;
    nav.selected = nav.item;
    nav.selected.background = t.accent.opacity(dark_ ? 0.12f : 0.1f);
    nav.selected.text = t.accent;
    nav.selected.bold = true;
    nav.hover_background = t.elevated;
    u.selection_list("nav", {14, nav_y, side - 28, 4 * nav_h + 3 * nav_gap}, nav_h, pages, page_, nav);
    for (int i = 0; i < 4; ++i) {
        const float y = nav_y + i * (nav_h + nav_gap);
        icon({28, y + (nav_h - 17) * 0.5f}, i, page_ == i ? t.accent : t.muted, 17);
        if (page_ == i)
            d.rect({14, y + 12, 3, 16}, t.accent, 1.5f);
    }
    d.line({24, 331}, {side - 24, 331}, t.border.opacity(.6f));
    d.text({26, 358}, "SCENE COLLECTION", 10, t.muted, true);
    const std::string_view models[]{"Torus knot", "UV sphere", "Torus", "Cube"};
    const float list_y = 386, list_h = 34, list_gap = 5;
    SelectionListStyle collection;
    collection.gap = list_gap;
    collection.item.align = TextAlign::left;
    collection.item.padding = 42;
    collection.item.font_size = 13;
    collection.item.text = t.muted;
    collection.item.radius = 7;
    collection.selected = collection.item;
    collection.selected.text = t.text;
    collection.hover_background = t.elevated;
    if (u.selection_list("collection", {14, list_y, side - 28, 4 * list_h + 3 * list_gap}, list_h, models,
                         model_index_, collection)) {
        select_model(model_index_);
        page_ = 0;
    }
    for (int i = 0; i < 4; ++i) {
        const float y = list_y + i * (list_h + list_gap);
        icon({29, y + (list_h - 14) * 0.5f}, 4, model_index_ == i ? t.accent : t.muted.opacity(.7f), 14);
        if (model_index_ == i)
            d.circle({side - 30, y + list_h * 0.5f}, 3, t.accent);
    }
    const float y = height() - 181;
    d.gradient({18, y, side - 36, 100}, blend(t.panel, t.accent, .1f), t.panel, 11);
    d.outline({18, y, side - 36, 100}, t.border, 11);
    d.text({32, y + 16}, "Small library.", 15, t.text, true);
    d.text({32, y + 37}, "Big possibilities.", 15, t.text, true);
    d.text({32, y + 70}, "2D interface. 3D imagination.", 10, t.muted);
    d.circle({33, height() - 44}, 12, t.accent.opacity(.16f));
    d.text({27, height() - 53}, "F", 14, t.accent, true);
    d.text({55, height() - 56}, "Fluid engine", 12, t.text, true);
    d.text({55, height() - 37}, "v0.1 / native C++", 10, t.muted);
}
void fluid_test::mini_components(Rect b) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    float gap = 16, cw = (b.w - gap) / 2;
    u.panel({b.x, b.y, cw, b.h});
    u.panel({b.x + cw + gap, b.y, cw, b.h});
    d.text({b.x + 20, b.y + 18}, "Designed to interact", 16, t.text, true);
    d.text({b.x + 20, b.y + 45}, "Thoughtful details, in every control.", 12, t.muted);
    if (u.button("try-button", {b.x + 20, b.y + 80, 116, 35}, "Try a button", true)) {
        ++click_count_;
        toast("That felt good. Button pressed " + std::to_string(click_count_) + " times.");
    }
    u.toggle("mini-toggle", {b.x + cw - 64, b.y + 85, 40, 22}, demo_enabled_);
    d.text({b.x + 20, b.y + b.h - 30}, demo_enabled_ ? "Interactive by default" : "Toggle switched off", 11,
           t.muted);
    const float x = b.x + cw + gap;
    d.text({x + 20, b.y + 18}, "A smoother workflow", 16, t.text, true);
    d.text({x + 20, b.y + 45}, "Adjust a value. See it respond.", 12, t.muted);
    u.slider("mini-progress", {x + 20, b.y + 81, cw - 40, 24}, progress_);
    d.text({x + 20, b.y + b.h - 30}, "Progress", 11, t.muted);
    auto percent = std::to_string(int(progress_ * 100)) + "%";
    d.text({x + cw - 20 - d.text_width(percent, 12), b.y + b.h - 31}, percent, 12, t.accent, true);
}
void fluid_test::overview(Rect c) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    section_title({c.x, c.y}, "Everything, in its element.",
                  "A native canvas for beautiful interfaces and a new dimension of interaction.");
    const float inspector_w = 268, gap = 20, main_w = c.w - inspector_w - gap;
    Rect card{c.x, c.y + 94, main_w, c.h - 94 - 194};
    u.panel(card, 14);
    d.text({card.x + 20, card.y + 18}, "Scene preview", 15, t.text, true);
    BackgroundedTextStyle live;
    live.background = t.positive.opacity(0.12f);
    live.text = t.positive;
    live.align = TextAlign::center;
    live.font_size = 9;
    live.bold = true;
    live.radius = 5;
    live.padding = 4;
    u.backgrounded_text({card.x + 137, card.y + 17, 39, 21}, "LIVE", live);
    if (u.button("solid", {card.x + card.w - 166, card.y + 12, 69, 30}, "Solid", false, !wireframe_))
        wireframe_ = false;
    if (u.button("wire", {card.x + card.w - 91, card.y + 12, 77, 30}, "Wireframe", false, wireframe_))
        wireframe_ = true;
    d.line({card.x, card.y + 54}, {card.x + card.w, card.y + 54}, t.border.opacity(.6f));
    viewport_ = {card.x + 1, card.y + 55, card.w - 2, card.h - 101};
    d.gradient(viewport_, dark_ ? Color::hex(0x232431) : Color::hex(0xECE8F5),
               dark_ ? Color::hex(0x171A21) : Color::hex(0xE1E6EF));
    if ((input().mouse_pressed || input().right_pressed) && viewport_.contains(input().mouse))
        orbiting_ = true;
    if (!input().mouse_down && !input().right_down)
        orbiting_ = false;
    if (orbiting_)
        camera_.orbit(input().mouse_delta);
    if (viewport_.contains(input().mouse)) {
        camera_.zoom(input().scroll);
    }
    SceneView scene;
    scene.bounds = viewport_;
    scene.mesh = model_index_ < 4 ? &meshes_[model_index_] : &imported_;
    scene.camera = camera_;
    scene.rotation = rotation_;
    scene.tint = swatches[swatch_];
    scene.metallic = metallic_;
    scene.roughness = roughness_;
    scene.exposure = exposure_;
    scene.wireframe = wireframe_;
    scene.grid = grid_;
    d.scene(scene);
    d.push_clip(viewport_);
    BackgroundedTextStyle perspective;
    perspective.background = t.background.opacity(0.55f);
    perspective.text = t.text.opacity(0.85f);
    perspective.align = TextAlign::left;
    perspective.padding = 24;
    perspective.font_size = 11;
    perspective.radius = 6;
    u.backgrounded_text({viewport_.x + 18, viewport_.y + 18, 112, 27}, "Perspective", perspective);
    d.circle({viewport_.x + 31, viewport_.y + 31.5f}, 3, t.positive);
    d.text({viewport_.x + 19, viewport_.y + viewport_.h - 30}, "Drag to orbit  /  Scroll to zoom", 11,
           t.muted);
    const float ax = viewport_.x + viewport_.w - 44, ay = viewport_.y + viewport_.h - 42;
    d.line({ax, ay}, {ax + 20, ay + 6}, Color::hex(0xD38C9C), 1.6f);
    d.text({ax + 22, ay}, "X", 9, Color::hex(0xD38C9C));
    d.line({ax, ay}, {ax, ay - 24}, Color::hex(0xA2CDA8), 1.6f);
    d.text({ax - 3, ay - 38}, "Y", 9, Color::hex(0xA2CDA8));
    d.line({ax, ay}, {ax - 17, ay + 12}, Color::hex(0x91B7EC), 1.6f);
    d.text({ax - 28, ay + 7}, "Z", 9, Color::hex(0x91B7EC));
    d.pop_clip();
    const float fy = card.y + card.h - 45;
    d.line({card.x, fy}, {card.x + card.w, fy}, t.border.opacity(.6f));
    icon({card.x + 19, fy + 15}, 4, t.muted, 14);
    const auto tri = std::to_string(scene.mesh->indices.size() / 3) + " triangles";
    const float triangle_x = card.x + card.w - d.text_width(tri, 11) - 19;
    d.push_clip({card.x + 44, fy + 5, std::max(0.f, triangle_x - card.x - 60), 35});
    d.text({card.x + 44, fy + 13}, model_name_, 12, t.text);
    d.pop_clip();
    d.text({triangle_x, fy + 14}, tri, 11, t.muted);
    mini_components({c.x, card.y + card.h + 18, main_w, 176});
    inspector({c.x + main_w + gap, c.y + 94, inspector_w, c.h - 94});
}
void fluid_test::inspector(Rect b) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    u.panel(b, 14);
    d.push_clip({b.x + 1, b.y + 1, b.w - 2, b.h - 2});
    if (b.contains(input().mouse))
        inspector_scroll_ -= input().scroll * 28;
    inspector_scroll_ = std::clamp(inspector_scroll_, 0.f, std::max(0.f, 820.f - b.h));
    const float x = b.x + 20, w = b.w - 40;
    float y = b.y + 19 - inspector_scroll_;
    d.text({x, y}, "Scene properties", 15, t.text, true);
    y += 38;
    d.text({x, y}, "OBJECT NAME", 10, t.muted, true);
    y += 22;
    u.text_field("object-name", {x, y, w, 34}, model_name_, "Untitled object");
    y += 56;
    d.text({x, y}, "MATERIAL", 10, t.muted, true);
    y += 27;
    for (int i = 0; i < 5; ++i) {
        const Rect r{x + float(i) * 43, y, 32, 32};
        if (u.hit("swatch" + std::to_string(i), r))
            swatch_ = i;
        d.circle({r.x + 16, r.y + 16}, 12, swatches[i]);
        if (swatch_ == i) {
            d.outline({r.x, r.y, 32, 32}, t.text.opacity(.8f), 16);
            d.circle({r.x + 16, r.y + 16}, 3, Color::hex(0x292535));
        }
    }
    y += 57;
    auto property = [&](const char *id, const char *label, float &value, float lo, float hi) {
        d.text({x, y}, label, 12, t.muted);
        auto v = decimal(value);
        d.text({x + w - d.text_width(v, 12), y}, v, 12, t.text);
        y += 24;
        u.slider(id, {x, y, w, 18}, value, lo, hi);
        y += 40;
    };
    property("roughness", "Roughness", roughness_, .05f, 1);
    property("metallic", "Metallic", metallic_, 0, 1);
    property("exposure", "Exposure", exposure_, .4f, 2);
    d.line({x, y - 7}, {x + w, y - 7}, t.border.opacity(.6f));
    y += 13;
    d.text({x, y + 3}, "Auto-rotate", 12, t.text);
    u.toggle("rotate", {x + w - 38, y, 38, 21}, auto_rotate_);
    y += 38;
    d.text({x, y + 3}, "Ground grid", 12, t.text);
    u.toggle("grid", {x + w - 38, y, 38, 21}, grid_);
    y += 38;
    d.text({x, y}, "GRAPHICS", 10, t.muted, true);
    y += 22;
    d.text({x, y + 3}, "Scene anti-aliasing", 12, t.text);
    bool aa = scene_aa();
    u.toggle("scene-aa", {x + w - 38, y, 38, 21}, aa);
    set_scene_aa(aa);
    y += 38;
    d.text({x, y + 3}, "Ray tracing", 12, t.text);
    bool rt = ray_tracing();
    u.toggle("ray-tracing", {x + w - 38, y, 38, 21}, rt);
    set_ray_tracing(rt);
    y += 28;
    d.text({x, y}, ray_tracing_available() ? "Hardware RT, all 3D views" : "Unavailable on this GPU", 10,
           t.muted);
    y += 28;
    d.text({x, y + 3}, "DLSS", 12, t.text);
    bool dl = dlss();
    u.toggle("dlss", {x + w - 38, y, 38, 21}, dl);
    set_dlss(dl);
    y += 47;
    if (u.button("reset-camera", {x, y, w, 34}, "Reset camera")) {
        camera_.reset();
        camera_.distance = 3.6f;
        rotation_ = 0;
        toast("Camera reset to its starting position.");
    }
    y += 55;
    d.text({x, y}, "BRING YOUR OWN MODEL", 10, t.muted, true);
    y += 22;
    u.text_field("import-path", {x, y, w - 62, 32}, import_path_, "Path to .obj");
    if (u.button("import", {x + w - 56, y, 56, 32}, "Load"))
        import_model(import_path_);
    y += 42;
    d.text({x, y}, "Or drop an OBJ file into the window.", 10, t.muted);
    d.pop_clip();
    if (b.h < 820) {
        const float track = b.h - 24, thumb = track * b.h / 820;
        d.rect({b.x + b.w - 6, b.y + 12, 2, track}, t.border.opacity(.3f), 1);
        d.rect({b.x + b.w - 6, b.y + 12 + (track - thumb) * inspector_scroll_ / (820 - b.h), 2, thumb},
               t.muted.opacity(.65f), 1);
    }
}
void fluid_test::components(Rect c) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    section_title({c.x, c.y}, "Details make the difference.",
                  "A small, expressive set of controls. Every example below is live.");
    float gap = 20, cw = (c.w - gap) / 2, rh = (c.h - 114) / 2;
    Rect a{c.x, c.y + 94, cw, rh}, b{c.x + cw + gap, c.y + 94, cw, rh};
    Rect cc{c.x, c.y + 114 + rh, cw, rh}, e{c.x + cw + gap, c.y + 114 + rh, cw, rh};
    for (auto r : {a, b, cc, e})
        u.panel(r, 14);
    d.text({a.x + 24, a.y + 22}, "01   Buttons & actions", 18, t.text, true);
    d.text({a.x + 24, a.y + 55}, "Hover, press, release. Or navigate with Tab.", 13, t.muted);
    if (u.button("primary-demo", {a.x + 24, a.y + 101, 134, 40}, "Primary action", true))
        ++click_count_;
    if (u.button("secondary-demo", {a.x + 170, a.y + 101, 134, 40}, "Secondary"))
        ++click_count_;
    if (u.button("reset-count", {a.x + 24, a.y + 160, 106, 34}, "Reset count"))
        click_count_ = 0;
    d.text({a.x + 146, a.y + 168}, std::to_string(click_count_) + " interactions", 13, t.accent);
    d.text({a.x + 24, a.y + a.h - 35}, "Keyboard focus is part of the experience.", 12, t.muted);
    d.text({b.x + 24, b.y + 22}, "02   Switches & preferences", 18, t.text, true);
    d.text({b.x + 24, b.y + 55}, "Simple state, immediately reflected.", 13, t.muted);
    d.text({b.x + 24, b.y + 108}, "Enable notifications", 14, t.text);
    u.toggle("notifications", {b.x + b.w - 70, b.y + 106, 42, 23}, notifications_);
    d.text({b.x + 24, b.y + 157}, "Dark appearance", 14, t.text);
    u.toggle("dark-mode", {b.x + b.w - 70, b.y + 155, 42, 23}, dark_);
    d.text({b.x + 24, b.y + 206}, "Animate scene", 14, t.text);
    u.toggle("animate-pref", {b.x + b.w - 70, b.y + 204, 42, 23}, auto_rotate_);
    d.circle({b.x + 29, b.y + b.h - 28}, 3, notifications_ ? t.positive : t.muted);
    d.text({b.x + 43, b.y + b.h - 36},
           notifications_ ? "Notifications are enabled" : "Notifications are paused", 12, t.muted);
    d.text({cc.x + 24, cc.y + 22}, "03   Text & focus", 18, t.text, true);
    d.text({cc.x + 24, cc.y + 55}, "Click to edit. Type something that feels like you.", 13, t.muted);
    d.text({cc.x + 24, cc.y + 98}, "YOUR MESSAGE", 10, t.muted, true);
    u.text_field("demo-note", {cc.x + 24, cc.y + 122, cc.w - 48, 42}, note_, "Write a little something...");
    d.push_clip({cc.x + 24, cc.y + 192, cc.w - 48, 50});
    d.text({cc.x + 24, cc.y + 196}, note_.empty() ? "Your words, here." : note_, 20, t.accent, true);
    d.pop_clip();
    d.text({cc.x + 24, cc.y + cc.h - 35}, "UTF-8 text input  /  Backspace to edit", 12, t.muted);
    d.text({e.x + 24, e.y + 22}, "04   A continuous response", 18, t.text, true);
    d.text({e.x + 24, e.y + 55}, "Drag the slider, or use the arrow keys.", 13, t.muted);
    d.text({e.x + 24, e.y + 103}, "Completion", 13, t.muted);
    d.text({e.x + e.w - 85, e.y + 94}, std::to_string(int(progress_ * 100)) + "%", 26, t.accent, true);
    u.slider("component-value", {e.x + 24, e.y + 152, e.w - 48, 24}, progress_);
    u.progress({e.x + 24, e.y + 208, e.w - 48, 8}, progress_, t.accent);
    d.text({e.x + 24, e.y + e.h - 35}, "Values stay in sync across the playground.", 12, t.muted);
}
void fluid_test::typography(Rect c) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    section_title({c.x, c.y}, "Words with room to breathe.",
                  "Crisp, atlas-rendered type with a clear hierarchy and a human touch.");
    Rect a{c.x, c.y + 94, c.w, c.h - 94};
    u.panel(a, 14);
    d.text({a.x + 28, a.y + 25}, "TYPE PLAYGROUND", 10, t.muted, true);
    u.text_field("type-sample", {a.x + 28, a.y + 57, a.w - 56, 42}, type_sample_,
                 "Enter your own preview text");
    d.text({a.x + 28, a.y + 127}, "Preview size", 12, t.muted);
    u.slider("font-size", {a.x + 129, a.y + 121, std::min(300.f, a.w - 250), 24}, font_size_, 18, 64);
    d.text({a.x + 450, a.y + 126}, std::to_string(int(font_size_)) + " px", 12, t.accent);
    d.push_clip({a.x + 28, a.y + 181, a.w - 56, 110});
    d.text({a.x + 28, a.y + 191}, type_sample_, font_size_, t.text, true);
    d.pop_clip();
    d.line({a.x + 28, a.y + 307}, {a.x + a.w - 28, a.y + 307}, t.border);
    const float x = a.x + 28, tx = a.x + 160;
    float y = a.y + std::min(339.f, a.h - 255.f);
    d.text({x, y + 6}, "DISPLAY / 32", 10, t.muted, true);
    d.text({tx, y}, "Create with clarity.", 32, t.text, true);
    y += 66;
    d.text({x, y + 3}, "HEADING / 22", 10, t.muted, true);
    d.text({tx, y}, "An interface that feels effortless.", 22, t.text, true);
    y += 57;
    d.text({x, y + 2}, "BODY / 15", 10, t.muted, true);
    d.text({tx, y}, "Give every idea a beautiful place to live.", 15, t.muted);
    y += 47;
    d.text({x, y}, "CAPTION / 12", 10, t.muted, true);
    d.text({tx, y}, "Made with Fluid. Rendered in real time.", 12, t.muted);
    d.rect({x, a.y + a.h - 59, a.w - 56, 1}, t.border.opacity(.5f));
    d.text({x, a.y + a.h - 38}, "Regular + semibold   /   Scalable text   /   Alpha-blended glyph atlas", 12,
           t.muted);
}
void fluid_test::motion(Rect c) {
    auto &u = ui();
    Painter d{u};
    const auto &t = u.theme();
    section_title({c.x, c.y}, "An interface with a pulse.",
                  "Time-based motion and layered 2D geometry, drawn by Fluid in real time.");
    Rect a{c.x, c.y + 94, c.w, c.h - 94};
    u.panel(a, 14);
    if (u.button("motion-play", {a.x + 24, a.y + 20, 92, 34}, motion_playing_ ? "Pause" : "Play", true))
        motion_playing_ = !motion_playing_;
    if (u.button("motion-reset", {a.x + 127, a.y + 20, 82, 34}, "Restart"))
        motion_time_ = 0;
    d.text({a.x + a.w - 330, a.y + 29}, "Speed", 12, t.muted);
    u.slider("motion-speed", {a.x + a.w - 270, a.y + 25, 180, 22}, motion_speed_, .1f, 3);
    d.text({a.x + a.w - 72, a.y + 29}, decimal(motion_speed_, 1) + "x", 12, t.accent);
    Rect stage{a.x + 1, a.y + 75, a.w - 2, a.h - 132};
    d.push_clip(stage);
    d.gradient(stage, t.background, t.panel);
    Vec2 center{stage.x + stage.w / 2, stage.y + stage.h / 2};
    float radius = std::min(stage.w * .28f, stage.h * .31f);
    for (int j = 3; j >= 0; --j) {
        float rr = radius * (.52f + float(j) * .23f);
        d.outline({center.x - rr, center.y - rr, rr * 2, rr * 2}, t.border.opacity(.45f), rr);
    }
    for (int i = 0; i < 7; ++i) {
        float angle = motion_time_ * (.35f + float(i % 3) * .1f) + float(i) * pi * 2 / 7;
        float orbit = radius * (.75f + float(i % 3) * .22f);
        Vec2 p{center.x + std::cos(angle) * orbit, center.y + std::sin(angle) * orbit * .75f};
        float rr = 12 + float(i % 3) * 7;
        for (int k = 4; k > 0; --k)
            d.circle(p, rr + float(k) * 8, swatches[i % 5].opacity(.018f));
        d.circle(p, rr, swatches[i % 5].opacity(.9f));
        d.circle({p.x - rr * .25f, p.y - rr * .3f}, rr * .18f, Color{}.opacity(.38f));
    }
    d.circle(center, 38, t.accent.opacity(.12f));
    d.circle(center, 23, t.accent);
    d.line({center.x - 8, center.y + 6}, {center.x + 6, center.y - 8}, t.accent_text, 5);
    d.line({center.x - 1, center.y + 12}, {center.x + 11, center.y}, t.accent_text, 5);
    d.pop_clip();
    d.text({a.x + 24, a.y + a.h - 34}, "Seven particles. One shared clock. Endless combinations.", 12,
           t.muted);
    d.text({a.x + a.w - 115, a.y + a.h - 34}, motion_playing_ ? "IN MOTION" : "PAUSED", 10,
           motion_playing_ ? t.positive : t.muted, true);
}
void fluid_test::tick() {
    ++frame_;
    if (options_.smoke_test) {
        if (frame_ == 2)
            glfwSetWindowSize(native_handle(), 1200, 860);
        if (frame_ >= 3 && frame_ <= 6)
            page_ = int(frame_ - 3);
        if (frame_ == 7) {
            page_ = 0;
            select_model(1);
            wireframe_ = true;
        }
        if (frame_ == 8) {
            select_model(2);
            dark_ = false;
        }
        if (frame_ == 9) {
            select_model(3);
            grid_ = false;
        }
        if (frame_ == 10) {
            select_model(0);
            wireframe_ = false;
            grid_ = true;
            dark_ = true;
            glfwSetWindowSize(native_handle(), 1440, 960);
        }
    }
    ui().theme() = dark_ ? Theme::dark() : Theme::light();
    if (page_ != 0 || (!input().mouse_down && !input().right_down))
        orbiting_ = false;
    const bool starting_orbit =
        page_ == 0 && viewport_.contains(input().mouse) && (input().mouse_down || input().right_down);
    if (auto_rotate_ && !orbiting_ && !starting_orbit)
        rotation_ += delta_time() * .24f;
    if (motion_playing_)
        motion_time_ += delta_time() * motion_speed_;
    if (delta_time() > 0)
        fps_ = fps_ * .95f + std::min(1000.f, 1.f / delta_time()) * .05f;
    for (const auto &path : input().dropped_paths)
        import_model(path);
    Painter d{ui()};
    const auto &t = ui().theme();
    d.rect({0, 0, width(), height()}, t.background);
    const float side = 208;
    sidebar(side);
    topbar(side);
    Rect content{side + 32, 102, width() - side - 64, height() - 150};
    if (page_ == 0)
        overview(content);
    else if (page_ == 1)
        components(content);
    else if (page_ == 2)
        typography(content);
    else
        motion(content);
    d.line({side, height() - 27}, {width(), height() - 27}, t.border.opacity(.5f));
    d.circle({side + 20, height() - 13}, 3, t.positive);
    d.text({side + 31, height() - 21}, "All systems fluid", 10, t.muted);
    const auto stats = std::to_string(int(fps_)) + " fps  /  Vulkan renderer";
    d.text({width() - 24 - d.text_width(stats, 10), height() - 21}, stats, 10, t.muted);
    if (float(elapsed_time()) < toast_until_) {
        const float tw = std::min(width() - 80, d.text_width(toast_message_, 13) + 48);
        Rect r{(width() - tw) / 2, height() - 83, tw, 41};
        d.rect({r.x, r.y + 4, r.w, r.h}, Color::hex(0, .25f), 10);
        d.rect(r, t.elevated, 10);
        d.outline(r, t.border, 10);
        d.push_clip({r.x + 16, r.y + 5, r.w - 32, r.h - 10});
        d.text({r.x + 20, r.y + 11}, toast_message_, 13, t.text);
        d.pop_clip();
    }
    if (!options_.screenshot_path.empty() && frame_ == (options_.frames ? options_.frames : 1))
        screenshot(options_.screenshot_path);
}
