#pragma once

#include "fluid/model.h"
#include "fluid/types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Fluid {

struct Image;
class FontAtlas;
class Ui;

struct UiVertex {
    Vec2 position;
    Vec2 uv;
    Color color;
};

struct DrawCommand {
    enum class Kind { triangles, scene };
    Kind kind = Kind::triangles;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    Rect clip{};
    std::uint32_t scene_index = 0;
};

// Internal painter. Widgets record into this list; the renderer reads it after end_frame().
class DrawList {
  public:
    std::vector<UiVertex> vertices;
    std::vector<DrawCommand> commands;
    std::vector<SceneView> scenes;

    void reset(float width, float height, const FontAtlas &font);
    void set_origin(Vec2 origin);
    void reset_clip(Rect root);
    void push_clip(Rect clip);
    void pop_clip();
    Rect current_clip() const;
    void rect(Rect bounds, Color color, float radius = 0.0f);
    void gradient(Rect bounds, Color top, Color bottom, float radius = 0.0f);
    void outline(Rect bounds, Color color, float radius = 0.0f, float thickness = 1.0f);
    void line(Vec2 from, Vec2 to, Color color, float thickness = 1.0f);
    void circle(Vec2 center, float radius, Color color);
    void text(Vec2 position, std::string_view text, float size, Color color, bool bold = false);
    void image(Rect bounds, const Image &image, Color tint = {1, 1, 1, 1}, float radius = 0.0f);
    void scene(const SceneView &view);
    float text_width(std::string_view text, float size, bool bold = false) const;

  private:
    const FontAtlas *font_ = nullptr;
    Vec2 origin_{};
    std::vector<Rect> clips_;
    Vec2 to_window(Vec2 point) const;
    Rect to_window(Rect bounds) const;
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color ca, Color cb, Color cc);
    void triangle_uv(Vec2 a, Vec2 b, Vec2 c, Vec2 ua, Vec2 ub, Vec2 uc, Color ca, Color cb, Color cc);
    void textured_quad(Rect bounds, Vec2 uv0, Vec2 uv1, Color color);
    void polygon(const std::vector<Vec2> &points, Rect bounds, Color top, Color bottom);
    void append_command(std::uint32_t count);
};

DrawList &fluid_draw_list(Ui &ui);
const DrawList &fluid_draw_list(const Ui &ui);

} // namespace Fluid
