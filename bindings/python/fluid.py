"""ctypes bindings for the Fluid C ABI (fluid.dll / libfluid.so / libfluid.dylib)."""

from __future__ import annotations

import ctypes
import os
import sys
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_char_p,
    c_float,
    c_int,
    c_uint32,
    c_void_p,
)
from ctypes.util import find_library
from pathlib import Path
from typing import Optional

FLUID_KEY_COUNT = 512
TickFn = CFUNCTYPE(None, c_void_p, c_void_p)


class Vec2(Structure):
    _fields_ = [("x", c_float), ("y", c_float)]


class Rect(Structure):
    _fields_ = [("x", c_float), ("y", c_float), ("w", c_float), ("h", c_float)]


class Color(Structure):
    _fields_ = [("r", c_float), ("g", c_float), ("b", c_float), ("a", c_float)]


class Theme(Structure):
    _fields_ = [
        ("background", Color),
        ("panel", Color),
        ("elevated", Color),
        ("border", Color),
        ("text", Color),
        ("muted", Color),
        ("accent", Color),
        ("accent_text", Color),
        ("positive", Color),
    ]


class Input(Structure):
    _fields_ = [
        ("mouse", Vec2),
        ("mouse_delta", Vec2),
        ("mouse_down", c_int),
        ("mouse_pressed", c_int),
        ("mouse_released", c_int),
        ("right_down", c_int),
        ("right_pressed", c_int),
        ("scroll", c_float),
        ("keys_down", c_int * FLUID_KEY_COUNT),
        ("keys_pressed", c_int * FLUID_KEY_COUNT),
        ("text", c_char_p),
        ("dropped_paths", POINTER(c_char_p)),
        ("dropped_count", c_int),
    ]


class OrbitCamera(Structure):
    _fields_ = [("yaw", c_float), ("pitch", c_float), ("distance", c_float)]


class SceneView(Structure):
    _fields_ = [
        ("bounds", Rect),
        ("mesh", c_void_p),
        ("camera", OrbitCamera),
        ("tint", Color),
        ("rotation", c_float),
        ("metallic", c_float),
        ("roughness", c_float),
        ("exposure", c_float),
        ("wireframe", c_int),
        ("grid", c_int),
    ]


class WindowConfig(Structure):
    _fields_ = [
        ("fullscreen", c_int),
        ("resizable", c_int),
        ("width", c_uint32),
        ("height", c_uint32),
        ("title", c_char_p),
        ("visible", c_int),
    ]


def _library_names() -> tuple[str, ...]:
    if sys.platform.startswith("win"):
        return ("fluid.dll", "libfluid.dll")
    if sys.platform == "darwin":
        return ("libfluid.dylib", "libfluid.0.dylib")
    return ("libfluid.so", "libfluid.so.0", "libfluid.so.0.1.0")


def _search_roots(explicit: Optional[str]) -> list[Path]:
    roots: list[Path] = []
    if explicit:
        roots.append(Path(explicit))
    env = os.environ.get("FLUID_LIBRARY")
    if env:
        roots.append(Path(env))
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    roots.extend(
        [
            Path.cwd(),
            here,
            repo / "cmake-build-debug",
            repo / "cmake-build-release",
            repo / "build",
            repo / "build" / "Release",
            repo / "build" / "Debug",
            repo / "build" / "lib",
            repo / "lib",
        ]
    )
    return roots


def _candidates(explicit: Optional[str]) -> list[Path]:
    names = _library_names()
    found: list[Path] = []
    for root in _search_roots(explicit):
        if root.is_file():
            found.append(root)
            continue
        if root.is_dir():
            for name in names:
                candidate = root / name
                if candidate.is_file():
                    found.append(candidate)
    located = find_library("fluid")
    if located:
        found.append(Path(located))
    return found


def _open_library(path: str) -> ctypes.CDLL:
    if sys.platform.startswith("win"):
        directory = str(Path(path).resolve().parent)
        if hasattr(os, "add_dll_directory"):
            try:
                os.add_dll_directory(directory)
            except OSError:
                pass
        return ctypes.CDLL(path)
    mode = 0
    if hasattr(os, "RTLD_GLOBAL"):
        mode |= os.RTLD_GLOBAL
    if hasattr(os, "RTLD_NOW"):
        mode |= os.RTLD_NOW
    return ctypes.CDLL(path, mode=mode) if mode else ctypes.CDLL(path)


def load(path: Optional[str] = None) -> ctypes.CDLL:
    last_error = ""
    for candidate in _candidates(path):
        try:
            return _open_library(str(candidate))
        except OSError as error:
            last_error = str(error)
    raise FileNotFoundError(
        "Could not load Fluid shared library (fluid.dll / libfluid.so / libfluid.dylib). "
        "Build with FLUID_BUILD_SHARED=ON or set FLUID_LIBRARY. "
        f"Last error: {last_error}"
    )


def _bind(lib: ctypes.CDLL) -> ctypes.CDLL:
    lib.fluid_version.restype = c_char_p
    lib.fluid_platform.restype = c_char_p
    lib.fluid_last_error.restype = c_char_p
    lib.fluid_color_hex.argtypes = [c_uint32, c_float]
    lib.fluid_color_hex.restype = Color
    lib.fluid_mesh_knot.argtypes = [c_uint32, c_uint32]
    lib.fluid_mesh_knot.restype = c_void_p
    lib.fluid_mesh_destroy.argtypes = [c_void_p]
    lib.fluid_camera_default.restype = OrbitCamera
    lib.fluid_camera_orbit.argtypes = [POINTER(OrbitCamera), Vec2]
    lib.fluid_camera_zoom.argtypes = [POINTER(OrbitCamera), c_float]
    lib.fluid_scene_view_default.restype = SceneView
    lib.fluid_window_config_default.restype = WindowConfig
    lib.fluid_window_create.argtypes = [POINTER(WindowConfig)]
    lib.fluid_window_create.restype = c_void_p
    lib.fluid_window_destroy.argtypes = [c_void_p]
    lib.fluid_window_set_tick.argtypes = [c_void_p, TickFn, c_void_p]
    lib.fluid_window_init.argtypes = [c_void_p, c_void_p]
    lib.fluid_window_init.restype = c_int
    lib.fluid_window_loop.argtypes = [c_void_p]
    lib.fluid_window_loop.restype = c_int
    lib.fluid_window_ui.argtypes = [c_void_p]
    lib.fluid_window_ui.restype = c_void_p
    lib.fluid_window_input.argtypes = [c_void_p, POINTER(Input)]
    lib.fluid_window_input.restype = c_int
    lib.fluid_window_width.argtypes = [c_void_p]
    lib.fluid_window_width.restype = c_float
    lib.fluid_window_height.argtypes = [c_void_p]
    lib.fluid_window_height.restype = c_float
    lib.fluid_window_delta_time.argtypes = [c_void_p]
    lib.fluid_window_delta_time.restype = c_float
    lib.fluid_window_set_scene_aa.argtypes = [c_void_p, c_int]
    lib.fluid_window_scene_aa.argtypes = [c_void_p]
    lib.fluid_window_scene_aa.restype = c_int
    lib.fluid_window_set_ray_tracing.argtypes = [c_void_p, c_int]
    lib.fluid_window_ray_tracing.argtypes = [c_void_p]
    lib.fluid_window_ray_tracing.restype = c_int
    lib.fluid_window_ray_tracing_available.argtypes = [c_void_p]
    lib.fluid_window_ray_tracing_available.restype = c_int
    lib.fluid_window_set_dlss.argtypes = [c_void_p, c_int]
    lib.fluid_window_dlss.argtypes = [c_void_p]
    lib.fluid_window_dlss.restype = c_int
    lib.fluid_window_dlss_available.argtypes = [c_void_p]
    lib.fluid_window_dlss_available.restype = c_int
    lib.fluid_ui_draw.argtypes = [c_void_p]
    lib.fluid_ui_draw.restype = c_void_p
    lib.fluid_ui_theme.argtypes = [c_void_p, POINTER(Theme)]
    lib.fluid_ui_set_theme.argtypes = [c_void_p, Theme]
    lib.fluid_ui_button.argtypes = [c_void_p, c_char_p, Rect, c_char_p, c_int, c_int]
    lib.fluid_ui_button.restype = c_int
    lib.fluid_ui_toggle.argtypes = [c_void_p, c_char_p, Rect, POINTER(c_int)]
    lib.fluid_ui_toggle.restype = c_int
    lib.fluid_ui_slider.argtypes = [c_void_p, c_char_p, Rect, POINTER(c_float), c_float, c_float]
    lib.fluid_ui_slider.restype = c_int
    lib.fluid_ui_label.argtypes = [c_void_p, Vec2, c_char_p, c_float, c_int]
    lib.fluid_ui_panel.argtypes = [c_void_p, Rect, c_float]
    lib.fluid_draw_rect.argtypes = [c_void_p, Rect, Color, c_float]
    lib.fluid_draw_scene.argtypes = [c_void_p, POINTER(SceneView)]
    lib.fluid_rect_contains.argtypes = [Rect, Vec2]
    lib.fluid_rect_contains.restype = c_int
    return lib


class Fluid:
    def __init__(self, path: Optional[str] = None):
        self.lib = _bind(load(path))
        self._tick = None

    def version(self) -> str:
        return self.lib.fluid_version().decode()

    def platform(self) -> str:
        text = self.lib.fluid_platform()
        return text.decode() if text else ""

    def last_error(self) -> str:
        text = self.lib.fluid_last_error()
        return text.decode() if text else ""


def run_example(library: Optional[str] = None) -> None:
    api = Fluid(library)
    lib = api.lib
    mesh = lib.fluid_mesh_knot(192, 20)
    if not mesh:
        raise RuntimeError(api.last_error())
    camera = lib.fluid_camera_default()
    roughness = c_float(0.3)
    rotating = c_int(1)
    angle = 0.0

    @TickFn
    def tick(window: int, _user: int) -> None:
        nonlocal camera, angle
        ui = lib.fluid_window_ui(window)
        draw = lib.fluid_ui_draw(ui)
        theme = Theme()
        lib.fluid_ui_theme(ui, ctypes.byref(theme))
        incoming = Input()
        lib.fluid_window_input(window, ctypes.byref(incoming))
        width = lib.fluid_window_width(window)
        height = lib.fluid_window_height(window)
        lib.fluid_draw_rect(draw, Rect(0, 0, width, height), theme.background, 0)
        lib.fluid_ui_panel(ui, Rect(20, 20, 260, 180), 12)
        lib.fluid_ui_label(ui, Vec2(40, 40), b"Hello from Python", 24, 1)
        lib.fluid_ui_slider(ui, b"roughness", Rect(40, 85, 220, 24), ctypes.byref(roughness), 0.05, 1.0)
        lib.fluid_ui_toggle(ui, b"rotate", Rect(40, 135, 42, 24), ctypes.byref(rotating))
        viewport = Rect(300, 20, width - 320, height - 40)
        if lib.fluid_rect_contains(viewport, incoming.mouse):
            if incoming.mouse_down:
                lib.fluid_camera_orbit(ctypes.byref(camera), incoming.mouse_delta)
            lib.fluid_camera_zoom(ctypes.byref(camera), incoming.scroll)
        if rotating.value:
            angle += lib.fluid_window_delta_time(window) * 0.25
        scene = lib.fluid_scene_view_default()
        scene.bounds = viewport
        scene.mesh = mesh
        scene.camera = camera
        scene.rotation = angle
        scene.roughness = roughness.value
        lib.fluid_draw_scene(draw, ctypes.byref(scene))

    api._tick = tick
    config = lib.fluid_window_config_default()
    config.title = b"Fluid Python"
    window = lib.fluid_window_create(ctypes.byref(config))
    if not window:
        lib.fluid_mesh_destroy(mesh)
        raise RuntimeError(api.last_error())
    lib.fluid_window_set_tick(window, tick, None)
    lib.fluid_window_set_scene_aa(window, 1)
    try:
        if not lib.fluid_window_init(window, None) or not lib.fluid_window_loop(window):
            raise RuntimeError(api.last_error())
    finally:
        lib.fluid_window_destroy(window)
        lib.fluid_mesh_destroy(mesh)


if __name__ == "__main__":
    run_example()
