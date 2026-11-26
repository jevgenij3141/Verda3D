#pragma once

#include "material_editor.h"
#include "scene.h"

#include <atomic>
#include <memory>
#include <mutex>

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
    std::vector<Model> volumes;
    std::vector<MaterialEdit> materials;
    int width = 0;
    int height = 0;
    int samples = 24;
    int causticPhotons = 0;
    double timeSeconds = 0;
};

struct RenderTile {
    int x, y, width, height;
};

struct RenderPreview {
    std::mutex mutex;
    int thumbnailStep = 0;
    std::vector<unsigned char> thumbnail;
    std::vector<RenderTile> completedTiles;
};

struct RenderJob {
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<int> rows{0};
    std::atomic<bool> tracingPhotons{false};
    std::vector<unsigned char> pixels;
    std::unique_ptr<RenderPreview> preview;
    bool failed = false;
};

void renderGlobal(const TraceScene& scene, RenderJob& job) noexcept;
