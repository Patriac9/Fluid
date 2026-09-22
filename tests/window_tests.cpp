#include "window.h"
#include <filesystem>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void check_png(const std::filesystem::path &path, unsigned width, unsigned height) {
    std::ifstream file(path, std::ios::binary);
    unsigned char header[24]{};
    file.read(reinterpret_cast<char *>(header), sizeof(header));
    require(file.gcount() == 24 && header[0] == 137 && header[1] == 'P' && header[2] == 'N' &&
                header[3] == 'G',
            "Screenshot is not a PNG");
    auto number = [&](unsigned offset) {
        return (static_cast<std::uint32_t>(header[offset]) << 24) |
               (static_cast<std::uint32_t>(header[offset + 1]) << 16) |
               (static_cast<std::uint32_t>(header[offset + 2]) << 8) | header[offset + 3];
    };
    require(number(16) == width && number(20) == height,
            "Screenshot dimensions do not match the framebuffer");
    file.close();
    std::filesystem::remove(path);
}
window_cfg hidden_config() {
    window_cfg cfg;
    cfg.width = 640;
    cfg.height = 480;
    cfg.visible = false;
    cfg.window_title = "Fluid GPU lifecycle check";
    return cfg;
}
class ProbeWindow final : public Fluid::window {
  public:
    explicit ProbeWindow(bool resize = false) : Fluid::window(hidden_config()), resize_(resize) {}
    unsigned ticks = 0;
    std::filesystem::path resize_capture;

  protected:
    void tick() override {
        ++ticks;
        ui().panel({0, 0, width(), height()}, 0, Fluid::BackgroundShape::rectangle);
        ui().panel({20, 20, 220, 100}, 16);
        ui().label({36, 50}, "Fluid lifecycle", 22, Fluid::Color::hex(0x18121F), true);
        if (resize_ && ticks == 2)
            glfwSetWindowSize(native_handle(), 720, 520);
        if (resize_ && ticks == 4 && !resize_capture.empty())
            screenshot(resize_capture.string());
    }

  private:
    bool resize_;
};
class QualityWindow final : public Fluid::window {
  public:
    QualityWindow() : Fluid::window(hidden_config()) {
        set_scene_aa(true);
        set_ray_tracing(true);
        set_dlss(true);
    }

  protected:
    void tick() override {
        Fluid::SceneView scene;
        scene.bounds = {0, 0, width(), height()};
        scene.mesh = &cube_;
        scene.grid = false;
        ui().scene(scene);
    }

  private:
    Fluid::Mesh cube_ = Fluid::Mesh::cube();
};
} // namespace
int main() {
    try {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        // Both windows must retain the GLFW runtime while their peer is closed.
        {
            ProbeWindow first;
            ProbeWindow second(true);
            second.resize_capture =
                std::filesystem::current_path() / ("fluid-resize-test-" + suffix + ".png");
            first.Init();
            first.Init();
            second.Init();
            require(!first.device_name().empty(), "Missing device name");
            first.set_frame_limit(1);
            first.loop();
            require(first.ticks == 1, "Frame limit was not honored");
            first.destroy();
            first.destroy();
            require(first.native_handle() == nullptr, "Destroyed window still has a native handle");
            bool rejected = false;
            try {
                first.Init();
            } catch (const std::logic_error &) {
                rejected = true;
            }
            require(rejected, "Destroyed window accepted initialization");
            second.set_frame_limit(5);
            second.loop();
            require(second.ticks >= 5, "Surviving window stopped rendering");
            require(second.width() == 720 && second.height() == 520,
                    "Resize did not update logical dimensions");
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(second.native_handle(), &fw, &fh);
            check_png(second.resize_capture, static_cast<unsigned>(fw), static_cast<unsigned>(fh));
        }
        // GLFW and Vulkan can start again after the final window is destroyed.
        {
            ProbeWindow third;
            auto png = std::filesystem::current_path() / std::filesystem::path(u8"fluid-\u6d4b\u8bd5-");
            png += suffix + ".png";
            const auto png_utf8 = png.u8string();
            third.screenshot(
                std::string(png_utf8.begin(), png_utf8.end())); // UTF-8 captures also work before Init.
            third.Init();
            third.set_frame_limit(2);
            third.loop();
            int fw = 0, fh = 0;
            glfwGetFramebufferSize(third.native_handle(), &fw, &fh);
            check_png(png, static_cast<unsigned>(fw), static_cast<unsigned>(fh));
        }
        {
            QualityWindow quality;
            quality.Init();
            require(quality.scene_aa(), "Scene AA request was not stored");
            require(quality.dlss(), "DLSS request was not stored");
            require(quality.dlss_available(), "DLSS path should always be available");
            quality.set_frame_limit(2);
            quality.loop();
        }
        bool rejected = false;
        try {
            auto cfg = hidden_config();
            cfg.width = 0;
            Fluid::window invalid(cfg);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "Zero-sized window was accepted");
        std::cout << "Fluid GPU lifecycle checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Fluid GPU lifecycle test: " << error.what() << '\n';
        return 1;
    }
}
