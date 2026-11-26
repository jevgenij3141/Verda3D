#pragma once

#include "material_lighting.h"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

inline constexpr Vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
inline constexpr double nearPlane = .1;
inline constexpr double farPlane = 2000;
inline constexpr double minViewZoom = .025;
inline constexpr double halfBox = .5;
inline constexpr double pi = 3.141592653589793;

struct Ray { Vec3 origin, direction; };

struct Transform {
    Vec3 position{};
    std::array<Vec3, 3> basis{{axes[0], axes[1], axes[2]}};
    std::array<double, 3> size{{1, 1, 1}};
};

enum class KeyInterpolation { Linear, Smooth };

struct TransformKey {
    int frame = 0;
    Transform pose;
    KeyInterpolation interpolation = KeyInterpolation::Linear;
};

enum class ObjectKind { Box, Omni, Spot, Sky, Plane, Sphere, Cylinder, Pyramid, Camera, Mesh, Group };
inline constexpr const char* objectNames[] = {"Box", "Omni", "Spot", "Sky", "Plane", "Sphere",
                                             "Cylinder", "Pyramid", "Camera", "Mesh", "Group"};
inline constexpr ObjectKind creationKinds[] = {ObjectKind::Box, ObjectKind::Plane, ObjectKind::Sphere,
                                               ObjectKind::Cylinder, ObjectKind::Pyramid, ObjectKind::Camera};
inline bool isLight(ObjectKind kind) { return kind == ObjectKind::Omni || kind == ObjectKind::Spot || kind == ObjectKind::Sky; }
inline bool isGeometry(ObjectKind kind) {
    return !isLight(kind) && kind != ObjectKind::Camera && kind != ObjectKind::Group;
}

struct Light {
    Color color{1, 1, 1};
    double power = 40;
    double range = 0;
    double radius = .3;
    double cone = 50;
    bool enabled = true;
    bool shadows = false;
    int shadowSize = 128;
    double shadowTransparency = .15;
};

struct MeshFace {
    std::array<std::size_t, 4> vertices;
    std::array<Vec2, 4> uv;
    std::size_t count = 4;
};

struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<MeshFace> faces;
    std::vector<Vec3> normals{};
};

struct SmoothModifier {
    bool active = false;
    int levels = 1;
};
enum class MeshShading { Default, Smooth, Flat };
constexpr int maxSmoothLevels = 4;
constexpr int maxPlaneSegments = 32;
constexpr std::size_t maxModifiers = 16;

struct Model {
    Transform transform;
    std::string name;
    int material = -1;
    ObjectKind kind = ObjectKind::Box;
    Light light;
    Mesh mesh{};
    bool hidden = false;
    std::vector<TransformKey> keys{};
    std::vector<SmoothModifier> modifiers{};
    std::shared_ptr<const Mesh> evaluatedMesh{};
    int parent = -1;
    bool expanded = true;
    int planeSegmentsX = 1, planeSegmentsY = 1;
    MeshShading meshShading = MeshShading::Default;
    std::vector<std::array<Vec2, 4>> defaultTextureUvs{};
};

inline bool isDescendant(const std::vector<Model>& models, int child, int group) {
    for (int parent = models[child].parent; parent >= 0; parent = models[parent].parent) {
        if (parent == group) return true;
    }
    return false;
}

inline bool objectVisible(const std::vector<Model>& models, int index) {
    for (int current = index; current >= 0; current = models[current].parent) {
        if (models[current].hidden) return false;
    }
    return true;
}

struct SurfaceHit {
    double distance;
    Vec3 normal{};
    Vec2 uv{};
    std::size_t face = std::numeric_limits<std::size_t>::max();
    Vec3 shadingNormal{};
};

inline SurfaceHit intersectBox(Ray ray, const Transform& transform, double min, double max) {
    const Vec3 offset = ray.origin - transform.position;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    double entry = -infinity, exit = infinity;
    int entryAxis = 0, exitAxis = 0;
    double entrySign = 0, exitSign = 0;
    double origin[3], direction[3];
    for (int i = 0; i < 3; ++i) {
        const double extent = halfBox * transform.size[i];
        origin[i] = dot(offset, transform.basis[i]);
        direction[i] = dot(ray.direction, transform.basis[i]);
        if (std::abs(direction[i]) < 1e-10) {
            if (std::abs(origin[i]) > extent) return {max};
            continue;
        }
        const double a = (-extent - origin[i]) / direction[i];
        const double b = (extent - origin[i]) / direction[i];
        if (std::min(a, b) > entry) {
            entry = std::min(a, b);
            entryAxis = i;
            entrySign = a < b ? -1 : 1;
        }
        if (std::max(a, b) < exit) {
            exit = std::max(a, b);
            exitAxis = i;
            exitSign = a < b ? 1 : -1;
        }
        if (entry > exit) return {max};
    }
    const bool outside = entry >= min;
    const double distance = outside ? entry : exit;
    if (distance < min || distance >= max) return {max};
    const int axis = outside ? entryAxis : exitAxis;
    const double sign = outside ? entrySign : exitSign;
    const double x = (origin[0] + direction[0] * distance) / transform.size[0];
    const double y = (origin[1] + direction[1] * distance) / transform.size[1];
    const double z = (origin[2] + direction[2] * distance) / transform.size[2];
    const Vec2 uv = axis == 0 ? Vec2{.5 - z * sign, .5 - y}
                  : axis == 1 ? Vec2{.5 + x, .5 + z * sign}
                              : Vec2{.5 + x * sign, .5 - y};
    constexpr std::size_t faceByAxis[][2] = {{2, 3}, {4, 5}, {0, 1}};
    return {distance, transform.basis[axis] * sign, uv, faceByAxis[axis][sign > 0]};
}

inline Vec3 rgb(Color color) { return {color.r, color.g, color.b}; }
inline Vec3 multiply(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

inline double localLightPower(const Model& model, Vec3 sourceToPoint, double distance) {
    const Light& light = model.light;
    if (!light.enabled || (light.range > 0 && distance >= light.range)) return 0;
    double fade = light.range > 0 ? std::clamp((light.range - distance) / (light.range * .2), 0.0, 1.0) : 1;
    if (model.kind == ObjectKind::Spot) {
        const double outer = std::cos(light.cone * pi / 360);
        const double inner = std::cos(light.cone * pi / 480);
        const double cone = std::clamp((dot(sourceToPoint, model.transform.basis[2] * -1) - outer) /
                                       (inner - outer), 0.0, 1.0);
        fade *= cone * cone * (3 - 2 * cone);
    }
    return light.power * fade / (1 + distance * distance);
}

inline Vec3 skyRadiance(const Model& model, Vec3 direction) {
    if (!model.light.enabled) return {};
    const double hemisphere = .18 + .82 * std::max(0.0, dot(direction, model.transform.basis[1]));
    return rgb(model.light.color) * (model.light.power * hemisphere);
}
