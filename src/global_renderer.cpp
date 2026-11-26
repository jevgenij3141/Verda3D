#include "global_renderer.h"
#include "material_library.h"
#include "scene_mesh.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <thread>
#include <unordered_map>

namespace {
constexpr double epsilon = .0005;
constexpr int minPreviewPixelSize = 8, maxPreviewDimension = 192, renderTileSize = 16;
constexpr unsigned maxRenderThreads = 8;
constexpr double minFogTransmission = .005;
constexpr Vec3 fogFlowDirection{.55, .2, .4};
const Vec3 defaultLightDirection = normalized({-.45, .80, .65});

enum class FogPass { Thumbnail, Raster, Final };

struct FogSamples {
    int mist, cloud;
    int mistShadows, cloudShadows;
};

struct FogQualitySettings {
    int gridResolution;
    FogSamples raster, final;
};

constexpr std::array fogQualitySettings{
    FogQualitySettings{37, {8, 12, 3, 4}, {16, 28, 5, 7}},
    FogQualitySettings{57, {10, 18, 4, 6}, {24, 48, 8, 12}},
    FogQualitySettings{89, {20, 36, 8, 12}, {48, 96, 16, 24}},
};

FogQuality qualityForPass(FogQuality quality, FogPass pass) {
    return pass == FogPass::Thumbnail ? FogQuality::Fast : quality;
}

const FogSamples& fogSamples(const FogVolume& fog, FogPass pass) {
    const auto& settings = fogQualitySettings[std::size_t(qualityForPass(fog.quality, pass))];
    return pass == FogPass::Final ? settings.final : settings.raster;
}

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

struct CloudDensityGrid {
    int resolution = 0;
    std::vector<float> density;

    double sample(Vec3 local) const {
        const int last = resolution - 1;
        const Vec3 cell = (local + Vec3{halfBox, halfBox, halfBox}) * last;
        const int x = std::clamp(int(cell.x), 0, last - 1);
        const int y = std::clamp(int(cell.y), 0, last - 1);
        const int z = std::clamp(int(cell.z), 0, last - 1);
        const auto plane = [&](int depth) {
            return std::lerp(std::lerp(at(x, y, depth), at(x + 1, y, depth), cell.x - x),
                             std::lerp(at(x, y + 1, depth), at(x + 1, y + 1, depth), cell.x - x), cell.y - y);
        };
        return std::lerp(plane(z), plane(z + 1), cell.z - z);
    }

    double at(int x, int y, int z) const {
        return density[(std::size_t(z) * resolution + y) * resolution + x];
    }
};

struct TraceGeometry {
    const TraceScene& scene;
    std::vector<ModelCollider> objects;
    std::vector<std::shared_ptr<const CloudDensityGrid>> clouds;
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

double fogOpticalDepth(const TraceGeometry& geometry, Ray ray, double distance, FogPass pass);

Vec3 directLight(const TraceGeometry& geometry, const Surface& surface, Vec3 diffuse, Vec3 point, Vec3 normal,
                 Vec3 view, Random& random, FogPass pass) {
    const TraceScene& scene = geometry.scene;
    Vec3 value{};
    const auto add = [&](Vec3 direction, double distance, Vec3 light, double opacity) {
        const double cosine = dot(normal, direction);
        if (cosine <= 0 || dot(light, light) == 0) return;
        double visibility = 1;
        if (opacity > 0) {
            const Ray shadow{point + normal * epsilon, direction};
            const double transmitted = trace(geometry, shadow, false, distance).box ? 0
                : std::exp(-fogOpticalDepth(geometry, shadow, distance, pass));
            visibility = std::lerp(1.0, transmitted, opacity);
        }
        const double specular = specularHighlight(surface, normal, direction, view);
        value = value + multiply(light, diffuse * cosine + Vec3{specular, specular, specular}) * visibility;
    };
    if (scene.lights.empty()) {
        add(defaultLightDirection, farPlane, {1.1, 1.07, 1}, 1);
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

Vec3 radiance(const TraceGeometry& geometry, const CausticMap& caustics, Ray ray, Random& random,
              FogPass pass = FogPass::Final) {
    const TraceScene& scene = geometry.scene;
    Vec3 value{}, throughput{1, 1, 1};
    bool opticalPath = true;
    for (int bounce = 0; bounce < 4; ++bounce) {
        const Hit hit = trace(geometry, ray, bounce == 0);
        if (!hit.box) {
            const auto background = environment(scene, ray.direction, bounce == 0);
            const double visibility = bounce > 0
                ? std::exp(-fogOpticalDepth(geometry, ray, farPlane, pass)) : 1;
            return value + multiply(throughput, background) * visibility;
        }
        const auto& material = materialFor(scene, *hit.box);
        const Surface& surface = material.surface;
        const Vec3 base = baseColor(material, hit.surface.uv);
        const Vec3 point = ray.origin + ray.direction * hit.surface.distance;
        const bool outside = dot(hit.surface.normal, ray.direction) < 0;
        const Vec3 geometric = outside ? hit.surface.normal : hit.surface.normal * -1;
        const Vec3 normal = shadeNormal(material.preset, orientedShadingNormal(hit.surface, outside), point);
        const Optics optics = materialOptics(material);
        if (!outside && isCrystalPreset(material.preset)) {
            constexpr double gemstoneAbsorption = .8;
            const double distance = hit.surface.distance * gemstoneAbsorption;
            const auto attenuate = [&](double tint) { return std::pow(std::clamp(tint, .035, 1.0), distance); };
            throughput = multiply(throughput, {attenuate(base.x), attenuate(base.y), attenuate(base.z)});
        }
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
        const Vec3 direct = directLight(geometry, surface, diffuse, point, normal, ray.direction * -1, random, pass);
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

struct FogSegment {
    const Model* model;
    double start, end;
};

struct FogSample {
    Vec3 scattered{};
    double transmission = 1;
};

std::optional<FogSegment> fogSegment(const Model& model, Ray ray, double start, double end) {
    const Vec3 origin = localVector(model.transform, ray.origin - model.transform.position);
    const Vec3 direction = localVector(model.transform, ray.direction);
    const std::array starts{origin.x, origin.y, origin.z};
    const std::array slopes{direction.x, direction.y, direction.z};
    for (std::size_t i = 0; i < starts.size(); ++i) {
        if (std::abs(slopes[i]) < 1e-10) {
            if (std::abs(starts[i]) > halfBox) return {};
            continue;
        }
        double entry = (-halfBox - starts[i]) / slopes[i], exit = (halfBox - starts[i]) / slopes[i];
        if (entry > exit) std::swap(entry, exit);
        start = std::max(start, entry);
        end = std::min(end, exit);
        if (start >= end) return {};
    }
    return FogSegment{&model, start, end};
}

double fogNoise(Vec3 point) {
    const int x = int(std::floor(point.x)), y = int(std::floor(point.y)), z = int(std::floor(point.z));
    const auto smooth = [](double t) { return t * t * (3 - 2 * t); };
    const double u = smooth(point.x - x), v = smooth(point.y - y), w = smooth(point.z - z);
    const auto value = [](int a, int b, int c) {
        std::uint32_t bits = std::uint32_t(a) * 0x8da6b343u ^ std::uint32_t(b) * 0xd8163841u ^
                             std::uint32_t(c) * 0xcb1ab31fu;
        bits ^= bits >> 13;
        bits *= 0x85ebca6bu;
        return double(bits >> 8) / 16'777'215;
    };
    const auto plane = [&](int depth) {
        return std::lerp(std::lerp(value(x, y, depth), value(x + 1, y, depth), u),
                         std::lerp(value(x, y + 1, depth), value(x + 1, y + 1, depth), u), v);
    };
    return std::lerp(plane(z), plane(z + 1), w);
}

double smoothRange(double minimum, double maximum, double value) {
    const double t = std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double cellularDistance(Vec3 p) {
    const int x = int(std::floor(p.x)), y = int(std::floor(p.y)), z = int(std::floor(p.z));
    double closest = 1;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                std::uint32_t seed = std::uint32_t(x + dx) * 0x8da6b343u ^
                                     std::uint32_t(y + dy) * 0xd8163841u ^
                                     std::uint32_t(z + dz) * 0xcb1ab31fu;
                seed ^= seed >> 13;
                seed *= 0x85ebca6bu;
                const auto offset = [&](int shift) { return .18 + .64 * ((seed >> shift) & 255u) / 255; };
                const Vec3 center{x + dx + offset(0), y + dy + offset(8), z + dz + offset(16)};
                const auto delta = p - center;
                closest = std::min(closest, dot(delta, delta));
            }
        }
    }
    return std::sqrt(closest);
}

double cumulusDensity(Vec3 local, int detail, Vec3 drift) {
    struct Lobe { Vec3 center, extent; };
    constexpr Lobe lobes[] = {
        {{-.23, -.12, -.01}, {.25, .21, .29}}, {{.20, -.09, -.05}, {.27, .24, .29}},
        {{-.15, .09, -.07}, {.24, .30, .26}}, {{.19, .15, .02}, {.20, .245, .23}},
        {{-.19, .26, -.07}, {.15, .19, .18}}, {{-.31, .045, -.12}, {.15, .19, .22}},
        {{.08, -.05, .23}, {.235, .195, .22}}
    };
    constexpr Vec3 noiseOffset{7.3, 11.7, 4.1};
    constexpr double mergeWidth = .19, warpStrength = .21, cloudDensityGain = 2.1;
    const double boundary = halfBox - std::max({std::abs(local.x), std::abs(local.y), std::abs(local.z)});
    const double base = smoothRange(-.39, -.25, local.y);
    if (base <= 0 || boundary <= 0) return 0;

    const Vec3 macro = local * 3 + noiseOffset - drift * .24;
    const Vec3 warp{fogNoise(macro), fogNoise(macro + Vec3{14, 5, 9}), fogNoise(macro + Vec3{6, 13, 4})};
    const Vec3 point = local + (warp - Vec3{halfBox, halfBox, halfBox}) * warpStrength;
    double field = -1;
    for (const auto& lobe : lobes) {
        const auto offset = point - lobe.center;
        const Vec3 normalizedOffset{offset.x / lobe.extent.x, offset.y / lobe.extent.y, offset.z / lobe.extent.z};
        const double surface = 1 - std::sqrt(dot(normalizedOffset, normalizedOffset));
        const double blend = std::max(0.0, mergeWidth - std::abs(field - surface)) / mergeWidth;
        field = std::max(field, surface) + blend * blend * mergeWidth * .25;
    }
    if (field < -.30) return 0;

    const Vec3 detailPoint = local * detail + noiseOffset - drift;
    const double coreVariation = fogNoise(detailPoint * .72);
    const double rim = 1 - smoothRange(.16, .51, field);
    if (rim > 0) {
        const double billows = fogNoise(detailPoint * 1.6);
        const double erosion = cellularDistance(detailPoint * 3.4);
        const double wisps = fogNoise(detailPoint * 6.1);
        field += rim * (.33 * (billows - .39) - .22 * erosion + .11 * (wisps - .5));
    }
    const double interior = std::lerp(.48, 1.18, smoothRange(.21, .78, coreVariation));
    return smoothRange(-.035, .44, field) * interior * base * smoothRange(0, .095, boundary) * cloudDensityGain;
}

Vec3 fogDrift(const TraceScene& scene, const FogVolume& fog) {
    return fogFlowDirection * (scene.timeSeconds * fog.flow / maxFogFlow);
}

bool prepareCloudDensity(TraceGeometry& geometry, const RenderJob& job, FogPass pass) {
    constexpr std::size_t cacheBudget = 32 * 1024 * 1024;
    const auto& scene = geometry.scene;
    geometry.clouds.assign(scene.volumes.size(), {});
    std::unordered_map<int, std::shared_ptr<const CloudDensityGrid>> fields;
    std::size_t cachedBytes = 0;
    for (std::size_t i = 0; i < scene.volumes.size(); ++i) {
        const auto& fog = scene.volumes[i].fog;
        if (fog.style != FogStyle::Cloud || fog.density == 0) continue;
        const auto quality = std::size_t(qualityForPass(fog.quality, pass));
        const int parameters = (fog.detail * (maxFogFlow + 1) + fog.flow) * int(fogQualitySettings.size()) + int(quality);
        if (const auto found = fields.find(parameters); found != fields.end()) {
            geometry.clouds[i] = found->second;
            continue;
        }
        const int resolution = fogQualitySettings[quality].gridResolution;
        const auto cells = std::size_t(resolution) * resolution * resolution;
        const auto bytes = cells * sizeof(float);
        if (bytes > cacheBudget - cachedBytes) continue;
        auto field = std::make_shared<CloudDensityGrid>();
        field->resolution = resolution;
        field->density.resize(cells);
        const auto drift = fogDrift(scene, fog);
        const auto coordinate = [resolution](int index) { return double(index) / (resolution - 1) - halfBox; };
        for (int z = 0; z < resolution; ++z) {
            if (job.cancel.load(std::memory_order_relaxed)) return false;
            for (int y = 0; y < resolution; ++y) {
                for (int x = 0; x < resolution; ++x) {
                    const Vec3 local{coordinate(x), coordinate(y), coordinate(z)};
                    field->density[(std::size_t(z) * resolution + y) * resolution + x] =
                        float(cumulusDensity(local, fog.detail, drift));
                }
            }
        }
        fields.emplace(parameters, field);
        geometry.clouds[i] = std::move(field);
        cachedBytes += bytes;
    }
    return true;
}

double fogDensity(const TraceGeometry& geometry, const Model& model, Vec3 local, Vec3 drift) {
    const double boundary = halfBox - std::max({std::abs(local.x), std::abs(local.y), std::abs(local.z)});
    if (boundary <= 0) return 0;
    const auto& fog = model.fog;
    const double density = fog.density * fogDensityUnit;
    if (fog.style == FogStyle::Cloud) {
        const auto& field = geometry.clouds[std::size_t(&model - geometry.scene.volumes.data())];
        return density * (field ? field->sample(local) : cumulusDensity(local, fog.detail, drift));
    }
    const Vec3 sample = local * fog.detail + Vec3{7.3, 11.7, 4.1} - drift;
    return density * std::clamp(boundary * 8, 0.0, 1.0) * (.45 + .55 * fogNoise(sample));
}

double fogOpticalDepth(const TraceGeometry& geometry, Ray ray, double distance, FogPass pass) {
    const auto& scene = geometry.scene;
    double opticalDepth = 0;
    for (const auto& model : scene.volumes) {
        if (!model.fog.shadows || model.fog.density == 0) continue;
        const auto interval = fogSegment(model, ray, epsilon, distance);
        if (!interval) continue;
        const auto& samples = fogSamples(model.fog, pass);
        const int count = model.fog.style == FogStyle::Cloud ? samples.cloudShadows : samples.mistShadows;
        const double step = (interval->end - interval->start) / count;
        const Vec3 start = localVector(model.transform, ray.origin + ray.direction * interval->start -
                                       model.transform.position);
        const Vec3 localStep = localVector(model.transform, ray.direction) * step;
        const Vec3 drift = fogDrift(scene, model.fog);
        double density = 0;
        for (int i = 0; i < count; ++i)
            density += fogDensity(geometry, model, start + localStep * (i + .5), drift);
        opticalDepth += density * step;
    }
    return opticalDepth;
}

double fogPhase(FogStyle style, double cosine) {
    constexpr double cloudForward = .52, cloudBackward = -.22, mistAnisotropy = .23;
    constexpr double cloudBackscatterWeight = .14;
    const double angle = std::clamp(cosine, -1.0, 1.0);
    // Henyey–Greenstein relative to isotropic scattering.
    const auto phase = [&](double g) {
        const double denominator = 1 + g * g - 2 * g * angle;
        return (1 - g * g) / (denominator * std::sqrt(denominator));
    };
    return style == FogStyle::Cloud ? std::lerp(phase(cloudForward), phase(cloudBackward), cloudBackscatterWeight)
                                    : phase(mistAnisotropy);
}

FogSample integrateFog(const TraceGeometry& geometry, Ray ray, FogPass pass) {
    const auto& scene = geometry.scene;
    FogSample result;
    if (scene.volumes.empty()) return result;
    thread_local std::vector<FogSegment> segments;
    segments.clear();
    try {
        for (const auto& volume : scene.volumes) {
            if (!volume.fog.density) continue;
            if (const auto segment = fogSegment(volume, ray, nearPlane, farPlane)) segments.push_back(*segment);
        }
    } catch (const std::bad_alloc&) {
        return result;
    }
    if (segments.empty()) return result;
    const double surfaceDepth = trace(geometry, ray, true).surface.distance;
    std::ranges::sort(segments, {}, &FogSegment::start);
    for (const auto& segment : segments) {
        const double end = std::min(segment.end, surfaceDepth);
        if (end <= segment.start || result.transmission <= minFogTransmission) break;
        const Model& model = *segment.model;
        const bool cloud = model.fog.style == FogStyle::Cloud;
        const auto& samples = fogSamples(model.fog, pass);
        const int steps = cloud ? samples.cloud : samples.mist;
        const double step = (end - segment.start) / steps;
        const Vec3 tint = rgb(model.fog.color);
        const Vec3 drift = fogDrift(scene, model.fog);
        const Vec3 localStep = localVector(model.transform, ray.direction) * step;
        const Vec3 localStart = localVector(model.transform, ray.origin + ray.direction * segment.start -
                                           model.transform.position);
        for (int i = 0; i < steps && result.transmission > minFogTransmission; ++i) {
            const Vec3 local = localStart + localStep * (i + .5);
            const double density = fogDensity(geometry, model, local, drift);
            if (density <= 0) continue;
            const double skyFill = std::lerp(.48, 1.0, smoothRange(-.32, .34, local.y));
            Vec3 light = cloud ? Vec3{.075, .090, .12} * skyFill : Vec3{.11, .14, .20};
            const Vec3 point = ray.origin + ray.direction * (segment.start + step * (i + .5));
            const auto visibility = [&](Vec3 toLight, double distance, double opacity) {
                if (!model.fog.shadows) return 1.0;
                const Ray shadow{point, toLight};
                const double depth = fogOpticalDepth(geometry, shadow, distance, pass);
                double medium = std::exp(-depth);
                if (cloud) {
                    // A small, more penetrating term approximates light scattered within a thick cloud.
                    constexpr double multipleScatterWeight = .18, multipleScatterExtinction = .4;
                    medium = std::lerp(medium, std::exp(-depth * multipleScatterExtinction), multipleScatterWeight);
                }
                if (opacity > 0 && trace(geometry, shadow, false, distance).box) medium *= 1 - opacity;
                return medium;
            };
            if (scene.lights.empty()) {
                const double phase = fogPhase(model.fog.style, dot(ray.direction, defaultLightDirection));
                light = light + Vec3{.80, .82, .86} * (phase * visibility(defaultLightDirection, farPlane, 1));
            }
            for (const auto& source : scene.lights) {
                if (!source.light.enabled) continue;
                const double opacity = source.light.shadows ? 1 - source.light.shadowTransparency : 0;
                if (source.kind == ObjectKind::Sky) {
                    const Vec3 zenith = source.transform.basis[1];
                    light = light + skyRadiance(source, zenith) * (.45 * visibility(zenith, farPlane, opacity));
                    continue;
                }
                const Vec3 delta = point - source.transform.position;
                const double distance = std::sqrt(dot(delta, delta));
                if (distance <= epsilon) continue;
                const Vec3 direction = delta * (1 / distance);
                const double power = localLightPower(source, direction, distance);
                if (power <= 0) continue;
                const auto toLight = direction * -1;
                const double phase = fogPhase(model.fog.style, dot(ray.direction, toLight));
                light = light + rgb(source.light.color) * (power * phase * visibility(toLight, distance - epsilon, opacity));
            }
            const double absorption = 1 - std::exp(-density * step);
            result.scattered = result.scattered + multiply(tint, light) * (result.transmission * absorption);
            result.transmission *= 1 - absorption;
        }
    }
    return result;
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

bool drawSpan(const TraceGeometry& geometry, const CausticMap& caustics, RenderJob& job,
              int row, int first, int end, bool software) {
    const TraceScene& scene = geometry.scene;
    for (int x = first; x < end; ++x) {
        if (job.cancel.load(std::memory_order_relaxed)) return false;
        auto* pixel = job.pixels.data() + (std::size_t(row) * scene.width + x) * 4;
        if (software) {
            const auto ray = cameraRay(scene, x + .5, row + .5);
            if (!caustics.cells.empty()) overlayCaustic(geometry, caustics, ray, pixel);
            const auto fog = integrateFog(geometry, ray, FogPass::Raster);
            const double opacity = 1 - fog.transmission;
            if (opacity > 0) {
                const Vec3 color = fog.scattered * (1 / opacity);
                const std::array channels{color.z, color.y, color.x};
                for (std::size_t i = 0; i < channels.size(); ++i)
                    pixel[i] = static_cast<unsigned char>(pixel[i] * fog.transmission + displayChannel(channels[i]) * opacity);
            }
            continue;
        }
        Random random{static_cast<std::uint32_t>(std::size_t(row) * scene.width + x + 1)};
        Vec3 value{};
        for (int sample = 0; sample < scene.samples; ++sample) {
            if ((sample & 31) == 0 && job.cancel.load(std::memory_order_relaxed)) return false;
            const double horizontal = x + random.next(), vertical = row + random.next();
            value = value + radiance(geometry, caustics, cameraRay(scene, horizontal, vertical), random);
        }
        value = value * (1.0 / scene.samples);
        if (!scene.volumes.empty()) {
            const auto fog = integrateFog(geometry, cameraRay(scene, x + .5, row + .5), FogPass::Final);
            value = value * fog.transmission + fog.scattered;
        }
        pixel[0] = displayChannel(value.z);
        pixel[1] = displayChannel(value.y);
        pixel[2] = displayChannel(value.x);
        pixel[3] = 255;
    }
    return true;
}

void renderThumbnail(const TraceGeometry& geometry, const CausticMap& caustics, RenderJob& job) {
    const auto& scene = geometry.scene;
    const int step = std::max(minPreviewPixelSize,
                              (std::max(scene.width, scene.height) + maxPreviewDimension - 1) / maxPreviewDimension);
    const int width = (scene.width + step - 1) / step, height = (scene.height + step - 1) / step;
    std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (job.cancel.load(std::memory_order_relaxed)) return;
            const int left = x * step, bottom = y * step;
            const double px = left + std::min(step, scene.width - left) * .5;
            const double py = bottom + std::min(step, scene.height - bottom) * .5;
            Random random{static_cast<std::uint32_t>(std::size_t(bottom) * scene.width + left + 1)};
            const auto ray = cameraRay(scene, px, py);
            const auto fog = integrateFog(geometry, ray, FogPass::Thumbnail);
            const Vec3 value = radiance(geometry, caustics, ray, random, FogPass::Thumbnail) * fog.transmission + fog.scattered;
            auto* pixel = pixels.data() + (std::size_t(y) * width + x) * 4;
            pixel[0] = displayChannel(value.z);
            pixel[1] = displayChannel(value.y);
            pixel[2] = displayChannel(value.x);
            pixel[3] = 255;
        }
    }
    std::lock_guard<std::mutex> lock(job.preview->mutex);
    job.preview->thumbnailStep = step;
    job.preview->thumbnail = std::move(pixels);
}
} // namespace

void renderGlobal(const TraceScene& scene, RenderJob& job) noexcept {
    std::atomic<int> nextTask{0};
    TraceGeometry geometry{scene, {}, {}};
    CausticMap caustics;
    const bool software = !job.pixels.empty();
    std::vector<RenderTile> tiles;
    const auto tasks = [&] {
        while (!job.cancel.load(std::memory_order_relaxed)) {
            const int index = nextTask.fetch_add(1, std::memory_order_relaxed);
            if (tiles.empty()) {
                if (index >= scene.height || !drawSpan(geometry, caustics, job, index, 0, scene.width, software)) return;
                job.rows.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (index >= int(tiles.size())) return;
            const auto tile = tiles[index];
            for (int row = tile.y; row < tile.y + tile.height; ++row)
                if (!drawSpan(geometry, caustics, job, row, tile.x, tile.x + tile.width, false)) return;
            // The UI may read these pixels after publication. Workers never write this tile again.
            std::lock_guard<std::mutex> lock(job.preview->mutex);
            job.preview->completedTiles.push_back(tile);
        }
    };
    std::vector<std::thread> workers;
    try {
        geometry.objects.reserve(scene.boxes.size());
        for (const Model& model : scene.boxes) geometry.objects.emplace_back(model);
        if (job.preview && !software && prepareCloudDensity(geometry, job, FogPass::Thumbnail))
            renderThumbnail(geometry, caustics, job);
        if (job.cancel.load(std::memory_order_relaxed) ||
            !prepareCloudDensity(geometry, job, software ? FogPass::Raster : FogPass::Final)) {
            job.done.store(true, std::memory_order_release);
            return;
        }
        job.tracingPhotons.store(scene.causticPhotons > 0, std::memory_order_relaxed);
        caustics = traceCaustics(geometry, job);
        job.tracingPhotons.store(false, std::memory_order_relaxed);
        if ((software && caustics.cells.empty() && scene.volumes.empty()) || job.cancel.load(std::memory_order_relaxed)) {
            job.done.store(true, std::memory_order_release);
            return;
        }
        job.pixels.resize(std::size_t(scene.width) * scene.height * 4);
        if (job.preview && !software) {
            constexpr int size = renderTileSize;
            for (int y = 0; y < scene.height; y += size)
                for (int x = 0; x < scene.width; x += size)
                    tiles.push_back({x, y, std::min(size, scene.width - x), std::min(size, scene.height - y)});
            const auto distance = [&](const RenderTile& tile) {
                const int x = 2 * tile.x + tile.width - scene.width;
                const int y = 2 * tile.y + tile.height - scene.height;
                return x * x + y * y;
            };
            std::sort(tiles.begin(), tiles.end(), [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
            // Reserve before launching workers so their publication path cannot allocate.
            std::lock_guard<std::mutex> lock(job.preview->mutex);
            job.preview->completedTiles.reserve(tiles.size());
        }
        const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1u, maxRenderThreads);
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
