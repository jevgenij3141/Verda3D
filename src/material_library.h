#pragma once

#include "material_editor.h"
#include <array>

constexpr std::size_t presetCount = std::size_t(MaterialPreset::Amber);
const wchar_t* presetName(MaterialPreset preset);
MaterialEdit makePreset(MaterialPreset preset);
MaterialPreset chooseMaterialPreset(HWND owner);
Vec3 shadeNormal(MaterialPreset preset, Vec3 normal, Vec3 point);
bool usesSurfaceRipples(MaterialPreset preset);
bool isCrystalPreset(MaterialPreset preset);

struct CrystalFacet {
    std::array<Vec2, 4> corners;
    std::size_t count;
    Color color;
};
std::vector<CrystalFacet> crystalPreview(const MaterialEdit& material);
void drawCrystalPreview(HDC dc, RECT area, const MaterialEdit& material);
