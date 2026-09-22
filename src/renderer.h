#pragma once
#include <memory>
#include <string>
#include <cstdint>
struct GLFWwindow;
struct application_cfg;
namespace Fluid {
class Ui;
class Renderer {
  public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void initialize(GLFWwindow *, const application_cfg &, const Ui &);
    bool render(const Ui &, float logical_width, float logical_height);
    void screenshot(const std::string &path);
    const std::string &device_name() const;
    void set_scene_aa(bool enabled);
    bool scene_aa() const;
    void set_ray_tracing(bool enabled);
    bool ray_tracing() const;
    bool ray_tracing_available() const;
    void set_dlss(bool enabled);
    bool dlss() const;
    bool dlss_available() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace Fluid
