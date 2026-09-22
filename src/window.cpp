#include "window.h"
#include "renderer.h"
#include <algorithm>
#include <chrono>
#include <climits>
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
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Impl() { destroy(); }
    void destroy() {
        renderer.reset();
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
    GLFWmonitor *monitor = cfg.fullscreen ? (cfg.monitor ? cfg.monitor : glfwGetPrimaryMonitor()) : nullptr;
    impl_->handle = glfwCreateWindow(static_cast<int>(cfg.width), static_cast<int>(cfg.height),
                                     cfg.window_title.c_str(), monitor, nullptr);
    if (!impl_->handle)
        throw std::runtime_error("Cannot create the Fluid window");
    impl_->width = static_cast<float>(cfg.width);
    impl_->height = static_cast<float>(cfg.height);
    impl_->callbacks();
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
        auto now = std::chrono::steady_clock::now();
        impl_->dt = std::clamp(std::chrono::duration<float>(now - previous).count(), 0.0001f, 0.1f);
        impl_->elapsed = std::chrono::duration<double>(now - impl_->start).count();
        previous = now;
        impl_->ui.begin_frame(impl_->input, impl_->width, impl_->height, impl_->dt);
        tick();
        if (!impl_->handle || !impl_->renderer)
            break;
        impl_->ui.end_frame();
        if (impl_->renderer->render(impl_->ui, impl_->width, impl_->height)) {
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
