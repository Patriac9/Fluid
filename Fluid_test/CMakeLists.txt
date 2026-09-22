cmake_minimum_required(VERSION 3.20)
project(FluidStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FLUID_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(FLUID_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLUID_BUILD_GPU_TESTS OFF CACHE BOOL "" FORCE)
set(FLUID_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/fluid)

add_executable(FluidTest main.cpp fluid_app.cpp)
target_link_libraries(FluidTest PRIVATE Fluid::Fluid)
if(MSVC)
    target_compile_options(FluidTest PRIVATE /W4 /permissive- /utf-8)
endif()
fluid_copy_runtime_dlls(FluidTest)
