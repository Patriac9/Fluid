#pragma once
#include <fluid/fluid.h>
#include <array>
#include <string>

struct DemoOptions {
    bool hidden = false;
    bool smoke_test = false;
    bool scene_aa = false;
    bool ray_tracing = false;
    bool dlss = false;
    uint32_t frames = 0;
    std::string screenshot_path;
    std::string model_path;
    std::string page = "overview";
};

class fluid_test final : public Fluid::window {
  public:
    explicit fluid_test(DemoOptions options = {});
    void init();
    void run();

  private:
    void tick() override;
    void sidebar(float width);
    void topbar(float sidebar_width);
    void overview(Fluid::Rect content);
    void inspector(Fluid::Rect bounds);
    void components(Fluid::Rect content);
    void typography(Fluid::Rect content);
    void motion(Fluid::Rect content);
    void mini_components(Fluid::Rect bounds);
    void select_model(int index);
    void import_model(const std::string &path);
    void toast(std::string message);
    void icon(Fluid::Vec2 p, int kind, Fluid::Color color, float size = 18);
    void section_title(Fluid::Vec2 p, const std::string &title, const std::string &description);

    DemoOptions options_;
    std::array<Fluid::Mesh, 4> meshes_;
    Fluid::Mesh imported_;
    Fluid::OrbitCamera camera_;
    Fluid::Rect viewport_{};
    int page_ = 0, model_index_ = 0, swatch_ = 0;
    bool dark_ = true, auto_rotate_ = true, grid_ = true, wireframe_ = false;
    bool notifications_ = true, motion_playing_ = true, demo_enabled_ = true;
    bool orbiting_ = false;
    float roughness_ = .28f, metallic_ = .62f, exposure_ = 1.1f;
    float inspector_scroll_ = 0;
    float rotation_ = 0, progress_ = .68f, font_size_ = 38, motion_speed_ = 1;
    float motion_time_ = 0, toast_until_ = 0, fps_ = 60;
    uint32_t frame_ = 0;
    int click_count_ = 0;
    std::string model_name_ = "Torus knot", import_path_, note_ = "Make something wonderful.";
    std::string type_sample_ = "Good design feels natural.";
    std::string toast_message_;
};
