#include "mesh_cut.h"
#include "scene_mesh.h"

#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <unordered_map>

namespace {
constexpr double epsilon = 2e-7;
constexpr std::size_t maxInputFaces = 2600, maxPolygons = 16000;
struct ComplexityLimit {};

struct Vertex {
    Vec3 point;
    Vec2 uv;
    Vertex toward(Vertex other, double t) const {
        return {point + (other.point - point) * t, uv + (other.uv - uv) * t};
    }
};

struct Plane { Vec3 normal; double height; };

struct Polygon {
    std::vector<Vertex> vertices;
    Plane plane;
    void invert() {
        std::reverse(vertices.begin(), vertices.end());
        plane.normal = plane.normal * -1;
        plane.height *= -1;
    }
};
using Polygons = std::vector<Polygon>;

void append(Polygons& destination, Polygons&& source) {
    if (destination.size() + source.size() > maxPolygons) throw ComplexityLimit{};
    destination.insert(destination.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
}

int side(const Plane& plane, Vec3 p) {
    const double distance = dot(plane.normal, p) - plane.height;
    return distance > epsilon ? 1 : distance < -epsilon ? 2 : 0;
}

void split(const Plane& plane, Polygon polygon, Polygons& same, Polygons& opposite, Polygons& front, Polygons& back) {
    int sides = 0;
    std::vector<int> classes;
    classes.reserve(polygon.vertices.size());
    for (const Vertex& vertex : polygon.vertices) {
        classes.push_back(side(plane, vertex.point));
        sides |= classes.back();
    }
    if (sides == 0) {
        (dot(plane.normal, polygon.plane.normal) > 0 ? same : opposite).push_back(std::move(polygon));
    } else if (sides != 3) {
        (sides == 1 ? front : back).push_back(std::move(polygon));
    } else {
        Polygon positive{{}, polygon.plane}, negative{{}, polygon.plane};
        for (std::size_t i = 0; i < classes.size(); ++i) {
            const auto j = (i + 1) % classes.size();
            const auto& a = polygon.vertices[i];
            const auto& b = polygon.vertices[j];
            if (classes[i] != 2) positive.vertices.push_back(a);
            if (classes[i] != 1) negative.vertices.push_back(a);
            if ((classes[i] | classes[j]) != 3) continue;
            const double t = (plane.height - dot(plane.normal, a.point)) / dot(plane.normal, b.point - a.point);
            const Vertex intersection = a.toward(b, std::clamp(t, 0.0, 1.0));
            positive.vertices.push_back(intersection);
            negative.vertices.push_back(intersection);
        }
        if (positive.vertices.size() >= 3) front.push_back(std::move(positive));
        if (negative.vertices.size() >= 3) back.push_back(std::move(negative));
    }
}

Plane choosePlane(const Polygons& polygons) {
    const std::size_t trials = std::min<std::size_t>(8, polygons.size());
    Plane best = polygons.front().plane;
    std::size_t score = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < trials; ++i) {
        const Plane candidate = polygons[i * polygons.size() / trials].plane;
        std::array<std::size_t, 4> counts{};
        for (const Polygon& polygon : polygons) {
            int sides = 0;
            for (const Vertex& vertex : polygon.vertices) sides |= side(candidate, vertex.point);
            ++counts[sides];
        }
        const auto difference = counts[1] > counts[2] ? counts[1] - counts[2] : counts[2] - counts[1];
        const auto cost = counts[3] * 8 + difference;
        if (cost < score) { score = cost; best = candidate; }
    }
    return best;
}

struct Bsp {
    std::optional<Plane> plane;
    Polygons faces;
    std::unique_ptr<Bsp> front, back;

    void build(Polygons polygons, unsigned depth = 0) {
        if (polygons.empty()) return;
        if (depth >= 600 || polygons.size() > maxPolygons) throw ComplexityLimit{};
        if (!plane) plane = choosePlane(polygons);
        Polygons outside, inside;
        for (Polygon& polygon : polygons) split(*plane, std::move(polygon), faces, faces, outside, inside);
        if (!outside.empty()) {
            if (!front) front = std::make_unique<Bsp>();
            front->build(std::move(outside), depth + 1);
        }
        if (!inside.empty()) {
            if (!back) back = std::make_unique<Bsp>();
            back->build(std::move(inside), depth + 1);
        }
    }

    void invert() {
        for (Polygon& face : faces) face.invert();
        if (plane) { plane->normal = plane->normal * -1; plane->height *= -1; }
        if (front) front->invert();
        if (back) back->invert();
        std::swap(front, back);
    }

    Polygons clip(Polygons polygons) const {
        if (!plane || polygons.empty()) return polygons;
        Polygons outside, inside;
        for (Polygon& polygon : polygons) split(*plane, std::move(polygon), outside, inside, outside, inside);
        if (front) outside = front->clip(std::move(outside));
        if (back) append(outside, back->clip(std::move(inside)));
        return outside;
    }

    void clipTo(const Bsp& other) {
        faces = other.clip(std::move(faces));
        if (front) front->clipTo(other);
        if (back) back->clipTo(other);
    }

    Polygons takeFaces() {
        if (front) append(faces, front->takeFaces());
        if (back) append(faces, back->takeFaces());
        return std::move(faces);
    }
};

struct Cell {
    std::int64_t x, y, z;
    bool operator==(const Cell& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
};
struct CellHash {
    std::size_t operator()(Cell p) const {
        return std::uint64_t(p.x) * 0x9e3779b185ebca87ULL ^ std::uint64_t(p.y) * 0xc2b2ae3d27d4eb4fULL ^
               std::uint64_t(p.z) * 0x165667b19e3779f9ULL;
    }
};

struct Weld {
    std::vector<Vec3> points;
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> cells;

    std::size_t vertex(Vec3 point) {
        constexpr double size = epsilon * 2;
        const Cell cell{std::int64_t(std::floor(point.x / size)), std::int64_t(std::floor(point.y / size)),
                        std::int64_t(std::floor(point.z / size))};
        for (int z = -1; z <= 1; ++z) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            const auto bin = cells.find({cell.x + x, cell.y + y, cell.z + z});
            if (bin == cells.end()) continue;
            for (auto index : bin->second) {
                const Vec3 delta = points[index] - point;
                if (dot(delta, delta) <= size * size) return index;
            }
        }
        const std::size_t index = points.size();
        if (index >= 65000) throw ComplexityLimit{};
        points.push_back(point);
        cells[cell].push_back(index);
        return index;
    }
};

struct EdgeUse {
    struct Use { unsigned count = 0; int orientation = 0; };
    std::unordered_map<std::uint64_t, Use> edges;

    void add(std::size_t a, std::size_t b) {
        auto& use = edges[std::uint64_t(std::min(a, b)) << 32 | std::max(a, b)];
        ++use.count;
        use.orientation += a < b ? 1 : -1;
    }

    bool closed() const {
        return !edges.empty() && std::all_of(edges.begin(), edges.end(), [](const auto& entry) {
            return entry.second.count == 2 && entry.second.orientation == 0;
        });
    }
};

bool closed(const Polygons& polygons) {
    EdgeUse edges;
    Weld weld;
    for (const auto& polygon : polygons) {
        auto first = weld.vertex(polygon.vertices.front().point);
        auto a = first;
        for (std::size_t i = 1; i <= polygon.vertices.size(); ++i) {
            const auto b = i == polygon.vertices.size() ? first : weld.vertex(polygon.vertices[i].point);
            if (a == b) return false;
            edges.add(a, b);
            a = b;
        }
    }
    return edges.closed();
}

double volume(const Polygons& polygons) {
    double sum = 0;
    for (const Polygon& polygon : polygons) {
        const auto origin = polygon.vertices.front().point;
        for (std::size_t i = 1; i + 1 < polygon.vertices.size(); ++i) {
            sum += dot(origin, cross(polygon.vertices[i].point, polygon.vertices[i + 1].point));
        }
    }
    return sum / 6;
}

Polygons modelPolygons(const Model& target, const Model& source, Vec3 center, double scale) {
    Polygons polygons;
    const Mesh& mesh = modelMesh(source);
    std::vector<Vec3> points;
    points.reserve(mesh.vertices.size());
    const Transform& transform = source.transform;
    const Vec3 offset = transform.position - target.transform.position;
    for (Vec3 point : mesh.vertices) {
        const Vec3 local = &target == &source ? point : localVector(target.transform, offset +
            transform.basis[0] * (point.x * transform.size[0]) +
            transform.basis[1] * (point.y * transform.size[1]) +
            transform.basis[2] * (point.z * transform.size[2]));
        points.push_back((local - center) * (1 / scale));
    }
    const auto add = [&](std::vector<Vertex> vertices) {
        const Vec3 a = vertices[0].point;
        const Vec3 normal = normalized(cross(vertices[1].point - a, vertices[2].point - a));
        if (dot(normal, normal) >= .5) polygons.push_back({std::move(vertices), {normal, dot(normal, a)}});
    };
    for (const auto& face : mesh.faces) {
        std::vector<Vertex> vertices;
        for (std::size_t i = 0; i < face.count; ++i) vertices.push_back({points[face.vertices[i]], face.uv[i]});
        if (face.count == 4) {
            const Vec3 origin = vertices[0].point;
            const Vec3 normal = normalized(cross(vertices[1].point - origin, vertices[2].point - origin));
            if (std::abs(dot(normal, vertices[3].point - origin)) > epsilon) {
                add({vertices[0], vertices[1], vertices[2]});
                add({vertices[0], vertices[2], vertices[3]});
                continue;
            }
        }
        add(std::move(vertices));
    }
    return polygons;
}

Mesh stitch(const Polygons& polygons, Vec3 center, double scale) {
    Weld weld;
    std::vector<std::vector<std::size_t>> outlines;
    for (const Polygon& polygon : polygons) {
        auto& indices = outlines.emplace_back();
        for (const Vertex& vertex : polygon.vertices) indices.push_back(weld.vertex(vertex.point));
    }
    const std::size_t boundaryCount = weld.points.size();
    std::array<std::vector<std::size_t>, 3> sorted;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        auto& order = sorted[axis];
        order.resize(boundaryCount);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](auto a, auto b) {
            return dot(weld.points[a], axes[axis]) < dot(weld.points[b], axes[axis]);
        });
    }
    Mesh mesh;
    EdgeUse edges;
    const double areaScale = scale * scale;
    for (std::size_t f = 0; f < polygons.size(); ++f) {
        const Polygon& polygon = polygons[f];
        const auto& indices = outlines[f];
        Vec3 middle{};
        Vec2 uvMiddle{};
        for (std::size_t i = 0; i < indices.size(); ++i) {
            middle = middle + weld.points[indices[i]] * (1.0 / indices.size());
            uvMiddle = uvMiddle + polygon.vertices[i].uv * (1.0 / indices.size());
        }
        const auto mid = weld.vertex(middle);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const auto j = (i + 1) % indices.size();
            const auto a = indices[i], b = indices[j];
            const Vec3 start = weld.points[a], direction = weld.points[b] - start;
            const double length = dot(direction, direction);
            if (length < epsilon * epsilon) continue;
            const Vec3 d{std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)};
            const int axis = d.x > d.y && d.x > d.z ? 0 : d.y > d.z ? 1 : 2;
            const auto& sortedVertices = sorted[axis];
            const auto projection = [&](auto vertex) { return dot(weld.points[vertex], axes[axis]); };
            const double lo = std::min(projection(a), projection(b)) - epsilon * 2;
            const double hi = std::max(projection(a), projection(b)) + epsilon * 2;
            std::vector<std::pair<double, std::size_t>> points{{0, a}, {1, b}};
            for (auto candidate = std::lower_bound(sortedVertices.begin(), sortedVertices.end(), lo,
                     [&](auto index, double edge) { return projection(index) < edge; });
                 candidate != sortedVertices.end() && projection(*candidate) <= hi; ++candidate) {
                if (*candidate == a || *candidate == b) continue;
                const Vec3 offset = weld.points[*candidate] - start;
                const double t = dot(offset, direction) / length;
                if (t <= epsilon || t >= 1 - epsilon) continue;
                const Vec3 delta = offset - direction * t;
                if (dot(delta, delta) <= epsilon * epsilon * 4) points.push_back({t, *candidate});
            }
            std::sort(points.begin(), points.end());
            const Vec2 uvA = polygon.vertices[i].uv, change = polygon.vertices[j].uv - uvA;
            for (std::size_t k = 1; k < points.size(); ++k) {
                const auto [s, x] = points[k - 1];
                const auto [t, y] = points[k];
                const Vec3 n = cross(weld.points[x] - weld.points[mid], weld.points[y] - weld.points[mid]);
                if (dot(n, n) * areaScale * areaScale <= 1e-20) continue;
                mesh.faces.push_back({{mid, x, y, y}, {uvMiddle, uvA + change * s, uvA + change * t, {}}, 3});
                edges.add(mid, x);
                edges.add(x, y);
                edges.add(y, mid);
                if (mesh.faces.size() > 45000) throw ComplexityLimit{};
            }
        }
    }
    if (!edges.closed()) return {};
    mesh.vertices = std::move(weld.points);
    for (Vec3& p : mesh.vertices) p = p * scale + center;
    return mesh;
}
} // namespace

CutResult subtractMesh(const Model& target, const Model& cutter, Mesh& result) {
    if (!isGeometry(target.kind) || !isGeometry(cutter.kind) ||
        modelMesh(target).vertices.empty() || modelMesh(cutter).vertices.empty() ||
        modelMesh(target).faces.size() < 4 || modelMesh(cutter).faces.size() < 4) return CutResult::OpenMesh;
    const Bounds a = modelBounds(target), b = modelBounds(cutter);
    if (std::min(a.max.x, b.max.x) <= std::max(a.min.x, b.min.x) ||
        std::min(a.max.y, b.max.y) <= std::max(a.min.y, b.min.y) ||
        std::min(a.max.z, b.max.z) <= std::max(a.min.z, b.min.z)) return CutResult::NoOverlap;
    if (modelMesh(target).faces.size() + modelMesh(cutter).faces.size() > maxInputFaces) return CutResult::TooComplex;
    try {
        const auto& vertices = modelMesh(target).vertices;
        Bounds local{vertices.front(), vertices.front()};
        for (Vec3 p : vertices) {
            local.min = {std::min(local.min.x, p.x), std::min(local.min.y, p.y), std::min(local.min.z, p.z)};
            local.max = {std::max(local.max.x, p.x), std::max(local.max.y, p.y), std::max(local.max.z, p.z)};
        }
        const Vec3 center = local.center(), size = local.max - local.min;
        const double scale = std::max({size.x, size.y, size.z, epsilon});
        auto outside = modelPolygons(target, target, center, scale);
        auto tool = modelPolygons(target, cutter, center, scale);
        if (!closed(outside) || !closed(tool)) return CutResult::OpenMesh;
        const double original = volume(outside), toolVolume = volume(tool);
        if (std::abs(original) < 1e-14 || std::abs(toolVolume) < 1e-14) return CutResult::OpenMesh;
        if (original < 0) for (auto& face : outside) face.invert();
        if (toolVolume < 0) for (auto& face : tool) face.invert();
        Bsp solid, mask;
        solid.build(std::move(outside));
        mask.build(std::move(tool));
        solid.invert();
        solid.clipTo(mask);
        mask.clipTo(solid);
        mask.invert();
        mask.clipTo(solid);
        mask.invert();
        solid.build(mask.takeFaces());
        solid.invert();
        const auto faces = solid.takeFaces();
        const double remaining = volume(faces);
        const double volumeError = std::max(std::abs(original) * 1e-7, 1e-14);
        if (remaining <= volumeError) return CutResult::Empty;
        if (std::abs(original) - remaining <= volumeError) return CutResult::NoOverlap;
        Mesh cut = stitch(faces, center, scale);
        if (cut.faces.empty()) return CutResult::Unstable;
        result = std::move(cut);
        return CutResult::Success;
    } catch (const ComplexityLimit&) {
        return CutResult::TooComplex;
    }
}
