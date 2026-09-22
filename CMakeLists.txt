cmake_minimum_required(VERSION 3.20)
project(Fluid VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(FLUID_BUILD_EXAMPLE "Build the Fluid Studio example" ON)
option(FLUID_BUILD_TESTS "Build CPU tests" ON)
option(FLUID_BUILD_GPU_TESTS "Build tests requiring a Vulkan graphics device and desktop" OFF)
option(FLUID_BUILD_SHARED "Build the C ABI shared library for other languages" ON)

find_package(Vulkan REQUIRED)
find_program(FLUID_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT FLUID_GLSLC)
    message(FATAL_ERROR "glslc is required. Install the Vulkan SDK and add its bin directory to PATH.")
endif()
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw-3.4)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluidCopyRuntimeDlls.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FluidShared.cmake")
include(GNUInstallDirs)

set(FLUID_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(FLUID_SHADER_SOURCES ui.vert ui.frag scene.vert scene.frag blit.vert blit.frag fullscreen.vert
    upscale.frag)
foreach(SHADER ${FLUID_SHADER_SOURCES})
    set(OUTPUT "${FLUID_SHADER_OUTPUT}/${SHADER}.spv")
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FLUID_SHADER_OUTPUT}"
        COMMAND "${FLUID_GLSLC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" -o "${OUTPUT}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" VERBATIM)
    list(APPEND FLUID_SHADERS "${OUTPUT}")
endforeach()
set(FLUID_SCENE_RT_SPV "${FLUID_SHADER_OUTPUT}/scene_rt.frag.spv")
add_custom_command(OUTPUT "${FLUID_SCENE_RT_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FLUID_SHADER_OUTPUT}"
    COMMAND "${FLUID_GLSLC}" --target-env=vulkan1.2
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_rt.frag" -o "${FLUID_SCENE_RT_SPV}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_rt.frag" VERBATIM)
list(APPEND FLUID_SHADERS "${FLUID_SCENE_RT_SPV}")
set(FLUID_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/fluid_shaders.h")
add_custom_command(OUTPUT "${FLUID_SHADER_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND ${CMAKE_COMMAND} "-DSHADER_DIR=${FLUID_SHADER_OUTPUT}" "-DOUTPUT=${FLUID_SHADER_HEADER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS ${FLUID_SHADERS} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake" VERBATIM)
add_custom_target(FluidShaders DEPENDS "${FLUID_SHADER_HEADER}")

set(FLUID_CORE_SOURCES src/window.cpp src/renderer.cpp src/ui.cpp src/font.cpp src/model.cpp)

function(fluid_configure_core_library target)
    add_dependencies(${target} FluidShaders)
    target_include_directories(${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4/deps"
            "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(${target} PRIVATE $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_WARNINGS>)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(Fluid STATIC ${FLUID_CORE_SOURCES} src/fluid_c.cpp)
add_library(Fluid::Fluid ALIAS Fluid)
fluid_configure_core_library(Fluid)
target_include_directories(Fluid PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(Fluid PUBLIC glfw Vulkan::Vulkan)

if(FLUID_BUILD_SHARED)
    add_library(FluidC SHARED ${FLUID_CORE_SOURCES} src/fluid_c.cpp)
    add_library(Fluid::C ALIAS FluidC)
    fluid_configure_core_library(FluidC)
    target_link_libraries(FluidC PRIVATE glfw Vulkan::Vulkan)
    fluid_configure_c_shared(FluidC)
    fluid_copy_runtime_dlls(FluidC)
    install(TARGETS FluidC
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES include/fluid/capi.h include/fluid/export.h
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/fluid")
endif()

if(FLUID_BUILD_EXAMPLE)
    add_executable(FluidTest main.cpp Fluid_test/fluid_app.cpp)
    target_link_libraries(FluidTest PRIVATE Fluid::Fluid)
    fluid_copy_runtime_dlls(FluidTest)
    if(MSVC)
        target_compile_options(FluidTest PRIVATE /W4 /permissive- /utf-8)
    endif()
    if(FLUID_BUILD_SHARED)
        add_executable(FluidCHello examples/c/hello.c)
        target_link_libraries(FluidCHello PRIVATE FluidC)
        set_target_properties(FluidCHello PROPERTIES C_STANDARD 11)
        fluid_set_relocatable_rpath(FluidCHello)
        fluid_copy_runtime_dlls(FluidCHello)
    endif()
endif()
if(FLUID_BUILD_TESTS)
    enable_testing()
    add_executable(FluidCApiTests tests/c_api_tests.c)
    target_link_libraries(FluidCApiTests PRIVATE Fluid::Fluid)
    set_target_properties(FluidCApiTests PROPERTIES C_STANDARD 11 LINKER_LANGUAGE CXX)
    fluid_copy_runtime_dlls(FluidCApiTests)
    add_test(NAME Fluid.CApi COMMAND FluidCApiTests)
    add_executable(FluidModelTests tests/model_tests.cpp src/model.cpp)
    target_include_directories(FluidModelTests PRIVATE include third_party)
    fluid_copy_runtime_dlls(FluidModelTests)
    add_test(NAME Fluid.Models COMMAND FluidModelTests)
    add_executable(FluidUiTests tests/ui_tests.cpp src/ui.cpp src/font.cpp)
    target_include_directories(FluidUiTests PRIVATE include third_party third_party/glfw-3.4/deps)
    target_compile_definitions(FluidUiTests PRIVATE _CRT_SECURE_NO_WARNINGS)
    fluid_copy_runtime_dlls(FluidUiTests)
    add_test(NAME Fluid.UI COMMAND FluidUiTests)
endif()
if(FLUID_BUILD_GPU_TESTS)
    enable_testing()
    add_executable(FluidWindowTests tests/window_tests.cpp)
    target_link_libraries(FluidWindowTests PRIVATE Fluid::Fluid)
    fluid_copy_runtime_dlls(FluidWindowTests)
    add_test(NAME Fluid.Window COMMAND FluidWindowTests)
    set_tests_properties(Fluid.Window PROPERTIES TIMEOUT 60 LABELS gpu)
    if(FLUID_BUILD_EXAMPLE)
        add_test(NAME Fluid.Showcase COMMAND FluidTest --smoke-test --hidden
            --screenshot "${CMAKE_CURRENT_BINARY_DIR}/showcase-smoke.png")
        set_tests_properties(Fluid.Showcase PROPERTIES TIMEOUT 60 LABELS gpu)
        add_test(NAME Fluid.Import COMMAND FluidTest --hidden --frames 3
            --model "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/crystal.obj")
        set_tests_properties(Fluid.Import PROPERTIES TIMEOUT 60 LABELS gpu)
    endif()
endif()
