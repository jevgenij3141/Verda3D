#pragma once

#include "material_editor.h"

constexpr std::size_t presetCount = 11;
const wchar_t* presetName(MaterialPreset preset);
MaterialEdit makePreset(MaterialPreset preset);
MaterialPreset chooseMaterialPreset(HWND owner);
Vec3 shadeNormal(MaterialPreset preset, Vec3 normal, Vec3 point);
