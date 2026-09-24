#include "window.h"
#include "draw_list.h"
#include "renderer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Fluid {
namespace {
std::mutex glfw_mutex;
unsigned glfw_users = 0;
void acquire_glfw() {
    std::lock_guard lock(glfw_mutex);
    if (glfw_users == 0) {
        glfwSetErrorCallback(
            [](int code, const char *text) { std::cerr << "[Fluid GLFW " << code << "] " << text << '\n'; });
        if (!glfwInit())
            throw std::runtime_error("Cannot initialize the window system");
        if (!glfwVulkanSupported()) {
            glfwTerminate();
            throw std::runtime_error("Vulkan is unavailable. Install a Vulkan-capable graphics driver.");
        }
    }
    ++glfw_users;
}
void release_glfw() {
    std::lock_guard lock(glfw_mutex);
    if (glfw_users && --glfw_users == 0)
        glfwTerminate();
}
constexpr float kCaptionHeight = 32.0f;
constexpr float kCaptionButton = 46.0f;
constexpr float kResizeThickness = 6.0f;
constexpr double kDragThreshold = 4.0;
constexpr double kDoubleClickSeconds = 0.5;
enum class FrameEdge { none, left, right, top, bottom, top_left, top_right, bottom_left, bottom_right };
enum class CaptionButton { none, minimize, maximize, close };
enum class CursorKind { arrow, horizontal, vertical, main_diagonal, anti_diagonal, count };
int clamp_window_coord(double value) {
    value = std::clamp(value, -1.0e7, 1.0e7);
    return static_cast<int>(std::lround(value));
}
void append_utf8(std::string &result, unsigned codepoint) {
    if (codepoint <= 0x7f)
        result.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff && !(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}
} // namespace
struct window::Impl {
    GLFWwindow *handle{};
    bool glfw_acquired = false;
    std::unique_ptr<Renderer> renderer;
    Ui ui;
    InputState input;
    float width = 0, height = 0, dt = 1.0f / 60.0f;
    double elapsed = 0;
    uint32_t frame_limit = 0;
    bool scene_aa = false, ray_tracing = false, dlss = false;
    std::string pending_screenshot;
    std::string title;
    bool custom_chrome = false;
    bool resizable = true;
    bool maximized = false;
    bool caption_gesture = false;
    bool dragging = false;
    bool resizing = false;
    bool drag_armed = false;
    int cursor_kind = -1;
    int restored_x = 0, restored_y = 0, restored_w = 0, restored_h = 0;
    int drag_x = 0, drag_y = 0, drag_w = 0, drag_h = 0;
    double drag_screen_x = 0, drag_screen_y = 0;
    double last_caption_click = -1;
    FrameEdge resize_edge = FrameEdge::none;
    std::array<GLFWcursor *, static_cast<int>(CursorKind::count)> cursors{};
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Impl() { destroy(); }
    void destroy_cursors() {
        if (handle)
            glfwSetCursor(handle, nullptr);
        cursor_kind = -1;
        for (GLFWcursor *&cursor : cursors) {
            if (cursor)
                glfwDestroyCursor(cursor);
            cursor = nullptr;
        }
    }
    void destroy() {
        renderer.reset();
        destroy_cursors();
        if (handle)
            glfwDestroyWindow(handle);
        handle = nullptr;
        if (glfw_acquired)
            release_glfw();
        glfw_acquired = false;
    }
    static Impl *get(GLFWwindow *w) { return static_cast<Impl *>(glfwGetWindowUserPointer(w)); }
    void callbacks() {
        glfwSetWindowUserPointer(handle, this);
        glfwSetCursorPosCallback(handle, [](GLFWwindow *w, double x, double y) {
            auto &in = get(w)->input;
            in.mouse_delta.x += static_cast<float>(x) - in.mouse.x;
            in.mouse_delta.y += static_cast<float>(y) - in.mouse.y;
            in.mouse = {static_cast<float>(x), static_cast<float>(y)};
        });
        glfwSetMouseButtonCallback(handle, [](GLFWwindow *w, int button, int action, int) {
            auto &in = get(w)->input;
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                in.mouse_down = action != GLFW_RELEASE;
                if (action == GLFW_PRESS)
                    in.mouse_pressed = true;
                if (action == GLFW_RELEASE)
                    in.mouse_released = true;
            }
            if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                in.right_down = action != GLFW_RELEASE;
                if (action == GLFW_PRESS)
                    in.right_pressed = true;
            }
        });
        glfwSetScrollCallback(
            handle, [](GLFWwindow *w, double, double y) { get(w)->input.scroll += static_cast<float>(y); });
        glfwSetKeyCallback(handle, [](GLFWwindow *w, int key, int, int action, int) {
            auto &in = get(w)->input;
            if (key < 0 || key >= static_cast<int>(in.keys_down.size()))
                return;
            in.keys_down[static_cast<size_t>(key)] = action != GLFW_RELEASE;
            if (action == GLFW_PRESS || action == GLFW_REPEAT)
                in.keys_pressed[static_cast<size_t>(key)] = true;
        });
        glfwSetCharCallback(
            handle, [](GLFWwindow *w, unsigned codepoint) { append_utf8(get(w)->input.text, codepoint); });
        glfwSetDropCallback(handle, [](GLFWwindow *w, int count, const char **paths) {
            for (int i = 0; i < count; ++i)
                get(w)->input.dropped_paths.emplace_back(paths[i]);
        });
        glfwSetWindowFocusCallback(handle, [](GLFWwindow *w, int focused) {
            if (focused)
                return;
            auto &in = get(w)->input;
            if (in.mouse_down)
                in.mouse_released = true;
            in.mouse_down = in.right_down = false;
            in.keys_down.fill(false);
        });
        double x = 0, y = 0;
        glfwGetCursorPos(handle, &x, &y);
        input.mouse = {static_cast<float>(x), static_cast<float>(y)};
    }
    void reset_transient_input() {
        input.mouse_pressed = input.mouse_released = input.right_pressed = false;
        input.mouse_delta = {};
        input.scroll = 0;
        input.keys_pressed.fill(false);
        input.text.clear();
        input.dropped_paths.clear();
    }
    void create_cursors() {
        const int shapes[] = {GLFW_ARROW_CURSOR, GLFW_RESIZE_EW_CURSOR, GLFW_RESIZE_NS_CURSOR,
                              GLFW_RESIZE_NWSE_CURSOR, GLFW_RESIZE_NESW_CURSOR};
        static_assert(sizeof(shapes) / sizeof(shapes[0]) == static_cast<int>(CursorKind::count));
        for (int i = 0; i < static_cast<int>(CursorKind::count); ++i)
            cursors[static_cast<size_t>(i)] = glfwCreateStandardCursor(shapes[i]);
    }
    void screen_cursor(double &screen_x, double &screen_y) const {
        int window_x = 0, window_y = 0;
        double cursor_x = 0, cursor_y = 0;
        glfwGetWindowPos(handle, &window_x, &window_y);
        glfwGetCursorPos(handle, &cursor_x, &cursor_y);
        screen_x = static_cast<double>(window_x) + cursor_x;
        screen_y = static_cast<double>(window_y) + cursor_y;
    }
    void remember_placement() {
        glfwGetWindowPos(handle, &drag_x, &drag_y);
        glfwGetWindowSize(handle, &drag_w, &drag_h);
        screen_cursor(drag_screen_x, drag_screen_y);
    }
    Rect button_bounds(CaptionButton button) const {
        const int slot = button == CaptionButton::close ? 1 : button == CaptionButton::maximize ? 2 : 3;
        const float right = resizable ? kResizeThickness : 0;
        return {width - right - kCaptionButton * static_cast<float>(slot), 0, kCaptionButton, kCaptionHeight};
    }
    CaptionButton button_at(Vec2 mouse) const {
        for (CaptionButton button : {CaptionButton::minimize, CaptionButton::maximize, CaptionButton::close}) {
            if (button_bounds(button).contains(mouse))
                return button;
        }
        return CaptionButton::none;
    }
    FrameEdge edge_at(Vec2 mouse) const {
        if (!resizable || width <= 0 || height <= 0)
            return FrameEdge::none;
        const bool left = mouse.x >= 0 && mouse.x < kResizeThickness;
        const bool right = mouse.x >= width - kResizeThickness && mouse.x < width;
        const bool top = mouse.y >= 0 && mouse.y < kResizeThickness;
        const bool bottom = mouse.y >= height - kResizeThickness && mouse.y < height;
        if (top && left)
            return FrameEdge::top_left;
        if (top && right)
            return FrameEdge::top_right;
        if (bottom && left)
            return FrameEdge::bottom_left;
        if (bottom && right)
            return FrameEdge::bottom_right;
        if (left)
            return FrameEdge::left;
        if (right)
            return FrameEdge::right;
        if (top)
            return FrameEdge::top;
        if (bottom)
            return FrameEdge::bottom;
        return FrameEdge::none;
    }
    bool in_drag_area(Vec2 mouse) const {
        return mouse.y >= 0 && mouse.y < kCaptionHeight && mouse.x >= 0 && mouse.x < width &&
               button_at(mouse) == CaptionButton::none && edge_at(mouse) == FrameEdge::none;
    }
    GLFWmonitor *monitor_at_window() const {
        int window_x = 0, window_y = 0, window_w = 0, window_h = 0;
        glfwGetWindowPos(handle, &window_x, &window_y);
        glfwGetWindowSize(handle, &window_w, &window_h);
        const int center_x = window_x + window_w / 2;
        const int center_y = window_y + window_h / 2;
        int count = 0;
        GLFWmonitor **monitors = glfwGetMonitors(&count);
        if (monitors) {
            for (int i = 0; i < count; ++i) {
                if (!monitors[i])
                    continue;
                int area_x = 0, area_y = 0, area_w = 0, area_h = 0;
                glfwGetMonitorWorkarea(monitors[i], &area_x, &area_y, &area_w, &area_h);
                if (area_w > 0 && area_h > 0 && center_x >= area_x && center_x < area_x + area_w &&
                    center_y >= area_y && center_y < area_y + area_h)
                    return monitors[i];
            }
        }
        return glfwGetPrimaryMonitor();
    }
    void toggle_maximize() {
        if (!handle)
            return;
        if (maximized) {
            maximized = false;
            if (restored_w > 0 && restored_h > 0) {
                glfwSetWindowPos(handle, restored_x, restored_y);
                glfwSetWindowSize(handle, restored_w, restored_h);
            }
            return;
        }
        glfwGetWindowPos(handle, &restored_x, &restored_y);
        glfwGetWindowSize(handle, &restored_w, &restored_h);
        GLFWmonitor *monitor = monitor_at_window();
        if (!monitor)
            return;
        int area_x = 0, area_y = 0, area_w = 0, area_h = 0;
        glfwGetMonitorWorkarea(monitor, &area_x, &area_y, &area_w, &area_h);
        if (area_w <= 0 || area_h <= 0)
            return;
        glfwSetWindowPos(handle, area_x, area_y);
        glfwSetWindowSize(handle, area_w, area_h);
        maximized = true;
    }
    void restore_for_drag(double screen_x, double screen_y) {
        const double ratio =
            width > 0 ? std::clamp(static_cast<double>(input.mouse.x) / static_cast<double>(width), 0.0, 1.0) : 0.5;
        const int restored_width = std::max(restored_w, 1);
        const int restored_height = std::max(restored_h, 1);
        const int x = clamp_window_coord(screen_x - ratio * restored_width);
        const int y = clamp_window_coord(screen_y - std::min(static_cast<double>(input.mouse.y),
                                                              static_cast<double>(kCaptionHeight)));
        maximized = false;
        glfwSetWindowSize(handle, restored_width, restored_height);
        glfwSetWindowPos(handle, x, y);
        drag_x = x;
        drag_y = y;
        drag_w = restored_width;
        drag_h = restored_height;
        drag_screen_x = screen_x;
        drag_screen_y = screen_y;
    }
    void apply_drag() {
        double screen_x = 0, screen_y = 0;
        screen_cursor(screen_x, screen_y);
        glfwSetWindowPos(handle, clamp_window_coord(static_cast<double>(drag_x) + (screen_x - drag_screen_x)),
                         clamp_window_coord(static_cast<double>(drag_y) + (screen_y - drag_screen_y)));
    }
    void apply_resize() {
        double screen_x = 0, screen_y = 0;
        screen_cursor(screen_x, screen_y);
        const double dx = screen_x - drag_screen_x;
        const double dy = screen_y - drag_screen_y;
        const bool left = resize_edge == FrameEdge::left || resize_edge == FrameEdge::top_left ||
                          resize_edge == FrameEdge::bottom_left;
        const bool right = resize_edge == FrameEdge::right || resize_edge == FrameEdge::top_right ||
                           resize_edge == FrameEdge::bottom_right;
        const bool top = resize_edge == FrameEdge::top || resize_edge == FrameEdge::top_left ||
                         resize_edge == FrameEdge::top_right;
        const bool bottom = resize_edge == FrameEdge::bottom || resize_edge == FrameEdge::bottom_left ||
                            resize_edge == FrameEdge::bottom_right;
        int x = drag_x, y = drag_y, w = drag_w, h = drag_h;
        if (left) {
            x = clamp_window_coord(static_cast<double>(drag_x) + dx);
            w = std::max(1, drag_w - (x - drag_x));
        } else if (right)
            w = std::max(1, clamp_window_coord(static_cast<double>(drag_w) + dx));
        if (top) {
            y = clamp_window_coord(static_cast<double>(drag_y) + dy);
            h = std::max(1, drag_h - (y - drag_y));
        } else if (bottom)
            h = std::max(1, clamp_window_coord(static_cast<double>(drag_h) + dy));
        if (left || top)
            glfwSetWindowPos(handle, x, y);
        glfwSetWindowSize(handle, w, h);
        maximized = false;
    }
    void set_frame_cursor(FrameEdge edge) {
        CursorKind kind = CursorKind::arrow;
        switch (edge) {
        case FrameEdge::left:
        case FrameEdge::right:
            kind = CursorKind::horizontal;
            break;
        case FrameEdge::top:
        case FrameEdge::bottom:
            kind = CursorKind::vertical;
            break;
        case FrameEdge::top_left:
        case FrameEdge::bottom_right:
            kind = CursorKind::main_diagonal;
            break;
        case FrameEdge::top_right:
        case FrameEdge::bottom_left:
            kind = CursorKind::anti_diagonal;
            break;
        case FrameEdge::none:
            break;
        }
        const int index = static_cast<int>(kind);
        if (cursor_kind == index)
            return;
        cursor_kind = index;
        GLFWcursor *cursor = cursors[static_cast<size_t>(index)];
        if (handle && cursor)
            glfwSetCursor(handle, cursor);
    }
    void update_chrome() {
        if (!custom_chrome || !handle)
            return;
        const Vec2 mouse = input.mouse;
        if (input.mouse_pressed) {
            const CaptionButton button = button_at(mouse);
            const FrameEdge edge = edge_at(mouse);
            if (button != CaptionButton::none) {
                caption_gesture = true;
                if (button == CaptionButton::close)
                    glfwSetWindowShouldClose(handle, GLFW_TRUE);
                else if (button == CaptionButton::minimize)
                    glfwIconifyWindow(handle);
                else
                    toggle_maximize();
            } else if (edge != FrameEdge::none) {
                caption_gesture = true;
                resizing = true;
                resize_edge = edge;
                remember_placement();
            } else if (in_drag_area(mouse)) {
                const double now = glfwGetTime();
                const bool double_click =
                    last_caption_click >= 0.0 && now - last_caption_click <= kDoubleClickSeconds;
                last_caption_click = double_click ? -1.0 : now;
                caption_gesture = true;
                if (double_click)
                    toggle_maximize();
                else {
                    drag_armed = true;
                    remember_placement();
                }
            }
        }
        if (drag_armed && (input.mouse_down || input.mouse_pressed)) {
            double screen_x = 0, screen_y = 0;
            screen_cursor(screen_x, screen_y);
            if (std::hypot(screen_x - drag_screen_x, screen_y - drag_screen_y) >= kDragThreshold) {
                last_caption_click = -1;
                if (maximized)
                    restore_for_drag(screen_x, screen_y);
                dragging = true;
                drag_armed = false;
            }
        }
        if (dragging && (input.mouse_down || input.mouse_released))
            apply_drag();
        if (resizing && (input.mouse_down || input.mouse_released))
            apply_resize();
        const bool swallow = caption_gesture;
        if (input.mouse_released || (!input.mouse_down && !input.mouse_pressed)) {
            dragging = false;
            resizing = false;
            drag_armed = false;
            caption_gesture = false;
            resize_edge = FrameEdge::none;
        }
        FrameEdge cursor_edge = FrameEdge::none;
        if (resizing)
            cursor_edge = resize_edge;
        else if (!dragging && !drag_armed && button_at(mouse) == CaptionButton::none)
            cursor_edge = edge_at(mouse);
        set_frame_cursor(cursor_edge);
        if (!swallow)
            return;
        input.mouse_down = input.mouse_pressed = input.mouse_released = false;
        input.mouse_delta = {};
        input.scroll = 0;
    }
    void paint_glyph(DrawList &draw, CaptionButton button, Rect bounds, Color color) const {
        const float cx = bounds.x + bounds.w * 0.5f;
        const float cy = bounds.y + bounds.h * 0.5f;
        if (button == CaptionButton::minimize)
            draw.line({cx - 5, cy}, {cx + 5, cy}, color, 1);
        else if (button == CaptionButton::close) {
            draw.line({cx - 4.5f, cy - 4.5f}, {cx + 4.5f, cy + 4.5f}, color, 1);
            draw.line({cx + 4.5f, cy - 4.5f}, {cx - 4.5f, cy + 4.5f}, color, 1);
        } else if (maximized) {
            draw.outline({cx - 4.5f, cy - 2.5f, 8, 8}, color, 0, 1);
            draw.outline({cx - 1.5f, cy - 5.5f, 8, 8}, color, 0, 1);
        } else
            draw.outline({cx - 5, cy - 5, 10, 10}, color, 0, 1);
    }
    void paint_caption(float window_w, float window_h) {
        if (!custom_chrome || window_w <= 0 || window_h <= 0)
            return;
        DrawList &draw = fluid_draw_list(ui);
        const Theme &theme = ui.theme();
        const float bar_h = std::min(kCaptionHeight, window_h);
        draw.rect({0, 0, window_w, bar_h}, theme.panel);
        if (window_h > bar_h)
            draw.line({0, bar_h}, {window_w, bar_h}, theme.border, 1);
        const float buttons_left = window_w - (resizable ? kResizeThickness : 0) - kCaptionButton * 3;
        draw.push_clip({12, 0, std::max(0.0f, buttons_left - 20.0f), bar_h});
        draw.text({12, std::round((bar_h - 13.0f) * 0.5f)}, title, 13, theme.text);
        draw.pop_clip();
        const Color close_hover = Color::hex(0xC42B1C);
        for (CaptionButton button : {CaptionButton::minimize, CaptionButton::maximize, CaptionButton::close}) {
            const Rect bounds = button_bounds(button);
            const bool hover = bounds.contains(input.mouse);
            const bool close = button == CaptionButton::close;
            if (hover)
                draw.rect(bounds, close ? close_hover : theme.elevated);
            paint_glyph(draw, button, bounds, close && hover ? Color{1, 1, 1, 1} : theme.text);
        }
        draw.outline({0, 0, window_w, window_h}, theme.border, 0, 1);
    }
};
window::window(window_cfg cfg) : impl_(std::make_unique<Impl>()) {
    if (cfg.width == 0 || cfg.height == 0 || cfg.width > INT_MAX || cfg.height > INT_MAX)
        throw std::invalid_argument("Window dimensions must be positive integers");
    acquire_glfw();
    impl_->glfw_acquired = true;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, cfg.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, cfg.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, cfg.use_default_border ? GLFW_TRUE : GLFW_FALSE);
    impl_->custom_chrome = !cfg.use_default_border && !cfg.fullscreen;
    impl_->resizable = cfg.resizable;
    impl_->title = cfg.window_title;
    GLFWmonitor *monitor = cfg.fullscreen ? (cfg.monitor ? cfg.monitor : glfwGetPrimaryMonitor()) : nullptr;
    impl_->handle = glfwCreateWindow(static_cast<int>(cfg.width), static_cast<int>(cfg.height),
                                     cfg.window_title.c_str(), monitor, nullptr);
    if (!impl_->handle)
        throw std::runtime_error("Cannot create the Fluid window");
    impl_->width = static_cast<float>(cfg.width);
    impl_->height = static_cast<float>(cfg.height);
    impl_->callbacks();
    if (impl_->custom_chrome)
        impl_->create_cursors();
}
window::~window() = default;
void window::Init(const application_cfg &app) {
    if (!impl_->handle)
        throw std::logic_error("Cannot initialize a destroyed Fluid window");
    if (impl_->renderer)
        return;
    auto renderer = std::make_unique<Renderer>();
    renderer->initialize(impl_->handle, app, impl_->ui);
    renderer->set_scene_aa(impl_->scene_aa);
    renderer->set_ray_tracing(impl_->ray_tracing);
    renderer->set_dlss(impl_->dlss);
    if (!impl_->pending_screenshot.empty())
        renderer->screenshot(impl_->pending_screenshot);
    impl_->renderer = std::move(renderer);
    impl_->start = std::chrono::steady_clock::now();
}
void window::destroy() { impl_->destroy(); }
void window::loop() {
    if (!impl_->handle)
        throw std::logic_error("Cannot run a destroyed Fluid window");
    if (!impl_->renderer)
        Init();
    auto previous = std::chrono::steady_clock::now();
    uint32_t frames = 0;
    while (impl_->handle && !glfwWindowShouldClose(impl_->handle)) {
        impl_->reset_transient_input();
        glfwPollEvents();
        if (glfwWindowShouldClose(impl_->handle))
            break;
        int w = 0, h = 0, fw = 0, fh = 0;
        glfwGetWindowSize(impl_->handle, &w, &h);
        glfwGetFramebufferSize(impl_->handle, &fw, &fh);
        if (w <= 0 || h <= 0 || fw <= 0 || fh <= 0) {
            glfwWaitEventsTimeout(0.05);
            previous = std::chrono::steady_clock::now();
            continue;
        }
        impl_->width = static_cast<float>(w);
        impl_->height = static_cast<float>(h);
        const bool pointer_held = impl_->input.mouse_down;
        const bool pointer_released = impl_->input.mouse_released;
        impl_->update_chrome();
        const float window_w = impl_->width;
        const float window_h = impl_->height;
        const float caption = impl_->custom_chrome ? kCaptionHeight : 0.0f;
        const Vec2 window_mouse = impl_->input.mouse;
        if (caption > 0)
            impl_->input.mouse.y -= caption;
        impl_->height = std::max(0.0f, window_h - caption);
        auto now = std::chrono::steady_clock::now();
        impl_->dt = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0001f, 0.1f);
        impl_->elapsed = std::chrono::duration<double>(now - impl_->start).count();
        previous = now;
        impl_->ui.begin_frame(impl_->input, window_w, impl_->height, impl_->dt);
        if (caption > 0)
            fluid_draw_list(impl_->ui).set_origin({0, caption});
        tick();
        const bool alive = impl_->handle && impl_->renderer;
        if (caption > 0 && alive) {
            DrawList &draw = fluid_draw_list(impl_->ui);
            draw.set_origin({});
            draw.reset_clip({0, 0, window_w, window_h});
        }
        impl_->input.mouse = window_mouse;
        impl_->input.mouse_down = pointer_held && !pointer_released;
        impl_->height = window_h;
        if (!alive)
            break;
        impl_->paint_caption(window_w, window_h);
        impl_->ui.end_frame();
        if (impl_->renderer->render(impl_->ui, window_w, window_h)) {
            ++frames;
            if (impl_->frame_limit && frames >= impl_->frame_limit)
                break;
        }
    }
}
void window::tick() {}
Ui &window::ui() { return impl_->ui; }
const InputState &window::input() const { return impl_->input; }
GLFWwindow *window::native_handle() const { return impl_->handle; }
float window::width() const { return impl_->width; }
float window::height() const { return impl_->height; }
float window::caption_height() const { return impl_->custom_chrome ? kCaptionHeight : 0.0f; }
float window::delta_time() const { return impl_->dt; }
double window::elapsed_time() const { return impl_->elapsed; }
const std::string &window::device_name() const {
    static const std::string unavailable = "Not initialized";
    return impl_->renderer ? impl_->renderer->device_name() : unavailable;
}
void window::set_frame_limit(uint32_t frames) { impl_->frame_limit = frames; }
void window::set_scene_aa(bool enabled) {
    impl_->scene_aa = enabled;
    if (impl_->renderer)
        impl_->renderer->set_scene_aa(enabled);
}
bool window::scene_aa() const { return impl_->scene_aa; }
void window::set_ray_tracing(bool enabled) {
    impl_->ray_tracing = enabled;
    if (impl_->renderer)
        impl_->renderer->set_ray_tracing(enabled);
}
bool window::ray_tracing() const { return impl_->ray_tracing; }
bool window::ray_tracing_available() const {
    return impl_->renderer && impl_->renderer->ray_tracing_available();
}
void window::set_dlss(bool enabled) {
    impl_->dlss = enabled;
    if (impl_->renderer)
        impl_->renderer->set_dlss(enabled);
}
bool window::dlss() const { return impl_->dlss; }
bool window::dlss_available() const { return true; }
void window::screenshot(const std::string &path) {
    if (path.empty())
        throw std::invalid_argument("Screenshot path must not be empty");
    if (impl_->renderer)
        impl_->renderer->screenshot(path);
    else
        impl_->pending_screenshot = path;
}
} // namespace Fluid
