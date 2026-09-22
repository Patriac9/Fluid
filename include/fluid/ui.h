#pragma once

#include "fluid/model.h"
#include "fluid/types.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Fluid {

class DrawList;

enum class TextAlign { left, center, right };

// rectangle ignores radius. circle uses half the shorter side. rounded_rectangle uses radius.
enum class BackgroundShape { rounded_rectangle, rectangle, circle };

// A sub-rectangle of the UI atlas. Create one with Ui::add_image before or after
// the window is initialized; the renderer uploads the atlas again when it changes.
struct Image {
    int id = -1;
    explicit operator bool() const { return id >= 0; }
};

// background is the top of the fill. gradient, when set, is the bottom of a vertical
// gradient. An image replaces the fill and is tinted by background when that color is set.
// border is drawn only when set. align places the label; center ignores padding on x.
struct ButtonStyle {
    std::optional<Color> background;
    std::optional<Color> gradient;
    std::optional<Color> text;
    std::optional<Color> border;
    Image image{};
    BackgroundShape shape = BackgroundShape::rounded_rectangle;
    TextAlign align = TextAlign::center;
    float padding = 12.0f;
    float radius = 8.0f;
    float font_size = 13.0f;
    bool bold = true;
};

// A filled or photographic background with one line of text. Text is centered
// vertically in the bounds. Horizontal placement follows align.
struct BackgroundedTextStyle {
    std::optional<Color> background;
    std::optional<Color> gradient;
    std::optional<Color> text;
    std::optional<Color> border;
    Image image{};
    BackgroundShape shape = BackgroundShape::rounded_rectangle;
    TextAlign align = TextAlign::left;
    float padding = 12.0f;
    float radius = 8.0f;
    float font_size = 14.0f;
    bool bold = false;
};

// One selectable row uses `selected` when its index matches. Other rows use `item`.
// hover_background replaces the normal background while the pointer is over a row.
struct SelectionListStyle {
    BackgroundedTextStyle item{};
    BackgroundedTextStyle selected{};
    std::optional<Color> hover_background;
    float gap = 6.0f;
};

struct TextInk {
    float width = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
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
    // Empty paths keep the built-in search (Segoe UI, Arial, DejaVu, Liberation, then
    // the embedded font). Glyphs are rasterized on demand at the requested pixel size.
    // Call this before the first frame, or at any later time; the GPU atlas is refreshed
    // on the next frame.
    void set_typeface(std::string regular_path, std::string bold_path = {});
    Image add_image(const std::uint8_t *rgba, int width, int height);
    Image add_image_file(const std::string &path);
    const std::vector<unsigned char> &pixels() const;
    int width() const;
    int height() const;
    Vec2 white_uv() const;
    std::uint64_t revision() const;
    bool image_uv(Image image, Vec2 &uv0, Vec2 &uv1) const;
    // Glyph metrics are already in `size` pixels. Each size is rasterized on demand.
    Glyph glyph(std::uint32_t codepoint, bool bold, float size) const;
    float measure(std::string_view text, float size, bool bold = false) const;
    TextInk ink(std::string_view text, float size, bool bold = false) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Immediate-mode widgets keep keyboard focus and pointer capture by stable id.
// Input key indices follow GLFW's public key values without requiring GLFW here.
class Ui {
  public:
    Ui();
    ~Ui();
    Ui(const Ui &) = delete;
    Ui &operator=(const Ui &) = delete;
    void begin_frame(const InputState &input, float width, float height, float dt);
    void end_frame();
    const FontAtlas &font() const { return font_; }
    Theme &theme() { return theme_; }
    const Theme &theme() const { return theme_; }
    bool button(std::string_view id, Rect bounds, std::string_view label, bool primary = false,
                bool selected = false);
    bool button(std::string_view id, Rect bounds, std::string_view label, const ButtonStyle &style);
    void set_typeface(std::string regular_path, std::string bold_path = {});
    Image add_image(const std::uint8_t *rgba, int width, int height);
    Image add_image_file(const std::string &path);
    bool toggle(std::string_view id, Rect bounds, bool &value,
                BackgroundShape shape = BackgroundShape::circle, std::optional<Color> gradient = std::nullopt);
    bool slider(std::string_view id, Rect bounds, float &value, float min = 0.0f, float max = 1.0f,
                BackgroundShape shape = BackgroundShape::rounded_rectangle,
                std::optional<Color> gradient = std::nullopt);
    bool text_field(std::string_view id, Rect bounds, std::string &value, std::string_view placeholder = {},
                    BackgroundShape shape = BackgroundShape::rounded_rectangle,
                    std::optional<Color> gradient = std::nullopt);
    bool hit(std::string_view id, Rect bounds);
    bool hovered(Rect bounds) const;
    void label(Vec2 position, std::string_view text, float size = 14.0f, bool bold = false);
    void label(Vec2 position, std::string_view text, float size, Color color, bool bold = false);
    void line(Vec2 from, Vec2 to, Color color, float thickness = 1.0f);
    void push_clip(Rect clip);
    void pop_clip();
    void scene(const SceneView &view);
    void backgrounded_text(Rect bounds, std::string_view text, const BackgroundedTextStyle &style);
    // Rows are stacked from the top of `bounds`. `item_height` is each row.
    // Returns true when a row is activated and writes that index to `selected`.
    bool selection_list(std::string_view id, Rect bounds, float item_height,
                        std::span<const std::string_view> items, int &selected,
                        const SelectionListStyle &style);
    void panel(Rect bounds, float radius = 12.0f,
               BackgroundShape shape = BackgroundShape::rounded_rectangle,
               std::optional<Color> gradient = std::nullopt);
    void progress(Rect bounds, float value, Color color, BackgroundShape shape = BackgroundShape::circle,
                  std::optional<Color> gradient = std::nullopt);

  private:
    struct FieldState {
        std::size_t cursor = 0;
        float scroll = 0;
        bool select_all = false;
        bool initialized = false;
    };
    friend DrawList &fluid_draw_list(Ui &ui);
    friend const DrawList &fluid_draw_list(const Ui &ui);
    FontAtlas font_;
    std::unique_ptr<DrawList> draw_;
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
    float shape_radius(BackgroundShape shape, Rect bounds, float radius) const;
    void paint_fill(Rect bounds, BackgroundShape shape, float radius, Color top,
                    const std::optional<Color> &gradient);
    void paint_backgrounded_text(Rect bounds, std::string_view text, const BackgroundedTextStyle &style);
};

} // namespace Fluid
