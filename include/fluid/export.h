#pragma once

#if defined(FLUID_SHARED)
#if defined(_WIN32)
#if defined(FLUID_BUILDING_SHARED)
#define FLUID_C_API __declspec(dllexport)
#else
#define FLUID_C_API __declspec(dllimport)
#endif
#else
#if defined(FLUID_BUILDING_SHARED)
#define FLUID_C_API __attribute__((visibility("default")))
#else
#define FLUID_C_API
#endif
#endif
#else
#define FLUID_C_API
#endif
