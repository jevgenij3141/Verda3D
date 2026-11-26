#pragma once

#include "scene_mesh.h"
#include <span>

class DynamicsSimulation {
public:
    DynamicsSimulation(const std::vector<Model>& models, DynamicsSettings settings, std::span<const int> automatic = {});
    bool hasBodies() const { return !dynamicModels.empty(); }
    const std::vector<std::size_t>& movingModels() const { return dynamicModels; }
    void advance(double seconds);
    void apply(std::vector<Model>& models) const;

private:
    struct Body {
        std::size_t model;
        Vec3 center, halfExtent, centerOffset, velocity;
        double inverseMass, bounce, friction;
    };

    DynamicsSettings settings;
    std::vector<Body> bodies;
    std::vector<std::size_t> dynamicModels, order;
    double accumulator = 0;
    double minHalfExtent = 1;

    void step(double seconds);
    void contact(Body& a, Body& b) const;
    void groundContact(Body& body) const;
};
