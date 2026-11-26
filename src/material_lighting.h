#pragma once

#include "geometry.h"

#include <algorithm>

struct Color { float r, g, b; };

enum class Shading { BlinnPhong, Phong, Lambert, Unlit };

struct Surface {
    Color tint{1, 1, 1};
    Shading shading = Shading::BlinnPhong;
    int ambient = 28;
    int diffuse = 74;
    int specular = 35;
    int shininess = 32;
    int emission = 0;
};

struct Lighting { float base, highlight; };

inline float specularHighlight(const Surface& surface, Vec3 normal, Vec3 light, Vec3 towardEye) {
    const double diffuse = std::max(0.0, dot(normal, light));
    if (surface.shading == Shading::Lambert || surface.shading == Shading::Unlit ||
        diffuse == 0 || surface.specular == 0) return 0;
    const double alignment = surface.shading == Shading::Phong
        ? dot(normal * (2 * diffuse) - light, towardEye)
        : dot(normal, normalized(light + towardEye));
    return float(surface.specular *
        std::pow(std::clamp(alignment, 0.0, 1.0), surface.shininess) / 100);
}

inline Lighting illuminate(const Surface& surface, Vec3 normal, Vec3 light, Vec3 towardEye) {
    if (surface.shading == Shading::Unlit) return {1, 0};
    const double diffuse = std::max(0.0, dot(normal, light));
    const float base = float((surface.ambient + surface.diffuse * diffuse + surface.emission) / 100);
    return {base, specularHighlight(surface, normal, light, towardEye)};
}
