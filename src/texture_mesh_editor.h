#pragma once

#include "material_editor.h"
#include "scene.h"

using TextureUvs = std::vector<std::array<Vec2, 4>>;

struct TextureChoice {
    int material;
    const MaterialEdit* edit;
    std::wstring name;
};

TextureUvs unfoldTextureUvs(const Mesh& mesh);
bool hasStackedTextureUvs(const Mesh& mesh, const TextureUvs* layout = nullptr);

bool editTextureMesh(HWND owner, const Mesh& mesh, const TextureUvs& defaults, bool startWithDefaults,
                     const std::vector<TextureChoice>& textures, int& material, TextureUvs& result);
