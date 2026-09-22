#pragma once

#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLUID_VERSION_MAJOR 0
#define FLUID_VERSION_MINOR 1
#define FLUID_VERSION_PATCH 0
#define FLUID_KEY_COUNT 512

typedef struct Fluid_Window Fluid_Window;
typedef struct Fluid_Mesh Fluid_Mesh;
typedef struct Fluid_Ui Fluid_Ui;

typedef struct Fluid_Vec2 {
    float x;
    float y;
} Fluid_Vec2;

typedef struct Fluid_Rect {
    float x;
    float y;
    float w;
    float h;
} Fluid_Rect;

typedef struct Fluid_Color {
    float r;
    float g;
    float b;
    float a;
} Fluid_Color;

typedef struct Fluid_Theme {
    Fluid_Color background;
    Fluid_Color panel;
    Fluid_Color elevated;
    Fluid_Color border;
    Fluid_Color text;
    Fluid_Color muted;
    Fluid_Color accent;
    Fluid_Color accent_text;
    Fluid_Color positive;
} Fluid_Theme;

typedef struct Fluid_Input {
    Fluid_Vec2 mouse;
    Fluid_Vec2 mouse_delta;
    int mouse_down;
    int mouse_pressed;
    int mouse_released;
    int right_down;
    int right_pressed;
    float scroll;
    int keys_down[FLUID_KEY_COUNT];
    int keys_pressed[FLUID_KEY_COUNT];
    const char *text;
    const char *const *dropped_paths;
    int dropped_count;
} Fluid_Input;

typedef struct Fluid_OrbitCamera {
    float yaw;
    float pitch;
    float distance;
} Fluid_OrbitCamera;

typedef struct Fluid_SceneView {
    Fluid_Rect bounds;
    const Fluid_Mesh *mesh;
    Fluid_OrbitCamera camera;
    Fluid_Color tint;
    float rotation;
    float metallic;
    float roughness;
    float exposure;
    int wireframe;
    int grid;
} Fluid_SceneView;

typedef struct Fluid_WindowConfig {
    int fullscreen;
    int resizable;
    uint32_t width;
    uint32_t height;
    const char *title;
    int visible;
} Fluid_WindowConfig;

typedef struct Fluid_AppConfig {
    const char *app_name;
    uint32_t app_version[3];
    const char *engine_name;
    uint32_t engine_version[3];
} Fluid_AppConfig;

typedef void (*Fluid_TickFn)(Fluid_Window *window, void *user);

FLUID_C_API const char *fluid_version(void);
FLUID_C_API const char *fluid_platform(void);
FLUID_C_API const char *fluid_last_error(void);

FLUID_C_API Fluid_Color fluid_color_hex(uint32_t rgb, float alpha);
FLUID_C_API Fluid_Color fluid_color_opacity(Fluid_Color color, float amount);
FLUID_C_API int fluid_rect_contains(Fluid_Rect rect, Fluid_Vec2 point);
FLUID_C_API Fluid_Theme fluid_theme_dark(void);
FLUID_C_API Fluid_Theme fluid_theme_light(void);

FLUID_C_API Fluid_OrbitCamera fluid_camera_default(void);
FLUID_C_API void fluid_camera_orbit(Fluid_OrbitCamera *camera, Fluid_Vec2 delta);
FLUID_C_API void fluid_camera_zoom(Fluid_OrbitCamera *camera, float scroll);
FLUID_C_API void fluid_camera_reset(Fluid_OrbitCamera *camera);
FLUID_C_API void fluid_camera_view(const Fluid_OrbitCamera *camera, float out_mat4[16]);
FLUID_C_API void fluid_camera_projection(const Fluid_OrbitCamera *camera, float aspect, float out_mat4[16]);
FLUID_C_API Fluid_SceneView fluid_scene_view_default(void);

FLUID_C_API Fluid_Mesh *fluid_mesh_sphere(uint32_t segments, uint32_t rings);
FLUID_C_API Fluid_Mesh *fluid_mesh_torus(uint32_t segments, uint32_t sides);
FLUID_C_API Fluid_Mesh *fluid_mesh_knot(uint32_t segments, uint32_t sides);
FLUID_C_API Fluid_Mesh *fluid_mesh_cube(void);
FLUID_C_API Fluid_Mesh *fluid_mesh_load_obj(const char *path);
FLUID_C_API void fluid_mesh_destroy(Fluid_Mesh *mesh);
FLUID_C_API const char *fluid_mesh_name(const Fluid_Mesh *mesh);
FLUID_C_API uint32_t fluid_mesh_vertex_count(const Fluid_Mesh *mesh);
FLUID_C_API uint32_t fluid_mesh_index_count(const Fluid_Mesh *mesh);
FLUID_C_API int fluid_mesh_normalize(Fluid_Mesh *mesh);

FLUID_C_API Fluid_WindowConfig fluid_window_config_default(void);
FLUID_C_API Fluid_AppConfig fluid_app_config_default(void);
FLUID_C_API Fluid_Window *fluid_window_create(const Fluid_WindowConfig *config);
FLUID_C_API void fluid_window_destroy(Fluid_Window *window);
FLUID_C_API void fluid_window_set_tick(Fluid_Window *window, Fluid_TickFn tick, void *user);
FLUID_C_API int fluid_window_init(Fluid_Window *window, const Fluid_AppConfig *config);
FLUID_C_API int fluid_window_loop(Fluid_Window *window);
FLUID_C_API Fluid_Ui *fluid_window_ui(Fluid_Window *window);
FLUID_C_API int fluid_window_input(Fluid_Window *window, Fluid_Input *out);
FLUID_C_API void *fluid_window_native_handle(const Fluid_Window *window);
FLUID_C_API float fluid_window_width(const Fluid_Window *window);
FLUID_C_API float fluid_window_height(const Fluid_Window *window);
FLUID_C_API float fluid_window_delta_time(const Fluid_Window *window);
FLUID_C_API double fluid_window_elapsed_time(const Fluid_Window *window);
FLUID_C_API const char *fluid_window_device_name(const Fluid_Window *window);
FLUID_C_API void fluid_window_set_frame_limit(Fluid_Window *window, uint32_t frames);
FLUID_C_API int fluid_window_screenshot(Fluid_Window *window, const char *path);
FLUID_C_API void fluid_window_set_scene_aa(Fluid_Window *window, int enabled);
FLUID_C_API int fluid_window_scene_aa(const Fluid_Window *window);
FLUID_C_API void fluid_window_set_ray_tracing(Fluid_Window *window, int enabled);
FLUID_C_API int fluid_window_ray_tracing(const Fluid_Window *window);
FLUID_C_API int fluid_window_ray_tracing_available(const Fluid_Window *window);
FLUID_C_API void fluid_window_set_dlss(Fluid_Window *window, int enabled);
FLUID_C_API int fluid_window_dlss(const Fluid_Window *window);
FLUID_C_API int fluid_window_dlss_available(const Fluid_Window *window);

FLUID_C_API void fluid_ui_theme(const Fluid_Ui *ui, Fluid_Theme *out);
FLUID_C_API void fluid_ui_set_theme(Fluid_Ui *ui, Fluid_Theme theme);
typedef struct Fluid_ButtonStyle {
    Fluid_Color background;
    int use_background;
    Fluid_Color text_color;
    int use_text_color;
    Fluid_Color border;
    int use_border;
    int image_id;
    int align; /* 0 center, 1 left, 2 right */
    float padding;
    float radius;
    float font_size;
    int bold;
    int shape; /* 0 rounded rectangle, 1 rectangle, 2 circle */
    Fluid_Color gradient;
    int use_gradient;
} Fluid_ButtonStyle;

typedef struct Fluid_BackgroundedTextStyle {
    Fluid_Color background;
    int use_background;
    Fluid_Color text_color;
    int use_text_color;
    Fluid_Color border;
    int use_border;
    int image_id;
    int align; /* 0 center, 1 left, 2 right */
    float padding;
    float radius;
    float font_size;
    int bold;
    int shape; /* 0 rounded rectangle, 1 rectangle, 2 circle */
    Fluid_Color gradient;
    int use_gradient;
} Fluid_BackgroundedTextStyle;

FLUID_C_API int fluid_ui_set_typeface(Fluid_Ui *ui, const char *regular_path, const char *bold_path);
FLUID_C_API int fluid_ui_add_image(Fluid_Ui *ui, const unsigned char *rgba, int width, int height);
FLUID_C_API int fluid_ui_add_image_file(Fluid_Ui *ui, const char *path);
FLUID_C_API int fluid_ui_button(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, const char *label,
                               int primary, int selected);
FLUID_C_API int fluid_ui_button_styled(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, const char *label,
                                      const Fluid_ButtonStyle *style);
FLUID_C_API int fluid_ui_toggle(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, int *value);
FLUID_C_API int fluid_ui_slider(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, float *value, float min,
                               float max);
FLUID_C_API int fluid_ui_text_field(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, char *buffer,
                                   int buffer_size, const char *placeholder);
FLUID_C_API int fluid_ui_hit(Fluid_Ui *ui, const char *id, Fluid_Rect bounds);
FLUID_C_API int fluid_ui_hovered(const Fluid_Ui *ui, Fluid_Rect bounds);
FLUID_C_API void fluid_ui_label(Fluid_Ui *ui, Fluid_Vec2 position, const char *text, float size, int bold);
FLUID_C_API void fluid_ui_backgrounded_text(Fluid_Ui *ui, Fluid_Rect bounds, const char *text,
                                          const Fluid_BackgroundedTextStyle *style);
FLUID_C_API int fluid_ui_selection_list(Fluid_Ui *ui, const char *id, Fluid_Rect bounds, float item_height,
                                       const char *const *labels, int count, int *selected,
                                       const Fluid_BackgroundedTextStyle *item,
                                       const Fluid_BackgroundedTextStyle *selected_style, int use_hover,
                                       Fluid_Color hover, float gap);
FLUID_C_API void fluid_ui_panel(Fluid_Ui *ui, Fluid_Rect bounds, float radius);
FLUID_C_API void fluid_ui_progress(Fluid_Ui *ui, Fluid_Rect bounds, float value, Fluid_Color color);
FLUID_C_API float fluid_ui_measure_text(const Fluid_Ui *ui, const char *text, float size, int bold);
FLUID_C_API void fluid_ui_line(Fluid_Ui *ui, Fluid_Vec2 from, Fluid_Vec2 to, Fluid_Color color, float thickness);
FLUID_C_API void fluid_ui_push_clip(Fluid_Ui *ui, Fluid_Rect clip);
FLUID_C_API void fluid_ui_pop_clip(Fluid_Ui *ui);
FLUID_C_API void fluid_ui_scene(Fluid_Ui *ui, const Fluid_SceneView *view);

#ifdef __cplusplus
}
#endif
