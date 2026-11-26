#pragma once

#include "scene_mesh.h"

#include <vector>

struct ShadowProjection {
    Vec3 origin{}, forward{}, right{}, up{};
    double extent = 1;
    bool orthographic = false;
    int size = 0;
    std::vector<float> depth;
};

struct ShadowMap {
    std::vector<ShadowProjection> projections;
    void build(const Model& light, const std::vector<const Model*>& geometry);
    double visibility(Vec3 point, Vec3 normal, double transparency) const;
};
