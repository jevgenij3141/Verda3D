#pragma once

#include "material_editor.h"
#include "scene.h"

#include <atomic>

struct TraceCamera {
    Vec3 eye{}, right{}, up{}, back{};
    double focal = 1;
    double orthoScale = 1;
    bool perspective = true;
};

struct TraceScene {
    TraceCamera camera;
    std::vector<Model> boxes;
    std::vector<Model> lights;
    std::vector<MaterialEdit> materials;
    int width = 0;
    int height = 0;
    int samples = 24;
    int causticPhotons = 0;
};

struct RenderJob {
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<int> rows{0};
    std::atomic<bool> tracingPhotons{false};
    std::vector<unsigned char> pixels;
    bool failed = false;
};

void renderGlobal(const TraceScene& scene, RenderJob& job) noexcept;
