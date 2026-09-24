#pragma once
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "fluid/ui.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

struct window_cfg {
    bool use_default_border = true;
    bool fullscreen = false;
    bool resizable = true;
    uint32_t width = 1440;
    uint32_t height = 960;
    std::string window_title = "Fluid";
    GLFWmonitor *monitor = nullptr;
    GLFWwindow *share = nullptr;
    bool visible = true;
};
struct application_cfg {
    std::string app_name = "Fluid";
    std::array<uint32_t, 3> app_version{1, 0, 0};
    std::string engine_name = "Fluid";
    std::array<uint32_t, 3> engine_version{1, 0, 0};
};
namespace Fluid {
class window {
  public:
    explicit window(window_cfg cfg = {});
    virtual ~window();
    window(const window &) = delete;
    window &operator=(const window &) = delete;
    window(window &&) = delete;
    window &operator=(window &&) = delete;
    void Init(const application_cfg &app_cfg = {});
    void destroy();
    void loop();
    Ui &ui();
    const InputState &input() const;
    GLFWwindow *native_handle() const;
    float width() const;
    // During tick(), this is the area below the custom caption. With the platform
    // border, or outside tick(), it is the full window height.
    float height() const;
    float caption_height() const;
    float delta_time() const;
    double elapsed_time() const;
    const std::string &device_name() const;
    void set_frame_limit(uint32_t frames);
    void screenshot(const std::string &path);
    // GUI is always MSAA when the device supports it. The flags below apply to every
    // 3D view in this window; ray tracing and DLSS are ignored for 2D drawing.
    void set_scene_aa(bool enabled);
    bool scene_aa() const;
    void set_ray_tracing(bool enabled);
    bool ray_tracing() const;
    bool ray_tracing_available() const;
    void set_dlss(bool enabled);
    bool dlss() const;
    bool dlss_available() const;

  protected:
    virtual void tick();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Fluid
