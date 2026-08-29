//
// Created by Frank Zha on 8/3/26.
//

#include "fluid_app.h"

#include <iostream>
#include <ostream>

fluid_test::fluid_test() : main_window(win_cfg) {

}

void fluid_test::init() {
    application_cfg app_cfg;
    app_cfg.app_name = "fluid_test";
    app_cfg.engine_name = "fluid_test";
    app_cfg.app_version = std::array<uint32_t, 3>{1,0,0};
    app_cfg.engine_version = std::array<uint32_t, 3>{1,0,0};
    main_window.Init(app_cfg);
}

void fluid_test::run() {
    try {
        main_window.loop();
    }catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    main_window.destroy();
    return;
}
