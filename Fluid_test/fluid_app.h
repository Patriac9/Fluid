//
// Created by Frank Zha on 8/3/26.
//

#ifndef FLUID_FLUID_APP_H
#define FLUID_FLUID_APP_H

#endif //FLUID_FLUID_APP_H

#include <window.h>

class fluid_test {
public:
    fluid_test();
    ~fluid_test();
    void run();
    void init();

private:
    window_cfg win_cfg = {false, false,
        800,600, "vulkan triangle", nullptr,nullptr
    };

private:
    Fluid::window main_window;
};