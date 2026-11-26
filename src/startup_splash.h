#pragma once

#include <windows.h>
#include <thread>

inline constexpr wchar_t editorName[] = L"Verda 3D";

class StartupSplash {
public:
    explicit StartupSplash(bool enabled);
    ~StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;
    void finish();

private:
    HANDLE ready = nullptr;
    std::thread animation;
};
