#include "global_renderer.h"
#include "material_library.h"
#include "scene_mesh.h"

#include <cstdint>
#include <limits>
#include <thread>
#include <unordered_map>

namespace {
constexpr double epsilon = .0005;

struct Random {
    std::uint32_t state;
    double next() {
        state = state * 747796405u + 2891336453u;
        const std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return double((word >> 22u) ^ word) / 4294967296.0;
    }
};

Vec3 sampleLobe(Vec3 normal, double cosine, Random& random) {
    const Vec3 a = normalized(cross(std::abs(normal.y) > .96 ? axes[0] : axes[1], normal));
    const Vec3 b = cross(normal, a);
    const double angle = 2 * pi * random.next();
    const double sine = std::sqrt(std::max(0.0, 1 - cosine * cosine));
    return normal * cosine + (a * std::cos(angle) + b * std::sin(angle)) * sine;
}

struct Hit {
    const Model* box = nullptr;
    SurfaceHit surface{};
};

struct TraceGeometry {
    const TraceScene& scene;
    std::vector<ModelCollider> objects;
};

Hit trace(const TraceGeometry& geometry, Ray ray, bool primary, double max = farPlane) {
    Hit closest{nullptr, {max}};
    for (std::size_t i = 0; i < geometry.objects.size(); ++i) {
        const SurfaceHit hit = geometry.objects[i].intersect(ray, primary ? nearPlane : epsilon,
                                                             closest.surface.distance);
        if (hit.distance >= closest.surface.distance) continue;
        closest = {&geometry.scene.boxes[i], hit};
    }
    return closest;
}

Vec3 linear(Vec3 color) {
    return {std::pow(color.x, 2.2), std::pow(color.y, 2.2), std::pow(color.z, 2.2)};
}

Vec3 baseColor(const MaterialEdit& material, Vec2 uv) {
    Vec3 color = rgb(material.surface.tint);
    const auto* pixel = texturePixel(material, uv);
    if (!pixel) return linear(color);
    return linear(multiply(color, {pixel[2] / 255.0, pixel[1] / 255.0, pixel[0] / 255.0}));
}

const MaterialEdit& materialFor(const TraceScene& scene, const Model& model) {
    static const MaterialEdit defaultMaterial{Surface{{.57f, .63f, .72f}}, {}};
    return model.material < 0 ? defaultMaterial : scene.materials[model.material];
}

Vec3 orientedShadingNormal(const SurfaceHit& hit, bool front) {
    const Vec3 normal = dot(hit.shadingNormal, hit.shadingNormal) > .5 ? hit.shadingNormal : hit.normal;
    return front ? normal : normal * -1;
}

bool scatterDielectric(Vec3& direction, Vec3 normal, bool outside, double index, Random& random) {
    const double ratio = outside ? 1 / index : index;
    const double cosine = std::clamp(-dot(direction, normal), 0.0, 1.0);
    const double discriminant = 1 - ratio * ratio * (1 - cosine * cosine);
    const double r = (1 - index) / (1 + index);
    const double fresnel = r * r + (1 - r * r) * std::pow(1 - cosine, 5);
    if (discriminant < 0 || random.next() < fresnel) {
        direction = normalized(direction + normal * (2 * cosine));
        return false;
    }
    direction = normalized(direction * ratio + normal * (ratio * cosine - std::sqrt(discriminant)));
    return true;
}

Vec3 transmissionTint(const MaterialEdit& material, Vec2 uv) {
    return Vec3{.85, .85, .85} + baseColor(material, uv) * .15;
}

struct Cell {
    std::int64_t x, y, z;
    bool operator==(const Cell& other) const { return x == other.x && y == other.y && z == other.z; }
};

struct CellHash {
    std::size_t operator()(Cell cell) const {
        return std::uint64_t(cell.x) * 0x9e3779b185ebca87ULL ^
               std::uint64_t(cell.y) * 0xc2b2ae3d27d4eb4fULL ^ std::uint64_t(cell.z) * 0x165667b19e3779f9ULL;
    }
};

Cell cellFor(Vec3 position, double size) {
    return {std::int64_t(std::floor(position.x / size)), std::int64_t(std::floor(position.y / size)),
            std::int64_t(std::floor(position.z / size))};
}

struct Photon {
    const Model* receiver;
    Vec3 point{}, normal{}, flux{};
    unsigned count = 1;
};

struct CausticMap {
    double radius = .1;
    Bounds bounds{{farPlane, farPlane, farPlane}, {-farPlane, -farPlane, -farPlane}};
    std::unordered_map<Cell, std::vector<Photon>, CellHash> cells;

    Vec3 sample(const Model* receiver, Vec3 point, Vec3 normal) const {
        if (cells.empty() || point.x < bounds.min.x - radius || point.y < bounds.min.y - radius ||
            point.z < bounds.min.z - radius || point.x > bounds.max.x + radius ||
            point.y > bounds.max.y + radius || point.z > bounds.max.z + radius) return {};
        const auto base = cellFor(point, radius);
        Vec3 value{};
        for (int z = -1; z <= 1; ++z) {
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    const auto bin = cells.find({base.x + x, base.y + y, base.z + z});
                    if (bin == cells.end()) continue;
                    for (const Photon& photon : bin->second) {
                        if (receiver != photon.receiver || dot(photon.normal, normal) < .65) continue;
                        const Vec3 delta = photon.point - point;
                        if (std::abs(dot(delta, normal)) > radius * .26) continue;
                        const double distance = std::sqrt(dot(delta, delta));
                        if (distance < radius) value = value + photon.flux * (1 - distance / radius);
                    }
                }
            }
        }
        return value * (3 / (pi * radius * radius));
    }
};

struct Caster { const Model* model; Vec3 center; double radius; };

Ray photonRay(const Model& light, const Caster& caster, Random& random, double& measure) {
    if (light.kind == ObjectKind::Sky) {
        const Vec3 towardSky = sampleLobe(light.transform.basis[1], std::sqrt(random.next()), random);
        const Vec3 disk = sampleLobe(towardSky, 0, random) * (std::sqrt(random.next()) * caster.radius);
        const double distance = std::clamp(caster.radius + 20, 40.0, farPlane * .5);
        measure = pi * pi * caster.radius * caster.radius;
        return {caster.center + towardSky * distance + disk, towardSky * -1};
    }
    const Vec3 origin = light.transform.position + sampleLobe(axes[1], 2 * random.next() - 1, random) *
                        light.light.radius;
    const Vec3 aim = caster.center - origin;
    const double distanceSquared = dot(aim, aim);
    const double radiusSquared = caster.radius * caster.radius;
    const double cosine = distanceSquared <= radiusSquared ? -1 : std::sqrt(1 - radiusSquared / distanceSquared);
    measure = 2 * pi * (1 - cosine);
    const Vec3 axis = distanceSquared > epsilon * epsilon ? normalized(aim) : axes[1];
    return {origin, sampleLobe(axis, 1 - random.next() * (1 - cosine), random)};
}

CausticMap traceCaustics(const TraceGeometry& geometry, RenderJob& job) {
    const auto& scene = geometry.scene;
    CausticMap result;
    if (scene.causticPhotons <= 0) return result;
    std::vector<Caster> casters;
    std::vector<const Model*> lights;
    for (const Model& light : scene.lights) {
        if (light.light.enabled && light.light.power > 0) lights.push_back(&light);
    }
    if (lights.empty()) return result;
    for (std::size_t i = 0; i < scene.boxes.size(); ++i) {
        const Model& model = scene.boxes[i];
        const auto& material = materialFor(scene, model);
        const auto optics = materialOptics(material);
        if (optics.transmission <= 0 || optics.index <= 1 || optics.causticStrength <= 0) continue;
        const Bounds& bounds = geometry.objects[i].bounds();
        const Vec3 extent = bounds.max - bounds.min;
        const double radius = .5 * std::sqrt(dot(extent, extent));
        if (radius > epsilon) casters.push_back({&model, bounds.center(), radius});
    }
    if (casters.empty() || casters.size() == scene.boxes.size()) return result;
    double typical = 0;
    for (const Caster& caster : casters) typical += caster.radius;
    result.radius = std::clamp(typical / casters.size() * .20, .045, .48);
    using Bins = std::unordered_map<Cell, std::vector<Photon>, CellHash>;
    Bins samples;
    Random random{0x89cd3725u};
    const std::size_t pairCount = lights.size() * casters.size();
    // Each ray covers its share of the light's solid angle aimed at the caster.
    const double normalization = double(pairCount) / scene.causticPhotons;
    for (int sample = 0; sample < scene.causticPhotons; ++sample) {
        if ((sample & 63) == 0 && job.cancel.load(std::memory_order_relaxed)) return {};
        const auto pair = std::size_t(random.next() * pairCount);
        const Model& light = *lights[pair / casters.size()];
        const Caster& caster = casters[pair % casters.size()];
        double measure = 0;
        Ray ray = photonRay(light, caster, random, measure);
        Hit hit = trace(geometry, ray, false);
        if (hit.box != caster.model) continue;
        const bool sky = light.kind == ObjectKind::Sky;
        const double distance = hit.surface.distance;
        const double strength = sky ? 1 : localLightPower(light, ray.direction, distance) * distance * distance;
        Vec3 flux = (sky ? skyRadiance(light, ray.direction * -1) : rgb(light.light.color) * strength) *
                    (measure * normalization);
        bool refracted = false;
        double travel = 0;
        for (int bounce = 0; hit.box && bounce < 8 && dot(flux, flux) > 1e-24; ++bounce) {
            travel += hit.surface.distance;
            if (!sky && light.light.range > 0 && travel >= light.light.range) break;
            const auto& material = materialFor(scene, *hit.box);
            const auto optics = materialOptics(material);
            const Vec3 point = ray.origin + ray.direction * hit.surface.distance;
            const bool outside = dot(hit.surface.normal, ray.direction) < 0;
            const Vec3 geometric = outside ? hit.surface.normal : hit.surface.normal * -1;
            if (optics.transmission <= 0) {
                if (!refracted || material.surface.shading == Shading::Unlit) break;
                auto& bin = samples[cellFor(point, result.radius / 5)];
                const auto match = std::find_if(bin.begin(), bin.end(), [&](const Photon& photon) {
                    return photon.receiver == hit.box && dot(photon.normal, geometric) > .85;
                });
                if (match == bin.end()) bin.push_back({hit.box, point, geometric, flux, 1});
                else {
                    ++match->count;
                    match->point = match->point + (point - match->point) * (1.0 / match->count);
                    match->flux = match->flux + flux;
                }
                break;
            }
            const Vec3 incoming = ray.direction;
            Vec3 normal = shadeNormal(material.preset, orientedShadingNormal(hit.surface, outside), point);
            if (dot(normal, incoming) >= 0) normal = geometric;
            if (scatterDielectric(ray.direction, normal, outside, optics.index, random)) {
                if (outside) flux = flux * (optics.transmission * optics.causticStrength);
                flux = multiply(flux, transmissionTint(material, hit.surface.uv));
                refracted = refracted || dot(incoming, ray.direction) < .999999;
            } else {
                flux = flux * .96;
            }
            ray.origin = point + ray.direction * epsilon;
            hit = trace(geometry, ray, false);
        }
    }
    result.cells.reserve(samples.size() / 4);
    for (const auto& entry : samples) {
        for (const Photon& photon : entry.second) {
            const auto p = photon.point;
            result.cells[cellFor(p, result.radius)].push_back(photon);
            result.bounds.min = {std::min(result.bounds.min.x, p.x), std::min(result.bounds.min.y, p.y), std::min(result.bounds.min.z, p.z)};
            result.bounds.max = {std::max(result.bounds.max.x, p.x), std::max(result.bounds.max.y, p.y), std::max(result.bounds.max.z, p.z)};
        }
    }
    return result;
}

Vec3 environment(const TraceScene& scene, Vec3 direction, bool primary) {
    if (scene.lights.empty()) {
        if (primary) return {.028, .038, .056};
        return Vec3{.24, .29, .38} * (.45 + .55 * std::max(0.0, direction.y));
    }
    Vec3 sky{};
    for (const Model& light : scene.lights) {
        if (light.kind != ObjectKind::Sky) continue;
        const double weight = primary ? .55 : (light.light.shadows ? 1 - light.light.shadowTransparency : 0);
        sky = sky + skyRadiance(light, direction) * weight;
    }
    return primary && dot(sky, sky) == 0 ? Vec3{.018, .025, .038} : sky;
}

Vec3 directLight(const TraceGeometry& geometry, const Surface& surface, Vec3 diffuse, Vec3 point, Vec3 normal,
                 Vec3 view, Random& random) {
    const TraceScene& scene = geometry.scene;
    Vec3 value{};
    const auto add = [&](Vec3 direction, double distance, Vec3 light, double opacity) {
        const double cosine = dot(normal, direction);
        if (cosine <= 0 || dot(light, light) == 0) return;
        const double visibility = opacity > 0 &&
            trace(geometry, {point + normal * epsilon, direction}, false, distance).box ? 1 - opacity : 1;
        const double specular = specularHighlight(surface, normal, direction, view);
        value = value + multiply(light, diffuse * cosine + Vec3{specular, specular, specular}) * visibility;
    };
    if (scene.lights.empty()) {
        add(normalized({-.45, .80, .65}), farPlane, {1.1, 1.07, 1}, 1);
        return value;
    }
    for (const Model& light : scene.lights) {
        if (!light.light.enabled || light.kind == ObjectKind::Sky) continue;
        const Vec3 center = point - light.transform.position;
        const double centerDistance = std::sqrt(dot(center, center));
        if (centerDistance < epsilon) continue;
        const double power = localLightPower(light, center * (1 / centerDistance), centerDistance);
        if (power <= 0) continue;
        Vec3 source = light.transform.position;
        if (light.light.shadows && light.light.radius > 0) {
            source = source + sampleLobe(axes[1], 2 * random.next() - 1, random) * light.light.radius;
        }
        const Vec3 toLight = source - point;
        const double distance = std::sqrt(dot(toLight, toLight));
        if (distance < epsilon) continue;
        const double opacity = light.light.shadows ? 1 - light.light.shadowTransparency : 0;
        add(toLight * (1 / distance), distance - epsilon, rgb(light.light.color) * power, opacity);
    }
    return value;
}

Vec3 radiance(const TraceGeometry& geometry, const CausticMap& caustics, Ray ray, Random& random) {
    const TraceScene& scene = geometry.scene;
    Vec3 value{}, throughput{1, 1, 1};
    bool opticalPath = true;
    for (int bounce = 0; bounce < 4; ++bounce) {
        const Hit hit = trace(geometry, ray, bounce == 0);
        if (!hit.box) return value + multiply(throughput, environment(scene, ray.direction, bounce == 0));
        const auto& material = materialFor(scene, *hit.box);
        const Surface& surface = material.surface;
        const Vec3 base = baseColor(material, hit.surface.uv);
        const Vec3 point = ray.origin + ray.direction * hit.surface.distance;
        const bool outside = dot(hit.surface.normal, ray.direction) < 0;
        const Vec3 geometric = outside ? hit.surface.normal : hit.surface.normal * -1;
        const Vec3 normal = shadeNormal(material.preset, orientedShadingNormal(hit.surface, outside), point);
        const Optics optics = materialOptics(material);
        if (optics.transmission > 0 && random.next() < optics.transmission) {
            if (scatterDielectric(ray.direction, dot(normal, ray.direction) < 0 ? normal : geometric,
                                  outside, optics.index, random)) {
                throughput = multiply(throughput, Vec3{.85, .85, .85} + base * .15);
            }
            ray.origin = point + ray.direction * epsilon;
            continue;
        }
        if (surface.shading == Shading::Unlit) return value + multiply(throughput, base);
        const double gloss = surface.shading == Shading::Lambert ? 0 : surface.specular * .003;
        const Vec3 diffuse = base * (surface.diffuse * .01 * (1 - gloss));
        const Vec3 direct = directLight(geometry, surface, diffuse, point, normal, ray.direction * -1, random);
        const Vec3 focused = opticalPath ? multiply(diffuse, caustics.sample(hit.box, point, geometric)) : Vec3{};
        value = value + multiply(throughput, base * (surface.emission * .018) + direct + focused);
        opticalPath = false;

        if (random.next() < gloss) {
            const Vec3 reflection = ray.direction - normal * (2 * dot(ray.direction, normal));
            ray.direction = sampleLobe(reflection, std::pow(random.next(), 1.0 / (surface.shininess + 1)), random);
            if (dot(ray.direction, normal) <= 0) break;
            throughput = throughput * .82;
        } else {
            ray.direction = sampleLobe(normal, std::sqrt(random.next()), random);
            throughput = multiply(throughput, base * (surface.diffuse * .01));
        }
        for (const Model& light : scene.lights) {
            if (light.kind != ObjectKind::Sky) continue;
            const double leak = light.light.shadows ? light.light.shadowTransparency : 1;
            value = value + multiply(throughput, skyRadiance(light, ray.direction)) * leak;
        }
        ray.origin = point + geometric * epsilon;
    }
    return value;
}

unsigned char displayChannel(double value) {
    const double mapped = value * (2.51 * value + .03) / (value * (2.43 * value + .59) + .14);
    return static_cast<unsigned char>(255 * std::pow(std::clamp(mapped, 0.0, 1.0), 1.0 / 2.2) + .5);
}

Ray cameraRay(const TraceScene& scene, double x, double y) {
    const TraceCamera& camera = scene.camera;
    const Vec3 offset = camera.right * (x - scene.width * .5) + camera.up * (y - scene.height * .5);
    return camera.perspective
        ? Ray{camera.eye, normalized(offset * (1 / camera.focal) - camera.back)}
        : Ray{camera.eye + offset * (1 / camera.orthoScale), camera.back * -1};
}

void overlayCaustic(const TraceGeometry& geometry, const CausticMap& caustics, Ray ray, unsigned char* pixel) {
    double transmission = 1;
    for (int layer = 0; layer < 8; ++layer) {
        const Hit hit = trace(geometry, ray, layer == 0);
        if (!hit.box) return;
        const auto& material = materialFor(geometry.scene, *hit.box);
        const auto optics = materialOptics(material);
        const Vec3 point = ray.origin + ray.direction * hit.surface.distance;
        const bool front = dot(hit.surface.normal, ray.direction) < 0;
        if (optics.transmission > 0) {
            if (front) transmission *= optics.transmission * .84;
            ray.origin = point + ray.direction * epsilon;
            continue;
        }
        if (material.surface.shading == Shading::Unlit) return;
        const Vec3 normal = front ? hit.surface.normal : hit.surface.normal * -1;
        const Vec3 focus = multiply(baseColor(material, hit.surface.uv), caustics.sample(hit.box, point, normal)) *
                           (transmission * material.surface.diffuse * .01);
        const double channels[] = {focus.z, focus.y, focus.x};
        for (int i = 0; i < 3; ++i) {
            if (channels[i] <= 0) continue;
            const double base = pixel[i] / 255.0;
            pixel[i] = static_cast<unsigned char>(std::min(1.0, std::sqrt(base * base + channels[i])) * 255 + .5);
        }
        return;
    }
}

void drawRow(const TraceGeometry& geometry, const CausticMap& caustics, RenderJob& job, int row, bool software) {
    const TraceScene& scene = geometry.scene;
    for (int x = 0; x < scene.width && !job.cancel.load(std::memory_order_relaxed); ++x) {
        auto* pixel = job.pixels.data() + (std::size_t(row) * scene.width + x) * 4;
        if (software) {
            overlayCaustic(geometry, caustics, cameraRay(scene, x + .5, row + .5), pixel);
            continue;
        }
        Random random{static_cast<std::uint32_t>(std::size_t(row) * scene.width + x + 1)};
        Vec3 value{};
        for (int sample = 0; sample < scene.samples; ++sample) {
            if ((sample & 31) == 0 && job.cancel.load(std::memory_order_relaxed)) return;
            const double horizontal = x + random.next(), vertical = row + random.next();
            value = value + radiance(geometry, caustics, cameraRay(scene, horizontal, vertical), random);
        }
        value = value * (1.0 / scene.samples);
        pixel[0] = displayChannel(value.z);
        pixel[1] = displayChannel(value.y);
        pixel[2] = displayChannel(value.x);
        pixel[3] = 255;
    }
}
} // namespace

void renderGlobal(const TraceScene& scene, RenderJob& job) noexcept {
    std::atomic<int> nextRow{0};
    TraceGeometry geometry{scene, {}};
    CausticMap caustics;
    const bool software = !job.pixels.empty();
    const auto tasks = [&] {
        while (!job.cancel.load(std::memory_order_relaxed)) {
            const int row = nextRow.fetch_add(1, std::memory_order_relaxed);
            if (row >= scene.height) break;
            drawRow(geometry, caustics, job, row, software);
            job.rows.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::vector<std::thread> workers;
    try {
        geometry.objects.reserve(scene.boxes.size());
        for (const Model& model : scene.boxes) geometry.objects.emplace_back(model);
        job.tracingPhotons.store(true, std::memory_order_relaxed);
        caustics = traceCaustics(geometry, job);
        job.tracingPhotons.store(false, std::memory_order_relaxed);
        if ((software && caustics.cells.empty()) || job.cancel.load(std::memory_order_relaxed)) {
            job.done.store(true, std::memory_order_release);
            return;
        }
        job.pixels.resize(std::size_t(scene.width) * scene.height * 4);
        const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
        workers.reserve(count - 1);
        for (unsigned i = 1; i < count; ++i) workers.emplace_back(tasks);
        tasks();
    } catch (...) {
        job.failed = true;
        job.cancel.store(true, std::memory_order_relaxed);
    }
    for (auto& worker : workers) worker.join();
    job.done.store(true, std::memory_order_release);
}
