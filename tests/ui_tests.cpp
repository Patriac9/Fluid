#include "fluid/ui.h"
#include "draw_list.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void begin(Fluid::Ui &ui, Fluid::InputState input = {}) { ui.begin_frame(input, 600, 400, 1.0f / 60); }

void test_pointer_capture(Fluid::Ui &ui) {
    const Fluid::Rect bounds{10, 10, 100, 32};
    Fluid::InputState input;
    begin(ui);
    require(!ui.button("capture", bounds, "Button"), "Idle button clicked");
    ui.end_frame();
    input.mouse = {40, 20};
    input.mouse_down = true;
    input.mouse_pressed = true;
    begin(ui, input);
    require(!ui.button("capture", bounds, "Button"), "Button clicked before release");
    ui.end_frame();
    input.mouse_pressed = false;
    input.mouse = {150, 20};
    begin(ui, input);
    require(!ui.button("capture", bounds, "Button"), "Dragging fired button");
    ui.end_frame();
    input.mouse_down = false;
    input.mouse_released = true;
    begin(ui, input);
    require(!ui.button("capture", bounds, "Button"), "Outside release fired button");
    ui.end_frame();
    input = {};
    input.mouse = {40, 20};
    input.mouse_pressed = true;
    input.mouse_down = true;
    begin(ui, input);
    ui.button("capture", bounds, "Button");
    ui.end_frame();
    input.mouse_pressed = false;
    input.mouse_down = false;
    input.mouse_released = true;
    begin(ui, input);
    require(ui.button("capture", bounds, "Button"), "Inside release did not click");
    ui.end_frame();
    input = {};
    input.mouse = {150, 20};
    input.mouse_pressed = true;
    input.mouse_down = true;
    begin(ui, input);
    ui.button("capture", bounds, "Button");
    ui.end_frame();
    input = {};
    input.mouse = {40, 20};
    input.mouse_released = true;
    begin(ui, input);
    require(!ui.button("capture", bounds, "Button"), "Outside press captured a button");
    ui.end_frame();
}

void test_slider(Fluid::Ui &ui) {
    const Fluid::Rect bounds{10, 10, 114, 24};
    float value = 0;
    Fluid::InputState input;
    input.mouse = {67, 20};
    input.mouse_pressed = true;
    input.mouse_down = true;
    begin(ui, input);
    require(ui.slider("slider", bounds, value), "Slider ignored pointer press");
    ui.end_frame();
    require(std::abs(value - 0.5f) < 0.0001f, "Slider did not map pointer to value");
    input.mouse_pressed = false;
    input.mouse = {500, 20};
    begin(ui, input);
    ui.slider("slider", bounds, value);
    ui.end_frame();
    require(value == 1, "Slider lost capture outside bounds");
    input = {};
    input.mouse_released = true;
    input.mouse = {500, 20};
    begin(ui, input);
    ui.slider("slider", bounds, value);
    ui.end_frame();
    input = {};
    input.keys_pressed[263] = true;
    begin(ui, input);
    ui.slider("slider", bounds, value);
    ui.end_frame();
    require(std::abs(value - 0.99f) < 0.0001f, "Focused slider ignored left arrow");
    input = {};
    input.keys_pressed[268] = true;
    begin(ui, input);
    ui.slider("slider", bounds, value);
    ui.end_frame();
    require(value == 0, "Slider Home did not choose minimum");
    input = {};
    input.keys_pressed[262] = true;
    input.keys_down[340] = true;
    begin(ui, input);
    ui.slider("slider", bounds, value);
    ui.end_frame();
    require(std::abs(value - 0.1f) < 0.0001f, "Slider Shift+Arrow step was wrong");
}

void test_focus(Fluid::Ui &ui) {
    bool enabled = false;
    auto controls = [&]() {
        const bool clicked = ui.button("first", {10, 10, 100, 32}, "First");
        ui.toggle("second", {10, 60, 40, 22}, enabled);
        return clicked;
    };
    begin(ui);
    controls();
    ui.end_frame();
    Fluid::InputState input;
    input.keys_pressed[258] = true;
    begin(ui, input);
    controls();
    ui.end_frame();
    input = {};
    input.keys_pressed[257] = true;
    begin(ui, input);
    require(controls(), "Tab then Enter failed to activate first button");
    ui.end_frame();
    input = {};
    input.keys_pressed[258] = true;
    begin(ui, input);
    controls();
    ui.end_frame();
    input = {};
    input.keys_pressed[32] = true;
    begin(ui, input);
    controls();
    ui.end_frame();
    require(enabled, "Tab then Space failed to toggle second widget");
    input = {};
    input.keys_pressed[258] = true;
    input.keys_down[340] = true;
    begin(ui, input);
    controls();
    ui.end_frame();
    input = {};
    input.keys_pressed[257] = true;
    begin(ui, input);
    require(controls(), "Shift+Tab did not return to previous widget");
    ui.end_frame();
    input = {};
    input.mouse = {550, 350};
    input.mouse_pressed = true;
    begin(ui, input);
    controls();
    ui.end_frame();
    input = {};
    input.keys_pressed[257] = true;
    begin(ui, input);
    require(!controls(), "Click outside did not clear focus");
    ui.end_frame();
}

void test_text(Fluid::Ui &ui) {
    const Fluid::Rect bounds{10, 10, 200, 32};
    std::string value = "A\xC3\xA9\xF0\x9F\x98\x80";
    auto field = [&](Fluid::InputState input) {
        begin(ui, input);
        const bool changed = ui.text_field("unicode", bounds, value);
        ui.end_frame();
        return changed;
    };
    Fluid::InputState input;
    input.mouse = {200, 20};
    input.mouse_pressed = true;
    input.mouse_down = true;
    field(input);
    input = {};
    input.mouse_released = true;
    field(input);
    input = {};
    input.keys_pressed[259] = true;
    require(field(input), "Backspace failed to edit field");
    require(value == "A\xC3\xA9", "Backspace split a four-byte UTF-8 character");
    field(input);
    require(value == "A", "Backspace split a two-byte UTF-8 character");
    input = {};
    input.text = "\xCE\xA9";
    field(input);
    require(value == "A\xCE\xA9", "UTF-8 input failed");
    input = {};
    input.keys_pressed[268] = true;
    field(input);
    input = {};
    input.keys_pressed[261] = true;
    field(input);
    require(value == "\xCE\xA9", "Home and Delete failed");
    input = {};
    input.keys_pressed[269] = true;
    field(input);
    input = {};
    input.text = "!";
    field(input);
    require(value == "\xCE\xA9!", "End did not position the caret");
    input = {};
    input.keys_down[341] = true;
    input.keys_pressed[65] = true;
    field(input);
    input = {};
    input.text = "hello\n";
    field(input);
    require(value == "hello", "Select-all replacement or single-line filtering failed");
    input = {};
    input.keys_pressed[256] = true;
    field(input);
    input = {};
    input.text = "unfocused";
    field(input);
    require(value == "hello", "Unfocused field accepted text");
}

void test_draw_list(Fluid::Ui &ui) {
    begin(ui);
    auto &draw = fluid_draw_list(ui);
    draw.rect({0, 0, 80, 80}, Fluid::Color::hex(0xFFFFFF), 8);
    draw.circle({40, 40}, 10, Fluid::Color::hex(0xFF0000));
    require(draw.commands.size() == 1, "Adjacent compatible geometry was not batched");
    draw.push_clip({20, 20, 40, 40});
    draw.rect({0, 0, 100, 100}, Fluid::Color::hex(0xFFFFFF));
    require(draw.commands.size() == 2, "Clip change did not split geometry command");
    draw.push_clip({50, 10, 100, 30});
    const auto clip = draw.current_clip();
    require(clip.x == 50 && clip.y == 20 && clip.w == 10 && clip.h == 20,
            "Nested clips were not intersected");
    draw.pop_clip();
    draw.pop_clip();
    Fluid::Mesh mesh;
    Fluid::SceneView scene;
    scene.mesh = &mesh;
    scene.bounds = {0, 0, 200, 200};
    draw.scene(scene);
    draw.text({10, 10}, "Overlay", 14, Fluid::Color::hex(0xFFFFFF));
    require(draw.commands.size() == 4, "Scene insertion lost painter order");
    require(draw.commands[2].kind == Fluid::DrawCommand::Kind::scene &&
                draw.commands[3].kind == Fluid::DrawCommand::Kind::triangles,
            "Overlay did not remain after scene");
    require(draw.scenes.size() == 1, "Scene list missing view");
    const auto before = draw.vertices.size();
    draw.push_clip({900, 900, 20, 20});
    draw.circle({900, 900}, 5, Fluid::Color::hex(0xFFFFFF));
    draw.pop_clip();
    require(draw.vertices.size() == before, "Empty clip emitted triangles");
    for (const auto &vertex : draw.vertices) {
        require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y),
                "Geometry contains a nonfinite coordinate");
        require(vertex.color.a >= 0 && vertex.color.a <= 1, "Geometry alpha outside normalized range");
    }
    ui.end_frame();
    require(ui.font().width() > 0 && ui.font().height() > 0, "Font atlas is empty");
    require(ui.font().pixels().size() == static_cast<std::size_t>(ui.font().width()) * ui.font().height() * 4,
            "Font atlas is not RGBA8");
    require(ui.font().measure("Hello", 16) > ui.font().measure("Hello", 8), "Font metrics do not scale");
    require(ui.font().measure("\xFF", 14) > 0, "Malformed UTF-8 did not use fallback glyph");
    const auto small = ui.font().glyph('A', false, 12);
    const auto large = ui.font().glyph('A', false, 64);
    require(small.advance > 0 && large.advance > small.advance * 2, "Large type did not grow");
    require(small.u0 != large.u0 || small.v0 != large.v0, "Small and large sizes share one baked glyph");
    const auto exact = ui.font().glyph('A', false, 13);
    const float texel_width = (exact.u1 - exact.u0) * ui.font().width();
    require(std::abs(texel_width - (exact.x1 - exact.x0)) < 0.01f, "13px glyph is not rasterized at its pixel size");
}

void test_button_style(Fluid::Ui &ui) {
    const unsigned char pixel[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const auto image = ui.add_image(pixel, 2, 2);
    require(image.id >= 0, "Image was not packed");
    Fluid::Vec2 uv0, uv1;
    require(ui.font().image_uv(image, uv0, uv1), "Packed image has no coordinates");
    const Fluid::Rect bounds{20, 30, 160, 48};
    Fluid::ButtonStyle left;
    left.align = Fluid::TextAlign::left;
    left.background = Fluid::Color::hex(0x3366CC);
    left.padding = 16;
    begin(ui);
    ui.button("left", bounds, "Left", left);
    float left_x = 1e9f;
    const auto white = ui.font().white_uv();
    for (const auto &vertex : fluid_draw_list(ui).vertices) {
        if (vertex.uv.x != white.x || vertex.uv.y != white.y)
            left_x = std::min(left_x, vertex.position.x);
    }
    ui.end_frame();
    Fluid::ButtonStyle right = left;
    right.align = Fluid::TextAlign::right;
    begin(ui);
    ui.button("right", bounds, "Left", right);
    float right_x = 1e9f;
    float text_top = 1e9f, text_bottom = -1e9f;
    for (const auto &vertex : fluid_draw_list(ui).vertices) {
        if (vertex.uv.x == white.x && vertex.uv.y == white.y)
            continue;
        right_x = std::min(right_x, vertex.position.x);
        text_top = std::min(text_top, vertex.position.y);
        text_bottom = std::max(text_bottom, vertex.position.y);
    }
    ui.end_frame();
    require(right_x > left_x + 20, "Right-aligned label did not move");
    const float mid = (text_top + text_bottom) * 0.5f;
    require(std::abs(mid - (bounds.y + bounds.h * 0.5f)) < 3.0f, "Button label is not vertically centered");
    Fluid::ButtonStyle pictured = left;
    pictured.image = image;
    pictured.align = Fluid::TextAlign::center;
    begin(ui);
    require(!ui.button("picture", bounds, "Picture", pictured), "Idle image button clicked");
    bool saw_image = false;
    for (const auto &vertex : fluid_draw_list(ui).vertices) {
        if (vertex.uv.x >= uv0.x && vertex.uv.x <= uv1.x && vertex.uv.y >= uv0.y && vertex.uv.y <= uv1.y)
            saw_image = true;
    }
    ui.end_frame();
    require(saw_image, "Image button did not sample its background");
    bool missing = false;
    try {
        ui.set_typeface("missing-fluid-font.ttf");
    } catch (const std::runtime_error &) {
        missing = true;
    }
    require(missing, "Missing font file was accepted");
    require(ui.font().measure("Hello", 16) > 0, "Font atlas was lost after a rejected typeface");
}

void test_backgrounded_text_and_list(Fluid::Ui &ui) {
    const Fluid::Rect chip{40, 50, 120, 32};
    Fluid::BackgroundedTextStyle style;
    style.background = Fluid::Color::hex(0x224466);
    style.text = Fluid::Color::hex(0xFFFFFF);
    style.align = Fluid::TextAlign::center;
    style.font_size = 14;
    begin(ui);
    ui.backgrounded_text(chip, "LIVE", style);
    float top = 1e9f, bottom = -1e9f;
    const auto white = ui.font().white_uv();
    for (const auto &vertex : fluid_draw_list(ui).vertices) {
        if (vertex.uv.x == white.x && vertex.uv.y == white.y)
            continue;
        top = std::min(top, vertex.position.y);
        bottom = std::max(bottom, vertex.position.y);
    }
    ui.end_frame();
    require(std::abs((top + bottom) * 0.5f - (chip.y + chip.h * 0.5f)) < 3.0f,
            "Backgrounded text is not vertically centered");

    const std::string_view items[]{"Overview", "Components", "Typography"};
    Fluid::SelectionListStyle list;
    list.item.align = Fluid::TextAlign::left;
    list.item.padding = 16;
    list.item.text = Fluid::Color::hex(0xAAAAAA);
    list.selected = list.item;
    list.selected.background = Fluid::Color::hex(0x334455);
    list.selected.text = Fluid::Color::hex(0xFFFFFF);
    list.hover_background = Fluid::Color::hex(0x222222);
    int selected = 0;
    Fluid::InputState input;
    input.mouse = {80, 50 + 36 + 8};
    input.mouse_pressed = true;
    input.mouse_down = true;
    begin(ui, input);
    require(!ui.selection_list("pages", {40, 50, 180, 3 * 36 + 2 * 6}, 36, items, selected, list),
            "Selection list clicked before release");
    ui.end_frame();
    input.mouse_pressed = false;
    input.mouse_down = false;
    input.mouse_released = true;
    begin(ui, input);
    require(ui.selection_list("pages", {40, 50, 180, 3 * 36 + 2 * 6}, 36, items, selected, list),
            "Selection list did not activate a row");
    ui.end_frame();
    require(selected == 1, "Selection list did not choose the second row");
}
} // namespace

int main() {
    try {
        Fluid::Ui ui;
        test_pointer_capture(ui);
        test_slider(ui);
        test_focus(ui);
        test_text(ui);
        test_draw_list(ui);
        test_button_style(ui);
        test_backgrounded_text_and_list(ui);
        std::cout << "Fluid UI tests passed: pointer capture, keyboard focus, sliders, UTF-8 editing, and "
                     "draw ordering.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "Fluid UI test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
