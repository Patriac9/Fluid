#pragma once

#include "fluid/model.h"
#include "fluid/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Fluid {

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

class FontAtlas {
  public:
    struct Glyph {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float advance = 0;
    };
    FontAtlas();
    ~FontAtlas();
    FontAtlas(const FontAtlas &) = delete;
    FontAtlas &operator=(const FontAtlas &) = delete;
    const std::vector<unsigned char> &pixels() const;
    int width() const;
    int height() const;
    Vec2 white_uv() const;
    Glyph glyph(std::uint32_t codepoint, bool bold = false) const;
    float base_size() const;
    float measure(std::string_view text, float size, bool bold = false) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A painter-ordered display list. All positions use logical window pixels.
// The renderer consumes the list after end_frame(), including embedded scenes.
class DrawList {
  public:
    std::vector<UiVertex> vertices;
    std::vector<DrawCommand> commands;
    std::vector<SceneView> scenes;

    void reset(float width, float height, const FontAtlas &font);
    void push_clip(Rect clip);
    void pop_clip();
    Rect current_clip() const;
    void rect(Rect bounds, Color color, float radius = 0.0f);
    void gradient(Rect bounds, Color top, Color bottom, float radius = 0.0f);
    void outline(Rect bounds, Color color, float radius = 0.0f, float thickness = 1.0f);
    void line(Vec2 from, Vec2 to, Color color, float thickness = 1.0f);
    void circle(Vec2 center, float radius, Color color);
    void text(Vec2 position, std::string_view text, float size, Color color, bool bold = false);
    void scene(const SceneView &view);
    float text_width(std::string_view text, float size, bool bold = false) const;

  private:
    const FontAtlas *font_ = nullptr;
    std::vector<Rect> clips_;
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color ca, Color cb, Color cc);
    void textured_quad(Rect bounds, Vec2 uv0, Vec2 uv1, Color color);
    void polygon(const std::vector<Vec2> &points, Rect bounds, Color top, Color bottom);
    void append_command(std::uint32_t count);
};

// Immediate-mode widgets keep keyboard focus and pointer capture by stable id.
// Input key indices follow GLFW's public key values without requiring GLFW here.
class Ui {
  public:
    Ui();
    void begin_frame(const InputState &input, float width, float height, float dt);
    void end_frame();
    DrawList &draw() { return draw_; }
    const DrawList &draw() const { return draw_; }
    const FontAtlas &font() const { return font_; }
    DrawList &draw_list() { return draw_; }
    const DrawList &draw_list() const { return draw_; }
    const FontAtlas &font_atlas() const { return font_; }
    Theme &theme() { return theme_; }
    const Theme &theme() const { return theme_; }
    const InputState &input() const { return input_; }
    bool button(std::string_view id, Rect bounds, std::string_view label, bool primary = false,
                bool selected = false);
    bool toggle(std::string_view id, Rect bounds, bool &value);
    bool slider(std::string_view id, Rect bounds, float &value, float min = 0.0f, float max = 1.0f);
    bool text_field(std::string_view id, Rect bounds, std::string &value, std::string_view placeholder = {});
    bool hit(std::string_view id, Rect bounds);
    bool hovered(Rect bounds) const;
    void label(Vec2 position, std::string_view text, float size = 14.0f, bool bold = false);
    void panel(Rect bounds, float radius = 12.0f);
    void progress(Rect bounds, float value, Color color);

  private:
    struct FieldState {
        std::size_t cursor = 0;
        float scroll = 0;
        bool select_all = false;
        bool initialized = false;
    };
    FontAtlas font_;
    DrawList draw_;
    Theme theme_;
    InputState input_;
    std::string active_;
    std::string focused_;
    std::vector<std::string> focus_order_;
    std::vector<std::string> previous_focus_order_;
    std::unordered_map<std::string, FieldState> fields_;
    bool keyboard_focus_ = false;
    float time_ = 0;
    void register_widget(std::string_view id);
    void focus_outline(std::string_view id, Rect bounds, float radius);
};

} // namespace Fluid
