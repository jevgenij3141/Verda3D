#include "texture_mesh_editor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace {
constexpr double minNormalAlignment = .8660254037844386; // Cosine of the 30-degree chart angle.
constexpr double overlapTolerance = 1e-10;
constexpr double minChartExtent = 1e-6;
constexpr double maxGutter = .006;
constexpr double gutterAreaFactor = .2;
constexpr int packingIterations = 24;
constexpr int minGridResolution = 8, maxGridResolution = 256;
constexpr int maxIndexedCells = 64;
constexpr int gridStride = 2 * maxGridResolution + 1;
constexpr std::size_t unassigned = std::numeric_limits<std::size_t>::max();

struct Bounds2 {
    Vec2 low{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec2 high{-low.x, -low.y};

    void include(Vec2 p) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y)};
    }

    Vec2 extent() const {
        return {std::max(minChartExtent, high.x - low.x), std::max(minChartExtent, high.y - low.y)};
    }

    bool overlaps(const Bounds2& other) const {
        return std::min(high.x, other.high.x) - std::max(low.x, other.low.x) > overlapTolerance &&
               std::min(high.y, other.high.y) - std::max(low.y, other.low.y) > overlapTolerance;
    }
};

struct Chart {
    std::vector<std::size_t> faces;
    Bounds2 bounds;
    Vec2 placement{};
};

struct FaceNormals {
    Vec3 first{}, second{};
};

std::vector<Vec3> scaledPositions(const Mesh& mesh, const Transform& transform) {
    // Normalize the coordinate range before applying object scale and taking cross products.
    double limit = 0;
    for (const auto p : mesh.vertices)
        limit = std::max({limit, std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    if (limit == 0) return mesh.vertices;
    std::vector<Vec3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto p : mesh.vertices) {
        positions.push_back({(p.x / limit) * transform.size[0],
                             (p.y / limit) * transform.size[1],
                             (p.z / limit) * transform.size[2]});
    }
    Vec3 low = positions.front(), high = low;
    for (const auto p : positions) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const Vec3 span = high - low;
    const double extent = std::max({span.x, span.y, span.z});
    if (extent == 0) return positions;
    const Vec3 center = low + span * .5;
    for (auto& p : positions) {
        p = p - center;
        p = {p.x / extent, p.y / extent, p.z / extent};
    }
    return positions;
}

std::vector<std::array<std::size_t, 4>> faceNeighbors(const Mesh& mesh) {
    struct Edge { std::size_t low, high, face, side; bool forward; };
    std::vector<Edge> edges;
    edges.reserve(mesh.faces.size() * 4);
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const auto& face = mesh.faces[i];
        for (std::size_t side = 0; side < face.count; ++side) {
            const auto a = face.vertices[side], b = face.vertices[(side + 1) % face.count];
            edges.push_back({std::min(a, b), std::max(a, b), i, side, a < b});
        }
    }
    const auto key = [](const Edge& edge) { return std::pair{edge.low, edge.high}; };
    std::ranges::sort(edges, {}, key);
    std::vector<std::array<std::size_t, 4>> neighbors(mesh.faces.size(), {unassigned, unassigned, unassigned, unassigned});
    for (std::size_t first = 0; first < edges.size();) {
        std::size_t end = first + 1;
        while (end < edges.size() && key(edges[first]) == key(edges[end])) ++end;
        if (end - first == 2) {
            const auto& a = edges[first];
            const auto& b = edges[first + 1];
            if (a.face != b.face && a.forward != b.forward) {
                neighbors[a.face][a.side] = b.face;
                neighbors[b.face][b.side] = a.face;
            }
        }
        first = end;
    }
    return neighbors;
}

double cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

bool trianglesOverlap(const std::array<Vec2, 3>& a, const std::array<Vec2, 3>& b) {
    const auto separates = [](const auto& triangle, const auto& other) {
        const double area = cross2(triangle[1] - triangle[0], triangle[2] - triangle[0]);
        if (std::abs(area) <= overlapTolerance) return true;
        const double sign = std::copysign(1.0, area);
        for (std::size_t i = 0; i < triangle.size(); ++i) {
            const auto point = triangle[i], direction = triangle[(i + 1) % triangle.size()] - point;
            if (std::ranges::all_of(other, [&](Vec2 p) {
                    return sign * cross2(direction, p - point) <= overlapTolerance;
                })) return true;
        }
        return false;
    };
    return !separates(a, b) && !separates(b, a);
}

bool facesOverlap(const Mesh& mesh, const TextureUvs& uvs, std::size_t first, std::size_t second) {
    const auto triangle = [&](std::size_t face, std::size_t part) {
        return std::array{uvs[face][0], uvs[face][1 + part], uvs[face][2 + part]};
    };
    for (std::size_t a = 0; a < mesh.faces[first].count - 2; ++a)
        for (std::size_t b = 0; b < mesh.faces[second].count - 2; ++b)
            if (trianglesOverlap(triangle(first, a), triangle(second, b))) return true;
    return false;
}

double packCharts(std::vector<Chart>& charts) {
    const double gutter = std::min(maxGutter, gutterAreaFactor / std::sqrt(double(charts.size())));
    std::vector<std::size_t> order(charts.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, [&](auto a, auto b) {
        return charts[a].bounds.extent().y > charts[b].bounds.extent().y;
    });
    double longest = 0;
    for (const auto& chart : charts) {
        const auto extent = chart.bounds.extent();
        longest = std::max({longest, extent.x, extent.y});
    }
    const auto pack = [&](double scale, bool save) {
        double x = gutter, y = gutter, rowHeight = 0;
        for (const auto index : order) {
            auto& chart = charts[index];
            const auto size = chart.bounds.extent() * scale;
            if (x + size.x + gutter > 1 && rowHeight > 0) {
                y += rowHeight + gutter;
                x = gutter;
                rowHeight = 0;
            }
            if (x + size.x + gutter > 1 || y + size.y + gutter > 1) return false;
            if (save) chart.placement = {x, y};
            x += size.x + gutter;
            rowHeight = std::max(rowHeight, size.y);
        }
        return true;
    };
    double low = 0, high = (1 - gutter * 2) / longest;
    for (int i = 0; i < packingIterations; ++i) {
        const double scale = (low + high) * .5;
        if (pack(scale, false)) low = scale;
        else high = scale;
    }
    pack(low, true);
    return low;
}
} // namespace

TextureUvs smartUnfoldTextureUvs(const Mesh& mesh, const Transform& transform) {
    TextureUvs uvs(mesh.faces.size());
    if (mesh.faces.empty()) return uvs;
    const auto positions = scaledPositions(mesh, transform);
    const auto neighbors = faceNeighbors(mesh);
    std::vector<FaceNormals> normals;
    normals.reserve(mesh.faces.size());
    for (const auto& face : mesh.faces) {
        const auto p = positions[face.vertices[0]];
        const auto diagonal = positions[face.vertices[2]] - p;
        const auto first = normalized(cross(positions[face.vertices[1]] - p, diagonal));
        const auto second = face.count == 4 ? normalized(cross(diagonal, positions[face.vertices[3]] - p)) : first;
        normals.push_back({first, second});
    }

    const int resolution = std::clamp(int(std::ceil(std::sqrt(double(mesh.faces.size())))),
                                      minGridResolution, maxGridResolution);
    const auto gridCoordinate = [&](double p) { return int(std::floor((p + 1) * resolution)); };
    const auto key = [](int x, int y) { return y * gridStride + x; };
    std::vector<Chart> charts;
    std::vector<Bounds2> faceBounds(mesh.faces.size());
    std::vector<std::size_t> queued(mesh.faces.size(), unassigned), tried(mesh.faces.size(), unassigned);
    std::vector<bool> assigned(mesh.faces.size(), false);
    std::size_t pass = 0;
    for (std::size_t seed = 0; seed < mesh.faces.size(); ++seed) {
        if (assigned[seed]) continue;
        const auto chartIndex = charts.size();
        auto& chart = charts.emplace_back();
        Vec3 normal = normalized(normals[seed].first + normals[seed].second);
        if (dot(normal, normal) == 0) normal = normals[seed].first;
        if (dot(normal, normal) == 0) normal = {0, 0, 1};
        const auto reference = std::abs(normal.y) <= std::abs(normal.z) ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
        const auto right = normalized(cross(reference, normal));
        const auto up = cross(normal, right);
        using FaceGrid = std::unordered_map<int, std::vector<std::size_t>>;
        FaceGrid cells;
        // Long faces are indexed along their shorter axis to limit cell duplication.
        std::array<FaceGrid, 2> strips;
        std::vector<std::size_t> pending{seed};
        queued[seed] = chartIndex;
        for (std::size_t next = 0; next < pending.size(); ++next) {
            const auto faceId = pending[next];
            const auto& face = mesh.faces[faceId];
            Bounds2 bounds;
            for (std::size_t i = 0; i < face.count; ++i) {
                const auto p = positions[face.vertices[i]];
                uvs[faceId][i] = {dot(p, right), dot(p, up)};
                bounds.include(uvs[faceId][i]);
            }
            const int left = gridCoordinate(bounds.low.x), rightCell = gridCoordinate(bounds.high.x);
            const int bottom = gridCoordinate(bounds.low.y), top = gridCoordinate(bounds.high.y);
            const bool broad = (rightCell - left + 1) * (top - bottom + 1) > maxIndexedCells;
            const std::array firstCell{left, bottom}, lastCell{rightCell, top};
            ++pass;
            const auto overlaps = [&](std::size_t other) {
                if (tried[other] == pass) return false;
                tried[other] = pass;
                return bounds.overlaps(faceBounds[other]) && facesOverlap(mesh, uvs, faceId, other);
            };
            const auto occupied = [&](const FaceGrid& grid, int cell) {
                const auto found = grid.find(cell);
                return found != grid.end() && std::ranges::any_of(found->second, overlaps);
            };
            bool conflict = false;
            for (std::size_t axis = 0; axis < strips.size() && !conflict; ++axis) {
                if (strips[axis].empty()) continue;
                for (int cell = firstCell[axis]; cell <= lastCell[axis] && !conflict; ++cell)
                    conflict = occupied(strips[axis], cell);
            }
            if (!cells.empty()) {
                for (int y = bottom; y <= top && !conflict; ++y) {
                    for (int x = left; x <= rightCell && !conflict; ++x)
                        conflict = occupied(cells, key(x, y));
                }
            }
            if (conflict) continue;
            assigned[faceId] = true;
            faceBounds[faceId] = bounds;
            chart.faces.push_back(faceId);
            chart.bounds.include(bounds.low);
            chart.bounds.include(bounds.high);
            if (broad) {
                const std::size_t axis = rightCell - left <= top - bottom ? 0 : 1;
                for (int cell = firstCell[axis]; cell <= lastCell[axis]; ++cell)
                    strips[axis][cell].push_back(faceId);
            } else {
                for (int y = bottom; y <= top; ++y)
                    for (int x = left; x <= rightCell; ++x) cells[key(x, y)].push_back(faceId);
            }
            for (const auto neighbor : neighbors[faceId]) {
                if (neighbor == unassigned || assigned[neighbor] || queued[neighbor] == chartIndex) continue;
                const auto [a, b] = normals[neighbor];
                if (dot(normal, a) < minNormalAlignment || dot(normal, b) < minNormalAlignment) continue;
                pending.push_back(neighbor);
                queued[neighbor] = chartIndex;
            }
        }
    }
    const double scale = packCharts(charts);
    for (const auto& chart : charts) {
        for (const auto id : chart.faces) {
            for (std::size_t i = 0; i < mesh.faces[id].count; ++i)
                uvs[id][i] = chart.placement + (uvs[id][i] - chart.bounds.low) * scale;
        }
    }
    return uvs;
}
