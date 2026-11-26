#pragma once

#include "scene.h"

bool importObj(const wchar_t* filename, std::vector<Model>& models, std::wstring& error);
bool exportObj(const wchar_t* filename, const std::vector<Model>& models);
