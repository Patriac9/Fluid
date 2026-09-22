#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fluid {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool contains(Vec2 point) const {
        return w > 0.0f && h > 0.0f && point.x >= x && point.y >= y && point.x < x + w && point.y < y + h;
    }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    static constexpr Color hex(std::uint32_t rgb, float alpha = 1.0f) {
        return {static_cast<float>((rgb >> 16) & 255) / 255.0f, static_cast<float>((rgb >> 8) & 255) / 255.0f,
                static_cast<float>(rgb & 255) / 255.0f, alpha};
    }
    constexpr Color opacity(float amount) const { return {r, g, b, a * amount}; }
};

struct InputState {
    Vec2 mouse{};
    Vec2 mouse_delta{};
    bool mouse_down = false;
    bool mouse_pressed = false;
    bool mouse_released = false;
    bool right_down = false;
    bool right_pressed = false;
    float scroll = 0.0f;
    std::array<bool, 512> keys_down{};
    std::array<bool, 512> keys_pressed{};
    std::string text;
    std::vector<std::string> dropped_paths;
};

struct Theme {
    Color background = Color::hex(0x101115);
    Color panel = Color::hex(0x181A20);
    Color elevated = Color::hex(0x22252D);
    Color border = Color::hex(0x30333D);
    Color text = Color::hex(0xF3F4F7);
    Color muted = Color::hex(0x9298A8);
    Color accent = Color::hex(0xB6A2FF);
    Color accent_text = Color::hex(0x191329);
    Color positive = Color::hex(0x85DAB0);
    static Theme dark() { return {}; }
    static Theme light() {
        Theme theme;
        theme.background = Color::hex(0xF2F3F7);
        theme.panel = Color::hex(0xFFFFFF);
        theme.elevated = Color::hex(0xEAECF2);
        theme.border = Color::hex(0xD8DCE7);
        theme.text = Color::hex(0x20232E);
        theme.muted = Color::hex(0x70788C);
        theme.accent = Color::hex(0x8061D4);
        theme.accent_text = Color::hex(0xFFFFFF);
        theme.positive = Color::hex(0x278967);
        return theme;
    }
};

} // namespace Fluid
