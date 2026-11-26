#pragma once

#include <windows.h>
#include <vector>

bool saveRenderPng(HWND owner, int width, int height, std::vector<unsigned char> bottomUp);
