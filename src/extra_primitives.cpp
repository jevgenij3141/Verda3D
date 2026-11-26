#include "scene_mesh.h"

namespace {
constexpr std::array<Vec2, 4> faceUv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

void bridgeRings(Mesh& mesh, std::size_t first, std::size_t second, std::size_t sides, bool reverse = false) {
    for (std::size_t i = 0; i < sides; ++i) {
        const auto j = (i + 1) % sides;
        std::array vertices{first + i, second + i, second + j, first + j};
        if (reverse) std::reverse(vertices.begin(), vertices.end());
        mesh.faces.push_back({vertices, faceUv});
    }
}

void capRing(Mesh& mesh, std::size_t first, std::size_t sides, Vec3 center, bool reverse = false) {
    const auto tip = mesh.vertices.size();
    mesh.vertices.push_back(center);
    for (std::size_t i = 0; i < sides; ++i) {
        std::array vertices{tip, first + i, first + (i + 1) % sides, tip};
        if (reverse) std::swap(vertices[1], vertices[2]);
        mesh.faces.push_back({vertices, faceUv, 3});
    }
}

Mesh crystal(ObjectKind kind) {
    const bool point = kind == ObjectKind::CrystalPoint, step = kind == ObjectKind::CrystalStep;
    const bool octahedron = kind == ObjectKind::CrystalOctahedron;
    const std::size_t sides = octahedron ? 4 : point ? 6 : 8;
    constexpr Vec2 clippedRectangle[]{{-.31,-.5},{.31,-.5},{.5,-.31},{.5,.31},
                                      {.31,.5},{-.31,.5},{-.5,.31},{-.5,-.31}};
    Mesh mesh;
    const auto ring = [&](double y, double size) {
        const auto first = mesh.vertices.size();
        for (std::size_t i = 0; i < sides; ++i) {
            const double angle = 2 * pi * i / sides;
            const Vec2 p = step ? clippedRectangle[i] : Vec2{.5 * std::cos(angle), .5 * std::sin(angle)};
            mesh.vertices.push_back({p.x * size, y, p.y * size * (step ? .73 : 1)});
        }
        return first;
    };
    if (octahedron) {
        ring(0, 1);
        capRing(mesh, 0, sides, {0, -.5, 0});
        capRing(mesh, 0, sides, {0, .5, 0}, true);
    } else if (point) {
        ring(-.39, .68);
        ring(.18, .74);
        bridgeRings(mesh, 0, sides, sides);
        capRing(mesh, 0, sides, {0, -.5, 0});
        capRing(mesh, sides, sides, {.06, .5, -.06}, true);
    } else {
        ring(-.39, step ? .35 : .22);
        ring(-.05, 1);
        ring(.015, 1);
        ring(step ? .20 : .24, step ? .71 : .57);
        for (std::size_t i = 1; i < 4; ++i) bridgeRings(mesh, (i - 1) * sides, i * sides, sides);
        capRing(mesh, 0, sides, {0, step ? -.39 : -.48, 0});
        capRing(mesh, sides * 3, sides, {0, step ? .20 : .24, 0}, true);
    }
    return mesh;
}

Mesh teapot() {
    Mesh mesh;
    constexpr std::size_t sides = 24;
    const auto lathe = [&](const auto& profile) {
        const auto first = mesh.vertices.size();
        for (const auto section : profile) {
            for (std::size_t i = 0; i < sides; ++i) {
                const auto angle = 2 * pi * i / sides;
                mesh.vertices.push_back({section.x * std::cos(angle), section.y, section.x * std::sin(angle)});
            }
        }
        for (std::size_t i = 1; i < std::size(profile); ++i)
            bridgeRings(mesh, first + (i - 1) * sides, first + i * sides, sides);
        capRing(mesh, first, sides, {0, profile[0].y, 0});
        capRing(mesh, first + (std::size(profile) - 1) * sides, sides, {0, profile[std::size(profile)-1].y, 0}, true);
    };
    constexpr Vec2 body[]{{.21,-.26},{.30,-.24},{.41,-.13},{.445,.01},{.407,.16},{.31,.245},{.255,.265}};
    constexpr Vec2 lid[]{{.27,.254},{.289,.277},{.255,.307},{.16,.349},{.058,.368},{.058,.402},{.036,.425}};
    lathe(body);
    lathe(lid);
    const auto tube = [&](const auto& centers, const auto& radii) {
        constexpr std::size_t tubeSides = 12;
        const auto first = mesh.vertices.size();
        for (std::size_t i = 0; i < std::size(centers); ++i) {
            const auto a = centers[i == 0 ? 0 : i - 1], b = centers[std::min(i + 1, std::size(centers)-1)];
            const auto t = normalized(b - a);
            const Vec3 n{-t.y, t.x, 0};
            for (std::size_t j = 0; j < tubeSides; ++j) {
                const double angle = 2 * pi * j / tubeSides;
                mesh.vertices.push_back(centers[i] + (n * std::cos(angle) + Vec3{0,0,std::sin(angle)}) * radii[i]);
            }
        }
        for (std::size_t i = 1; i < std::size(centers); ++i)
            bridgeRings(mesh, first + (i - 1) * tubeSides, first + i * tubeSides, tubeSides, true);
        capRing(mesh, first, tubeSides, centers[0], true);
        capRing(mesh, first + (std::size(centers)-1)*tubeSides, tubeSides, centers[std::size(centers)-1]);
    };
    constexpr Vec3 spout[]{{.29,-.07,0},{.43,-.025,0},{.54,.08,0},{.63,.21,0},{.72,.28,0}};
    constexpr double spoutWidth[]{.132,.11,.075,.059,.074};
    constexpr Vec3 handle[]{{-.33,.16,0},{-.49,.232,0},{-.65,.20,0},{-.70,.06,0},{-.65,-.10,0},
                             {-.54,-.161,0},{-.366,-.123,0}};
    constexpr double handleWidth[]{.055,.055,.05,.049,.047,.051,.063};
    tube(spout, spoutWidth);
    tube(handle, handleWidth);
    for (auto& p : mesh.vertices) p = p * .70;
    return mesh;
}
}

Mesh createExtraPrimitive(ObjectKind kind) {
    return kind == ObjectKind::Teapot ? teapot() : crystal(kind);
}
