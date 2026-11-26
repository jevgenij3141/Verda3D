#pragma once

#include "material_lighting.h"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

struct TextureImage {
    int width = 0;
    int height = 0;
    std::wstring filename;
    std::vector<unsigned char> pixels;
};

enum class MaterialPreset { None, Ocean, Glass, Ground, Brick, Ice, Wood, Steel, Concrete, Marble, Sand, Checker };

struct Optics {
    double transmission = 0;
    double index = 1.5;
    double causticStrength = 1;
};

struct MaterialEdit {
    Surface surface;
    TextureImage image;
    MaterialPreset preset = MaterialPreset::None;
    std::optional<Optics> optics{};
    int repeatX = 1, repeatY = 1;
};

constexpr int maxTextureRepeat = 256;

inline double repeatedTextureCoordinate(double coordinate, int repeat) {
    if (!std::isfinite(coordinate)) return 0;
    const double tiled = (coordinate - std::floor(coordinate)) * repeat;
    return tiled - std::floor(tiled);
}

inline const unsigned char* texturePixel(const MaterialEdit& material, Vec2 uv) {
    const auto& image = material.image;
    if (image.pixels.empty()) return nullptr;
    const int x = std::min(image.width - 1, int(repeatedTextureCoordinate(uv.x, material.repeatX) * image.width));
    const int y = std::min(image.height - 1, int(repeatedTextureCoordinate(uv.y, material.repeatY) * image.height));
    return image.pixels.data() + (std::size_t(y) * image.width + x) * 4;
}

inline Optics materialOptics(const MaterialEdit& material) {
    if (material.optics) return *material.optics;
    switch (material.preset) {
    case MaterialPreset::Glass: return {.93, 1.50, 1};
    case MaterialPreset::Ice: return {.65, 1.31, 1};
    case MaterialPreset::Ocean: return {.35, 1.33, 1};
    default: return {};
    }
}

bool editMaterial(HWND owner, int number, const MaterialEdit& current, MaterialEdit& result);
