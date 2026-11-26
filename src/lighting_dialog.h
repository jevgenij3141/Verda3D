#pragma once

#include "scene.h"

#include <windows.h>

enum class RenderMode { Software, GlobalIllumination };
constexpr int minRenderSamples = 1, maxRenderSamples = 65'536;
constexpr int minShadowSize = 16;
constexpr int maxShadowSize = 1024;
constexpr int minCausticPhotons = 5000;
constexpr int maxCausticPhotons = 500000;
constexpr int minRenderDimension = 64, maxRenderDimension = 4096;

struct RenderSettings {
    RenderMode mode = RenderMode::Software;
    int samples = 24;
    bool caustics = false;
    int causticPhotons = 60000;
    int width = 1280, height = 720;
};

bool setupRender(HWND owner, RenderSettings& settings);
bool editViewportSettings(HWND owner, bool& showRenderFrame);
bool editGraphicsConfiguration(HWND owner, bool& useVulkan, const wchar_t* activeBackend);
bool editSceneLight(HWND owner, ObjectKind kind, Light& light);
bool editLightIntensity(HWND owner, double& intensity);
bool editLightColor(HWND owner, Color& color);
bool editShadowSize(HWND owner, int& size);
bool editObjectName(HWND owner, std::string& name);
bool editObjectTransform(HWND owner, ObjectKind kind, Transform& transform);
bool editWholeNumber(HWND owner, const wchar_t* title, const wchar_t* label, int& value, int low, int high);
bool chooseSmoothModifier(HWND owner);
bool editCollision(HWND owner, CollisionSettings* body, DynamicsSettings& dynamics);
bool editWaterEmitter(HWND owner, EmitterSettings& settings);
