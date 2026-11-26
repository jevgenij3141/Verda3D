#pragma once

#include "scene_mesh.h"
#include <cstdint>

class FluidSimulation {
public:
    FluidSimulation(const std::vector<Model>& models, DynamicsSettings settings);
    bool active() const { return !sources.empty(); }
    std::size_t particleCount() const { return particles.size(); }
    void advance(double seconds, const std::vector<Model>& models);
    Mesh surface() const;

private:
    struct Source { std::size_t model; Transform transform; EmitterSettings settings; double pending = 0; };
    struct Particle { Vec3 position, velocity; double radius, viscosity, remaining; };
    struct Obstacle { std::size_t model; Transform transform; ModelCollider collider; };
    std::vector<Source> sources;
    std::vector<Particle> particles;
    std::vector<Obstacle> obstacles;
    DynamicsSettings settings;
    double accumulator = 0;
    std::uint32_t randomState = 1;
    double maxRadius = minParticleRadius;
    FogQuality quality = FogQuality::Fast;

    double random();
    void step(double seconds);
};

Mesh createWaterDish();
