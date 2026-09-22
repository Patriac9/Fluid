#include "fluid/ui.h"

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
    auto &draw = ui.draw();
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
        std::cout << "Fluid UI tests passed: pointer capture, keyboard focus, sliders, UTF-8 editing, and "
                     "draw ordering.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "Fluid UI test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
