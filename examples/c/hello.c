#include <fluid/capi.h>

#include <stdio.h>

static Fluid_Mesh *g_mesh;
static Fluid_OrbitCamera g_camera;
static float g_roughness = 0.3f;
static float g_angle;
static int g_rotating = 1;

static void tick(Fluid_Window *window, void *user) {
    (void)user;
    Fluid_Ui *ui = fluid_window_ui(window);
    Fluid_DrawList *draw = fluid_ui_draw(ui);
    Fluid_Theme theme;
    Fluid_Input input;
    fluid_ui_theme(ui, &theme);
    fluid_window_input(window, &input);

    Fluid_Rect background = {0.0f, 0.0f, fluid_window_width(window), fluid_window_height(window)};
    fluid_draw_rect(draw, background, theme.background, 0.0f);

    Fluid_Rect panel = {20.0f, 20.0f, 260.0f, 180.0f};
    fluid_ui_panel(ui, panel, 12.0f);
    fluid_ui_label(ui, (Fluid_Vec2){40.0f, 40.0f}, "Hello from C", 24.0f, 1);
    fluid_ui_slider(ui, "roughness", (Fluid_Rect){40.0f, 85.0f, 220.0f, 24.0f}, &g_roughness, 0.05f, 1.0f);
    fluid_ui_toggle(ui, "rotate", (Fluid_Rect){40.0f, 135.0f, 42.0f, 24.0f}, &g_rotating);

    Fluid_Rect viewport = {300.0f, 20.0f, fluid_window_width(window) - 320.0f,
                           fluid_window_height(window) - 40.0f};
    if (fluid_rect_contains(viewport, input.mouse)) {
        if (input.mouse_down)
            fluid_camera_orbit(&g_camera, input.mouse_delta);
        fluid_camera_zoom(&g_camera, input.scroll);
    }
    if (g_rotating)
        g_angle += fluid_window_delta_time(window) * 0.25f;

    Fluid_SceneView scene = fluid_scene_view_default();
    scene.bounds = viewport;
    scene.mesh = g_mesh;
    scene.camera = g_camera;
    scene.rotation = g_angle;
    scene.roughness = g_roughness;
    fluid_draw_scene(draw, &scene);
}

int main(void) {
    g_mesh = fluid_mesh_knot(192, 20);
    if (!g_mesh) {
        fprintf(stderr, "Fluid: %s\n", fluid_last_error());
        return 1;
    }
    g_camera = fluid_camera_default();

    Fluid_WindowConfig config = fluid_window_config_default();
    config.title = "Fluid C ABI";
    Fluid_Window *window = fluid_window_create(&config);
    if (!window) {
        fprintf(stderr, "Fluid: %s\n", fluid_last_error());
        fluid_mesh_destroy(g_mesh);
        return 1;
    }
    fluid_window_set_tick(window, tick, NULL);
    if (!fluid_window_init(window, NULL) || !fluid_window_loop(window)) {
        fprintf(stderr, "Fluid: %s\n", fluid_last_error());
        fluid_window_destroy(window);
        fluid_mesh_destroy(g_mesh);
        return 1;
    }
    fluid_window_destroy(window);
    fluid_mesh_destroy(g_mesh);
    return 0;
}
