# Fluid

An immediate-mode UI library in C++20 and Vulkan. Two-dimensional controls and a depth-buffered 3D viewport share one window. Shaders are compiled into the library at build time, so a shipped program does not need a `shaders/` directory.

`Fluid_test` is the companion Fluid Studio. It exercises controls, text, animation, and models. It has its own CMake project and links the static library `Fluid::Fluid`.

## Requirements

To compile:

- CMake 3.20 or newer, and a C++20 compiler.
- The [Vulkan SDK](https://vulkan.lunarg.com/), including `glslc`. Set `VULKAN_SDK` before configuring.
- A GPU and driver that can run Vulkan.

Linux also needs the X11 or Wayland development packages GLFW uses. macOS needs a Vulkan implementation such as MoltenVK. GLFW, GLM, font rasterization, and PNG writing live in `third_party`. The build does not download anything else.

The SDK is a compile-time dependency (headers, `vulkan-1.lib`, and `glslc`). A shipped binary does not include the SDK. At run time it uses the loader that ships with the driver. On Windows, executables and the shared library `FluidC` copy `vulkan-1.dll` next to themselves after the build. The static library `Fluid` does not.

## Build the library and tests

```sh
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The defaults build the static library `Fluid::Fluid`, the C API shared library `Fluid::C`, and the CPU tests (models, UI, and the C API), which do not open a window. Window tests that need a GPU and a desktop are off unless you pass `-DFLUID_BUILD_GPU_TESTS=ON`. When embedding Fluid in another project, pass `-DFLUID_BUILD_TESTS=OFF` and `-DFLUID_BUILD_SHARED=OFF`.

Single-config generators write libraries into `build/`. Visual Studio multi-config generators write them into directories such as `build/Release/`. If the CMake on `PATH` is older than the installed Visual Studio, use the CMake that ships with Visual Studio.

## Fluid Studio

Configure it separately from the library:

```sh
cmake -S Fluid_test -B Fluid_test/build
cmake --build Fluid_test/build --config Release --parallel
```

The Visual Studio generator runs `Fluid_test/build/Release/FluidTest.exe`. A single-config generator runs `Fluid_test/build/FluidTest`.

Four pages:

- **Overview**: a torus knot, sphere, torus, or cube. Drag in the viewport to orbit and scroll to zoom. Change color, roughness, metallic, and exposure, and toggle rotation, the ground grid, wireframe, 3D antialiasing, ray tracing, and upscaling. The interface itself uses MSAA whenever the GPU supports it.
- **Components**: buttons, toggles, a text field, a slider, and a progress bar. Tab and Shift+Tab move focus, Enter or Space activates a control, and the arrow keys adjust the slider.
- **Typography**: edit a passage and change its size.
- **Motion**: pause, replay, and scrub a time-based 2D animation.

The top bar switches between dark and light themes and writes a PNG snapshot into `snapshots/` in the working directory. When the window is short, the property panel scrolls. Drop a Wavefront `.obj` onto the window, type a path in the scene properties, or pass `--model` on the command line. Import reads normals, negative indices, and concave polygons, then centers the model and scales it to a fixed size. Material libraries and textures are not imported.

```sh
FluidTest --frames 4 --hidden --screenshot fluid-studio.png
FluidTest --page components --frames 4 --hidden --screenshot components.png
FluidTest --smoke-test --hidden --screenshot smoke.png
FluidTest --help
```

`--smoke-test` switches pages and models, changes the appearance and wireframe, and resizes the window before it exits. Debug builds enable Vulkan validation when the validation layers are installed.

## Use it from C++

Link `Fluid::Fluid` and include `<fluid/fluid.h>`. `Fluid::window` owns the window, renderer, font atlas, and event loop. Override `tick()` and describe the interface each frame with stable control ids. Coordinates are logical window pixels. The renderer scales them to the framebuffer.

```cpp
#include <fluid/fluid.h>

class Example : public Fluid::window {
    Fluid::Mesh model = Fluid::Mesh::knot();
    Fluid::OrbitCamera camera;
    float roughness = 0.3f;
    bool rotating = true;
    float angle = 0;

    void tick() override {
        auto& controls = ui();
        controls.panel({0, 0, width(), height()}, 0, Fluid::BackgroundShape::rectangle);
        controls.panel({20, 20, 260, 180});
        controls.label({40, 40}, "Hello, Fluid", 24, true);
        controls.slider("roughness", {40, 85, 220, 24}, roughness, .05f, 1.f);
        controls.toggle("rotate", {40, 135, 42, 24}, rotating);

        Fluid::Rect bounds{300, 20, width() - 320, height() - 40};
        if (bounds.contains(input().mouse)) {
            if (input().mouse_down) camera.orbit(input().mouse_delta);
            camera.zoom(input().scroll);
        }
        if (rotating) angle += delta_time() * .25f;
        Fluid::SceneView scene;
        scene.bounds = bounds;
        scene.mesh = &model;
        scene.camera = camera;
        scene.rotation = angle;
        scene.roughness = roughness;
        controls.scene(scene);
    }
};

int main() {
    Example app;
    app.Init();
    app.loop();
}
```

A color is four float components in `Fluid::Color`, or `Color::hex(0xRRGGBB, alpha)`. Themes live in `Theme`. `Theme::dark()` and `Theme::light()` are the two defaults.

Controls record drawing in an internal list. Callers do not touch that list. A mesh pointer must stay alive until the end of the frame. The window and the event loop run on the main thread. `destroy()` may be called more than once, and the object also cleans itself up when it leaves scope.

### Controls

Controls that have a background can be a rectangle, a circle, or a rounded rectangle, and can take a vertical gradient. When an image is set, it replaces the solid fill and the background color tints it.

| Call | What it does |
| --- | --- |
| `button` | A button. The simple overloads use the theme. `ButtonStyle` sets the background, gradient, image, border, alignment, padding, radius, and font size. |
| `toggle` / `slider` / `text_field` / `progress` | A switch, a slider, a single-line field, and a progress bar. |
| `panel` | A panel with a shadow and a border. The fill comes from the theme. |
| `selection_list` | A vertical list of choices. Selected and idle rows have their own background and text styles. Activation writes the index back. |
| `backgrounded_text` | One background and one line of text. The background can be a solid color, a gradient, or an image. |
| `label` | Text at a position, in the theme color or a color you pass. |
| `line` | A line segment. |
| `push_clip` / `pop_clip` | Clip later drawing. |
| `scene` | Draw a 3D mesh inside a rectangle. |
| `hit` / `hovered` | A click with a stable id, or a query for whether the pointer is over a rectangle. |

`BackgroundShape` is `rectangle`, `circle`, or `rounded_rectangle`. A circle on a non-square area is a capsule. Text alignment is `TextAlign::left`, `center`, or `right`.

### Text and images

Glyphs are rasterized on demand at the requested pixel size and drawn on pixel boundaries. The library looks for Segoe UI, Arial, DejaVu Sans, and Liberation Sans, then falls back to an embedded font. `set_typeface` loads your own TTF. The fonts that ship those glyphs cover Latin, Greek, Cyrillic, and punctuation, and text fields edit UTF-8. Complex shaping, IME composition, and a system accessibility bridge are not implemented.

`add_image` takes RGBA pixels. On Windows, `add_image_file` reads common image files through WIC. On other platforms, decode the file yourself and pass the pixels to `add_image`. The atlas is uploaded to the GPU on the next frame.

## C API

`FLUID_BUILD_SHARED` defaults to on and produces the shared-library target `Fluid::C`. Declarations are in `include/fluid/capi.h`. On the C side, shape values are `0` for a rounded rectangle, `1` for a rectangle, and `2` for a circle. Alignment is `0` for center, `1` for left, and `2` for right. Read the last error with `fluid_last_error()`.

## What is not here yet

This is an immediate-mode implementation: the caller supplies coordinates and submits the interface once per frame. It is a base for a native tool. Docking, automatic layout, material and texture import, and a retained widget tree are not included. 3D antialiasing, hardware ray tracing, and upscaling apply only to the 3D viewport, not to the 2D interface.
