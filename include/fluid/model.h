#pragma once

#include "fluid/types.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Fluid {

struct MeshVertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

// Indexed, outward-facing triangles with unit normals. Primitives and OBJ files
// are centered at the origin and scaled to a maximum bounding-box extent of 2.
struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string name;

    static Mesh sphere(std::uint32_t segments = 64, std::uint32_t rings = 32);
    static Mesh torus(std::uint32_t segments = 96, std::uint32_t sides = 32);
    static Mesh knot(std::uint32_t segments = 192, std::uint32_t sides = 20);
    static Mesh cube();
    // Loads positions, optional UV references, and optional vertex normals.
    // Supports negative indices and triangulates concave polygon faces.
    // Throws std::runtime_error with filename/line context for invalid input.
    static Mesh load_obj(const std::filesystem::path &path);
    void normalize();
};

struct OrbitCamera {
    float yaw = 0.65f;
    float pitch = 0.38f;
    float distance = 4.6f;

    void orbit(Vec2 delta);
    void zoom(float scroll);
    void reset();
    glm::mat4 view() const;
    // Right-handed Vulkan clip coordinates: depth 0..1, flipped Y axis.
    glm::mat4 projection(float aspect) const;
};

struct SceneView {
    Rect bounds;
    const Mesh *mesh = nullptr;
    OrbitCamera camera;
    Color tint = Color::hex(0xB7A6EF);
    float rotation = 0.0f;
    float metallic = 0.45f;
    float roughness = 0.3f;
    float exposure = 1.0f;
    bool wireframe = false;
    bool grid = true;
};

} // namespace Fluid
