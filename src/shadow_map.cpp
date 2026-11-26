#include "shadow_map.h"

namespace {
constexpr double clippingDepth = .005;

double edge(Vec2 a, Vec2 b, Vec2 p) {
    const Vec2 u = b - a, v = p - a;
    return u.x * v.y - u.y * v.x;
}

void rasterize(ShadowProjection& map, Vec3 a, Vec3 b, Vec3 c) {
    const double size = map.size;
    const auto project = [&](Vec3 p) {
        const double extent = map.extent * (map.orthographic ? 1 : p.z);
        return Vec2{size * (.5 + .5 * p.x / extent), size * (.5 + .5 * p.y / extent)};
    };
    const Vec2 p = project(a), q = project(b), r = project(c);
    const double area = edge(p, q, r);
    if (std::abs(area) < 1e-9) return;
    const int x0 = int(std::clamp(std::floor(std::min({p.x, q.x, r.x})), 0.0, size));
    const int y0 = int(std::clamp(std::floor(std::min({p.y, q.y, r.y})), 0.0, size));
    const int x1 = int(std::clamp(std::ceil(std::max({p.x, q.x, r.x})), 0.0, size));
    const int y1 = int(std::clamp(std::ceil(std::max({p.y, q.y, r.y})), 0.0, size));
    const double za = map.orthographic ? a.z : 1 / a.z;
    const double zb = map.orthographic ? b.z : 1 / b.z;
    const double zc = map.orthographic ? c.z : 1 / c.z;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Vec2 sample{x + .5, y + .5};
            const double w0 = edge(q, r, sample) / area, w1 = edge(r, p, sample) / area;
            const double w2 = 1 - w0 - w1;
            if (std::min({w0, w1, w2}) < -1e-9) continue;
            const double z = za * w0 + zb * w1 + zc * w2;
            float& pixel = map.depth[y * map.size + x];
            pixel = std::min(pixel, float(map.orthographic ? z : 1 / z));
        }
    }
}

void drawFace(ShadowProjection& map, const FaceGeometry& face) {
    const auto corners = face.corners();
    std::array<Vec3, 4> projected{};
    for (std::size_t i = 0; i < face.count(); ++i) {
        const Vec3 p = corners[i] - map.origin;
        projected[i] = {dot(p, map.right), dot(p, map.up), dot(p, map.forward)};
    }
    std::array<Vec3, 6> clipped{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < face.count(); ++i) {
        const Vec3 a = projected[i], b = projected[(i + 1) % face.count()];
        const bool insideA = a.z >= clippingDepth, insideB = b.z >= clippingDepth;
        if (insideA) clipped[count++] = a;
        if (insideA != insideB) {
            clipped[count++] = a + (b - a) * ((clippingDepth - a.z) / (b.z - a.z));
        }
    }
    for (std::size_t i = 1; i + 1 < count; ++i) rasterize(map, clipped[0], clipped[i], clipped[i + 1]);
}
} // namespace

void ShadowMap::build(const Model& model, const std::vector<const Model*>& geometry) {
    if (!model.light.enabled || !model.light.shadows || geometry.empty()) return;
    const auto addProjection = [&](Vec3 origin, Vec3 forward, double extent, bool orthographic) {
        const Vec3 vertical = std::abs(forward.y) > .96 ? axes[2] : axes[1];
        const Vec3 right = normalized(cross(forward, vertical));
        projections.push_back({origin, forward, right, cross(right, forward),
                               extent, orthographic, model.light.shadowSize, {}});
    };
    const Vec3 position = model.transform.position;
    if (model.kind == ObjectKind::Omni) {
        for (Vec3 axis : axes) {
            addProjection(position, axis, 1, false);
            addProjection(position, axis * -1, 1, false);
        }
    } else if (model.kind == ObjectKind::Spot) {
        addProjection(position, model.transform.basis[2] * -1, std::tan(model.light.cone * pi / 360), false);
    } else {
        const Vec3 center = modelBounds(*geometry.front()).center();
        double extent = 2;
        for (const Model* object : geometry) {
            const Bounds bounds = modelBounds(*object);
            const Vec3 delta = bounds.center() - center;
            const Vec3 size = bounds.max - bounds.min;
            extent = std::max(extent, std::sqrt(dot(delta, delta)) +
                .5 * std::sqrt(dot(size, size)) + .1);
        }
        const Vec3 sky = model.transform.basis[1];
        addProjection(center + sky * extent, sky * -1, extent, true);
    }
    for (auto& projection : projections) {
        projection.depth.assign(std::size_t(projection.size) * projection.size, 1e10f);
        for (const Model* object : geometry) {
            for (std::size_t face = 0; face < modelMesh(*object).faces.size(); ++face) {
                drawFace(projection, faceGeometry(*object, face));
            }
        }
    }
}

double ShadowMap::visibility(Vec3 point, Vec3 normal, double transparency) const {
    const ShadowProjection* best = nullptr;
    double bestDepth = 0;
    for (const auto& projection : projections) {
        const double depth = dot(point - projection.origin, projection.forward);
        if (depth <= bestDepth) continue;
        bestDepth = depth;
        best = &projection;
    }
    if (!best) return 1;
    const auto& projection = *best;
    const double width = projection.extent * (projection.orthographic ? 1 : bestDepth);
    const double bias = std::max(.012, width / projection.size);
    const Vec3 offset = point + normal * bias - projection.origin;
    const double depth = dot(offset, projection.forward);
    if (depth <= 0) return 1;
    const double extent = projection.extent * (projection.orthographic ? 1 : depth);
    const double u = .5 + .5 * dot(offset, projection.right) / extent;
    const double v = .5 + .5 * dot(offset, projection.up) / extent;
    if (u < 0 || u > 1 || v < 0 || v > 1) return 1;
    const int size = projection.size;
    const int x = static_cast<int>(u * size), y = static_cast<int>(v * size);
    double visible = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int col = std::clamp(x + dx, 0, size - 1);
            const int row = std::clamp(y + dy, 0, size - 1);
            visible += depth - bias <= projection.depth[row * size + col] ? 1 : transparency;
        }
    }
    return visible / 9;
}
