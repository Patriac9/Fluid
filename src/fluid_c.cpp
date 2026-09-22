#include "fluid/capi.h"
#include <fluid/fluid.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

struct Fluid_Mesh {
    Fluid::Mesh impl;
};

struct Fluid_Window : Fluid::window {
    using Fluid::window::window;
    Fluid_TickFn tick_fn = nullptr;
    void *tick_user = nullptr;
    mutable std::string text_cache;
    mutable std::vector<std::string> dropped_owned;
    mutable std::vector<const char *> dropped_ptrs;

  protected:
    void tick() override {
        if (tick_fn)
            tick_fn(this, tick_user);
    }
};

namespace {
thread_local std::string g_error;

void clear_error() { g_error.clear(); }

void set_error(const char *text) { g_error = text ? text : "unknown error"; }

void set_error(const std::exception &error) { g_error = error.what(); }

template <typename T>
T *ok(T *value) {
    clear_error();
    return value;
}

int ok_bool(bool value) {
    clear_error();
    return value ? 1 : 0;
}

const char *empty_if_null(const char *text) { return text ? text : ""; }

std::filesystem::path utf8_path(const char *path) {
    const std::string text = empty_if_null(path);
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

Fluid::Vec2 vec(Fluid_Vec2 value) { return {value.x, value.y}; }
Fluid_Vec2 vec(Fluid::Vec2 value) { return {value.x, value.y}; }
Fluid::Rect rect(Fluid_Rect value) { return {value.x, value.y, value.w, value.h}; }
Fluid_Rect rect(Fluid::Rect value) { return {value.x, value.y, value.w, value.h}; }
Fluid::Color color(Fluid_Color value) { return {value.r, value.g, value.b, value.a}; }
Fluid_Color color(Fluid::Color value) { return {value.r, value.g, value.b, value.a}; }

Fluid::Theme theme(Fluid_Theme value) {
    Fluid::Theme result;
    result.background = color(value.background);
    result.panel = color(value.panel);
    result.elevated = color(value.elevated);
    result.border = color(value.border);
    result.text = color(value.text);
    result.muted = color(value.muted);
    result.accent = color(value.accent);
    result.accent_text = color(value.accent_text);
    result.positive = color(value.positive);
    return result;
}

Fluid_Theme theme(const Fluid::Theme &value) {
    return {color(value.background), color(value.panel),     color(value.elevated),
            color(value.border),     color(value.text),      color(value.muted),
            color(value.accent),     color(value.accent_text), color(value.positive)};
}

Fluid::OrbitCamera camera(Fluid_OrbitCamera value) { return {value.yaw, value.pitch, value.distance}; }
Fluid_OrbitCamera camera(const Fluid::OrbitCamera &value) {
    return {value.yaw, value.pitch, value.distance};
}

void copy_mat4(const glm::mat4 &matrix, float *out) {
    if (!out)
        return;
    const float *ptr = glm::value_ptr(matrix);
    std::memcpy(out, ptr, sizeof(float) * 16);
}

const Fluid::Mesh *owned(const Fluid_Mesh *mesh) { return mesh ? &mesh->impl : nullptr; }

Fluid::SceneView scene(const Fluid_SceneView &value) {
    Fluid::SceneView result;
    result.bounds = rect(value.bounds);
    result.mesh = owned(value.mesh);
    result.camera = camera(value.camera);
    result.tint = color(value.tint);
    result.rotation = value.rotation;
    result.metallic = value.metallic;
    result.roughness = value.roughness;
    result.exposure = value.exposure;
    result.wireframe = value.wireframe != 0;
    result.grid = value.grid != 0;
    return result;
}

Fluid_Mesh *wrap_mesh(Fluid::Mesh &&mesh) {
    try {
        return ok(new Fluid_Mesh{std::move(mesh)});
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}
} // namespace

extern "C" {

const char *fluid_version(void) {
    return "0.1.0";
}

const char *fluid_platform(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const char *fluid_last_error(void) {
    return g_error.c_str();
}

Fluid_Color fluid_color_hex(uint32_t rgb, float alpha) {
    return color(Fluid::Color::hex(rgb, alpha));
}

Fluid_Color fluid_color_opacity(Fluid_Color value, float amount) {
    return color(color(value).opacity(amount));
}

int fluid_rect_contains(Fluid_Rect bounds, Fluid_Vec2 point) {
    return rect(bounds).contains(vec(point)) ? 1 : 0;
}

Fluid_Theme fluid_theme_dark(void) {
    return theme(Fluid::Theme::dark());
}

Fluid_Theme fluid_theme_light(void) {
    return theme(Fluid::Theme::light());
}

Fluid_OrbitCamera fluid_camera_default(void) {
    return camera(Fluid::OrbitCamera{});
}

void fluid_camera_orbit(Fluid_OrbitCamera *cam, Fluid_Vec2 delta) {
    if (!cam)
        return;
    auto value = camera(*cam);
    value.orbit(vec(delta));
    *cam = camera(value);
}

void fluid_camera_zoom(Fluid_OrbitCamera *cam, float scroll) {
    if (!cam)
        return;
    auto value = camera(*cam);
    value.zoom(scroll);
    *cam = camera(value);
}

void fluid_camera_reset(Fluid_OrbitCamera *cam) {
    if (!cam)
        return;
    *cam = fluid_camera_default();
}

void fluid_camera_view(const Fluid_OrbitCamera *cam, float out_mat4[16]) {
    if (!cam)
        return;
    copy_mat4(camera(*cam).view(), out_mat4);
}

void fluid_camera_projection(const Fluid_OrbitCamera *cam, float aspect, float out_mat4[16]) {
    if (!cam)
        return;
    copy_mat4(camera(*cam).projection(aspect), out_mat4);
}

Fluid_SceneView fluid_scene_view_default(void) {
    const auto value = Fluid::SceneView{};
    Fluid_SceneView result{};
    result.bounds = rect(value.bounds);
    result.mesh = nullptr;
    result.camera = camera(value.camera);
    result.tint = color(value.tint);
    result.rotation = value.rotation;
    result.metallic = value.metallic;
    result.roughness = value.roughness;
    result.exposure = value.exposure;
    result.wireframe = value.wireframe ? 1 : 0;
    result.grid = value.grid ? 1 : 0;
    return result;
}

Fluid_Mesh *fluid_mesh_sphere(uint32_t segments, uint32_t rings) {
    try {
        return wrap_mesh(Fluid::Mesh::sphere(segments, rings));
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}

Fluid_Mesh *fluid_mesh_torus(uint32_t segments, uint32_t sides) {
    try {
        return wrap_mesh(Fluid::Mesh::torus(segments, sides));
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}

Fluid_Mesh *fluid_mesh_knot(uint32_t segments, uint32_t sides) {
    try {
        return wrap_mesh(Fluid::Mesh::knot(segments, sides));
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}

Fluid_Mesh *fluid_mesh_cube(void) {
    try {
        return wrap_mesh(Fluid::Mesh::cube());
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}

Fluid_Mesh *fluid_mesh_load_obj(const char *path) {
    try {
        if (!path || !path[0]) {
            set_error("OBJ path must not be empty");
            return nullptr;
        }
        return wrap_mesh(Fluid::Mesh::load_obj(utf8_path(path)));
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}

void fluid_mesh_destroy(Fluid_Mesh *mesh) {
    delete mesh;
}

const char *fluid_mesh_name(const Fluid_Mesh *mesh) {
    return mesh ? mesh->impl.name.c_str() : "";
}

uint32_t fluid_mesh_vertex_count(const Fluid_Mesh *mesh) {
    return mesh ? static_cast<uint32_t>(mesh->impl.vertices.size()) : 0;
}

uint32_t fluid_mesh_index_count(const Fluid_Mesh *mesh) {
    return mesh ? static_cast<uint32_t>(mesh->impl.indices.size()) : 0;
}

int fluid_mesh_normalize(Fluid_Mesh *mesh) {
    if (!mesh) {
        set_error("null mesh");
        return 0;
    }
    try {
        mesh->impl.normalize();
        clear_error();
        return 1;
    } catch (const std::exception &error) {
        set_error(error);
        return 0;
    }
}

Fluid_WindowConfig fluid_window_config_default(void) {
    return {0, 1, 1440, 960, "Fluid", 1};
}

Fluid_AppConfig fluid_app_config_default(void) {
    static const Fluid_AppConfig config{"Fluid", {1, 0, 0}, "Fluid", {1, 0, 0}};
    return config;
}

Fluid_Window *fluid_window_create(const Fluid_WindowConfig *config) {
    try {
        window_cfg cfg;
        if (config) {
            cfg.fullscreen = config->fullscreen != 0;
            cfg.resizable = config->resizable != 0;
            cfg.width = config->width;
            cfg.height = config->height;
            if (config->title)
                cfg.window_title = config->title;
            cfg.visible = config->visible != 0;
        }
        return ok(new Fluid_Window(std::move(cfg)));
    } catch (const std::exception &error) {
        set_error(error);
        return nullptr;
    }
}

void fluid_window_destroy(Fluid_Window *window) {
    delete window;
}

void fluid_window_set_tick(Fluid_Window *window, Fluid_TickFn tick, void *user) {
    if (!window)
        return;
    window->tick_fn = tick;
    window->tick_user = user;
}

int fluid_window_init(Fluid_Window *window, const Fluid_AppConfig *config) {
    if (!window) {
        set_error("null window");
        return 0;
    }
    try {
        application_cfg app;
        if (config) {
            if (config->app_name)
                app.app_name = config->app_name;
            app.app_version = {config->app_version[0], config->app_version[1], config->app_version[2]};
            if (config->engine_name)
                app.engine_name = config->engine_name;
            app.engine_version = {config->engine_version[0], config->engine_version[1],
                                  config->engine_version[2]};
        }
        window->Init(app);
        clear_error();
        return 1;
    } catch (const std::exception &error) {
        set_error(error);
        return 0;
    }
}

int fluid_window_loop(Fluid_Window *window) {
    if (!window) {
        set_error("null window");
        return 0;
    }
    try {
        window->loop();
        clear_error();
        return 1;
    } catch (const std::exception &error) {
        set_error(error);
        return 0;
    }
}

Fluid_Ui *fluid_window_ui(Fluid_Window *window) {
    return window ? reinterpret_cast<Fluid_Ui *>(&window->ui()) : nullptr;
}

int fluid_window_input(Fluid_Window *window, Fluid_Input *out) {
    if (!window || !out) {
        set_error("null window or input");
        return 0;
    }
    const auto &in = window->input();
    out->mouse = vec(in.mouse);
    out->mouse_delta = vec(in.mouse_delta);
    out->mouse_down = in.mouse_down ? 1 : 0;
    out->mouse_pressed = in.mouse_pressed ? 1 : 0;
    out->mouse_released = in.mouse_released ? 1 : 0;
    out->right_down = in.right_down ? 1 : 0;
    out->right_pressed = in.right_pressed ? 1 : 0;
    out->scroll = in.scroll;
    for (int i = 0; i < FLUID_KEY_COUNT; ++i) {
        out->keys_down[i] = in.keys_down[static_cast<size_t>(i)] ? 1 : 0;
        out->keys_pressed[i] = in.keys_pressed[static_cast<size_t>(i)] ? 1 : 0;
    }
    window->text_cache = in.text;
    out->text = window->text_cache.c_str();
    window->dropped_owned = in.dropped_paths;
    window->dropped_ptrs.clear();
    window->dropped_ptrs.reserve(window->dropped_owned.size());
    for (const auto &path : window->dropped_owned)
        window->dropped_ptrs.push_back(path.c_str());
    out->dropped_paths = window->dropped_ptrs.empty() ? nullptr : window->dropped_ptrs.data();
    out->dropped_count = static_cast<int>(window->dropped_ptrs.size());
    clear_error();
    return 1;
}

void *fluid_window_native_handle(const Fluid_Window *window) {
    return window ? static_cast<void *>(window->native_handle()) : nullptr;
}

float fluid_window_width(const Fluid_Window *window) {
    return window ? window->width() : 0;
}

float fluid_window_height(const Fluid_Window *window) {
    return window ? window->height() : 0;
}

float fluid_window_delta_time(const Fluid_Window *window) {
    return window ? window->delta_time() : 0;
}

double fluid_window_elapsed_time(const Fluid_Window *window) {
    return window ? window->elapsed_time() : 0;
}

const char *fluid_window_device_name(const Fluid_Window *window) {
    return window ? window->device_name().c_str() : "";
}

void fluid_window_set_frame_limit(Fluid_Window *window, uint32_t frames) {
    if (window)
        window->set_frame_limit(frames);
}

int fluid_window_screenshot(Fluid_Window *window, const char *path) {
    if (!window) {
        set_error("null window");
        return 0;
    }
    try {
        window->screenshot(empty_if_null(path));
        clear_error();
        return 1;
    } catch (const std::exception &error) {
        set_error(error);
        return 0;
    }
}

void fluid_window_set_scene_aa(Fluid_Window *window, int enabled) {
    if (window)
        window->set_scene_aa(enabled != 0);
}

int fluid_window_scene_aa(const Fluid_Window *window) {
    return window && window->scene_aa() ? 1 : 0;
}

void fluid_window_set_ray_tracing(Fluid_Window *window, int enabled) {
    if (window)
        window->set_ray_tracing(enabled != 0);
}

int fluid_window_ray_tracing(const Fluid_Window *window) {
    return window && window->ray_tracing() ? 1 : 0;
}

int fluid_window_ray_tracing_available(const Fluid_Window *window) {
    return window && window->ray_tracing_available() ? 1 : 0;
}

void fluid_window_set_dlss(Fluid_Window *window, int enabled) {
    if (window)
        window->set_dlss(enabled != 0);
}

int fluid_window_dlss(const Fluid_Window *window) {
    return window && window->dlss() ? 1 : 0;
}

int fluid_window_dlss_available(const Fluid_Window *window) {
    return window && window->dlss_available() ? 1 : 0;
}

Fluid_DrawList *fluid_ui_draw(Fluid_Ui *ui) {
    return ui ? reinterpret_cast<Fluid_DrawList *>(&reinterpret_cast<Fluid::Ui *>(ui)->draw()) : nullptr;
}

void fluid_ui_theme(const Fluid_Ui *ui, Fluid_Theme *out) {
    if (!ui || !out)
        return;
    *out = theme(reinterpret_cast<const Fluid::Ui *>(ui)->theme());
}

void fluid_ui_set_theme(Fluid_Ui *ui, Fluid_Theme value) {
    if (!ui)
        return;
    reinterpret_cast<Fluid::Ui *>(ui)->theme() = theme(value);
}

int fluid_ui_button(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, const char *label, int primary,
                    int selected) {
    if (!ui)
        return 0;
    return ok_bool(reinterpret_cast<Fluid::Ui *>(ui)->button(empty_if_null(id), rect(bounds),
                                                            empty_if_null(label), primary != 0, selected != 0));
}

int fluid_ui_toggle(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, int *value) {
    if (!ui || !value)
        return 0;
    bool flag = *value != 0;
    const bool changed = reinterpret_cast<Fluid::Ui *>(ui)->toggle(empty_if_null(id), rect(bounds), flag);
    *value = flag ? 1 : 0;
    return ok_bool(changed);
}

int fluid_ui_slider(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, float *value, float min, float max) {
    if (!ui || !value)
        return 0;
    return ok_bool(
        reinterpret_cast<Fluid::Ui *>(ui)->slider(empty_if_null(id), rect(bounds), *value, min, max));
}

int fluid_ui_text_field(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, char *buffer, int buffer_size,
                        const char *placeholder) {
    if (!ui || !buffer || buffer_size <= 0)
        return 0;
    std::string text(buffer);
    const bool changed = reinterpret_cast<Fluid::Ui *>(ui)->text_field(
        empty_if_null(id), rect(bounds), text, placeholder ? placeholder : std::string_view{});
    if (static_cast<int>(text.size()) >= buffer_size)
        text.resize(static_cast<size_t>(buffer_size - 1));
    std::memcpy(buffer, text.c_str(), text.size() + 1);
    return ok_bool(changed);
}

int fluid_ui_hit(Fluid_Ui *ui, const char *id, Fluid_Rect bounds) {
    if (!ui)
        return 0;
    return ok_bool(reinterpret_cast<Fluid::Ui *>(ui)->hit(empty_if_null(id), rect(bounds)));
}

int fluid_ui_hovered(const Fluid_Ui *ui, Fluid_Rect bounds) {
    if (!ui)
        return 0;
    return reinterpret_cast<const Fluid::Ui *>(ui)->hovered(rect(bounds)) ? 1 : 0;
}

void fluid_ui_label(Fluid_Ui *ui, Fluid_Vec2 position, const char *text, float size, int bold) {
    if (!ui)
        return;
    reinterpret_cast<Fluid::Ui *>(ui)->label(vec(position), empty_if_null(text), size, bold != 0);
}

void fluid_ui_panel(Fluid_Ui *ui, Fluid_Rect bounds, float radius) {
    if (!ui)
        return;
    reinterpret_cast<Fluid::Ui *>(ui)->panel(rect(bounds), radius);
}

void fluid_ui_progress(Fluid_Ui *ui, Fluid_Rect bounds, float value, Fluid_Color progress_color) {
    if (!ui)
        return;
    reinterpret_cast<Fluid::Ui *>(ui)->progress(rect(bounds), value, color(progress_color));
}

float fluid_ui_measure_text(const Fluid_Ui *ui, const char *text, float size, int bold) {
    if (!ui)
        return 0;
    return reinterpret_cast<const Fluid::Ui *>(ui)->font().measure(empty_if_null(text), size, bold != 0);
}

void fluid_draw_push_clip(Fluid_DrawList *draw, Fluid_Rect clip) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->push_clip(rect(clip));
}

void fluid_draw_pop_clip(Fluid_DrawList *draw) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->pop_clip();
}

Fluid_Rect fluid_draw_current_clip(const Fluid_DrawList *draw) {
    if (!draw)
        return {};
    return rect(reinterpret_cast<const Fluid::DrawList *>(draw)->current_clip());
}

void fluid_draw_rect(Fluid_DrawList *draw, Fluid_Rect bounds, Fluid_Color fill, float radius) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->rect(rect(bounds), color(fill), radius);
}

void fluid_draw_gradient(Fluid_DrawList *draw, Fluid_Rect bounds, Fluid_Color top, Fluid_Color bottom,
                         float radius) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->gradient(rect(bounds), color(top), color(bottom), radius);
}

void fluid_draw_outline(Fluid_DrawList *draw, Fluid_Rect bounds, Fluid_Color line, float radius,
                        float thickness) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->outline(rect(bounds), color(line), radius, thickness);
}

void fluid_draw_line(Fluid_DrawList *draw, Fluid_Vec2 from, Fluid_Vec2 to, Fluid_Color line, float thickness) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->line(vec(from), vec(to), color(line), thickness);
}

void fluid_draw_circle(Fluid_DrawList *draw, Fluid_Vec2 center, float radius, Fluid_Color fill) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->circle(vec(center), radius, color(fill));
}

void fluid_draw_text(Fluid_DrawList *draw, Fluid_Vec2 position, const char *text, float size, Fluid_Color fill,
                     int bold) {
    if (draw)
        reinterpret_cast<Fluid::DrawList *>(draw)->text(vec(position), empty_if_null(text), size, color(fill),
                                                        bold != 0);
}

void fluid_draw_scene(Fluid_DrawList *draw, const Fluid_SceneView *view) {
    if (draw && view)
        reinterpret_cast<Fluid::DrawList *>(draw)->scene(scene(*view));
}

float fluid_draw_text_width(const Fluid_DrawList *draw, const char *text, float size, int bold) {
    if (!draw)
        return 0;
    return reinterpret_cast<const Fluid::DrawList *>(draw)->text_width(empty_if_null(text), size, bold != 0);
}

} // extern "C"
