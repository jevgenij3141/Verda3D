#pragma once

#include "scene.h"

constexpr std::size_t noFace = std::numeric_limits<std::size_t>::max();

struct FaceGeometry {
    Vec3 origin, u, v, normal;
    Vec2 uvOrigin, uvU, uvV;
    bool triangle;
    std::array<Vec3, 4> shadingNormals{};
    Vec3 diagonal{};
    Vec2 uvDiagonal{};

    std::array<Vec3, 4> corners() const { return {origin, origin + u, triangle ? origin + v : origin + diagonal, origin + v}; }
    std::size_t count() const { return triangle ? 3 : 4; }
    Vec3 shade(double s, double t) const {
        if (dot(shadingNormals[0], shadingNormals[0]) < .5) return normal;
        return normalized(shadingNormals[0] * (1 - s - t) + shadingNormals[1] * s + shadingNormals[2] * t);
    }
    bool needsSplit() const;
    FaceGeometry trianglePart(int part) const;
};

struct Bounds {
    Vec3 min, max;
    Vec3 center() const { return (min + max) * .5; }
};

const Mesh& modelMesh(const Model& model, bool baseOnly = false);
Mesh createPrimitive(ObjectKind kind);
Mesh createExtraPrimitive(ObjectKind kind);
Mesh createPlane(int segmentsX, int segmentsY);
FaceGeometry faceGeometry(const Model& model, std::size_t face, bool baseOnly = false);
Bounds modelBounds(const Model& model, bool baseOnly = false);
bool rebuildModifiers(Model& model);
Vec3 localVector(const Transform& transform, Vec3 world);
Vec3 worldVector(const Transform& transform, Vec3 local);
struct ExtrudedVertex { std::size_t index; Vec3 original; };
struct ExtrusionTopology {
    std::vector<ExtrudedVertex> moved;
    std::vector<std::vector<std::size_t>> sealedRegions;
};
bool prepareExtrusion(Mesh& mesh, const std::vector<std::size_t>& faces, ExtrusionTopology& topology);
bool orientExtrusion(Mesh& mesh, const ExtrusionTopology& topology);
bool bridgeFaces(Mesh& mesh, const Transform& transform, std::size_t first, std::size_t second);

class ModelCollider {
public:
    explicit ModelCollider(const Model& model, bool baseOnly = false);
    SurfaceHit intersect(Ray ray, double min, double max) const;
    const Bounds& bounds() const { return worldBounds; }

private:
    struct Plane {
        FaceGeometry face;
        Vec3 alongU, alongV;
        Bounds bounds;
        std::size_t index;
    };
    struct Node {
        Bounds bounds;
        std::size_t first, count;
        int left = -1, right = -1;
    };

    Transform transform;
    Bounds worldBounds;
    bool analyticBox;
    std::vector<Plane> planes;
    std::vector<Node> nodes;
    int buildNode(std::size_t first, std::size_t count);
};
