#include "fluid/model.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Fluid {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr std::size_t maxVertices = 2'000'000;
constexpr std::size_t maxIndices = 12'000'000;
constexpr std::uintmax_t maxObjBytes = 64 * 1024 * 1024;
constexpr float minDistance = 2.0f;
constexpr float maxDistance = 20.0f;
constexpr float maxPitch = 1.48f;

bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

void checkResolution(std::uint32_t u, std::uint32_t v, std::uint32_t minU, std::uint32_t minV) {
    if (u < minU || v < minV || std::uint64_t(u) * v > maxVertices)
        throw std::invalid_argument("Mesh resolution is too small or exceeds the 2,000,000 vertex limit");
}

void quad(Mesh &mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
}

struct ObjReference {
    std::size_t position;
    std::size_t normal;
    bool operator==(const ObjReference &) const = default;
};

struct ObjHash {
    std::size_t operator()(const ObjReference &value) const {
        const auto a = std::hash<std::size_t>{}(value.position);
        const auto b = std::hash<std::size_t>{}(value.normal);
        return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
    }
};

constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();

[[noreturn]] void objError(const std::filesystem::path &path, std::size_t line, const std::string &reason) {
    throw std::runtime_error("OBJ '" + path.string() + "'" + (line ? ":" + std::to_string(line) : "") + ": " +
                             reason);
}

std::size_t parseIndex(const std::string &text, std::size_t count, const std::filesystem::path &path,
                       std::size_t line) {
    std::int64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0)
        objError(path, line, "face indices must be nonzero integers");
    if (value > 0) {
        if (std::uint64_t(value) > count)
            objError(path, line, "face index is out of range");
        return std::size_t(value - 1);
    }
    if (value < -std::int64_t(count))
        objError(path, line, "negative face index is out of range");
    return std::size_t(std::int64_t(count) + value);
}

ObjReference parseReference(const std::string &token, std::size_t positions, std::size_t texcoords,
                            std::size_t normals, const std::filesystem::path &path, std::size_t line) {
    const auto first = token.find('/');
    if (first == std::string::npos)
        return {parseIndex(token, positions, path, line), absent};
    ObjReference result{parseIndex(token.substr(0, first), positions, path, line), absent};
    const auto second = token.find('/', first + 1);
    if (second == std::string::npos) {
        parseIndex(token.substr(first + 1), texcoords, path, line);
        return result;
    }
    if (token.find('/', second + 1) != std::string::npos)
        objError(path, line, "face reference has too many '/' separators");
    if (second > first + 1)
        parseIndex(token.substr(first + 1, second - first - 1), texcoords, path, line);
    result.normal = parseIndex(token.substr(second + 1), normals, path, line);
    return result;
}

std::vector<double> parseNumbers(std::istringstream &stream, std::size_t minimum, std::size_t maximum,
                                 const std::filesystem::path &path, std::size_t line) {
    std::vector<double> values;
    std::string token;
    while (stream >> token) {
        std::size_t read = 0;
        double number;
        try {
            number = std::stod(token, &read);
        } catch (const std::exception &) {
            objError(path, line, "invalid numeric coordinate");
        }
        if (read != token.size() || !std::isfinite(number) || std::abs(number) > 1e20)
            objError(path, line, "coordinate must be finite and have magnitude at most 1e20");
        values.push_back(number);
        if (values.size() > maximum)
            objError(path, line, "too many coordinate components");
    }
    if (values.size() < minimum)
        objError(path, line, "missing coordinate components");
    return values;
}

double cross2(glm::dvec2 a, glm::dvec2 b, glm::dvec2 c) {
    const auto ab = b - a;
    const auto ac = c - a;
    return ab.x * ac.y - ab.y * ac.x;
}

// Project onto the dominant plane and clip ears, preserving the source winding.
// Unlike a triangle fan this also handles ordinary concave OBJ polygons.
std::vector<std::array<std::size_t, 3>> triangulate(const std::vector<ObjReference> &polygon,
                                                    const std::vector<glm::dvec3> &positions,
                                                    const std::filesystem::path &path, std::size_t line) {
    glm::dvec3 faceNormal(0.0);
    const auto origin = positions[polygon[0].position];
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
        faceNormal +=
            glm::cross(positions[polygon[i].position] - origin, positions[polygon[i + 1].position] - origin);
    const auto absolute = glm::abs(faceNormal);
    const int drop =
        absolute.x > absolute.y && absolute.x > absolute.z ? 0 : (absolute.y > absolute.z ? 1 : 2);
    if (absolute[drop] <= 0.0)
        objError(path, line, "face has zero area");
    std::vector<glm::dvec2> points;
    points.reserve(polygon.size());
    for (const auto &reference : polygon) {
        const auto p = positions[reference.position] - origin;
        points.push_back(drop == 0 ? glm::dvec2(p.y, p.z)
                                   : (drop == 1 ? glm::dvec2(p.x, p.z) : glm::dvec2(p.x, p.y)));
    }
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
        area += cross2(glm::dvec2(0.0), points[i], points[(i + 1) % points.size()]);
    const double sign = area > 0.0 ? 1.0 : -1.0;
    const double epsilon = std::abs(area) * 1e-12;
    std::vector<std::size_t> remaining(polygon.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    std::vector<std::array<std::size_t, 3>> triangles;
    triangles.reserve(polygon.size() - 2);
    while (remaining.size() > 3) {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto a = remaining[(i + remaining.size() - 1) % remaining.size()];
            const auto b = remaining[i];
            const auto c = remaining[(i + 1) % remaining.size()];
            if (sign * cross2(points[a], points[b], points[c]) <= epsilon)
                continue;
            bool containsPoint = false;
            for (const auto p : remaining) {
                if (p == a || p == b || p == c)
                    continue;
                if (sign * cross2(points[a], points[b], points[p]) >= -epsilon &&
                    sign * cross2(points[b], points[c], points[p]) >= -epsilon &&
                    sign * cross2(points[c], points[a], points[p]) >= -epsilon) {
                    containsPoint = true;
                    break;
                }
            }
            if (containsPoint)
                continue;
            triangles.push_back({a, b, c});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped)
            objError(path, line,
                     "cannot triangulate face; check for repeated vertices or intersecting edges");
    }
    if (sign * cross2(points[remaining[0]], points[remaining[1]], points[remaining[2]]) <= epsilon)
        objError(path, line, "face contains a degenerate triangle");
    triangles.push_back({remaining[0], remaining[1], remaining[2]});
    return triangles;
}

} // namespace

void Mesh::normalize() {
    if (vertices.empty())
        throw std::invalid_argument("Cannot normalize an empty mesh");
    glm::dvec3 minimum(std::numeric_limits<double>::max());
    glm::dvec3 maximum(std::numeric_limits<double>::lowest());
    for (const auto &vertex : vertices) {
        if (!finite(vertex.position) || !finite(vertex.normal))
            throw std::invalid_argument("Mesh contains non-finite vertex data");
        minimum = glm::min(minimum, glm::dvec3(vertex.position));
        maximum = glm::max(maximum, glm::dvec3(vertex.position));
    }
    const auto extent = maximum - minimum;
    const double longest = std::max({extent.x, extent.y, extent.z});
    if (longest <= 0.0)
        throw std::invalid_argument("Cannot normalize a mesh with zero extent");
    const auto center = minimum + extent * 0.5;
    for (auto &vertex : vertices)
        vertex.position = glm::vec3((glm::dvec3(vertex.position) - center) * (2.0 / longest));
}

Mesh Mesh::sphere(std::uint32_t segments, std::uint32_t rings) {
    checkResolution(segments, rings, 3, 2);
    Mesh mesh;
    mesh.name = "Sphere";
    mesh.vertices.reserve(2 + std::size_t(segments) * (rings - 1));
    mesh.indices.reserve(std::size_t(segments) * (rings - 1) * 6);
    mesh.vertices.push_back({{0, 1, 0}, {0, 1, 0}});
    for (std::uint32_t ring = 1; ring < rings; ++ring) {
        const float theta = pi * float(ring) / float(rings);
        for (std::uint32_t segment = 0; segment < segments; ++segment) {
            const float phi = 2.0f * pi * float(segment) / float(segments);
            const glm::vec3 p(std::sin(theta) * std::cos(phi), std::cos(theta),
                              std::sin(theta) * std::sin(phi));
            mesh.vertices.push_back({p, p});
        }
    }
    const auto bottom = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, -1, 0}, {0, -1, 0}});
    for (std::uint32_t segment = 0; segment < segments; ++segment) {
        const auto next = (segment + 1) % segments;
        mesh.indices.insert(mesh.indices.end(), {0, 1 + next, 1 + segment});
        for (std::uint32_t ring = 0; ring + 2 < rings; ++ring) {
            const auto a = 1 + ring * segments + segment;
            const auto b = 1 + ring * segments + next;
            const auto c = a + segments;
            const auto d = b + segments;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
        const auto last = 1 + (rings - 2) * segments;
        mesh.indices.insert(mesh.indices.end(), {last + segment, last + next, bottom});
    }
    mesh.normalize();
    return mesh;
}

Mesh Mesh::torus(std::uint32_t segments, std::uint32_t sides) {
    checkResolution(segments, sides, 3, 3);
    Mesh mesh;
    mesh.name = "Torus";
    mesh.vertices.reserve(std::size_t(segments) * sides);
    mesh.indices.reserve(std::size_t(segments) * sides * 6);
    constexpr float major = 0.72f;
    constexpr float minor = 0.28f;
    for (std::uint32_t segment = 0; segment < segments; ++segment) {
        const float u = 2.0f * pi * float(segment) / float(segments);
        const glm::vec3 radial(std::cos(u), 0.0f, std::sin(u));
        for (std::uint32_t side = 0; side < sides; ++side) {
            const float v = 2.0f * pi * float(side) / float(sides);
            const auto normal = radial * std::cos(v) + glm::vec3(0, std::sin(v), 0);
            mesh.vertices.push_back({radial * major + normal * minor, normal});
            const auto nextSegment = (segment + 1) % segments;
            const auto nextSide = (side + 1) % sides;
            quad(mesh, segment * sides + side, nextSegment * sides + side, segment * sides + nextSide,
                 nextSegment * sides + nextSide);
        }
    }
    mesh.normalize();
    return mesh;
}

Mesh Mesh::knot(std::uint32_t segments, std::uint32_t sides) {
    checkResolution(segments, sides, 24, 3);
    Mesh mesh;
    mesh.name = "Torus knot";
    mesh.vertices.reserve(std::size_t(segments) * sides);
    mesh.indices.reserve(std::size_t(segments) * sides * 6);
    for (std::uint32_t segment = 0; segment < segments; ++segment) {
        const float t = 2.0f * pi * float(segment) / float(segments);
        const float radius = 2.0f + 0.65f * std::cos(3.0f * t);
        const float dr = -1.95f * std::sin(3.0f * t);
        const glm::vec3 radial(std::cos(2.0f * t), 0.0f, std::sin(2.0f * t));
        const glm::vec3 center = radial * radius + glm::vec3(0, 0.9f * std::sin(3.0f * t), 0);
        const auto tangent =
            glm::normalize(glm::vec3(dr * radial.x - 2.0f * radius * radial.z, 2.7f * std::cos(3.0f * t),
                                     dr * radial.z + 2.0f * radius * radial.x));
        // Project the outward radial vector into the normal plane. This frame
        // is smooth and periodic, including the closing seam of the knot.
        const auto normalAxis = glm::normalize(radial - tangent * glm::dot(radial, tangent));
        const auto binormal = glm::cross(tangent, normalAxis);
        for (std::uint32_t side = 0; side < sides; ++side) {
            const float v = 2.0f * pi * float(side) / float(sides);
            const auto normal = normalAxis * std::cos(v) + binormal * std::sin(v);
            mesh.vertices.push_back({center + 0.32f * normal, normal});
            const auto nextSegment = (segment + 1) % segments;
            const auto nextSide = (side + 1) % sides;
            quad(mesh, segment * sides + side, nextSegment * sides + side, segment * sides + nextSide,
                 nextSegment * sides + nextSide);
        }
    }
    mesh.normalize();
    return mesh;
}

Mesh Mesh::cube() {
    Mesh mesh;
    mesh.name = "Cube";
    const std::array<glm::vec3, 6> normals{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (const auto normal : normals) {
        const auto u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const auto v = glm::cross(normal, u);
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({normal - u - v, normal});
        mesh.vertices.push_back({normal + u - v, normal});
        mesh.vertices.push_back({normal + u + v, normal});
        mesh.vertices.push_back({normal - u + v, normal});
        mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
    }
    return mesh;
}

Mesh Mesh::load_obj(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file)
        objError(path, 0, "could not open file");
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > maxObjBytes)
        objError(path, 0, "file exceeds the 64 MiB size limit");
    Mesh mesh;
    mesh.name = path.stem().string();
    std::vector<glm::dvec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::dvec3> generatedNormals;
    std::vector<ObjReference> outputReferences;
    std::unordered_map<ObjReference, std::uint32_t, ObjHash> lookup;
    std::size_t texcoords = 0;
    std::size_t lineNumber = 0;
    std::size_t bytesRead = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++lineNumber;
        bytesRead += line.size() + 1;
        if (bytesRead > maxObjBytes)
            objError(path, lineNumber, "file exceeds the 64 MiB size limit");
        if (const auto comment = line.find('#'); comment != std::string::npos)
            line.resize(comment);
        std::istringstream stream(line);
        std::string kind;
        if (!(stream >> kind))
            continue;
        if (kind == "v") {
            if (positions.size() >= maxVertices)
                objError(path, lineNumber, "too many vertex positions");
            const auto values = parseNumbers(stream, 3, 4, path, lineNumber);
            const double w = values.size() == 4 ? values[3] : 1.0;
            if (w == 0.0)
                objError(path, lineNumber, "homogeneous vertex weight cannot be zero");
            const glm::dvec3 p = glm::dvec3(values[0], values[1], values[2]) / w;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) > 1e20)
                objError(path, lineNumber, "homogeneous vertex is outside the supported coordinate range");
            positions.push_back(p);
            generatedNormals.emplace_back(0.0);
        } else if (kind == "vn") {
            if (normals.size() >= maxVertices)
                objError(path, lineNumber, "too many vertex normals");
            const auto values = parseNumbers(stream, 3, 3, path, lineNumber);
            const glm::dvec3 normal(values[0], values[1], values[2]);
            const double length = glm::length(normal);
            if (length <= 0.0)
                objError(path, lineNumber, "vertex normal has zero length");
            normals.push_back(glm::vec3(normal / length));
        } else if (kind == "vt") {
            if (texcoords >= maxVertices)
                objError(path, lineNumber, "too many texture coordinates");
            parseNumbers(stream, 1, 3, path, lineNumber);
            ++texcoords;
        } else if (kind == "f") {
            std::vector<ObjReference> polygon;
            std::string token;
            while (stream >> token) {
                if (polygon.size() >= 4096)
                    objError(path, lineNumber, "face exceeds the 4096 vertex limit");
                polygon.push_back(
                    parseReference(token, positions.size(), texcoords, normals.size(), path, lineNumber));
            }
            if (polygon.size() < 3)
                objError(path, lineNumber, "face requires at least three vertices");
            if (mesh.indices.size() + (polygon.size() - 2) * 3 > maxIndices)
                objError(path, lineNumber, "mesh exceeds the triangle limit");
            const auto triangles = triangulate(polygon, positions, path, lineNumber);
            for (const auto &triangle : triangles) {
                const auto a = positions[polygon[triangle[0]].position];
                const auto b = positions[polygon[triangle[1]].position];
                const auto c = positions[polygon[triangle[2]].position];
                const auto faceNormal = glm::cross(b - a, c - a);
                for (const auto corner : triangle) {
                    const auto reference = polygon[corner];
                    generatedNormals[reference.position] += faceNormal;
                    auto found = lookup.find(reference);
                    if (found == lookup.end()) {
                        if (mesh.vertices.size() >= maxVertices)
                            objError(path, lineNumber, "too many unique mesh vertices");
                        const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back(
                            {{}, reference.normal == absent ? glm::vec3(0) : normals[reference.normal]});
                        outputReferences.push_back(reference);
                        found = lookup.emplace(reference, index).first;
                    }
                    mesh.indices.push_back(found->second);
                }
            }
        }
        // Object/group/material/smoothing declarations are intentionally ignored;
        // all faces are combined into one mesh with the SceneView material.
    }
    if (file.bad())
        objError(path, lineNumber, "failed while reading file");
    if (mesh.indices.empty())
        objError(path, 0, "file contains no polygon faces");
    // Normalize in double precision before converting large/off-center source
    // positions to floats, retaining the local shape of translated OBJ files.
    glm::dvec3 minimum(std::numeric_limits<double>::max());
    glm::dvec3 maximum(std::numeric_limits<double>::lowest());
    for (const auto &reference : outputReferences) {
        minimum = glm::min(minimum, positions[reference.position]);
        maximum = glm::max(maximum, positions[reference.position]);
    }
    const auto extent = maximum - minimum;
    const auto longest = std::max({extent.x, extent.y, extent.z});
    if (longest <= 0.0)
        objError(path, 0, "mesh has zero extent");
    const auto center = minimum + extent * 0.5;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto reference = outputReferences[i];
        mesh.vertices[i].position = glm::vec3((positions[reference.position] - center) * (2.0 / longest));
        if (reference.normal == absent) {
            const auto normal = generatedNormals[reference.position];
            const double length = glm::length(normal);
            if (length <= 0.0)
                objError(path, 0, "opposing faces cancel a generated normal; provide explicit normals");
            mesh.vertices[i].normal = glm::vec3(normal / length);
        }
    }
    return mesh;
}

void OrbitCamera::orbit(Vec2 delta) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
        return;
    const float safeYaw = std::isfinite(yaw) ? yaw : 0.65f;
    const float safePitch = std::isfinite(pitch) ? pitch : 0.38f;
    yaw = static_cast<float>(std::remainder(double(safeYaw) - double(delta.x) * 0.008, 2.0 * double(pi)));
    pitch = static_cast<float>(
        std::clamp(double(safePitch) + double(delta.y) * 0.008, -double(maxPitch), double(maxPitch)));
}

void OrbitCamera::zoom(float scroll) {
    if (!std::isfinite(scroll))
        return;
    const float safeDistance =
        std::isfinite(distance) ? std::clamp(distance, minDistance, maxDistance) : 4.6f;
    distance = static_cast<float>(
        std::clamp(double(safeDistance) * std::exp(std::clamp(-double(scroll) * 0.12, -50.0, 50.0)),
                   double(minDistance), double(maxDistance)));
}

void OrbitCamera::reset() { *this = OrbitCamera{}; }

glm::mat4 OrbitCamera::view() const {
    const float safeYaw = std::isfinite(yaw) ? yaw : 0.65f;
    const float safePitch = std::isfinite(pitch) ? std::clamp(pitch, -maxPitch, maxPitch) : 0.38f;
    const float safeDistance =
        std::isfinite(distance) ? std::clamp(distance, minDistance, maxDistance) : 4.6f;
    const glm::vec3 eye =
        safeDistance * glm::vec3(std::cos(safePitch) * std::sin(safeYaw), std::sin(safePitch),
                                 std::cos(safePitch) * std::cos(safeYaw));
    return glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
}

glm::mat4 OrbitCamera::projection(float aspect) const {
    const float safeAspect =
        std::isfinite(aspect) && aspect > 0.0f ? std::clamp(aspect, 0.01f, 100.0f) : 1.0f;
    auto result = glm::perspectiveRH_ZO(42.0f * pi / 180.0f, safeAspect, 0.05f, 100.0f);
    result[1][1] *= -1.0f;
    return result;
}

} // namespace Fluid
