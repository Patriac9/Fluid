//
// Created by Frank Zha on 8/3/26.
//

#include "fluid_app.h"

#include <iostream>
#include <ostream>

fluid_test::fluid_test() : main_window(800,600, "vulkan triangle", false, false) {

}

void fluid_test::init() {
    main_window.Init("vulkan triangle");
}

void fluid_test::run() {
    try {
        main_window.loop();
    }catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return;
}
