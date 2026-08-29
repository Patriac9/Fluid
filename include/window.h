//
// Created by Frank Zha on 8/3/26.
//

#ifndef FLUID_WINDOW_H
#define FLUID_WINDOW_H
#include <GLFW/glfw3.h>
#include <string>
#include <array>
#include <exception>
#include <vector>
#include <cstdint>
#include <vulkan/vulkan.h>

struct window_cfg {
    bool fullscreen;
    bool resizable;
    uint32_t width;
    uint32_t height;
    std::string window_title;
    GLFWmonitor* monitor;
    GLFWwindow* share;
};

struct application_cfg {
    std::string app_name;
    std::array<uint32_t,3> app_version;
    std::string engine_name;
    std::array<uint32_t,3> engine_version;
};
namespace Fluid {
    class window {
        public:
        window(window_cfg cfg);
        ~window();
        void Init(const application_cfg &app_cfg);
        void destroy();

        void loop();


    private:
        VkInstance instance;
        GLFWwindow* app_window;
        bool check_validation_layers_support();
        std::vector<const char*> get_required_extensions();

        const std::vector<const char*> validation_layers = {
            "VK_LAYER_KHRONOS_validation"
        };

#ifdef NDEBUG
        const bool enable_validation_layers = true;
#else
        const bool enable_validation_layers = true;
#endif

    protected:
        virtual void tick();

    };
}

#endif //FLUID_WINDOW_H