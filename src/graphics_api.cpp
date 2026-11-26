#define GRAPHICS_IMPLEMENTATION
#include "graphics_api.h"

#include <array>
#include <string_view>
#include <vector>

namespace {
GraphicsApi driver;
GraphicsBackend backendInUse = GraphicsBackend::OpenGL;
HMODULE module = nullptr;

std::filesystem::path settingsPath() {
    return executableDirectory() / L"graphics.ini";
}

bool runVulkanProbe() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!length || length >= path.size()) return false;
    std::wstring command = L"\"" + std::wstring(path.data(), length) + L"\" --probe-vulkan";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    const DWORD result = WaitForSingleObject(process.hProcess, 5500);
    DWORD code = 1;
    if (result == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hProcess);
    return code == 0;
}
} // namespace

const GraphicsApi& graphics() { return driver; }
GraphicsBackend activeGraphicsBackend() { return backendInUse; }

std::filesystem::path executableDirectory() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!length || length >= path.size()) return {};
    return std::filesystem::path(path.data()).parent_path();
}

GraphicsBackend preferredGraphicsBackend() {
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"Graphics", L"Backend", L"OpenGL", value, DWORD(std::size(value)), settingsPath().c_str());
    return _wcsicmp(value, L"Vulkan") == 0 ? GraphicsBackend::Vulkan : GraphicsBackend::OpenGL;
}

bool saveGraphicsBackend(GraphicsBackend backend) {
    return WritePrivateProfileStringW(L"Graphics", L"Backend", backend == GraphicsBackend::Vulkan ? L"Vulkan" : L"OpenGL",
                                      settingsPath().c_str()) != FALSE;
}

bool initializeGraphics(GraphicsBackend backend) {
    if (backend == backendInUse && module) return true;
    const bool vulkan = backend == GraphicsBackend::Vulkan;
    std::filesystem::path path;
    if (vulkan) {
        path = executableDirectory() / L"vulkan" / L"opengl32.dll";
        if (!SetEnvironmentVariableW(L"GALLIUM_DRIVER", L"zink")) return false;
    } else {
        std::array<wchar_t, MAX_PATH> system{};
        const UINT count = GetSystemDirectoryW(system.data(), UINT(system.size()));
        if (!count || count >= system.size()) return false;
        path = std::filesystem::path(system.data()) / L"opengl32.dll";
    }
    const HMODULE candidateModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!candidateModule) return false;
    GraphicsApi candidate;
    bool valid = true;
#define GRAPHICS_LOAD(name) \
    candidate.name = graphicsFunction<decltype(candidate.name)>(GetProcAddress(candidateModule, #name)); \
    valid = valid && candidate.name != nullptr;
    EDITOR_GL_FUNCTIONS(GRAPHICS_LOAD)
#undef GRAPHICS_LOAD
    candidate.choosePixelFormat = graphicsFunction<decltype(candidate.choosePixelFormat)>(
        GetProcAddress(candidateModule, "wglChoosePixelFormat"));
    candidate.setPixelFormat = graphicsFunction<decltype(candidate.setPixelFormat)>(
        GetProcAddress(candidateModule, "wglSetPixelFormat"));
    candidate.swapBuffers = graphicsFunction<decltype(candidate.swapBuffers)>(
        GetProcAddress(candidateModule, "wglSwapBuffers"));
    if (!valid || !candidate.choosePixelFormat || !candidate.setPixelFormat || !candidate.swapBuffers) {
        FreeLibrary(candidateModule);
        return false;
    }
    if (module) FreeLibrary(module);
    module = candidateModule;
    driver = candidate;
    backendInUse = backend;
    return true;
}

bool findWorkingVulkanDriver() {
    if (GetEnvironmentVariableW(L"VK_LOADER_DRIVERS_SELECT", nullptr, 0)) return runVulkanProbe();
    bool nvidia = false, amd = false;
    for (DWORD i = 0; ; ++i) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, i, &adapter, 0)) break;
        const std::wstring_view id(adapter.DeviceID);
        nvidia = nvidia || id.find(L"VEN_10DE") != std::wstring_view::npos;
        amd = amd || id.find(L"VEN_1002") != std::wstring_view::npos;
    }
    std::vector<const wchar_t*> filters;
    if (nvidia) filters.push_back(sizeof(void*) == 8 ? L"nv-vk64.json" : L"nv-vk32.json");
    if (amd) filters.push_back(sizeof(void*) == 8 ? L"amd-vulkan64.json" : L"amd-vulkan32.json");
    filters.push_back(nullptr);
    // Isolate the probe because an incompatible Vulkan ICD can stall during context creation.
    for (const auto* filter : filters) {
        SetEnvironmentVariableW(L"VK_LOADER_DRIVERS_SELECT", filter);
        if (runVulkanProbe()) return true;
    }
    const auto* manifestName = sizeof(void*) == 8 ? L"lvp_icd.x86_64.json" : L"lvp_icd.x86.json";
    const auto manifest = executableDirectory() / L"vulkan" / manifestName;
    if (GetFileAttributesW(manifest.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    SetEnvironmentVariableW(L"VK_DRIVER_FILES", manifest.c_str());
    SetEnvironmentVariableW(L"VK_LOADER_DRIVERS_SELECT", manifestName);
    SetEnvironmentVariableW(L"LIBGL_ALWAYS_SOFTWARE", L"1");
    return runVulkanProbe();
}

bool setGraphicsPixelFormat(HDC dc, GraphicsTarget target) {
    PIXELFORMATDESCRIPTOR spec{};
    spec.nSize = sizeof(spec);
    spec.nVersion = 1;
    spec.dwFlags = PFD_SUPPORT_OPENGL | (target == GraphicsTarget::Window
        ? PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER : PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI);
    spec.iPixelType = PFD_TYPE_RGBA;
    spec.cColorBits = 32;
    spec.cDepthBits = 24;
    const int format = graphics().choosePixelFormat(dc, &spec);
    return format && graphics().setPixelFormat(dc, format, &spec);
}

HGLRC createGraphicsContext(HDC dc, GraphicsTarget target) {
    return setGraphicsPixelFormat(dc, target) ? graphics().wglCreateContext(dc) : nullptr;
}
