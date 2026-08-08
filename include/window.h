//
// Created by Frank Zha on 8/3/26.
//

#ifndef FLUID_WINDOW_H
#define FLUID_WINDOW_H
#include <GLFW/glfw3.h>
#include <string>
#include <exception>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace Fluid {
    class window {
        public:
        window(uint32_t width, uint32_t height, std::string title, bool resizable, bool fullscreen);
        ~window();
        void Init(std::string app_name);

        void loop();

        private:
        VkInstance instance;
        GLFWwindow* app_window;

    protected:
        virtual void tick();

    };
}

#endif //FLUID_WINDOW_H