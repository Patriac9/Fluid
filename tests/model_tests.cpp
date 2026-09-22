#include "fluid/model.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void require(bool condition, const std::string &reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
bool close(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(const glm::mat4 &m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r]))
                return false;
    return true;
}
void throws(const std::function<void()> &operation, const std::string &context) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected an exception: " + context);
}

void validateMesh(const Fluid::Mesh &mesh) {
    require(!mesh.vertices.empty(), "Mesh must contain vertices");
    require(!mesh.indices.empty() && mesh.indices.size() % 3 == 0, "Mesh must contain triangle indices");
    glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
    for (const auto &vertex : mesh.vertices) {
        require(finite(vertex.position) && finite(vertex.normal), "Mesh vertex must be finite");
        require(close(glm::length(vertex.normal), 1.0f), "Mesh normal must have unit length");
        minimum = glm::min(minimum, vertex.position);
        maximum = glm::max(maximum, vertex.position);
    }
    const auto extent = maximum - minimum;
    require(close(std::max({extent.x, extent.y, extent.z}), 2.0f), "Mesh must be normalized to extent 2");
    require(glm::length(minimum + maximum) < 1e-4f, "Mesh bounds must be centered at origin");
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
        require(ia < mesh.vertices.size() && ib < mesh.vertices.size() && ic < mesh.vertices.size(),
                "Index outside mesh");
        require(ia != ib && ib != ic && ic != ia, "Triangle repeats an index");
        const auto &a = mesh.vertices[ia];
        const auto &b = mesh.vertices[ib];
        const auto &c = mesh.vertices[ic];
        const auto cross = glm::cross(b.position - a.position, c.position - a.position);
        require(glm::length(cross) > 1e-9f, "Triangle must have nonzero area");
        require(glm::dot(cross, a.normal + b.normal + c.normal) > 0.0f,
                "Triangle winding must agree with normals");
    }
}

// Closed primitives must use every shared edge once in each direction. This
// catches missing closing strips, reversed seams, and holes at sphere poles.
void validateManifold(const Fluid::Mesh &mesh) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edges;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto a = mesh.indices[i + corner], b = mesh.indices[i + (corner + 1) % 3];
            auto &counts = edges[{std::min(a, b), std::max(a, b)}];
            ++counts.first;
            counts.second += a < b ? 1 : -1;
        }
    }
    for (const auto &[edge, counts] : edges) {
        (void)edge;
        require(counts.first == 2 && counts.second == 0,
                "Primitive has a non-manifold or incorrectly wound edge");
    }
}

struct FixtureDirectory {
    std::filesystem::path path;
    FixtureDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = std::filesystem::temp_directory_path() /
                   ("fluid-model-tests-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path))
                return;
        }
        throw std::runtime_error("Could not create unique fixture directory");
    }
    ~FixtureDirectory() {
        // Only this invocation's newly created, isolated fixture directory.
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path write(const std::string &name, const std::string &data) const {
        const auto target = path / name;
        std::ofstream file(target);
        file << data;
        require(bool(file), "Could not write OBJ fixture");
        return target;
    }
};

void primitiveTests() {
    for (const auto &mesh : {Fluid::Mesh::sphere(), Fluid::Mesh::torus(), Fluid::Mesh::knot(),
                             Fluid::Mesh::sphere(3, 2), Fluid::Mesh::sphere(9, 5), Fluid::Mesh::torus(3, 3),
                             Fluid::Mesh::torus(9, 5), Fluid::Mesh::knot(24, 3), Fluid::Mesh::knot(48, 8)}) {
        validateMesh(mesh);
        validateManifold(mesh);
    }
    const auto cube = Fluid::Mesh::cube();
    validateMesh(cube);
    require(cube.vertices.size() == 24 && cube.indices.size() == 36, "Cube must retain sharp face normals");
    throws([] { Fluid::Mesh::sphere(2, 16); }, "sphere minimum segments");
    throws([] { Fluid::Mesh::sphere(16, 1); }, "sphere minimum rings");
    throws([] { Fluid::Mesh::torus(0, 8); }, "torus minimum resolution");
    throws([] { Fluid::Mesh::knot(8, 8); }, "knot minimum resolution");
    throws([] { Fluid::Mesh::torus(std::numeric_limits<std::uint32_t>::max(), 10); }, "resolution overflow");
}

void normalizationTests() {
    Fluid::Mesh translated;
    translated.vertices = {{{8, -5, 20}, {0, 1, 0}}, {{12, -3, 22}, {0, 1, 0}}};
    translated.normalize();
    require(glm::length(translated.vertices[0].position - glm::vec3(-1, -0.5f, -0.5f)) < 1e-5f,
            "Normalization must preserve proportions");
    require(glm::length(translated.vertices[1].position - glm::vec3(1, 0.5f, 0.5f)) < 1e-5f,
            "Normalization center incorrect");
    const auto before = translated.vertices[1].position;
    translated.normalize();
    require(glm::length(translated.vertices[1].position - before) < 1e-5f,
            "Normalization should be idempotent");
    throws([] { Fluid::Mesh{}.normalize(); }, "empty normalization");
    throws(
        [] {
            Fluid::Mesh mesh;
            mesh.vertices.resize(1);
            mesh.normalize();
        },
        "zero-extent normalization");
    throws(
        [] {
            auto mesh = Fluid::Mesh::cube();
            mesh.vertices[0].position.x = std::numeric_limits<float>::infinity();
            mesh.normalize();
        },
        "non-finite normalization");
}

void objTests(const FixtureDirectory &fixtures) {
    const auto mesh = Fluid::Mesh::load_obj(fixtures.write(
        "translated-square.obj", "# Comments and declarations\no Example\ng Surface\nusemtl Neutral\ns 1\n"
                                 "v 8 -2 4\nv 12 -2 4\nv 12 2 4\nv 8 2 4\nf 1 2 3 4 # Quad\n"));
    validateMesh(mesh);
    require(mesh.name == "translated-square", "OBJ display name must come from filename");
    require(mesh.vertices.size() == 4 && mesh.indices.size() == 6, "Quad must become two indexed triangles");
    for (const auto &v : mesh.vertices)
        require(v.normal.z > 0.999f, "Generated normals must face +Z");

    const auto explicitNormals = Fluid::Mesh::load_obj(fixtures.write(
        "normals.obj",
        "v 0 0 0\nv 2 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 7\nf -3/-3/-1 -2/-2/-1 -1/-1/-1\n"));
    validateMesh(explicitNormals);
    for (const auto &v : explicitNormals.vertices)
        require(close(v.normal.z, 1), "Imported normals must be normalized");
    validateMesh(Fluid::Mesh::load_obj(
        fixtures.write("double-slash.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n")));
    validateMesh(Fluid::Mesh::load_obj(
        fixtures.write("uv-only.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0\nvt 1 0\nvt 0 1 0\nf 1/1 2/2 3/3\n")));
    validateMesh(Fluid::Mesh::load_obj(
        fixtures.write("mixed-normals.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2 3\n")));
    validateMesh(Fluid::Mesh::load_obj(
        fixtures.write("homogeneous.obj", "v 0 0 0 2\nv 4 0 0 2\nv 0 4 0 2\nf 1 2 3\n")));

    // An L-shaped concave polygon must preserve area and leave its upper-right
    // missing corner empty. A naive triangle fan can fill exterior regions.
    const std::string concaveVertices = "v 0 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nv 1 2 0\nv 0 2 0\n";
    const auto concave =
        Fluid::Mesh::load_obj(fixtures.write("concave.obj", concaveVertices + "f 1 2 3 4 5 6\n"));
    validateMesh(concave);
    require(concave.indices.size() == 12, "Concave hexagon must become four triangles");
    double area = 0.0;
    for (std::size_t i = 0; i < concave.indices.size(); i += 3) {
        const auto a = concave.vertices[concave.indices[i]].position,
                   b = concave.vertices[concave.indices[i + 1]].position,
                   c = concave.vertices[concave.indices[i + 2]].position;
        area += glm::length(glm::cross(b - a, c - a)) * 0.5;
        const auto centroid = (a + b + c) / 3.0f;
        require(centroid.x <= 0 || centroid.y <= 0, "Concave triangulation filled an exterior region");
    }
    require(std::abs(area - 3.0) < 1e-5, "Concave triangulation changed polygon area");
    validateMesh(Fluid::Mesh::load_obj(fixtures.write("clockwise.obj", concaveVertices + "f 6 5 4 3 2 1\n")));
    validateMesh(Fluid::Mesh::load_obj(fixtures.write(
        "large-translation.obj",
        "v 10000000000 10000000000 0\nv 10000000002 10000000000 0\nv 10000000000 10000000002 0\nf 1 2 3\n")));

    const std::string p = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    const std::vector<std::pair<std::string, std::string>> invalid{
        {"empty", ""},
        {"no-faces", p},
        {"short-face", p + "f 1 2\n"},
        {"zero-index", p + "f 0 2 3\n"},
        {"large-index", p + "f 1 2 4\n"},
        {"negative-index", p + "f -4 -2 -1\n"},
        {"invalid-index", p + "f 1.0 2 3\n"},
        {"missing-uv", p + "f 1/1 2/1 3/1\n"},
        {"missing-normal", p + "f 1//1 2//1 3//1\n"},
        {"extra-slash", p + "f 1///1 2 3\n"},
        {"trailing-slash", p + "f 1/ 2 3\n"},
        {"not-a-number", "v nan 0 0\n"},
        {"infinity", "v inf 0 0\n"},
        {"overflow", "v 1e100 0 0\n"},
        {"short-position", "v 0 0\n"},
        {"bad-number", "v 1.2suffix 0 0\n"},
        {"zero-normal", p + "vn 0 0 0\nf 1 2 3\n"},
        {"zero-weight", "v 0 0 0 0\n"},
        {"degenerate", p + "f 1 2 2\n"},
        {"collinear", "v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n"},
        {"bowtie", "v 0 0 0\nv 1 1 0\nv 0 1 0\nv 1 0 0\nf 1 2 3 4\n"}};
    for (const auto &[name, data] : invalid) {
        const auto path = fixtures.write(name + ".obj", data);
        try {
            (void)Fluid::Mesh::load_obj(path);
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()).find(name + ".obj") != std::string::npos,
                    "OBJ error must identify source file");
            continue;
        }
        throw std::runtime_error("Invalid OBJ accepted: " + name);
    }
    throws([&] { Fluid::Mesh::load_obj(fixtures.path / "does-not-exist.obj"); }, "missing file");
    const auto oversized = fixtures.write("oversized.obj", p);
    std::filesystem::resize_file(oversized, 64 * 1024 * 1024 + 1);
    throws([&] { Fluid::Mesh::load_obj(oversized); }, "oversized OBJ");
}

void cameraTests() {
    Fluid::OrbitCamera camera;
    require(finite(camera.view()) && finite(camera.projection(16.0f / 9.0f)),
            "Default camera matrices must be finite");
    const auto target = camera.view() * glm::vec4(0, 0, 0, 1);
    require(close(target.x, 0) && close(target.y, 0) && close(target.z, -camera.distance),
            "Orbit camera must look at origin from configured distance");
    const auto perspective = camera.projection(2);
    require(perspective[1][1] < 0 && perspective[0][0] > 0, "Vulkan projection must flip only Y");
    require(close(-perspective[1][1] / perspective[0][0], 2), "Projection must respect aspect ratio");
    const auto nearPoint = perspective * glm::vec4(0, 0, -0.05f, 1),
               farPoint = perspective * glm::vec4(0, 0, -100.0f, 1);
    require(close(nearPoint.z / nearPoint.w, 0), "Vulkan near plane must map to depth 0");
    require(close(farPoint.z / farPoint.w, 1), "Vulkan far plane must map to depth 1");
    const float previousYaw = camera.yaw;
    camera.orbit({25, 15});
    require(!close(camera.yaw, previousYaw), "Dragging must orbit camera");
    camera.orbit({1e30f, 1e30f});
    require(std::abs(camera.yaw) <= 3.142f && camera.pitch < 1.571f, "Orbit must wrap yaw and avoid poles");
    camera.orbit({0, -1e30f});
    require(camera.pitch > -1.571f, "Negative pitch must be clamped");
    camera.zoom(1e30f);
    require(close(camera.distance, 2), "Zoom in must be bounded");
    camera.zoom(-1e30f);
    require(close(camera.distance, 20), "Zoom out must be bounded");
    const float distance = camera.distance;
    camera.zoom(std::numeric_limits<float>::quiet_NaN());
    require(camera.distance == distance, "Non-finite scroll must be ignored");
    camera.yaw = std::numeric_limits<float>::infinity();
    camera.pitch = std::numeric_limits<float>::quiet_NaN();
    camera.distance = -10;
    require(finite(camera.view()), "Camera view must tolerate invalid public state");
    for (const float aspect : {0.0f, -1.0f, 1e30f, std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()})
        require(finite(camera.projection(aspect)), "Projection must tolerate invalid viewport dimensions");
    camera.reset();
    require(close(camera.yaw, 0.65f) && close(camera.pitch, 0.38f) && close(camera.distance, 4.6f),
            "Reset must restore camera defaults");
}
} // namespace

int main() {
    try {
        primitiveTests();
        normalizationTests();
        FixtureDirectory fixtures;
        objTests(fixtures);
        cameraTests();
        std::cout
            << "Fluid model tests passed: geometry, winding, closed seams, OBJ parsing and Vulkan camera.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Fluid model tests failed: " << error.what() << '\n';
        return 1;
    }
}
