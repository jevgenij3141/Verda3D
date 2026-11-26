#pragma once

#include "animation.h"
#include "lighting_dialog.h"
#include "material_editor.h"

enum class ViewportMode { Object, Wireframe, Software };

struct SavedView {
    Vec3 back, right, up, target;
    double zoom = 1;
    int perspective = 0, orbited = 0, camera = -1;
    bool showRenderFrame = false;
    ViewportMode mode = ViewportMode::Object;
};

struct SceneDocument {
    std::vector<Model> models;
    std::vector<MaterialEdit> materials;
    std::array<SavedView, 4> views;
    RenderSettings rendering;
    int activeView = 0, selected = -1;
    TimelineSettings timeline;
    DynamicsSettings dynamics;
};

bool writeScene(const wchar_t* filename, SceneDocument& scene);
bool readScene(const wchar_t* filename, SceneDocument& scene);
