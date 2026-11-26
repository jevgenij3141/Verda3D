#include "scene_mesh.h"

#include <cstdint>
#include <unordered_map>

namespace {
constexpr std::size_t triangleBudget = 32768;

struct Edge {
    std::size_t a, b;
    std::size_t faces = 0;
    Vec3 faceSum{};
};

struct VertexSum {
    Vec3 faceCenters{}, midpoints{}, boundaryNeighbors{};
    std::size_t faces = 0, edges = 0, boundary = 0;
};

std::uint64_t edgeKey(std::size_t a, std::size_t b) {
    return std::uint64_t(std::min(a, b)) << 32 | std::max(a, b);
}

Mesh subdivide(const Mesh& mesh) {
    const auto vertexCount = mesh.vertices.size();
    const auto faceCount = mesh.faces.size();
    std::vector<VertexSum> sums(vertexCount);
    std::vector<Edge> edges;
    std::vector<Vec3> centers(faceCount);
    std::vector<std::array<std::size_t, 4>> faceEdges(faceCount);
    std::unordered_map<std::uint64_t, std::size_t> edgeIndices;
    std::size_t outputFaces = 0;
    for (std::size_t i = 0; i < faceCount; ++i) {
        const MeshFace& face = mesh.faces[i];
        Vec3 center{};
        for (std::size_t j = 0; j < face.count; ++j) center = center + mesh.vertices[face.vertices[j]];
        center = center * (1.0 / face.count);
        centers[i] = center;
        outputFaces += face.count;
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto a = face.vertices[j], b = face.vertices[(j + 1) % face.count];
            sums[a].faceCenters = sums[a].faceCenters + center;
            ++sums[a].faces;
            const auto entry = edgeIndices.emplace(edgeKey(a, b), edges.size());
            if (entry.second) edges.push_back({a, b});
            const auto edgeIndex = entry.first->second;
            Edge& edge = edges[edgeIndex];
            ++edge.faces;
            edge.faceSum = edge.faceSum + center;
            faceEdges[i][j] = edgeIndex;
        }
    }

    Mesh result;
    result.vertices.resize(vertexCount + edges.size() + faceCount);
    result.faces.reserve(outputFaces);
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto& edge = edges[i];
        const Vec3 a = mesh.vertices[edge.a], b = mesh.vertices[edge.b];
        const Vec3 midpoint = (a + b) * .5;
        auto& from = sums[edge.a];
        auto& to = sums[edge.b];
        from.midpoints = from.midpoints + midpoint;
        to.midpoints = to.midpoints + midpoint;
        ++from.edges;
        ++to.edges;
        if (edge.faces == 2) {
            result.vertices[vertexCount + i] = (a + b + edge.faceSum) * .25;
        } else {
            ++from.boundary;
            ++to.boundary;
            from.boundaryNeighbors = from.boundaryNeighbors + b;
            to.boundaryNeighbors = to.boundaryNeighbors + a;
            result.vertices[vertexCount + i] = midpoint;
        }
    }
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const auto& sum = sums[i];
        const Vec3 p = mesh.vertices[i];
        if (sum.boundary == 2) {
            result.vertices[i] = (p * 6 + sum.boundaryNeighbors) * .125;
        } else if (sum.boundary == 0 && sum.edges >= 3 && sum.edges == sum.faces) {
            const double n = double(sum.edges);
            const Vec3 center = sum.faceCenters * (1 / n), mid = sum.midpoints * (1 / n);
            result.vertices[i] = (center + mid * 2 + p * (n - 3)) * (1 / n);
        } else result.vertices[i] = p;
    }
    for (std::size_t i = 0; i < faceCount; ++i) {
        const auto& face = mesh.faces[i];
        const auto center = vertexCount + edges.size() + i;
        result.vertices[center] = centers[i];
        Vec2 uvCenter{};
        for (std::size_t j = 0; j < face.count; ++j) uvCenter = uvCenter + face.uv[j] * (1.0 / face.count);
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto prev = (j + face.count - 1) % face.count;
            const auto next = (j + 1) % face.count;
            result.faces.push_back({{face.vertices[j], vertexCount + faceEdges[i][j], center,
                                      vertexCount + faceEdges[i][prev]},
                {face.uv[j], (face.uv[j] + face.uv[next]) * .5, uvCenter, (face.uv[j] + face.uv[prev]) * .5}});
        }
    }
    return result;
}
void smoothNormals(Mesh& mesh) {
    mesh.normals.assign(mesh.vertices.size(), {});
    for (const MeshFace& face : mesh.faces) {
        const Vec3 origin = mesh.vertices[face.vertices[0]];
        const Vec3 diagonal = mesh.vertices[face.vertices[2]] - origin;
        Vec3 normal = cross(mesh.vertices[face.vertices[1]] - origin, diagonal);
        if (face.count == 4) normal = normal + cross(diagonal, mesh.vertices[face.vertices[3]] - origin);
        for (std::size_t i = 0; i < face.count; ++i)
            mesh.normals[face.vertices[i]] = mesh.normals[face.vertices[i]] + normal;
    }
    for (Vec3& normal : mesh.normals) normal = normalized(normal);
}
} // namespace

bool rebuildModifiers(Model& model) {
    int levels = 0;
    for (const auto& modifier : model.modifiers) {
        if (!modifier.active) continue;
        if (modifier.levels < 1 || modifier.levels > maxSmoothLevels) return false;
        levels += modifier.levels;
    }
    if (levels == 0) {
        if (model.meshShading == MeshShading::Smooth) {
            auto evaluated = std::make_shared<Mesh>(modelMesh(model, true));
            smoothNormals(*evaluated);
            model.evaluatedMesh = std::move(evaluated);
        } else model.evaluatedMesh.reset();
        return true;
    }
    const Mesh& base = modelMesh(model, true);
    std::size_t quads = 0;
    for (const MeshFace& face : base.faces) quads += face.count;
    for (int level = 1; level < levels; ++level) {
        if (quads > triangleBudget / 8) return false;
        quads *= 4;
    }
    if (quads > triangleBudget / 2) return false;

    Mesh smooth = subdivide(base);
    for (int level = 1; level < levels; ++level) smooth = subdivide(smooth);
    auto evaluated = std::make_shared<Mesh>();
    evaluated->vertices = std::move(smooth.vertices);
    evaluated->faces.reserve(smooth.faces.size() * 2);
    // The evaluated quads can be curved across the diagonal.
    for (const MeshFace& quad : smooth.faces) {
        for (const std::size_t next : {std::size_t(1), std::size_t(2)}) {
            evaluated->faces.push_back({{quad.vertices[0], quad.vertices[next], quad.vertices[next + 1], quad.vertices[next + 1]},
                {quad.uv[0], quad.uv[next], quad.uv[next + 1], quad.uv[next + 1]}, 3});
        }
    }
    if (model.meshShading != MeshShading::Flat) smoothNormals(*evaluated);
    model.evaluatedMesh = std::move(evaluated);
    return true;
}
