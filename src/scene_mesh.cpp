#include "scene_mesh.h"

#include <cstdint>
#include <numeric>
#include <unordered_map>

Vec3 worldVector(const Transform& transform, Vec3 local) {
    return transform.basis[0] * (local.x * transform.size[0]) +
           transform.basis[1] * (local.y * transform.size[1]) +
           transform.basis[2] * (local.z * transform.size[2]);
}

namespace {
bool intersectsBounds(Ray ray, const Bounds& bounds, double entry, double exit) {
    constexpr double tolerance = 1e-8;
    for (Vec3 axis : axes) {
        const double origin = dot(ray.origin, axis);
        const double direction = dot(ray.direction, axis);
        const double low = dot(bounds.min, axis) - tolerance;
        const double high = dot(bounds.max, axis) + tolerance;
        if (std::abs(direction) < 1e-10) {
            if (origin < low || origin > high) return false;
            continue;
        }
        const double a = (low - origin) / direction, b = (high - origin) / direction;
        entry = std::max(entry, std::min(a, b));
        exit = std::min(exit, std::max(a, b));
        if (entry > exit) return false;
    }
    return true;
}
} // namespace

const Mesh& modelMesh(const Model& model, bool baseOnly) {
    constexpr double h = halfBox;
    static const Mesh box{
        {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
         {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}},
        {
            {{0, 3, 2, 1}, {{{1, 1}, {1, 0}, {0, 0}, {0, 1}}}},
            {{4, 5, 6, 7}, {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}}},
            {{0, 4, 7, 3}, {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}}},
            {{1, 2, 6, 5}, {{{1, 1}, {1, 0}, {0, 0}, {0, 1}}}},
            {{0, 1, 5, 4}, {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}}},
            {{3, 7, 6, 2}, {{{0, 0}, {0, 1}, {1, 1}, {1, 0}}}},
        }
    };
    if (!baseOnly && model.evaluatedMesh) return *model.evaluatedMesh;
    return model.mesh.faces.empty() ? box : model.mesh;
}

Mesh createPrimitive(ObjectKind kind) {
    if (kind >= ObjectKind::Teapot) return createExtraPrimitive(kind);
    Mesh mesh;
    constexpr std::array<Vec2, 4> quadUv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    const auto tri = [&](std::size_t a, std::size_t b, std::size_t c, Vec2 u, Vec2 v, Vec2 w) {
        mesh.faces.push_back({{a, b, c, c}, {u, v, w, w}, 3});
    };
    if (kind == ObjectKind::Plane) {
        return createPlane(1, 1);
    } else if (kind == ObjectKind::Pyramid) {
        mesh.vertices = {{-.5, -.5, -.5}, {.5, -.5, -.5}, {.5, -.5, .5}, {-.5, -.5, .5}, {0, .5, 0}};
        mesh.faces.push_back({{0, 1, 2, 3}, quadUv});
        for (std::size_t i = 0; i < 4; ++i) tri((i + 1) % 4, i, 4, {0, 1}, {1, 1}, {.5, 0});
    } else if (kind == ObjectKind::Cylinder) {
        constexpr std::size_t sides = 16;
        for (double y : {-.5, .5}) {
            for (std::size_t i = 0; i < sides; ++i) {
                const double angle = 2 * pi * i / sides;
                mesh.vertices.push_back({.5 * std::cos(angle), y, .5 * std::sin(angle)});
            }
        }
        mesh.vertices.push_back({0, -.5, 0});
        mesh.vertices.push_back({0, .5, 0});
        const auto capUv = [&](std::size_t i) { const Vec3 p = mesh.vertices[i]; return Vec2{.5 + p.x, .5 + p.z}; };
        for (std::size_t i = 0; i < sides; ++i) {
            const std::size_t j = (i + 1) % sides;
            const double a = double(i) / sides, b = double(i + 1) / sides;
            mesh.faces.push_back({{j, i, i + sides, j + sides}, {{{b, 1}, {a, 1}, {a, 0}, {b, 0}}}});
            tri(2 * sides, i, j, {.5, .5}, capUv(i), capUv(j));
            tri(2 * sides + 1, j + sides, i + sides, {.5, .5}, capUv(j), capUv(i));
        }
    } else if (kind == ObjectKind::Sphere) {
        constexpr std::size_t segments = 16, rings = 8;
        mesh.vertices.push_back({0, .5, 0});
        for (std::size_t row = 1; row < rings; ++row) {
            const double latitude = pi * row / rings;
            for (std::size_t i = 0; i < segments; ++i) {
                const double angle = 2 * pi * i / segments;
                mesh.vertices.push_back({.5 * std::sin(latitude) * std::cos(angle), .5 * std::cos(latitude),
                                         .5 * std::sin(latitude) * std::sin(angle)});
            }
        }
        const std::size_t south = mesh.vertices.size();
        mesh.vertices.push_back({0, -.5, 0});
        const auto index = [south](std::size_t row, std::size_t col) {
            return row == 0 ? 0 : row == rings ? south : 1 + (row - 1) * segments + col % segments;
        };
        for (std::size_t row = 0; row < rings; ++row) {
            for (std::size_t i = 0; i < segments; ++i) {
                const double a = double(i) / segments, b = double(i + 1) / segments;
                const double y = double(row) / rings, z = double(row + 1) / rings;
                const auto tl = index(row, i), tr = index(row, i + 1);
                const auto bl = index(row + 1, i), br = index(row + 1, i + 1);
                if (row != rings - 1) tri(tl, br, bl, {row == 0 ? (a + b) * .5 : a, y}, {b, z}, {a, z});
                if (row != 0) tri(tl, tr, br, {a, y}, {b, y}, {row == rings - 1 ? (a + b) * .5 : b, z});
            }
        }
    }
    return mesh;
}

Mesh createPlane(int segmentsX, int segmentsY) {
    Mesh mesh;
    const std::size_t pitch = segmentsX + 1;
    for (int y = 0; y <= segmentsY; ++y)
        for (int x = 0; x <= segmentsX; ++x)
            mesh.vertices.push_back({double(x) / segmentsX - .5, 0, .5 - double(y) / segmentsY});
    for (int y = 0; y < segmentsY; ++y) {
        for (int x = 0; x < segmentsX; ++x) {
            const std::size_t a = y * pitch + x;
            const double u = double(x) / segmentsX, v = 1.0 - double(y) / segmentsY;
            const double nextU = double(x + 1) / segmentsX, nextV = 1.0 - double(y + 1) / segmentsY;
            mesh.faces.push_back({{a, a + 1, a + pitch + 1, a + pitch},
                                 {{{u,v}, {nextU,v}, {nextU,nextV}, {u,nextV}}}});
        }
    }
    return mesh;
}

bool FaceGeometry::needsSplit() const {
    if (triangle) return false;
    const Vec3 deviation = diagonal - u - v;
    const Vec2 uvDeviation = uvDiagonal - uvU - uvV;
    return dot(shadingNormals[0], shadingNormals[0]) > .5 ||
           dot(deviation, deviation) > 1e-16 * std::max(dot(u,u) + dot(v,v), 1e-18) ||
           dot(uvDeviation, uvDeviation) > 1e-16;
}

FaceGeometry FaceGeometry::trianglePart(int part) const {
    const Vec3 a = part == 0 ? u : diagonal, b = part == 0 ? diagonal : v;
    FaceGeometry result{origin, a, b, normalized(cross(a,b)), uvOrigin,
                        part == 0 ? uvU : uvDiagonal, part == 0 ? uvDiagonal : uvV, true};
    result.shadingNormals = {shadingNormals[0], shadingNormals[part+1], shadingNormals[part+2]};
    return result;
}

Vec3 localVector(const Transform& transform, Vec3 world) {
    return {dot(world, transform.basis[0]) / transform.size[0],
            dot(world, transform.basis[1]) / transform.size[1],
            dot(world, transform.basis[2]) / transform.size[2]};
}

FaceGeometry faceGeometry(const Model& model, std::size_t index, bool baseOnly) {
    const Mesh& mesh = modelMesh(model, baseOnly);
    const MeshFace& face = mesh.faces[index];
    const auto last = face.count - 1;
    const Vec3 origin = mesh.vertices[face.vertices[0]];
    const Vec3 u = worldVector(model.transform, mesh.vertices[face.vertices[1]] - origin);
    const Vec3 v = worldVector(model.transform, mesh.vertices[face.vertices[last]] - origin);
    FaceGeometry result{model.transform.position + worldVector(model.transform, origin), u, v, normalized(cross(u, v)),
                        face.uv[0], face.uv[1] - face.uv[0], face.uv[last] - face.uv[0], face.count == 3};
    if (!result.triangle) {
        result.diagonal = worldVector(model.transform, mesh.vertices[face.vertices[2]] - origin);
        result.uvDiagonal = face.uv[2] - face.uv[0];
    }
    if (!mesh.normals.empty()) {
        const Transform& t = model.transform;
        for (std::size_t i = 0; i < face.count; ++i) {
            const Vec3 n = mesh.normals[face.vertices[i]];
            result.shadingNormals[i] = normalized(t.basis[0] * (n.x / t.size[0]) + t.basis[1] * (n.y / t.size[1]) +
                                                  t.basis[2] * (n.z / t.size[2]));
        }
    }
    return result;
}

Bounds modelBounds(const Model& model, bool baseOnly) {
    const Mesh& mesh = modelMesh(model, baseOnly);
    const Vec3 first = model.transform.position + worldVector(model.transform, mesh.vertices.front());
    Bounds bounds{first, first};
    for (Vec3 local : mesh.vertices) {
        const Vec3 p = model.transform.position + worldVector(model.transform, local);
        bounds.min = {std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y), std::min(bounds.min.z, p.z)};
        bounds.max = {std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y), std::max(bounds.max.z, p.z)};
    }
    return bounds;
}

bool prepareExtrusion(Mesh& mesh, const std::vector<std::size_t>& faces, ExtrusionTopology& topology) {
    if (faces.empty() || mesh.vertices.size() > maxMeshVertices || mesh.faces.size() > maxMeshFaces) return false;
    const auto key = [](std::size_t a, std::size_t b) { return (std::uint64_t(a) << 32) | b; };
    const auto edgeKey = [&](std::size_t a, std::size_t b) { return key(std::min(a,b), std::max(a,b)); };
    struct Edge {
        std::size_t from, to, face;
        int selected = 1, total = 0;
    };
    std::unordered_map<std::uint64_t, Edge> edges;
    std::vector<std::uint64_t> edgeOrder;
    std::vector<std::size_t> parents(faces.size());
    std::iota(parents.begin(), parents.end(), 0);
    const auto region = [&](std::size_t i) {
        while (parents[i] != i) {
            parents[i] = parents[parents[i]];
            i = parents[i];
        }
        return i;
    };
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (faces[i] >= mesh.faces.size()) return false;
        const auto& face = mesh.faces[faces[i]];
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto a = face.vertices[j], b = face.vertices[(j+1) % face.count];
            const auto id = edgeKey(a,b);
            const auto [entry, inserted] = edges.try_emplace(id, Edge{a,b,i});
            if (inserted) { edgeOrder.push_back(id); continue; }
            Edge& edge = entry->second;
            if (edge.selected != 1 || edge.from != b || edge.to != a) return false;
            ++edge.selected;
            parents[region(i)] = region(edge.face);
        }
    }
    for (const auto& face : mesh.faces) {
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto it = edges.find(edgeKey(face.vertices[j], face.vertices[(j+1) % face.count]));
            if (it != edges.end() && ++it->second.total > 2) return false;
        }
    }
    std::vector<bool> boundary(faces.size()), detached(faces.size(), true);
    std::size_t addedFaces = 0;
    for (const auto& [unused, edge] : edges) {
        if (edge.selected != 1) continue;
        const auto group = region(edge.face);
        boundary[group] = true;
        if (edge.total == 2) detached[group] = false;
        ++addedFaces;
    }
    std::vector<std::size_t> sealed(faces.size(), noFace);
    topology = {};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto group = region(i);
        if (!boundary[group]) return false;
        if (!detached[group]) continue;
        if (sealed[group] == noFace) {
            sealed[group] = topology.sealedRegions.size();
            topology.sealedRegions.emplace_back();
        }
        ++addedFaces;
    }
    if (mesh.faces.size() + addedFaces > maxMeshFaces) return false;

    std::unordered_map<std::uint64_t, std::size_t> capVertices;
    auto& moved = topology.moved;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        MeshFace cap = mesh.faces[faces[i]];
        const auto group = region(i);
        if (detached[group]) {
            MeshFace base = cap;
            std::reverse(base.vertices.begin(), base.vertices.begin() + cap.count);
            std::reverse(base.uv.begin(), base.uv.begin() + cap.count);
            auto& shell = topology.sealedRegions[sealed[group]];
            shell.push_back(faces[i]);
            shell.push_back(mesh.faces.size());
            mesh.faces.push_back(base);
        }
        for (std::size_t j = 0; j < cap.count; ++j) {
            const auto index = cap.vertices[j];
            const auto [entry, added] = capVertices.try_emplace(key(group,index), mesh.vertices.size());
            if (added) {
                const Vec3 point = mesh.vertices[index];
                mesh.vertices.push_back(point);
                moved.push_back({entry->second, point});
            }
            cap.vertices[j] = entry->second;
        }
        mesh.faces[faces[i]] = cap;
    }
    for (const auto id : edgeOrder) {
        const auto& edge = edges.at(id);
        if (edge.selected != 1) continue;
        const auto group = region(edge.face);
        if (detached[group]) topology.sealedRegions[sealed[group]].push_back(mesh.faces.size());
        mesh.faces.push_back({{edge.from, edge.to, capVertices.at(key(group,edge.to)), capVertices.at(key(group,edge.from))},
                              {{{0, 1}, {1, 1}, {1, 0}, {0, 0}}}});
    }
    std::vector<std::size_t> remap(mesh.vertices.size(), noFace);
    for (const auto& face : mesh.faces)
        for (std::size_t j = 0; j < face.count; ++j) remap[face.vertices[j]] = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < remap.size(); ++i) {
        if (remap[i] == noFace) continue;
        remap[i] = count;
        mesh.vertices[count++] = mesh.vertices[i];
    }
    if (count > maxMeshVertices) return false;
    mesh.vertices.resize(count);
    mesh.normals.clear();
    for (auto& face : mesh.faces)
        for (std::size_t j = 0; j < face.count; ++j) face.vertices[j] = remap[face.vertices[j]];
    for (auto& vertex : moved) vertex.index = remap[vertex.index];
    return true;
}

bool orientExtrusion(Mesh& mesh, const ExtrusionTopology& topology) {
    for (const auto& shell : topology.sealedRegions) {
        const Vec3 reference = mesh.vertices[mesh.faces[shell.front()].vertices[0]];
        double volume = 0;
        for (const auto id : shell) {
            const auto& face = mesh.faces[id];
            const Vec3 a = mesh.vertices[face.vertices[0]] - reference;
            for (std::size_t j = 1; j+1 < face.count; ++j) {
                const Vec3 b = mesh.vertices[face.vertices[j]] - reference;
                const Vec3 c = mesh.vertices[face.vertices[j+1]] - reference;
                volume += dot(a, cross(b,c));
            }
        }
        if (std::abs(volume) <= 1e-14) return false;
        if (volume > 0) continue;
        for (const auto id : shell) {
            auto& face = mesh.faces[id];
            std::reverse(face.vertices.begin(), face.vertices.begin() + face.count);
            std::reverse(face.uv.begin(), face.uv.begin() + face.count);
        }
    }
    return true;
}

bool bridgeFaces(Mesh& mesh, const Transform& transform, std::size_t first, std::size_t second) {
    if (first == second || first >= mesh.faces.size() || second >= mesh.faces.size()) return false;
    const auto a = mesh.faces[first], b = mesh.faces[second];
    for (std::size_t i = 0; i < a.count; ++i)
        for (std::size_t j = 0; j < b.count; ++j)
            if (a.vertices[i] == b.vertices[j]) return false;

    const auto across = [&](std::size_t i, std::size_t j, std::size_t offset) {
        return std::pair{a.vertices[i % a.count], b.vertices[(offset + b.count - j % b.count) % b.count]};
    };
    const auto walk = [&](const auto& visit) {
        std::size_t i = 0, j = 0;
        while (i < a.count || j < b.count) {
            const auto endA = (i + 1) * b.count, endB = (j + 1) * a.count;
            const bool stepA = i < a.count && (j == b.count || endA <= endB);
            const bool stepB = j < b.count && (i == a.count || endB <= endA);
            visit(i, j, i + stepA, j + stepB);
            i += stepA;
            j += stepB;
        }
    };
    std::size_t alignment = 0;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t start = 0; start < b.count; ++start) {
        double cost = 0;
        walk([&](std::size_t i, std::size_t j, std::size_t, std::size_t) {
            const auto [x,y] = across(i,j,start);
            const Vec3 direction = worldVector(transform, mesh.vertices[x] - mesh.vertices[y]);
            cost += dot(direction, direction);
        });
        if (cost < best) { best = cost; alignment = start; }
    }

    std::vector<std::pair<std::size_t,std::size_t>> seams;
    std::vector<MeshFace> sides;
    walk([&](std::size_t i, std::size_t j, std::size_t nextI, std::size_t nextJ) {
        const auto [x,y] = across(i,j,alignment);
        const auto [nextX,nextY] = across(nextI,nextJ,alignment);
        seams.emplace_back(std::min(x,y), std::max(x,y));
        const Vec2 uvX{double(i)/a.count,0}, uvY{double(j)/b.count,1};
        const Vec2 uvNextX{double(nextI)/a.count,0}, uvNextY{double(nextJ)/b.count,1};
        if (i == nextI) sides.push_back({{x,nextY,y,y},{uvX,uvNextY,uvY,uvY},3});
        else if (j == nextJ) sides.push_back({{x,nextX,y,y},{uvX,uvNextX,uvY,uvY},3});
        else sides.push_back({{x,nextX,nextY,y},{uvX,uvNextX,uvNextY,uvY}});
    });
    if (mesh.faces.size() - 2 + sides.size() > maxMeshFaces) return false;
    for (const auto& face : mesh.faces) {
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto x = face.vertices[j], y = face.vertices[(j+1) % face.count];
            if (std::find(seams.begin(), seams.end(), std::pair{std::min(x,y), std::max(x,y)}) != seams.end())
                return false;
        }
    }
    const auto collapsed = [](Vec3 normal) { return dot(normal, normal) <= 1e-20; };
    for (const auto& side : sides) {
        const Vec3 origin = mesh.vertices[side.vertices[0]];
        const Vec3 u = mesh.vertices[side.vertices[1]] - origin;
        const Vec3 v = mesh.vertices[side.vertices[side.count - 1]] - origin;
        if (collapsed(cross(u,v))) return false;
        if (side.count == 4) {
            const Vec3 diagonal = mesh.vertices[side.vertices[2]] - origin;
            const Vec3 normalA = cross(u,diagonal), normalB = cross(diagonal,v);
            if (collapsed(normalA) || collapsed(normalB) || dot(normalA,normalB) < 0) return false;
        }
    }
    if (first > second) std::swap(first,second);
    mesh.faces.erase(mesh.faces.begin() + second);
    mesh.faces.erase(mesh.faces.begin() + first);
    mesh.faces.insert(mesh.faces.end(), sides.begin(), sides.end());
    mesh.normals.clear();
    return true;
}

namespace {
Bounds merged(Bounds a, Bounds b) {
    return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
            {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}};
}
} // namespace

ModelCollider::ModelCollider(const Model& model, bool baseOnly)
    : transform(model.transform), worldBounds(modelBounds(model, baseOnly)),
      analyticBox(model.mesh.faces.empty() && (baseOnly || !model.evaluatedMesh)) {
    if (analyticBox) return;
    const auto count = modelMesh(model, baseOnly).faces.size();
    planes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const FaceGeometry polygon = faceGeometry(model, i, baseOnly);
        const bool split = polygon.needsSplit();
        for (int part = 0; part < (split ? 2 : 1); ++part) {
            const FaceGeometry face = split ? polygon.trianglePart(part) : polygon;
            const Vec3 alongU = cross(face.v, face.normal);
            const Vec3 alongV = cross(face.normal, face.u);
            const double u = dot(alongU, face.u), v = dot(alongV, face.v);
            if (u <= 1e-12 || v <= 1e-12) continue;
            Bounds bounds{face.origin, face.origin};
            for (Vec3 p : face.corners()) bounds = merged(bounds, {p, p});
            planes.push_back({face, alongU * (1 / u), alongV * (1 / v), bounds, i});
        }
    }
    if (!planes.empty()) buildNode(0, planes.size());
}

int ModelCollider::buildNode(std::size_t first, std::size_t count) {
    Bounds bounds = planes[first].bounds;
    const Vec3 origin = planes[first].bounds.center();
    Bounds centroids{origin, origin};
    for (std::size_t i = first + 1; i < first + count; ++i) {
        bounds = merged(bounds, planes[i].bounds);
        const Vec3 center = planes[i].bounds.center();
        centroids = merged(centroids, {center, center});
    }
    const int index = int(nodes.size());
    nodes.push_back({bounds, first, count});
    if (count <= 8) return index;
    const Vec3 size = centroids.max - centroids.min;
    const Vec3 axis = size.x > size.y && size.x > size.z ? axes[0] : size.y > size.z ? axes[1] : axes[2];
    const auto middle = first + count / 2;
    std::nth_element(planes.begin() + first, planes.begin() + middle, planes.begin() + first + count,
        [axis](const Plane& a, const Plane& b) { return dot(a.bounds.center(), axis) < dot(b.bounds.center(), axis); });
    const int left = buildNode(first, middle - first);
    const int right = buildNode(middle, first + count - middle);
    nodes[index].left = left;
    nodes[index].right = right;
    return index;
}

SurfaceHit ModelCollider::intersect(Ray ray, double min, double max) const {
    if (analyticBox) return intersectBox(ray, transform, min, max);
    SurfaceHit closest{max};
    if (nodes.empty()) return closest;
    std::array<int, 64> pending{};
    std::size_t depth = 1;
    while (depth > 0) {
        const Node& node = nodes[pending[--depth]];
        if (!intersectsBounds(ray, node.bounds, min, closest.distance)) continue;
        if (node.left >= 0) {
            pending[depth++] = node.left;
            pending[depth++] = node.right;
            continue;
        }
        for (std::size_t i = node.first; i < node.first + node.count; ++i) {
            const Plane& plane = planes[i];
            const FaceGeometry& face = plane.face;
            const double denominator = dot(ray.direction, face.normal);
            if (std::abs(denominator) < 1e-10) continue;
            const Vec3 offset = face.origin - ray.origin;
            const double distance = dot(offset, face.normal) / denominator;
            if (distance < min || distance >= closest.distance) continue;
            const Vec3 local = ray.direction * distance - offset;
            const double s = dot(local, plane.alongU), t = dot(local, plane.alongV);
            constexpr double tolerance = 1e-8;
            if (s < -tolerance || s > 1 + tolerance || t < -tolerance || t > 1 + tolerance ||
                (face.triangle && s + t > 1 + tolerance)) continue;
            closest = {distance, face.normal, face.uvOrigin + face.uvU * s + face.uvV * t, plane.index, face.shade(s, t)};
        }
    }
    return closest;
}
