#include "Fluid_test/fluid_app.h"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    try {
        DemoOptions options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (arg == "--frames") {
                auto number = value();
                auto result = std::from_chars(number.data(), number.data() + number.size(), options.frames);
                if (result.ec != std::errc{} || result.ptr != number.data() + number.size() ||
                    options.frames == 0)
                    throw std::invalid_argument("--frames must be a positive integer");
            } else if (arg == "--screenshot")
                options.screenshot_path = value();
            else if (arg == "--model")
                options.model_path = value();
            else if (arg == "--page") {
                options.page = value();
                if (options.page != "overview" && options.page != "components" &&
                    options.page != "typography" && options.page != "motion")
                    throw std::invalid_argument("--page must be overview, components, typography, or motion");
            } else if (arg == "--hidden")
                options.hidden = true;
            else if (arg == "--scene-aa")
                options.scene_aa = true;
            else if (arg == "--ray-tracing")
                options.ray_tracing = true;
            else if (arg == "--dlss")
                options.dlss = true;
            else if (arg == "--smoke-test") {
                options.smoke_test = true;
                if (!options.frames)
                    options.frames = 16;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "Fluid Studio - the Fluid GUI and 3D rendering showcase\n"
                             "  --model file.obj     Open a Wavefront OBJ model\n"
                             "  --page NAME          overview, components, typography, motion\n"
                             "  --frames N           Exit after N rendered frames\n"
                             "  --screenshot file.png Capture the final frame (use with --frames)\n"
                             "  --scene-aa           Enable 3D MSAA for every model in the window\n"
                             "  --ray-tracing        Enable 3D hardware ray tracing when available\n"
                             "  --dlss               Enable 3D DLSS-quality upscaling\n"
                             "  --hidden             Create a hidden window for render checks\n"
                             "  --smoke-test         Exercise pages, resize, and material modes\n";
                return 0;
            } else
                throw std::invalid_argument("Unknown argument: " + arg);
        }
        fluid_test app(options);
        app.init();
        app.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Fluid: " << error.what() << '\n';
        return 1;
    }
}
