#include "fluid/capi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

static void require(int condition, const char *message) {
    if (condition)
        return;
    fprintf(stderr, "Fluid C API: %s\n", message);
    ++g_failures;
}

int main(void) {
    require(strcmp(fluid_version(), "0.1.0") == 0, "unexpected version string");
    require(fluid_platform()[0] != '\0', "platform string");

    const Fluid_Color accent = fluid_color_hex(0xB6A2FF, 1.0f);
    require(fabs(accent.r - 182.0f / 255.0f) < 0.001f, "hex red channel");
    const Fluid_Color faded = fluid_color_opacity(accent, 0.5f);
    require(fabs(faded.a - 0.5f) < 0.001f, "opacity alpha");

    const Fluid_Rect bounds = {10.0f, 20.0f, 100.0f, 50.0f};
    const Fluid_Vec2 inside = {50.0f, 40.0f};
    const Fluid_Vec2 outside = {200.0f, 40.0f};
    require(fluid_rect_contains(bounds, inside) == 1, "point should be inside");
    require(fluid_rect_contains(bounds, outside) == 0, "point should be outside");

    Fluid_Theme dark = fluid_theme_dark();
    Fluid_Theme light = fluid_theme_light();
    require(dark.background.r < light.background.r, "light theme is brighter");

    Fluid_OrbitCamera camera = fluid_camera_default();
    const float yaw = camera.yaw;
    fluid_camera_orbit(&camera, (Fluid_Vec2){10.0f, 0.0f});
    require(camera.yaw != yaw, "orbit should change yaw");
    fluid_camera_reset(&camera);
    require(fabs(camera.distance - 4.6f) < 0.001f, "reset distance");

    float view[16];
    memset(view, 0, sizeof(view));
    fluid_camera_view(&camera, view);
    require(view[0] != 0.0f || view[5] != 0.0f, "view matrix should be populated");

    Fluid_Mesh *knot = fluid_mesh_knot(64, 8);
    require(knot != NULL, fluid_last_error());
    require(fluid_mesh_vertex_count(knot) > 0, "knot has vertices");
    require(fluid_mesh_index_count(knot) % 3 == 0, "knot has triangle indices");
    require(strcmp(fluid_mesh_name(knot), "Torus knot") == 0, "knot name");
    fluid_mesh_destroy(knot);

    Fluid_Mesh *missing = fluid_mesh_load_obj("definitely-missing-fluid-model.obj");
    require(missing == NULL, "missing OBJ should fail");
    require(strstr(fluid_last_error(), "OBJ") != NULL, "OBJ error mentions file type");

    Fluid_Mesh *cube = fluid_mesh_cube();
    require(cube != NULL, fluid_last_error());
    require(fluid_mesh_normalize(cube) == 1, "cube normalize");
    fluid_mesh_destroy(cube);

    require(fluid_window_scene_aa(NULL) == 0, "null window scene aa");
    require(fluid_window_ray_tracing(NULL) == 0, "null window ray tracing");
    require(fluid_window_dlss(NULL) == 0, "null window dlss");
    fluid_window_set_scene_aa(NULL, 1);
    fluid_window_set_ray_tracing(NULL, 1);
    fluid_window_set_dlss(NULL, 1);

    if (g_failures) {
        fprintf(stderr, "Fluid C API: %d check(s) failed\n", g_failures);
        return 1;
    }
    return 0;
}
