#include "dynamics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr double fixedStep = 1.0 / 120;
constexpr int solverIterations = 4, maxSubsteps = 24;
constexpr double minimumColliderHalfSize = .0125, restSpeed = .35;

Vec3 limitedVelocity(Vec3 velocity) {
    const double speed = std::sqrt(dot(velocity, velocity));
    return speed > maxBodyVelocity ? velocity * (maxBodyVelocity / speed) : velocity;
}
}

DynamicsSimulation::DynamicsSimulation(const std::vector<Model>& models, DynamicsSettings settings, std::span<const int> automatic)
    : settings(settings) {
    for (std::size_t i = 0; i < models.size(); ++i) {
        const auto& model = models[i];
        const auto& collision = model.collision;
        if (!isGeometry(model.kind) || collision.role == CollisionRole::Ignore || !objectVisible(models, int(i))) continue;
        const auto bounds = modelBounds(model);
        const Vec3 size = (bounds.max - bounds.min) * .5;
        const Vec3 half{std::max(size.x, minimumColliderHalfSize), std::max(size.y, minimumColliderHalfSize),
                        std::max(size.z, minimumColliderHalfSize)};
        const auto center = bounds.center();
        const bool dynamic = collision.role == CollisionRole::Dynamic ||
            (collision.role == CollisionRole::Automatic && std::ranges::find(automatic, int(i)) != automatic.end());
        if (dynamic) dynamicModels.push_back(i);
        minHalfExtent = std::min({minHalfExtent, half.x, half.y, half.z});
        bodies.push_back({i, center, half, center - model.transform.position,
                          dynamic ? limitedVelocity(collision.velocity) : Vec3{},
                          dynamic ? 1 / collision.mass : 0, collision.bounce, collision.friction});
    }
    order.resize(bodies.size());
    std::iota(order.begin(), order.end(), 0);
}

void DynamicsSimulation::advance(double seconds) {
    if (!hasBodies()) return;
    accumulator += seconds;
    while (accumulator + 1e-10 >= fixedStep) {
        double speed = 0;
        for (const auto& body : bodies)
            if (body.inverseMass > 0) speed = std::max(speed, std::sqrt(dot(body.velocity, body.velocity)));
        const int substeps = std::clamp(int(std::ceil((speed + settings.gravity * fixedStep) * fixedStep / minHalfExtent)),
                                        1, maxSubsteps);
        for (int i = 0; i < substeps; ++i) step(fixedStep / substeps);
        accumulator = std::max(0.0, accumulator - fixedStep);
    }
}

void DynamicsSimulation::apply(std::vector<Model>& models) const {
    for (const auto& body : bodies)
        if (body.inverseMass > 0) models[body.model].transform.position = body.center - body.centerOffset;
}

void DynamicsSimulation::step(double seconds) {
    for (auto& body : bodies) {
        if (body.inverseMass == 0) continue;
        body.velocity.y -= settings.gravity * seconds;
        body.velocity = limitedVelocity(body.velocity);
        body.center = body.center + body.velocity * seconds;
    }
    for (int iteration = 0; iteration < solverIterations; ++iteration) {
        if (settings.ground)
            for (auto& body : bodies) groundContact(body);
        std::ranges::sort(order, {}, [&](std::size_t i) { return bodies[i].center.x - bodies[i].halfExtent.x; });
        for (std::size_t i = 0; i < order.size(); ++i) {
            auto& a = bodies[order[i]];
            for (std::size_t j = i + 1; j < order.size(); ++j) {
                auto& b = bodies[order[j]];
                if (b.center.x - b.halfExtent.x > a.center.x + a.halfExtent.x) break;
                if (a.inverseMass + b.inverseMass > 0) contact(a, b);
            }
        }
    }
    if (settings.ground)
        for (auto& body : bodies) groundContact(body);
}

void DynamicsSimulation::groundContact(Body& body) const {
    if (body.inverseMass == 0 || body.center.y - body.halfExtent.y >= settings.groundHeight) return;
    body.center.y = settings.groundHeight + body.halfExtent.y;
    if (body.velocity.y >= 0) return;
    const double incoming = -body.velocity.y;
    body.velocity.y = incoming >= restSpeed ? incoming * body.bounce : 0;
    const double horizontal = std::hypot(body.velocity.x, body.velocity.z);
    const double friction = horizontal > 0
        ? std::max(0.0, 1 - (incoming + body.velocity.y) * body.friction / horizontal) : 0;
    body.velocity.x *= friction;
    body.velocity.z *= friction;
}

void DynamicsSimulation::contact(Body& a, Body& b) const {
    const Vec3 offset = b.center - a.center;
    const Vec3 combined = a.halfExtent + b.halfExtent;
    const double positions[] = {offset.x, offset.y, offset.z};
    const double depth[] = {combined.x - std::abs(offset.x), combined.y - std::abs(offset.y),
                            combined.z - std::abs(offset.z)};
    if (std::min({depth[0], depth[1], depth[2]}) <= 0) return;
    const auto axis = std::min_element(std::begin(depth), std::end(depth)) - depth;
    const Vec3 normal = axes[axis] * (positions[axis] >= 0 ? 1 : -1);
    const double mass = a.inverseMass + b.inverseMass;
    const Vec3 correction = normal * (depth[axis] / mass);
    a.center = a.center - correction * a.inverseMass;
    b.center = b.center + correction * b.inverseMass;
    const Vec3 relative = b.velocity - a.velocity;
    const double approach = dot(relative, normal);
    if (approach >= 0) return;
    const double bounce = -approach >= restSpeed ? std::max(a.bounce, b.bounce) : 0;
    const double normalImpulse = -(1 + bounce) * approach / mass;
    Vec3 impulse = normal * normalImpulse;
    const Vec3 slip = relative - normal * approach;
    const double tangentSpeed = std::sqrt(dot(slip, slip));
    if (tangentSpeed > 0) {
        const double friction = std::sqrt(a.friction * b.friction);
        impulse = impulse - slip * (std::min(tangentSpeed / mass, friction * normalImpulse) / tangentSpeed);
    }
    a.velocity = a.velocity - impulse * a.inverseMass;
    b.velocity = b.velocity + impulse * b.inverseMass;
}
