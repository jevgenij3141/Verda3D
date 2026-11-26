#include "fluid.h"

#include <algorithm>
#include <unordered_map>

namespace {
constexpr double fluidStep = 1.0 / 120;
constexpr double minRayDistance = 1e-5, surfaceIso = .52;
constexpr std::array particleLimits{600u, 1600u, 3500u};
constexpr std::array gridLimits{55'000u, 130'000u, 250'000u};
constexpr std::array gridSpacing{1.28, .93, .67};

struct Cell {
    int x, y, z;
    bool operator==(const Cell&) const = default;
};

struct CellHash {
    std::size_t operator()(Cell cell) const {
        return std::uint32_t(cell.x) * 0x8da6b343u ^ std::uint32_t(cell.y) * 0xd8163841u ^
               std::uint32_t(cell.z) * 0xcb1ab31fu;
    }
};
}

FluidSimulation::FluidSimulation(const std::vector<Model>& models, DynamicsSettings settings) : settings(settings) {
    for (std::size_t i = 0; i < models.size(); ++i) {
        const auto& model = models[i];
        if (!objectVisible(models, int(i)) || !isEmitter(model.kind)) continue;
        sources.push_back({i, model.transform, model.emitter});
        quality = std::max(quality, model.emitter.quality);
        maxRadius = std::max(maxRadius, model.emitter.radius);
    }
    if (!active()) return;
    for (std::size_t i = 0; i < models.size(); ++i) {
        const auto& model = models[i];
        if (isGeometry(model.kind) && model.collision.role != CollisionRole::Ignore && objectVisible(models, int(i)))
            obstacles.push_back({i, model.transform, ModelCollider(model)});
    }
}

double FluidSimulation::random() {
    randomState = randomState * 1664525u + 1013904223u;
    return double(randomState >> 8) / 16777216;
}

void FluidSimulation::advance(double seconds, const std::vector<Model>& models) {
    if (!active()) return;
    for (auto& source : sources) source.transform = models[source.model].transform;
    for (auto& obstacle : obstacles) {
        const auto& model = models[obstacle.model];
        const auto same = [](Vec3 a, Vec3 b) { const auto delta = a - b; return dot(delta, delta) < 1e-14; };
        if (!same(model.transform.position, obstacle.transform.position) ||
            model.transform.size != obstacle.transform.size ||
            !std::ranges::equal(model.transform.basis, obstacle.transform.basis, same)) {
            obstacle.collider = ModelCollider(model);
            obstacle.transform = model.transform;
        }
    }
    accumulator += seconds;
    while (accumulator + 1e-10 >= fluidStep) {
        step(fluidStep);
        accumulator = std::max(0.0, accumulator - fluidStep);
    }
}

void FluidSimulation::step(double seconds) {
    for (auto& p : particles) p.remaining -= seconds;
    std::erase_if(particles, [](const auto& p) { return p.remaining <= 0 || std::abs(p.position.y) > farPlane; });
    const auto limit = particleLimits[std::size_t(quality)];
    for (auto& source : sources) {
        auto& stream = source.settings;
        source.pending += seconds * stream.rate;
        const int count = int(source.pending);
        source.pending -= count;
        const auto& pose = source.transform;
        for (int i = 0; i < count && particles.size() < limit; ++i) {
            const double angle = random() * 2 * pi, r = std::sqrt(random()) * halfBox;
            const Vec3 offset = pose.basis[0] * (std::cos(angle) * r * pose.size[0]) +
                                pose.basis[2] * (std::sin(angle) * r * pose.size[2]);
            const Vec3 velocity = pose.basis[1] * -stream.speed + offset * .8;
            particles.push_back({pose.position + offset, velocity, stream.radius, stream.viscosity, stream.lifetime});
        }
    }
    if (particles.empty()) return;
    std::vector<Vec3> previous;
    previous.reserve(particles.size());
    const double cellSize = maxRadius * 3;
    const auto cellAt = [&](Vec3 p) { return Cell{int(std::floor(p.x / cellSize)), int(std::floor(p.y / cellSize)),
                                                 int(std::floor(p.z / cellSize))}; };
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> grid;
    for (std::size_t i = 0; i < particles.size(); ++i) {
        auto& p = particles[i];
        previous.push_back(p.position);
        p.velocity.y -= settings.gravity * seconds;
        p.position = p.position + p.velocity * seconds;
        grid[cellAt(p.position)].push_back(i);
    }
    for (const auto& [cell, entries] : grid) {
        for (const auto i : entries) {
            auto& a = particles[i];
            for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y) for (int z = -1; z <= 1; ++z) {
                const auto found = grid.find({cell.x + x, cell.y + y, cell.z + z});
                if (found == grid.end()) continue;
                for (const auto j : found->second) {
                    if (j <= i) continue;
                    auto& b = particles[j];
                    const Vec3 offset = b.position - a.position;
                    const double distance = std::sqrt(dot(offset, offset)), contact = a.radius + b.radius;
                    if (distance < minRayDistance || distance >= contact * 1.5) continue;
                    const auto axis = offset * (1 / distance);
                    const double pressure = (contact - distance) * (distance < contact ? .24 : .018);
                    a.position = a.position - axis * pressure;
                    b.position = b.position + axis * pressure;
                    const auto blend = (b.velocity - a.velocity) * ((a.viscosity + b.viscosity) * .05);
                    a.velocity = a.velocity + blend;
                    b.velocity = b.velocity - blend;
                }
            }
        }
    }
    for (std::size_t i = 0; i < particles.size(); ++i) {
        auto& p = particles[i];
        const Vec3 travel = p.position - previous[i];
        const double length = std::sqrt(dot(travel, travel));
        if (length > minRayDistance) {
            const Ray ray{previous[i], travel * (1 / length)};
            SurfaceHit closest{length + p.radius};
            for (const auto& obstacle : obstacles) {
                const auto hit = obstacle.collider.intersect(ray, minRayDistance, closest.distance);
                if (hit.distance < closest.distance) closest = hit;
            }
            if (closest.face != noFace) {
                const Vec3 normal = dot(closest.normal, ray.direction) < 0 ? closest.normal : closest.normal * -1;
                p.position = ray.origin + ray.direction * closest.distance + normal * p.radius;
                const double inward = std::min(0.0, dot(p.velocity, normal));
                p.velocity = (p.velocity - normal * (1.08 * inward)) * (1 - p.viscosity * .08);
            }
        }
        if (settings.ground && p.position.y < settings.groundHeight + p.radius) {
            p.position.y = settings.groundHeight + p.radius;
            if (p.velocity.y < 0) p.velocity.y *= -.08;
            p.velocity.x *= .96;
            p.velocity.z *= .96;
        }
    }
}

Mesh FluidSimulation::surface() const {
    Mesh mesh;
    if (particles.empty()) return mesh;
    Vec3 low = particles.front().position, high = low;
    for (const auto& p : particles) {
        low = {std::min(low.x, p.position.x), std::min(low.y, p.position.y), std::min(low.z, p.position.z)};
        high = {std::max(high.x, p.position.x), std::max(high.y, p.position.y), std::max(high.z, p.position.z)};
    }
    const double margin = maxRadius * 3;
    low = low - Vec3{margin, margin, margin};
    high = high + Vec3{margin, margin, margin};
    const auto extent = high - low;
    const auto q = std::size_t(quality);
    double cell = std::max(maxRadius * gridSpacing[q], std::max({extent.x, extent.y, extent.z}) / 160);
    int nx, ny, nz;
    while (true) {
        nx = int(std::ceil(extent.x / cell)) + 1;
        ny = int(std::ceil(extent.y / cell)) + 1;
        nz = int(std::ceil(extent.z / cell)) + 1;
        if (std::size_t(nx) * ny * nz <= gridLimits[q]) break;
        cell *= 1.16;
    }
    const auto index = [=](int x, int y, int z) { return (z * ny + y) * nx + x; };
    const auto position = [=](int id) {
        return low + Vec3{double(id % nx), double(id / nx % ny), double(id / (nx * ny))} * cell;
    };
    std::vector<float> values(std::size_t(nx) * ny * nz);
    for (const auto& p : particles) {
        const auto center = (p.position - low) * (1 / cell);
        const double radius = p.radius * 2.55;
        const int reach = int(std::ceil(radius / cell));
        const int cx = int(center.x), cy = int(center.y), cz = int(center.z);
        for (int z = std::max(0, cz - reach); z <= std::min(nz - 1, cz + reach); ++z)
            for (int y = std::max(0, cy - reach); y <= std::min(ny - 1, cy + reach); ++y)
                for (int x = std::max(0, cx - reach); x <= std::min(nx - 1, cx + reach); ++x) {
                    const int id = index(x, y, z);
                    const auto d = position(id) - p.position;
                    const double weight = 1 - dot(d, d) / (radius * radius);
                    if (weight > 0) values[id] += float(weight * weight * weight);
                }
    }
    const auto gradient = [&](int id) {
        const int x = id % nx, y = id / nx % ny, z = id / (nx * ny);
        return Vec3{values[index(std::max(0,x-1),y,z)]-values[index(std::min(nx-1,x+1),y,z)],
                    values[index(x,std::max(0,y-1),z)]-values[index(x,std::min(ny-1,y+1),z)],
                    values[index(x,y,std::max(0,z-1))]-values[index(x,y,std::min(nz-1,z+1))]};
    };
    std::unordered_map<std::uint64_t, std::size_t> edges;
    const auto edge = [&](int a, int b) {
        const auto key = (std::uint64_t(std::min(a,b)) << 32) | std::uint32_t(std::max(a,b));
        const auto entry = edges.try_emplace(key, mesh.vertices.size());
        if (entry.second) {
            const double t = (surfaceIso - values[a]) / (values[b] - values[a]);
            mesh.vertices.push_back(position(a) * (1 - t) + position(b) * t);
            mesh.normals.push_back(normalized(gradient(a) * (1-t) + gradient(b) * t));
        }
        return entry.first->second;
    };
    const auto face = [&](std::size_t a, std::size_t b, std::size_t c, Vec3 outside) {
        const auto normal = cross(mesh.vertices[b]-mesh.vertices[a], mesh.vertices[c]-mesh.vertices[a]);
        if (dot(normal, normal) <= 1e-20) return;
        if (dot(normal, outside - mesh.vertices[a]) < 0) std::swap(b, c);
        const auto uv = [&](std::size_t i) { return Vec2{mesh.vertices[i].x, mesh.vertices[i].z}; };
        mesh.faces.push_back({{a,b,c,0}, {uv(a),uv(b),uv(c),{}}, 3});
    };
    constexpr int tetrahedra[][4]{{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
    for (int z = 0; z+1 < nz; ++z) for (int y = 0; y+1 < ny; ++y) for (int x = 0; x+1 < nx; ++x) {
        const int corners[8]{index(x,y,z),index(x+1,y,z),index(x,y+1,z),index(x+1,y+1,z),
                             index(x,y,z+1),index(x+1,y,z+1),index(x,y+1,z+1),index(x+1,y+1,z+1)};
        for (const auto& tetra : tetrahedra) {
            int inside[4], outside[4], in = 0, out = 0;
            for (const auto corner : tetra) {
                const int id = corners[corner];
                if (values[id] >= surfaceIso) inside[in++] = id;
                else outside[out++] = id;
            }
            if (in == 1) face(edge(inside[0], outside[0]), edge(inside[0], outside[1]),
                               edge(inside[0], outside[2]), position(outside[0]));
            else if (out == 1) face(edge(outside[0], inside[0]), edge(outside[0], inside[1]),
                                    edge(outside[0], inside[2]), position(outside[0]));
            else if (in == 2) {
                const auto a=edge(inside[0],outside[0]), b=edge(inside[0],outside[1]);
                const auto c=edge(inside[1],outside[1]), d=edge(inside[1],outside[0]);
                face(a,b,c,position(outside[0]));
                face(a,c,d,position(outside[0]));
            }
            if (mesh.faces.size() >= maxMeshFaces || mesh.vertices.size() >= maxMeshVertices) return {};
        }
    }
    if (mesh.faces.empty()) return {};
    return mesh;
}

Mesh createWaterDish() {
    constexpr int sides = 40;
    constexpr Vec2 profile[]{{.18,.14},{.92,.14},{1.17,.20},{1.39,.32},{1.55,.51},{1.64,.56},
                             {1.69,.51},{1.55,.27},{1.22,.07},{.95,.035},{.18,.035}};
    Mesh mesh;
    for (const auto ring : profile)
        for (int i = 0; i < sides; ++i) {
            const double a = 2 * pi * i / sides;
            mesh.vertices.push_back({ring.x * std::cos(a), ring.y, ring.x * std::sin(a)});
        }
    for (std::size_t ring = 1; ring < std::size(profile); ++ring) {
        for (int side = 0; side < sides; ++side) {
            const auto a = (ring - 1) * sides + side, b = (ring - 1) * sides + (side + 1) % sides;
            mesh.faces.push_back({{a,b,b+sides,a+sides}, {{{0,0},{1,0},{1,1},{0,1}}}});
        }
    }
    const auto top = mesh.vertices.size();
    mesh.vertices.push_back({0, profile[0].y, 0});
    const auto bottom = mesh.vertices.size();
    mesh.vertices.push_back({0, profile[std::size(profile)-1].y, 0});
    const auto last = (std::size(profile)-1)*sides;
    for(int i=0;i<sides;++i) {
        const auto next = std::size_t((i+1)%sides);
        mesh.faces.push_back({{top,next,std::size_t(i),0}, {{{.5,.5},{1,0},{0,0},{}}}, 3});
        mesh.faces.push_back({{bottom,last+i,last+next,0}, {{{.5,.5},{0,0},{1,0},{}}}, 3});
    }
    return mesh;
}
