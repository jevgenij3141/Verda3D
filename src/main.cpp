#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <GL/gl.h>

#include "dynamics.h"
#include "fluid.h"
#include "global_renderer.h"
#include "graphics_api.h"
#include "image_output.h"
#include "lighting_dialog.h"
#include "material_library.h"
#include "mesh_cut.h"
#include "obj_io.h"
#include "scene_io.h"
#include "shadow_map.h"
#include "startup_splash.h"
#include "text_encoding.h"
#include "texture_mesh_editor.h"
#include "video_dialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct Rect {
    int x, y, w, h;
    bool contains(Vec2 p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
    Vec2 center() const { return {x + w * 0.5, y + h * 0.5}; }
};

constexpr Color axisColors[] = {{1.0f, .34f, .36f}, {.37f, .86f, .53f}, {.34f, .63f, 1.0f}};
constexpr const char* axisNames[] = {"X", "Y", "Z"};
constexpr Color heading{.89f, .92f, .97f};
constexpr Color muted{.56f, .62f, .71f};
constexpr Color accent{.43f, .76f, 1.0f};
constexpr Color meshAccent{1.0f, .54f, .20f};
constexpr Color fogAccent{.68f, .82f, .92f};
constexpr int toolbarWidth = 56;
constexpr int sidebarWidth = 218;
constexpr int headerHeight = 36;
constexpr int statusHeight = 32;
constexpr int materialGridTop = 38, materialTileWidth = 98, materialTileHeight = 85;
constexpr int timelineHeight = 91;
constexpr int modelRowHeight = 32;
constexpr int modelListTop = 94;
constexpr int fogControlsTop = 65, fogControlSpacing = 27, fogControlHeight = 23;
constexpr int fogQualityRow = 5, fogShadowsRow = 6;
constexpr std::array fogQualityNames{"Fast", "Balanced", "High"};
constexpr std::array fogQualityMenuNames{L"Fast", L"Balanced", L"High"};
constexpr int fogPropertiesHeight = fogControlsTop + fogControlSpacing * fogShadowsRow + fogControlHeight + 12;
constexpr LONG minWindowWidth = 660, minWindowHeight = 540;
constexpr Color lightAccent{1.0f, .79f, .39f};
constexpr int bitmapSizes[] = {64, 128, 256};

void color(Color c) { glColor3f(c.r, c.g, c.b); }
void vertex(Vec2 p) { glVertex2d(p.x, p.y); }
void vertex(Vec3 p) { glVertex3d(p.x, p.y, p.z); }

void fill(Rect r, Color c) {
    color(c);
    glRecti(r.x, r.y, r.x + r.w, r.y + r.h);
}

void line(Vec2 a, Vec2 b, Color c, float width = 1) {
    glLineWidth(width);
    color(c);
    glBegin(GL_LINES);
    vertex(a);
    vertex(b);
    glEnd();
    glLineWidth(1);
}

void drawCheckbox(Rect row, bool checked) {
    fill({row.x, row.y + 3, 15, 15}, {.22f, .27f, .35f});
    if (!checked) return;
    const Vec2 origin{double(row.x), double(row.y)};
    line(origin + Vec2{3, 9}, origin + Vec2{6, 13}, accent, 2);
    line(origin + Vec2{6, 13}, origin + Vec2{12, 6}, accent, 2);
}

void diamond(Vec2 p, double radius, Color tint) {
    color(tint);
    glBegin(GL_TRIANGLE_FAN);
    vertex(p + Vec2{-radius, 0});
    vertex(p + Vec2{0, -radius});
    vertex(p + Vec2{radius, 0});
    vertex(p + Vec2{0, radius});
    glEnd();
}

void arrow(Vec2 start, Vec2 tip, Color c, double head, float width) {
    const Vec2 direction = (tip - start) * (1 / std::sqrt(dot(tip - start, tip - start)));
    const Vec2 base = tip - direction * head;
    const Vec2 perpendicular{-direction.y, direction.x};
    line(start, base, c, width);
    color(c);
    glBegin(GL_TRIANGLES);
    vertex(tip);
    vertex(base + perpendicular * (head * .55));
    vertex(base - perpendicular * (head * .55));
    glEnd();
}

void boxIcon(int x, int y, Color c) {
    const Vec2 points[] = {{0, 5}, {9, 0}, {18, 5}, {9, 10}, {0, 15}, {9, 20}, {18, 15}};
    constexpr int lines[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4},
                                {4, 5}, {5, 6}, {6, 2}, {3, 5}};
    const Vec2 origin{double(x), double(y)};
    for (const auto& edge : lines) line(origin + points[edge[0]], origin + points[edge[1]], c);
}

void modelingIcon(Vec2 center, Color tint) {
    line(center + Vec2{-7, 8}, center + Vec2{-7, -6}, tint, 2);
    line(center + Vec2{-13, -7}, center + Vec2{-1, -7}, tint, 4);
    constexpr Vec2 saw[] = {{0, 6}, {4, 8}, {12, -2}, {13, -8}, {8, -5}, {2, 1}, {4, 1}, {0, 6}};
    for (std::size_t i = 1; i < std::size(saw); ++i) line(center + saw[i - 1], center + saw[i], tint);
}

void cameraIcon(Vec2 center, Color tint) {
    const int x = int(center.x), y = int(center.y);
    line({double(x - 11), double(y - 6)}, {double(x + 3), double(y - 6)}, tint);
    line({double(x + 3), double(y - 6)}, {double(x + 3), double(y + 6)}, tint);
    line({double(x + 3), double(y + 6)}, {double(x - 11), double(y + 6)}, tint);
    line({double(x - 11), double(y + 6)}, {double(x - 11), double(y - 6)}, tint);
    line(center + Vec2{3, -2}, center + Vec2{10, -6}, tint);
    line(center + Vec2{10, -6}, center + Vec2{10, 6}, tint);
    line(center + Vec2{10, 6}, center + Vec2{3, 2}, tint);
}

struct View {
    const char* name;
    Vec3 back;
    Vec3 right;
    Vec3 up;
    int gridNormal;
    bool perspective;
    Rect rect{};
    Vec3 target{};
    double zoom = 1;
    bool orbited = false;
    int camera = -1;
    bool showRenderFrame = false;
    ViewportMode mode = ViewportMode::Object;
    double outputScale = 0;

    View(const char* name, Vec3 from, Vec3 vertical, int gridNormal, bool perspective = false)
        : name(name), back(normalized(from)), right(normalized(cross(vertical, back))),
          up(cross(back, right)), gridNormal(gridNormal), perspective(perspective) {}

    Vec3 eye() const { return target + back * (perspective ? 15 / zoom : std::max(150.0, 8 / zoom)); }
    double focal() const {
        return outputScale > 0 ? outputScale : rect.h * .5 / std::tan(22.5 * pi / 180) * (camera >= 0 ? zoom : 1);
    }
    double orthoScale() const { return outputScale > 0 ? outputScale : std::min(rect.w, rect.h) * zoom / 11; }
    double depth(Vec3 p) const { return dot(eye() - p, back); }
    Vec2 project(Vec3 p) const {
        const Vec3 offset = p - eye();
        const double scale = perspective ? focal() / depth(p) : orthoScale();
        return rect.center() + Vec2{dot(offset, right), -dot(offset, up)} * scale;
    }
    Ray ray(Vec2 p) const {
        const Vec2 offset = p - rect.center();
        const double scale = perspective ? focal() : orthoScale();
        const Vec3 lateral = right * (offset.x / scale) - up * (offset.y / scale);
        if (perspective) return {eye(), lateral - back};
        return {eye() + lateral, back * -1};
    }
    void applyCamera() const {
        const Vec3 position = eye();
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        if (perspective) {
            const double x = rect.w * .5 * nearPlane / focal();
            const double y = rect.h * .5 * nearPlane / focal();
            glFrustum(-x, x, -y, y, nearPlane, farPlane);
        } else {
            const double x = rect.w * .5 / orthoScale();
            const double y = rect.h * .5 / orthoScale();
            glOrtho(-x, x, -y, y, nearPlane, farPlane);
        }
        const GLdouble matrix[] = {
            right.x, up.x, back.x, 0, right.y, up.y, back.y, 0, right.z, up.z, back.z, 0,
            -dot(right, position), -dot(up, position), -dot(back, position), 1,
        };
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixd(matrix);
    }
};

constexpr const char* viewportModeNames[] = {"Object", "Wireframe", "Software"};

double renderFrameFit(const Rect& viewport, int width, int height) {
    return std::min(double(viewport.w) / width, double(viewport.h) / height);
}

View renderView(View view, int width, int height) {
    const double fit = renderFrameFit(view.rect, width, height);
    view.outputScale = (view.perspective ? view.focal() : view.orthoScale()) / fit;
    view.rect = {0, 0, width, height};
    return view;
}

const std::array<View, 4> defaultViews{{
    {"Perspective", {8, 6, 10}, {0, 1, 0}, 1, true},
    {"Top", {0, 1, 0}, {0, 0, -1}, 1},
    {"Front", {0, 0, 1}, {0, 1, 0}, 2},
    {"Right", {1, 0, 0}, {0, 1, 0}, 0},
}};

void lightIcon(Vec2 center, ObjectKind kind, Color tint, double radius) {
    color(tint);
    const int points = kind == ObjectKind::Spot ? 3 : 16;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < points; ++i) {
        const double angle = i * 2 * pi / points - pi / 2;
        vertex(center + Vec2{std::cos(angle), std::sin(angle)} * radius);
    }
    glEnd();
    if (kind == ObjectKind::Spot) return;
    for (int i = 0; i < 8; ++i) {
        const double angle = i * pi / 4;
        const Vec2 outward{std::cos(angle), std::sin(angle)};
        line(center + outward * (radius + 2), center + outward * (radius + 5), tint);
    }
}

struct Handle { Vec2 start, tip; bool visible = false; };
struct PlaneHandle {
    std::array<Vec2,4> corners;
    bool visible = false;
};
constexpr double cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
enum class Tool { Move, Scale, Rotate, Extrude, Bridge, Select };
bool selectsFaces(Tool tool) { return tool == Tool::Extrude || tool == Tool::Bridge; }
enum class DragKind { Transform, Pan, Orbit, Extrude, Component, Marquee };
enum class ComponentMode { Vertex, Edge, Polygon };
constexpr const char* componentNames[] = {"Vertex", "Edge", "Polygon"};
constexpr const char* toolNames[] = {"Move", "Scale", "Rotate", "Extrude", "Bridge Faces", "Select"};
constexpr UINT moveCommand = 201;
constexpr UINT scaleCommand = 202;
constexpr UINT rotateCommand = 203;
constexpr UINT renderCommand = 204;
constexpr UINT exitCommand = 205;
constexpr UINT resetViewCommand = 206;
constexpr UINT setupRenderCommand = 207;
constexpr UINT extrudeCommand = 208;
constexpr UINT openSceneCommand = 209;
constexpr UINT saveSceneCommand = 210;
constexpr UINT libraryCommand = 211;
constexpr UINT newSceneCommand = 212, saveAsCommand = 213, importObjCommand = 214, exportObjCommand = 215;
constexpr UINT undoCommand = 216, redoCommand = 217, duplicateCommand = 218, deleteCommand = 219;
constexpr UINT renameCommand = 220, visibilityCommand = 221, inspectCommand = 222, frameCommand = 223;
constexpr UINT groupCommand = 227, ungroupCommand = 228, cutCommand = 229;
constexpr UINT savePngCommand = 224;
constexpr UINT assignCameraCommand = 225;
constexpr UINT renderVideoCommand = 226;
constexpr UINT viewportSettingsCommand = 230;
constexpr UINT convertMeshCommand = 231, shadeSmoothCommand = 232, shadeFlatCommand = 233;
constexpr UINT configureGraphicsCommand = 234;
constexpr UINT bridgeCommand = 235;
constexpr UINT materialWindowCommand = 236;
constexpr UINT selectCommand = 237;
constexpr UINT cancelRenderMessage = WM_APP + 1;
constexpr UINT_PTR renderTimer = 10;
constexpr UINT renderPreviewIntervalMs = 80;
constexpr std::size_t undoHistoryLimit = 32;
constexpr std::size_t undoMeshBudgetBytes = 64 * 1024 * 1024;
constexpr UINT_PTR timelineTimer = 11;
constexpr UINT_PTR dynamicsTimer = 12;
constexpr UINT dynamicsTimerIntervalMs = 16;
constexpr int dynamicsMenuRowHeight = 33;
constexpr int ringSegments = 64;

struct Material {
    MaterialEdit edit;
    GLuint texture = 0;
    explicit Material(Color tint) { edit.surface.tint = tint; }
    ~Material() { if (texture) glDeleteTextures(1, &texture); }
};
using MaterialSlot = std::shared_ptr<const Material>;

std::vector<MaterialSlot> defaultMaterials() {
    const Color swatches[] = {{.78f, .82f, .88f}, {.82f, .55f, .42f}, {.43f, .65f, .90f},
                             {.47f, .74f, .60f}, {.82f, .72f, .46f}, {.66f, .55f, .80f}};
    std::vector<MaterialSlot> items;
    for (Color color : swatches) items.push_back(std::make_shared<Material>(color));
    return items;
}

void beginMaterial(const Material* material) {
    if (!material || !material->texture) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, material->texture);
}

void drawMaterialSphere(const Material& material, Vec2 center, bool highlight) {
    const Vec3 light = normalized({-.45, .65, 1});
    const Surface& surface = material.edit.surface;
    constexpr int segments = 24;
    constexpr double radius = 22;
    if (!highlight) beginMaterial(&material);
    for (int row = 0; row < segments; ++row) {
        glBegin(GL_TRIANGLE_STRIP);
        for (int col = 0; col <= segments; ++col) {
            const double u = double(col) / segments;
            const double longitude = (u - .5) * pi;
            for (const int ring : {row, row + 1}) {
                const double v = double(ring) / segments;
                const double latitude = (.5 - v) * pi;
                const Vec3 normal{std::cos(latitude) * std::sin(longitude), std::sin(latitude),
                                  std::cos(latitude) * std::cos(longitude)};
                const Lighting lighting = illuminate(surface, normal, light, {0, 0, 1});
                if (highlight) color({lighting.highlight, lighting.highlight, lighting.highlight});
                else color({surface.tint.r * lighting.base, surface.tint.g * lighting.base,
                            surface.tint.b * lighting.base});
                glTexCoord2d(u * material.edit.repeatX, v * material.edit.repeatY);
                vertex(center + Vec2{normal.x, -normal.y} * radius);
            }
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

struct RenderFrame {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
    std::wstring status;
    int progress = -1;
    bool complete = true;
    std::size_t completedPixels = 0;
};

bool copyRenderPreview(RenderFrame& frame, RenderJob& job) {
    std::vector<unsigned char> thumbnail;
    std::vector<RenderTile> tiles;
    int step = 0;
    {
        std::lock_guard<std::mutex> lock(job.preview->mutex);
        step = job.preview->thumbnailStep;
        thumbnail.swap(job.preview->thumbnail);
        tiles = job.preview->completedTiles;
        job.preview->completedTiles.clear();
    }
    if (thumbnail.empty() && tiles.empty()) return false;
    if (frame.pixels.empty()) frame.pixels.resize(std::size_t(frame.width) * frame.height * 4);
    if (!thumbnail.empty()) {
        const int thumbnailWidth = (frame.width + step - 1) / step;
        for (int y = 0; y < frame.height; ++y) {
            const auto* source = thumbnail.data() + std::size_t(y / step) * thumbnailWidth * 4;
            auto* destination = frame.pixels.data() + std::size_t(y) * frame.width * 4;
            for (int x = 0; x < frame.width; ++x)
                std::copy_n(source + (x / step) * 4, 4, destination + x * 4);
        }
    }
    for (const auto& tile : tiles) {
        for (int y = tile.y; y < tile.y + tile.height; ++y) {
            const auto offset = (std::size_t(y) * frame.width + tile.x) * 4;
            std::copy_n(job.pixels.data() + offset, tile.width * 4, frame.pixels.data() + offset);
        }
        frame.completedPixels += std::size_t(tile.width) * tile.height;
    }
    frame.progress = std::min(99, int(frame.completedPixels * 100 / (std::size_t(frame.width) * frame.height)));
    return true;
}

LRESULT CALLBACK renderWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    const auto* snapshot = reinterpret_cast<const RenderFrame*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_INITMENUPOPUP && snapshot) {
        const HMENU menu = reinterpret_cast<HMENU>(wParam);
        const bool canSave = snapshot->complete && !snapshot->pixels.empty();
        EnableMenuItem(menu, savePngCommand, MF_BYCOMMAND | (canSave ? MF_ENABLED : MF_GRAYED));
        return 0;
    }
    if ((message == WM_COMMAND && LOWORD(wParam) == savePngCommand) ||
        (message == WM_KEYDOWN && wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000))) {
        if (snapshot && snapshot->complete && !snapshot->pixels.empty() &&
            !saveRenderPng(window, snapshot->width, snapshot->height, snapshot->pixels)) {
            MessageBoxW(window, L"Could not write the PNG file.", L"Save Render", MB_OK | MB_ICONERROR);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT area{};
        GetClientRect(window, &area);
        const auto background = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!snapshot) {
            FillRect(dc, &area, background);
            EndPaint(window, &paint);
            return 0;
        }
        const auto& frame = *snapshot;
        const LONG bottom = area.bottom;
        if (frame.progress >= 0) area.bottom = std::max(0L, bottom - 24);
        if (!frame.pixels.empty()) {
            const double scale = std::min(double(area.right) / frame.width, double(area.bottom) / frame.height);
            const int w = int(frame.width * scale), h = int(frame.height * scale);
            const int x = (area.right - w) / 2, y = (area.bottom - h) / 2;
            for (const RECT margin : {RECT{0, 0, area.right, y}, RECT{0, y + h, area.right, area.bottom},
                                      RECT{0, y, x, y + h}, RECT{x + w, y, area.right, y + h}})
                FillRect(dc, &margin, background);
            BITMAPINFO bitmap{};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = frame.width;
            bitmap.bmiHeader.biHeight = frame.height;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            SetStretchBltMode(dc, HALFTONE);
            StretchDIBits(dc, x, y, w, h, 0, 0, frame.width, frame.height, frame.pixels.data(),
                          &bitmap, DIB_RGB_COLORS, SRCCOPY);
        } else {
            FillRect(dc, &area, background);
            if (!frame.status.empty()) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(220, 231, 246));
                SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(dc, frame.status.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        if (frame.progress >= 0) {
            const RECT footer{0, area.bottom, area.right, bottom};
            FillRect(dc, &footer, background);
            if (area.right > 62) {
                const RECT track{12, area.bottom + 11, area.right - 48, area.bottom + 14};
                FillRect(dc, &track, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
                const RECT filled{track.left, track.top,
                                  track.left + (track.right - track.left) * frame.progress / 100, track.bottom};
                SetDCBrushColor(dc, RGB(224, 54, 64));
                FillRect(dc, &filled, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(212, 216, 222));
                SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
                RECT percent{track.right + 4, area.bottom, area.right - 10, bottom};
                const auto label = std::to_wstring(frame.progress) + L"%";
                DrawTextW(dc, label.c_str(), -1, &percent, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_CLOSE || (message == WM_KEYDOWN && wParam == VK_ESCAPE)) {
        ShowWindow(window, SW_HIDE);
        SendMessageW(GetWindow(window, GW_OWNER), cancelRenderMessage, 0, 0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

class Editor {
public:
    bool init(HWND window);
    bool verifyRasterInitialization();
    void release();
    LRESULT onMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool dispatchVideoDialog(MSG& message) { return videoPanel && videoPanel->dispatch(message); }

private:
    HWND window{};
    HDC dc{};
    HGLRC context{};
    GLuint font{};
    HFONT uiFont{};
    mutable std::unordered_map<wchar_t, GLuint> glyphs;
    int width = 0, height = 0;
    std::array<View, 4> views = defaultViews;
    std::vector<Model> models;
    std::vector<MaterialSlot> materials = defaultMaterials();
    struct SceneState {
        std::vector<Model> models;
        std::vector<MaterialSlot> materials;
        std::array<View, 4> views;
        RenderSettings rendering;
        TimelineSettings animation;
        DynamicsSettings dynamics;
        int selected, activeView;
        std::uint64_t revision;
        std::vector<int> selection;
        std::vector<std::size_t> faceSelection;
    };
    std::vector<SceneState> undoHistory, redoHistory;
    std::optional<SceneState> interactionBefore;
    std::uint64_t revision = 0, savedRevision = 0, nextRevision = 0;
    std::wstring currentPath;
    bool interactionChanged = false;
    bool altUsedToOrbit = false;
    HWND materialWindow{};
    HDC materialDc{};
    int materialWidth = 0, materialHeight = 0, materialFirstRow = 0;
    int materialWheelDelta = 0;
    TimelineSettings timeline;
    DynamicsSettings dynamicsSettings;
    struct DynamicsPreview {
        DynamicsSimulation simulation;
        FluidSimulation fluid;
        std::vector<std::pair<std::size_t, Vec3>> original;
        std::size_t modelCount, materialCount;
        ULONGLONG lastTick = 0;
        ULONGLONG lastSurface = 0;
        int waterMaterial = -1;
        bool playing = false;
    };
    std::optional<DynamicsPreview> dynamics;
    KeyInterpolation newKeyInterpolation = KeyInterpolation::Linear;
    bool scrubbingTimeline = false;
    bool playing = false;
    ULONGLONG playbackStart = 0;
    int playbackStartFrame = 0;
    int selected = -1;
    std::vector<int> selection;
    struct SceneRow { int index, depth; };
    int selectedModifier = -1;
    int modifierListOffset = 0;
    int activeView = 0;
    int listOffset = 0;
    enum class Flyout { None, Creation, Crystals, Lighting, Transform, Modeling, Effects, Dynamics };
    Flyout flyout = Flyout::None;
    Tool tool = Tool::Move;
    std::vector<std::size_t> selectedFaces;
    struct MeshEditChange {
        ComponentMode mode;
        std::vector<std::size_t> components;
        std::vector<std::pair<std::size_t, Vec3>> positions;
        std::optional<Mesh> topology;
        ObjectKind kind = ObjectKind::Mesh;
        std::vector<std::array<Vec2, 4>> defaultUvs;
    };
    struct MeshEdit {
        Model draft;
        ComponentMode mode;
        Tool tool = Tool::Move;
        std::vector<std::array<std::size_t, 2>> edges;
        std::vector<std::size_t> components;
        std::vector<std::size_t> vertices;
        std::vector<Vec3> original;
        std::vector<MeshEditChange> undoHistory, redoHistory;
        double unitsPerPixel = 0;
    };
    std::optional<MeshEdit> meshEdit;
    HWND renderWindow{};
    RenderFrame renderFrame;
    RenderSettings renderSettings;
    std::shared_ptr<RenderJob> renderJob;
    std::thread renderWorker;
    std::unique_ptr<VideoPanel> videoPanel;
    bool renderJobGlobal = false;
    bool renderJobFog = false;
    std::vector<ShadowMap> shadowMaps;
    bool shadowMapsDirty = true;
    ULONGLONG lastShadowUpdate = 0;
    bool adjustingTransparency = false;
    struct {
        Mesh before;
        ExtrusionTopology topology;
        Vec3 localDirection{};
        double pixelsPerUnit = 1;
        double perspectiveFactor = 0;
        double minimum = -100;
        double maximum = 100;
        double distance = 0;
        bool changed = false;
        bool signedDirection = false;
        bool unavailable = false;
    } extrusion;
    bool fullscreen = false;
    LONG_PTR windowedStyle = 0;
    WINDOWPLACEMENT windowedPlacement{};
    struct {
        int edge = HTNOWHERE;
        POINT origin{};
        RECT bounds{};
    } resize;
    struct {
        int view = -1;
        int axis = -1;
        int plane = -1;
        DragKind kind = DragKind::Transform;
        Vec2 mouse{};
        Transform initialModel{};
        Vec3 initialTarget{};
        Vec3 initialBack{};
        Vec3 initialRight{};
        Vec3 initialUp{};
        bool wasOrbited = false;
        int initialCamera = -1;
        Vec2 tangent{};
        Vec3 componentPivot{};
        Vec2 marqueeEnd{};
        bool additiveMarquee = false;
    } drag;

    void layout(int w, int h);
    void paint();
    void drawView(const View& view);
    void drawGrid(const View& view);
    void drawModels(const View& view);
    void drawInterface();
    bool paintMaterials();
    void showMaterials();
    void refreshMaterials(int reveal = -1);
    static LRESULT CALLBACK materialWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void drawTimeline();
    void drawMotionPath(const View& view);
    void drawProperties();
    void drawModifiers();
    void drawLightMarkers(const View& view);
    void drawViewOverlay(const View& view);
    void drawTransformHandles(const View& view);
    void label(int x, int y, const std::string& value, Color c = heading) const;
    void overlay() const;
    void clip(Rect r) const;
    Rect toolbarButton() const { return {10, 62, 36, 36}; }
    Rect lightingButton() const { return {10, 108, 36, 36}; }
    Rect transformButton() const { return {10, 154, 36, 36}; }
    Rect modelingButton() const { return {10, 200, 36, 36}; }
    Rect boxMenu() const { return {56, 62, 150, 36 * (int(std::size(creationKinds)) + 1)}; }
    Rect crystalMenu() const {
        const auto box = boxMenu();
        return {box.x + box.w, box.y + box.h - 36, 194, 36 * int(std::size(crystalKinds))};
    }
    Rect lightingMenu() const { return {56, 108, 142, 108}; }
    Rect transformMenu() const { return {56, 154, 142, 108}; }
    Rect modelingMenu() const { return {56, 200, 170, 108}; }
    Rect selectButton() const { return {139, 6, 76, 24}; }
    Rect effectsButton() const { return {230, 6, 83, 24}; }
    Rect effectsMenu() const { return {effectsButton().x, headerHeight, 130, 36}; }
    Rect dynamicsButton() const { return {328, 6, 103, 24}; }
    Rect dynamicsMenu() const { return {dynamicsButton().x, headerHeight, 183, dynamicsMenuRowHeight * 7}; }
    Rect fullscreenButton() const { return {width - 149, 6, 136, 24}; }
    Rect resizeGrip() const { return {width - 24, height - 24, 24, 24}; }
    int timelineWidth() const { return width - sidebarWidth; }
    int timelineTop() const { return height - statusHeight - timelineHeight; }
    Rect timelinePanel() const { return {0, timelineTop(), timelineWidth(), timelineHeight}; }
    Rect timelineRuler() const { return {15, timelineTop() + 43, std::max(1, timelineWidth() - 150), 41}; }
    bool compactTimeline() const { return timelineWidth() < 520; }
    Rect timelineTransport(int i) const {
        return {compactTimeline() ? 10 + i * 23 : 14 + i * 29, timelineTop() + 10, compactTimeline() ? 21 : 26, 24};
    }
    Rect setKeyButton() const { return {compactTimeline() ? 151 : 196, timelineTop() + 10, compactTimeline() ? 56 : 68, 24}; }
    Rect keyTypeButton() const { return {compactTimeline() ? 211 : 269, timelineTop() + 10, compactTimeline() ? 66 : 88, 24}; }
    Rect removeKeyButton() const { return {compactTimeline() ? 280 : 362, timelineTop() + 10, compactTimeline() ? 16 : 24, 24}; }
    Rect timelineNumber(int row) const { return {timelineWidth() - 77, timelineTop() + 8 + row * 26, 61, 22}; }
    int materialColumns() const { return std::max(1, (materialWidth - 20) / materialTileWidth); }
    int materialVisibleRows() const { return std::max(1, (materialHeight - materialGridTop - 8) / materialTileHeight); }
    Rect materialTile(int index) const {
        return {14 + (index % materialColumns()) * materialTileWidth,
                materialGridTop + (index / materialColumns() - materialFirstRow) * materialTileHeight,
                materialTileWidth - 8, materialTileHeight - 8};
    }
    bool lightSelected() const { return selected >= 0 && isLight(models[selected].kind); }
    bool cameraSelected() const { return selected >= 0 && models[selected].kind == ObjectKind::Camera; }
    bool fogSelected() const { return selected >= 0 && isFog(models[selected].kind); }
    bool emitterSelected() const { return selected >= 0 && isEmitter(models[selected].kind); }
    bool geometrySelected() const { return selected >= 0 && isGeometry(models[selected].kind); }
    bool canRotateSelected() const { return selected >= 0 && objectVisible(models, selected) && models[selected].kind != ObjectKind::Omni; }
    Rect properties() const {
        const int h = selected < 0 ? 0 : lightSelected() ? 202 : fogSelected() ? fogPropertiesHeight :
            emitterSelected() ? 106 : geometrySelected()
            ? std::clamp(height - statusHeight - modelListTop - 53, 208, 279) : 69;
        return {width - sidebarWidth, height - statusHeight - h, sidebarWidth, h};
    }
    Rect lightSetupButton() const { const Rect r = properties(); return {r.x + 14, r.y + 33, 100, 24}; }
    Rect emitterSettingsButton() const { const Rect r = properties(); return {r.x + 14, r.y + 69, r.w - 28, 24}; }
    Rect fogOption(int row) const {
        const Rect r = properties();
        return {r.x + 104, r.y + fogControlsTop + row * fogControlSpacing, 97, fogControlHeight};
    }
    Rect fogShadowsButton() const {
        const Rect r = properties();
        return {r.x + 14, fogOption(fogShadowsRow).y, r.w - 28, fogControlHeight};
    }
    Rect inspectButton() const { const Rect r = properties(); return {r.x + 120, r.y + (geometrySelected() ? 6 : 33), 83, 24}; }
    Rect componentButton(int i) const { const Rect r = properties(); return {r.x + 14 + i * 30, r.y + 34, 26, 23}; }
    Rect planeSegmentsButton(int i) const { const Rect r = properties(); return {r.x + 110 + i * 48, r.y + 34, 45, 23}; }
    Rect textureMeshButton() const { const Rect r = properties(); return {r.x + 14, r.y + 62, 190, 23}; }
    Rect addModifierButton() const { const Rect r = properties(); return {r.x + 14, r.y + 111, 93, 24}; }
    Rect removeModifierButton() const { const Rect r = properties(); return {r.x + 112, r.y + 111, 92, 24}; }
    Rect modifiersList() const { const Rect r = properties(); return {r.x + 14, r.y + 141, 190, r.h - 178}; }
    Rect activeModifierButton() const { const Rect r = properties(); return {r.x + 14, r.y + r.h - 31, 75, 23}; }
    Rect modifierLevelsButton() const { const Rect r = properties(); return {r.x + 162, r.y + r.h - 31, 41, 23}; }
    Rect intensityButton() const { const Rect r = properties(); return {r.x + 108, r.y + 61, 95, 24}; }
    Rect lightColorButton() const { const Rect r = properties(); return {r.x + 126, r.y + 5, 77, 23}; }
    Rect shadowsButton() const { const Rect r = properties(); return {r.x + 14, r.y + 90, 189, 21}; }
    Rect bitmapOption(int i) const { const Rect r = properties(); return {r.x + 65 + 35 * i, r.y + 119, 33, 23}; }
    Rect transparencyTrack() const { const Rect r = properties(); return {r.x + 14, r.y + 169, 189, 19}; }
    Rect modelList() const { return {width - sidebarWidth, modelListTop, sidebarWidth,
                                     std::max(0, properties().y - modelListTop)}; }
    int visibleRows() const { return std::max(1, modelList().h / modelRowHeight); }
    int viewAt(Vec2 p) const;
    Rect viewportCameraButton(const View& view) const {
        return {view.rect.x + view.rect.w - 74, view.rect.y + 9, 65, 23};
    }
    Rect viewportModeButton(const View& view) const { return {view.rect.x + 9, view.rect.y + 9, 94, 23}; }
    Rect viewportNameArea(const View& view) const {
        const Rect mode = viewportModeButton(view);
        const int available = viewportCameraButton(view).x - (mode.x + mode.w) - 10;
        const int width = view.orbited || view.camera >= 0 ? 140 : 87;
        if (available >= 92) return {mode.x + mode.w + 5, mode.y, std::min(available, width), mode.h};
        return {mode.x, mode.y + mode.h + 3, std::min(view.rect.w - 18, width), mode.h};
    }
    int materialAt(Vec2 p) const;
    std::string viewName(const View& view) const;
    void syncCameras();
    void openMaterial(int index);
    void addLibraryMaterial();
    bool timelineClick(Vec2 point);
    void scrubTo(Vec2 point);
    void seekFrame(int frame, bool refreshPose = false);
    void setKey(bool remove);
    const TransformKey* currentTimelineKey() const;
    void chooseKeyInterpolation();
    void startPlayback();
    void stopPlayback();
    void tickPlayback();
    void configureDynamics();
    DynamicsSimulation createDynamicsSimulation() const;
    void toggleDynamics();
    void resetDynamics();
    void tickDynamics();
    void bakeDynamics();
    void addEmitter(bool withDish);
    void configureEmitter();
    void keepWaterSurface();
    void materialMenu(POINT screen);
    void toggleFullscreen();
    void setTool(Tool value);
    Tool activeTool() const { return meshEdit ? meshEdit->tool : tool; }
    bool toolAvailable(Tool value) const {
        return !meshEdit || value == Tool::Select || value == Tool::Move || value == Tool::Rotate || value == Tool::Scale ||
            (value == Tool::Extrude && meshEdit->mode == ComponentMode::Polygon);
    }
    void updateTransformMenu() const;
    void renderActiveView();
    void showVideoPanel();
    VideoSources videoSources() const;
    VideoFrameRenderer takeVideoSnapshot(const VideoSettings& settings) const;
    void showRenderWindow(const View& view, bool global);
    void startRayRender(View view, RenderFrame raster = {});
    void pollRender();
    void stopRender();
    void updateShadowMaps(bool interactive = false);
    bool propertyClick(Vec2 p);
    void openTextureMeshEditor();
    bool modifierClick(Vec2 p);
    bool commitModifiers(std::vector<SmoothModifier> modifiers);
    void changeTransparency(Vec2 p);
    std::array<Handle, 3> handles(const View& view) const;
    std::array<PlaneHandle, 3> planeHandles(const View& view) const;
    Vec3 extrusionCenter() const;
    std::array<Vec2, ringSegments + 1> rotationRing(const View& view, int axis) const;
    int pickRotationRing(const View& view, Vec2 point, Vec2& tangent) const;
    void addGeometry(ObjectKind kind);
    void addCamera(bool selectNew);
    void lookThroughCamera();
    void assignCamera(int viewIndex, int camera);
    void chooseViewportCamera(int viewIndex);
    void chooseViewportMode(int viewIndex);
    void resetViewportCamera(int viewIndex);
    bool saveScene(bool saveAs = false);
    void openScene();
    void newScene();
    void importModels();
    void exportModels();
    bool confirmLeaveScene();
    void updateTitle() const;
    SceneState sceneState() const;
    void restore(SceneState&& state);
    void record(SceneState&& before);
    void remember();
    void startInteraction();
    void commitInteraction();
    void undo(bool redo);
    void recordMeshChange(MeshEditChange change);
    void undoMeshChange(bool redo);
    void duplicate();
    void deleteSelection();
    void groupSelection();
    void ungroupSelection();
    void cutSelection();
    void bridgeSelection();
    bool canCut() const;
    bool canUngroup() const;
    std::vector<int> selectedRoots() const;
    std::vector<int> removeModels(const std::vector<bool>& removed);
    void transformGroup(int index, const std::vector<Model>& before);
    std::vector<SceneRow> sceneRows() const;
    void rename();
    void changeVisibility();
    void inspect();
    void frameSelection();
    void sceneMenu(POINT screen, int row);
    void addLight(ObjectKind kind);
    void addFog();
    void select(int model, bool toggle = false);
    void mouseDown(Vec2 p, bool orbit, bool toggle = false, bool marquee = false);
    void mouseMove(Vec2 p);
    void beginExtrusion(int view, Vec2 point, Vec3 direction, bool axisDrag = false);
    void updateExtrusion(Vec2 delta);
    void finishExtrusion(bool commit);
    void setPlaneSegments(int axis);
    void startMeshEdit(ComponentMode mode);
    void finishMeshEdit(bool commit);
    void convertToMesh();
    void setMeshShading(MeshShading mode);
    void beginComponentDrag(int view, Vec2 point, bool handlesOnly = false, bool toggle = false);
    void beginMarquee(int view, Vec2 point, bool additive);
    Rect marqueeBounds() const;
    void finishMarquee();
    void refreshComponentVertices();
    void pickExtrusionFace(int view, Vec2 point, std::size_t face, bool toggle);
    const Model& extrusionModel() const { return meshEdit ? meshEdit->draft : models[selected]; }
    const std::vector<std::size_t>& extrusionFaces() const { return meshEdit ? meshEdit->components : selectedFaces; }
    Vec3 componentCenter() const;
    void drawComponentMarkers(const View& view);
    void panStart(Vec2 p);
    void cancelDrag();
    void endDrag(bool commitComponent = false);
    void resizeWindow(POINT cursor);
    void finishWindowResize();
    void wheel(Vec2 p, double steps);
    void repaint() const { InvalidateRect(window, nullptr, FALSE); }
};

bool Editor::init(HWND hwnd) {
    window = hwnd;
    dc = GetDC(window);
    if (!dc) return false;

    context = createGraphicsContext(dc, GraphicsTarget::Window);
    if (!context || !wglMakeCurrent(dc, context)) return false;
    if (activeGraphicsBackend() == GraphicsBackend::Vulkan) {
        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (!renderer || std::string_view(renderer).find("zink") == std::string_view::npos) return false;
    }
    uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (!uiFont) return false;
    const auto previousFont = SelectObject(dc, uiFont);
    font = glGenLists(96);
    const bool ready = font && wglUseFontBitmapsW(dc, 32, 96, font);
    SelectObject(dc, previousFont);
    if (ready) addCamera(false);
    if (ready) updateTitle();
    return ready;
}

void Editor::release() {
    resetDynamics();
    if (materialWindow) DestroyWindow(materialWindow);
    videoPanel.reset();
    stopPlayback();
    stopRender();
    if (context) {
        interactionBefore.reset();
        undoHistory.clear();
        redoHistory.clear();
        materials.clear();
        for (const auto& glyph : glyphs) glDeleteLists(glyph.second, 1);
        if (font) glDeleteLists(font, 96);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context);
    }
    if (dc) ReleaseDC(window, dc);
    if (uiFont) DeleteObject(uiFont);
    uiFont = nullptr;
    font = 0;
    context = nullptr;
    dc = nullptr;
}

void Editor::layout(int w, int h) {
    width = w;
    height = h;
    if (geometrySelected()) {
        const int shown = std::max(1, modifiersList().h / 22);
        const int count = int(models[selected].modifiers.size());
        modifierListOffset = std::clamp(modifierListOffset, 0, std::max(0, count - shown));
    }
    constexpr int gap = 5;
    const int left = toolbarWidth + gap;
    const int top = headerHeight + gap;
    const int availableWidth = std::max(2, width - sidebarWidth - left - gap * 2);
    const int availableHeight = std::max(2, timelineTop() - top - gap * 2);
    const int leftWidth = availableWidth / 2;
    const int topHeight = availableHeight / 2;
    views[0].rect = {left, top, leftWidth, topHeight};
    views[1].rect = {left + leftWidth + gap, top, availableWidth - leftWidth, topHeight};
    views[2].rect = {left, top + topHeight + gap, leftWidth, availableHeight - topHeight};
    views[3].rect = {left + leftWidth + gap, top + topHeight + gap,
                     availableWidth - leftWidth, availableHeight - topHeight};
    listOffset = std::clamp(listOffset, 0, std::max(0, int(sceneRows().size()) - visibleRows()));
}

void Editor::label(int x, int y, const std::string& value, Color c) const {
    color(c);
    glRasterPos2i(x, y);
    if (std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= 32 && ch < 127; })) {
        glListBase(font - 32);
        glCallLists(static_cast<GLsizei>(value.size()), GL_UNSIGNED_BYTE, value.data());
        return;
    }
    for (wchar_t ch : widenUtf8(value)) {
        if (ch >= 32 && ch < 127) {
            glCallList(font + ch - 32);
            continue;
        }
        if (ch < 32 || (ch >= 0xd800 && ch <= 0xdfff)) ch = L'?';
        const auto cached = glyphs.find(ch);
        if (cached != glyphs.end()) {
            glCallList(cached->second);
            continue;
        }
        const GLuint displayList = glGenLists(1);
        const auto old = SelectObject(dc, uiFont);
        const bool ready = displayList && wglUseFontBitmapsW(dc, ch, 1, displayList);
        SelectObject(dc, old);
        if (ready) {
            glyphs.emplace(ch, displayList);
            glCallList(displayList);
        } else {
            if (displayList) glDeleteLists(displayList, 1);
            glCallList(font + L'?' - 32);
        }
    }
}

void Editor::overlay() const {
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void Editor::clip(Rect r) const {
    glScissor(r.x, height - r.y - r.h, r.w, r.h);
}

void Editor::drawGrid(const View& view) {
    constexpr int planeAxes[][2] = {{1, 2}, {0, 2}, {0, 1}};
    const Vec3 u = axes[planeAxes[view.gridNormal][0]];
    const Vec3 v = axes[planeAxes[view.gridNormal][1]];
    const double step = view.zoom < .42 ? 5 : 1;
    const int centerU = static_cast<int>(std::floor(dot(view.target, u) / step));
    const int centerV = static_cast<int>(std::floor(dot(view.target, v) / step));
    const int radius = static_cast<int>(std::ceil(15 / (view.zoom * step))) + 3;
    glBegin(GL_LINES);
    for (int i = -radius; i <= radius; ++i) {
        const int gridU = centerU + i;
        const int gridV = centerV + i;
        const Color major{.29f, .34f, .41f};
        const Color normal{.20f, .24f, .30f};
        const Color origin{.37f, .43f, .51f};
        color(gridU == 0 ? origin : gridU % 5 == 0 ? major : normal);
        vertex(u * (gridU * step) + v * ((centerV - radius) * step));
        vertex(u * (gridU * step) + v * ((centerV + radius) * step));
        color(gridV == 0 ? origin : gridV % 5 == 0 ? major : normal);
        vertex(v * (gridV * step) + u * ((centerU - radius) * step));
        vertex(v * (gridV * step) + u * ((centerU + radius) * step));
    }
    glEnd();
}

struct RasterLight { const Model& model; const ShadowMap& shadow; };

Vec3 shadeWithLights(const std::vector<RasterLight>& lights, const Surface& surface, Vec3 point,
                     Vec3 normal, Vec3 towardEye, bool highlight) {
    if (surface.shading == Shading::Unlit) return highlight ? Vec3{} : Vec3{1, 1, 1};
    const double ambient = (surface.ambient + surface.emission) / 100.0;
    Vec3 result = highlight ? Vec3{} : Vec3{ambient, ambient, ambient};
    for (const auto& source : lights) {
        const Model& light = source.model;
        if (!light.light.enabled) continue;
        if (light.kind == ObjectKind::Sky) {
            if (!highlight) {
                const double visibility = source.shadow.visibility(point, normal, light.light.shadowTransparency);
                result = result + skyRadiance(light, normal) * (surface.diffuse * .01 * visibility);
            }
            continue;
        }
        const Vec3 offset = light.transform.position - point;
        const double distance = std::sqrt(dot(offset, offset));
        if (distance < .001) continue;
        const Vec3 direction = offset * (1 / distance);
        const double cosine = dot(direction, normal);
        if (cosine <= 0) continue;
        const double power = localLightPower(light, direction * -1, distance);
        if (power <= 0) continue;
        const double amount = highlight ? specularHighlight(surface, normal, direction, towardEye)
                                        : cosine * surface.diffuse * .01;
        if (amount <= 0) continue;
        const double visibility = source.shadow.visibility(point, normal, light.light.shadowTransparency);
        result = result + rgb(light.light.color) * (amount * power * visibility);
    }
    return result;
}

void drawSurface(const View& view, const Model& model, const Surface& surface,
                 const std::vector<RasterLight>& lights, bool highlight, float opacity = 1,
                 MaterialPreset preset = MaterialPreset::None, bool baseOnly = false, Vec2 repeat = {1,1},
                 bool preview = false) {
    const Vec3 light = normalized({-.45, .80, .65});
    const bool glossy = surface.specular > 0 &&
        (surface.shading == Shading::Phong || surface.shading == Shading::BlinnPhong);
    if (highlight && !glossy) return;
    const bool perturbed = usesSurfaceRipples(preset);
    const bool lit = !lights.empty() && surface.shading != Shading::Unlit;
    const int divisions = perturbed || lit ? 24 : glossy && view.perspective && (highlight || opacity < 1) ? 12 : 1;
    const Vec3 eye = view.eye();
    const Mesh& mesh = modelMesh(model, baseOnly);
    const bool smoothNormals = !mesh.normals.empty();
    struct Sample { Vec3 position; Vec2 uv; float red, green, blue; };
    std::array<Sample, 25 * 25> samples;
    const auto emit = [&](const Sample& sample) {
        glColor4f(sample.red, sample.green, sample.blue, opacity);
        if (!highlight) glTexCoord2d(sample.uv.x, sample.uv.y);
        vertex(sample.position);
    };
    const auto drawPiece = [&](const FaceGeometry& face) {
        const Lighting lighting = illuminate(surface, face.normal, light, view.back);
        int steps = face.triangle ? 1 : divisions;
        if (preview && steps > 1) {
            const double depth = view.depth(face.origin + (face.u + face.v) * .5);
            const double scale = view.perspective ? view.focal() / std::max(1.0, depth) : view.orthoScale();
            const double span = std::sqrt(std::max(dot(face.u, face.u), dot(face.v, face.v))) * scale;
            steps = int(std::clamp(std::ceil(span / 24), 2.0, 8.0));
        }
        const auto sampleAt = [&](double s, double t) {
            const Vec3 position = face.origin + face.u * s + face.v * t;
            const Vec3 geometric = smoothNormals ? face.shade(s, t) : face.normal;
            const Vec3 normal = perturbed ? shadeNormal(preset, geometric, position) : geometric;
            const bool viewDependent = lit || perturbed || smoothNormals || highlight || opacity < 1;
            const Vec3 towardEye = view.perspective && viewDependent ? normalized(eye - position) : view.back;
            Vec3 brightness;
            if (lit) {
                brightness = shadeWithLights(lights, surface, position, normal, towardEye, highlight);
            } else {
                const Lighting local = perturbed || smoothNormals
                    ? illuminate(surface, normal, light, towardEye) : lighting;
                const double strength = highlight
                    ? (view.perspective && !perturbed ? specularHighlight(surface, normal, light, towardEye) : local.highlight)
                    : local.base;
                brightness = {strength, strength, strength};
            }
            if (highlight) return Sample{position, {}, float(brightness.x), float(brightness.y), float(brightness.z)};
            const float sheen = opacity < 1 ? specularHighlight(surface, normal, light, towardEye) : 0;
            const Vec2 uv = face.uvOrigin + face.uvU * s + face.uvV * t;
            return Sample{position, {uv.x * repeat.x, uv.y * repeat.y},
                          surface.tint.r * float(brightness.x) + sheen, surface.tint.g * float(brightness.y) + sheen,
                          surface.tint.b * float(brightness.z) + sheen};
        };
        if (face.triangle) {
            emit(sampleAt(0,0)); emit(sampleAt(1,0)); emit(sampleAt(0,1));
            return;
        }
        const int stride = steps + 1;
        for (int y = 0; y <= steps; ++y)
            for (int x = 0; x <= steps; ++x) samples[y * stride + x] = sampleAt(double(x) / steps, double(y) / steps);
        for (int y = 0; y < steps; ++y) {
            for (int x = 0; x < steps; ++x) {
                const int a = y * stride + x, b = a + stride;
                emit(samples[a]); emit(samples[a+1]); emit(samples[b+1]);
                emit(samples[a]); emit(samples[b+1]); emit(samples[b]);
            }
        }
    };
    glBegin(GL_TRIANGLES);
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const auto face = faceGeometry(model, i, baseOnly);
        if (face.needsSplit()) {
            drawPiece(face.trianglePart(0));
            drawPiece(face.trianglePart(1));
        } else drawPiece(face);
    }
    glEnd();
}

void drawModelScene(const View& view, const std::vector<Model>& models,
                    const std::vector<MaterialSlot>& materials, const std::vector<ShadowMap>& shadowMaps,
                    int selected = -1, bool cage = false, bool showTransparency = true, const Model* draft = nullptr,
                    bool preview = false) {
    std::vector<RasterLight> lights;
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (isLight(models[i].kind) && objectVisible(models, int(i))) lights.push_back({models[i], shadowMaps[i]});
    }
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1, 1);
    std::vector<const Model*> translucent;
    const auto modelAt = [&](int i) -> const Model& { return draft && i == selected ? *draft : models[i]; };
    const auto baseOnly = [&](const Model& model) { return cage && selected >= 0 && &model == &modelAt(selected); };
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = modelAt(i);
        if (!isGeometry(model.kind) || !objectVisible(models, i)) continue;
        if (model.material >= 0) {
            const Material& material = *materials[model.material];
            if (showTransparency && materialOptics(material.edit).transmission > 0) {
                translucent.push_back(&model);
                continue;
            }
            beginMaterial(&material);
            drawSurface(view, model, material.edit.surface, lights, false, 1, material.edit.preset, baseOnly(model),
                        {double(material.edit.repeatX), double(material.edit.repeatY)}, preview);
            glDisable(GL_TEXTURE_2D);
        } else {
            Surface surface;
            surface.tint = i == selected && !cage
                ? Color{.38f, .66f, .86f} : Color{.57f, .63f, .72f};
            drawSurface(view, model, surface, lights, false, 1, MaterialPreset::None, baseOnly(model), {1,1}, preview);
        }
    }
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = modelAt(i);
        if (!isGeometry(model.kind) || !objectVisible(models, i) || model.material < 0) continue;
        if (showTransparency && materialOptics(materials[model.material]->edit).transmission > 0) continue;
        const MaterialEdit& material = materials[model.material]->edit;
        drawSurface(view, model, material.surface, lights, true, 1, material.preset, baseOnly(model), {1,1}, preview);
    }
    if (!translucent.empty()) {
        std::stable_sort(translucent.begin(), translucent.end(), [&](const Model* a, const Model* b) {
            return view.depth(a->transform.position) > view.depth(b->transform.position);
        });
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        for (const Model* model : translucent) {
            const Material& material = *materials[model->material];
            beginMaterial(&material);
            const float opacity = 1.f - float(materialOptics(material.edit).transmission * .84);
            drawSurface(view, *model, material.edit.surface, lights, false, opacity, material.edit.preset, baseOnly(*model),
                        {double(material.edit.repeatX), double(material.edit.repeatY)}, preview);
            glDisable(GL_TEXTURE_2D);
        }
        glDisable(GL_CULL_FACE);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Editor::drawModels(const View& view) {
    const bool wireframe = view.mode == ViewportMode::Wireframe;
    const bool software = view.mode == ViewportMode::Software;
    const bool extrusionCage = !meshEdit && selectsFaces(tool) && selected >= 0;
    const bool cageSelected = extrusionCage || meshEdit.has_value();
    if (!wireframe)
        drawModelScene(view, models, materials, shadowMaps, software && !cageSelected ? -1 : selected,
                       cageSelected, software, meshEdit ? &meshEdit->draft : nullptr, true);
    const bool hasFaceSelection = extrusionCage && objectVisible(models, selected) && !selectedFaces.empty();
    if (!wireframe && hasFaceSelection) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glColor4f(meshAccent.r, meshAccent.g, meshAccent.b, .48f);
        glBegin(GL_TRIANGLES);
        for (const auto index : selectedFaces) {
            const auto face = faceGeometry(models[selected], index, true);
            const auto corners = face.corners();
            vertex(corners[0]); vertex(corners[1]); vertex(corners[2]);
            if (!face.triangle) { vertex(corners[0]); vertex(corners[2]); vertex(corners[3]); }
        }
        glEnd();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    std::vector<bool> chosen(models.size());
    for (int index : selection) chosen[index] = true;
    const auto drawEdges = [](const FaceGeometry& polygon) {
        const auto corners = polygon.corners();
        for (std::size_t j = 0; j < polygon.count(); ++j) {
            vertex(corners[j]);
            vertex(corners[(j+1) % polygon.count()]);
        }
    };
    if (wireframe) glDisable(GL_DEPTH_TEST);
    glBegin(GL_LINES);
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = meshEdit && i == selected ? meshEdit->draft : models[i];
        if (!isGeometry(model.kind) || !objectVisible(models, i)) continue;
        int member = i;
        while (member >= 0 && !chosen[member]) member = models[member].parent;
        if (software && member < 0) continue;
        color(cageSelected && i == selected ? meshAccent :
              member >= 0 ? Color{.72f, .88f, 1.0f} : Color{.57f, .64f, .73f});
        const bool cage = cageSelected && i == selected;
        for (std::size_t face = 0; face < modelMesh(model, cage).faces.size(); ++face)
            drawEdges(faceGeometry(model, face, cage));
    }
    glEnd();
    if (meshEdit && meshEdit->mode == ComponentMode::Vertex) {
        const auto& edit = *meshEdit;
        const Transform& transform = edit.draft.transform;
        const auto point = [&](Vec3 local) { vertex(transform.position + worldVector(transform,local)); };
        glDepthMask(GL_FALSE);
        glPointSize(4);
        color(meshAccent);
        glBegin(GL_POINTS);
        for (Vec3 local : edit.draft.mesh.vertices) point(local);
        glEnd();
        if (!edit.vertices.empty()) {
            glPointSize(7);
            color(heading);
            glBegin(GL_POINTS);
            for (const auto index : edit.vertices) point(edit.draft.mesh.vertices[index]);
            glEnd();
        }
        glPointSize(1);
        glDepthMask(GL_TRUE);
    }
    if (wireframe) {
        if (hasFaceSelection) {
            glLineWidth(2);
            color(heading);
            glBegin(GL_LINES);
            for (auto face : selectedFaces) drawEdges(faceGeometry(models[selected], face, true));
            glEnd();
            glLineWidth(1);
        }
        glEnable(GL_DEPTH_TEST);
    }
}

void buildShadowMaps(const std::vector<Model>& models, std::vector<ShadowMap>& shadowMaps) {
    shadowMaps.clear();
    shadowMaps.resize(models.size());
    std::vector<int> lights;
    std::vector<const Model*> geometry;
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = models[i];
        if (!objectVisible(models, i)) continue;
        if (isGeometry(model.kind)) geometry.push_back(&model);
        if (isLight(model.kind) && model.light.enabled && model.light.shadows) lights.push_back(i);
    }
    for (int i : lights) shadowMaps[i].build(models[i], geometry);
}

void Editor::updateShadowMaps(bool interactive) {
    if (!shadowMapsDirty) return;
    const ULONGLONG now = GetTickCount64();
    if (interactive && shadowMaps.size() == models.size() && now - lastShadowUpdate < 65) return;
    shadowMapsDirty = false;
    lastShadowUpdate = now;
    buildShadowMaps(models, shadowMaps);
}

void Editor::drawView(const View& view) {
    const Rect r = view.rect;
    clip(r);
    glViewport(r.x, height - r.y - r.h, r.w, r.h);
    glClearColor(.115f, .135f, .169f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    view.applyCamera();
    drawGrid(view);
    drawModels(view);
}

std::array<Handle, 3> Editor::handles(const View& view) const {
    std::array<Handle, 3> result{};
    if (selected < 0 || !objectVisible(models, selected) || activeTool() == Tool::Bridge ||
        activeTool() == Tool::Select) return result;
    if (meshEdit ? meshEdit->vertices.empty() || activeTool() == Tool::Rotate ||
        (activeTool() == Tool::Scale && meshEdit->vertices.size() < 2) : (tool == Tool::Extrude && selectedFaces.empty()) ||
        (tool == Tool::Scale && !geometrySelected() && !fogSelected() && !emitterSelected() &&
         models[selected].kind != ObjectKind::Group)) return result;
    const Transform& transform = models[selected].transform;
    const Vec3 position = meshEdit ? componentCenter() : tool == Tool::Extrude ? extrusionCenter() : transform.position;
    const double depth = view.depth(position);
    if (depth <= nearPlane || depth >= farPlane) return result;
    const Vec2 start = view.project(position);
    const double scale = view.perspective ? view.focal() / depth : view.orthoScale();
    for (int i = 0; i < 3; ++i) {
        const Vec3 axis = !meshEdit && tool == Tool::Scale ? transform.basis[i] : axes[i];
        const Vec3 end = position + axis * (50 / scale);
        if (view.depth(end) <= nearPlane) continue;
        const Vec2 tip = view.project(end);
        const Vec2 delta = tip - start;
        result[i] = {start, tip, dot(delta, delta) >= 64};
    }
    return result;
}

Vec3 Editor::extrusionCenter() const {
    const auto& model = models[selected];
    const auto& mesh = modelMesh(model, true);
    std::vector<std::size_t> vertices;
    vertices.reserve(selectedFaces.size() * 4);
    for (const auto index : selectedFaces) {
        const auto& face = mesh.faces[index];
        vertices.insert(vertices.end(), face.vertices.begin(), face.vertices.begin() + face.count);
    }
    std::sort(vertices.begin(), vertices.end());
    vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
    Vec3 center{};
    for (const auto index : vertices) center = center + mesh.vertices[index];
    return model.transform.position + worldVector(model.transform, center * (1.0 / vertices.size()));
}

Vec2 projectedAxis(const View& view, Vec3 pivot, Vec3 axis) {
    const Vec2 screen{dot(axis, view.right), -dot(axis, view.up)};
    if (!view.perspective) return screen;
    return screen + (view.project(pivot) - view.rect.center()) * (dot(axis, view.back) / view.focal());
}

constexpr double minPlaneDragDeterminant = .025;

std::array<PlaneHandle, 3> Editor::planeHandles(const View& view) const {
    std::array<PlaneHandle, 3> result{};
    if (selected < 0 || !objectVisible(models, selected)) return result;
    const Tool current = activeTool();
    if ((current != Tool::Move && current != Tool::Scale) ||
        (meshEdit && meshEdit->vertices.size() < (current == Tool::Scale ? 2u : 1u)) ||
        (current == Tool::Scale && !geometrySelected() && !fogSelected() && !emitterSelected() &&
         models[selected].kind != ObjectKind::Group)) return result;
    const Vec3 pivot = meshEdit ? componentCenter() : models[selected].transform.position;
    const double depth = view.depth(pivot);
    if (depth <= nearPlane || depth >= farPlane) return result;
    const double size = 50 / (view.perspective ? view.focal() / depth : view.orthoScale());
    const auto* basis = current == Tool::Scale && !meshEdit ? models[selected].transform.basis.data() : axes;
    for (int normal = 0; normal < 3; ++normal) {
        const Vec3 first = basis[(normal+1) % 3], second = basis[(normal+2) % 3];
        if (std::abs(cross2(projectedAxis(view, pivot, first), projectedAxis(view, pivot, second))) <
            minPlaneDragDeterminant) continue;
        const Vec3 a = first * size, b = second * size;
        const Vec2 layout[] = {{.23,.23}, {.45,.23}, {.45,.45}, {.23,.45}};
        auto& handle = result[normal];
        bool visible = true;
        for (int i = 0; i < 4; ++i) {
            const Vec3 p = pivot + a * layout[i].x + b * layout[i].y;
            if (view.depth(p) <= nearPlane) { visible = false; break; }
            handle.corners[i] = view.project(p);
        }
        handle.visible = visible && std::abs(cross2(handle.corners[1] - handle.corners[0],
                                                   handle.corners[3] - handle.corners[0])) >= 18;
    }
    return result;
}

Vec2 planeDragPixels(const View& view, Vec3 pivot, const Vec3* basis, int normal, Vec2 delta) {
    // Use the projection at mouse-down. Intersecting each cursor ray with the plane
    // makes a drag diverge when the cursor approaches the plane's horizon.
    const Vec2 a = projectedAxis(view, pivot, basis[(normal+1) % 3]);
    const Vec2 b = projectedAxis(view, pivot, basis[(normal+2) % 3]);
    const double determinant = cross2(a,b);
    if (std::abs(determinant) < minPlaneDragDeterminant) return {};
    return {cross2(delta,b) / determinant, cross2(a,delta) / determinant};
}

Vec3 planeMoveOffset(const View& view, Vec3 pivot, int normal, Vec2 delta) {
    const Vec2 projected = planeDragPixels(view, pivot, axes, normal, delta);
    const double depth = view.depth(pivot);
    const double scale = view.perspective ? view.focal() / depth : view.orthoScale();
    Vec3 offset = (axes[(normal+1) % 3] * projected.x + axes[(normal+2) % 3] * projected.y) * (1 / scale);
    if (view.perspective) {
        const double towardCamera = dot(offset, view.back);
        const double available = std::max(0.0, depth - 1.1);
        if (towardCamera > available) offset = offset * (available / towardCamera);
    }
    return offset;
}

std::array<double,3> dragScaleFactors(const View& view, Vec3 pivot, const Vec3* basis,
                                     int axis, int plane, Vec2 delta, Vec2 tangent, bool uniform = false) {
    std::array<double,3> result{1,1,1};
    const auto factor = [](double pixels) { return std::exp(std::clamp(pixels / 60, -8.0, 8.0)); };
    if (plane >= 0) {
        const Vec2 pixels = planeDragPixels(view, pivot, basis, plane, delta);
        if (uniform) result.fill(factor((pixels.x + pixels.y) * .5));
        else {
            result[(plane+1) % 3] = factor(pixels.x);
            result[(plane+2) % 3] = factor(pixels.y);
        }
    } else if (uniform) result.fill(factor(dot(delta,tangent)));
    else result[axis] = factor(dot(delta,tangent));
    return result;
}

bool hitPlaneHandle(const PlaneHandle& handle, Vec2 point) {
    if (!handle.visible) return false;
    const auto& p = handle.corners;
    const double winding = cross2(p[1] - p[0], p[3] - p[0]);
    for (int i = 0; i < 4; ++i)
        if (cross2(p[(i + 1) % 4] - p[i], point - p[i]) * winding < 0) return false;
    return true;
}

Vec3 axisMoveOffset(const View& view, Vec3 pivot, Vec3 axis, Vec2 delta) {
    Vec2 screenAxis{dot(axis, view.right), -dot(axis, view.up)};
    const double depth = view.depth(pivot);
    const double towardCamera = dot(axis, view.back);
    if (view.perspective) {
        const Vec2 fromCenter = view.project(pivot) - view.rect.center();
        screenAxis = screenAxis * (view.focal() / depth) + fromCenter * (towardCamera / depth);
    } else screenAxis = screenAxis * view.orthoScale();
    const double lengthSquared = dot(screenAxis, screenAxis);
    if (lengthSquared < 1e-8) return {};
    double distance = dot(delta, screenAxis) / lengthSquared;
    if (view.perspective) {
        // Apparent displacement changes as the edited point moves toward the camera.
        distance /= std::max(.1, 1 + distance * towardCamera / depth);
        const double travel = std::max(0.0, depth - 1.1);
        if (towardCamera > 0) distance = std::min(distance, travel / towardCamera);
        if (towardCamera < 0) distance = std::max(distance, travel / towardCamera);
    }
    return axis * distance;
}

std::array<Vec2, ringSegments + 1> Editor::rotationRing(const View& view, int axis) const {
    std::array<Vec2, ringSegments + 1> result{};
    const Vec3 position = meshEdit ? componentCenter() : models[selected].transform.position;
    const double depth = view.depth(position);
    const double scale = view.perspective ? view.focal() / depth : view.orthoScale();
    const double radius = (40 + 9 * axis) / scale;
    const Vec3 u = axes[(axis + 1) % 3], v = axes[(axis + 2) % 3];
    for (int i = 0; i <= ringSegments; ++i) {
        const double angle = 2 * pi * i / ringSegments;
        result[i] = view.project(position + (u * std::cos(angle) + v * std::sin(angle)) * radius);
    }
    return result;
}

int Editor::pickRotationRing(const View& view, Vec2 point, Vec2& tangent) const {
    if (!canRotateSelected() || (meshEdit && meshEdit->vertices.size() < 2)) return -1;
    const Vec3 pivot = meshEdit ? componentCenter() : models[selected].transform.position;
    if (view.depth(pivot) <= nearPlane * 2) return -1;
    const Vec2 fromCenter = point - view.project(pivot);
    if (dot(fromCenter, fromCenter) <= 14 * 14) return -1;
    int picked = -1;
    double best = 49;
    for (int axis = 0; axis < 3; ++axis) {
        const auto ring = rotationRing(view, axis);
        for (int i = 0; i < ringSegments; ++i) {
            const Vec2 segment = ring[i+1] - ring[i];
            const double length = dot(segment, segment);
            if (length <= 1e-6) continue;
            const Vec2 offset = point - ring[i];
            const Vec2 error = offset - segment * std::clamp(dot(offset, segment) / length, 0.0, 1.0);
            const double distance = dot(error,error);
            if (distance >= best) continue;
            best = distance;
            picked = axis;
            tangent = segment * (1 / std::sqrt(length));
        }
    }
    return picked;
}

double rotationDragAngle(const View& view, Vec3 pivot, int index, Vec2 start, Vec2 point, Vec2 tangent) {
    const Vec3 axis = axes[index];
    const Vec3 eye = view.perspective ? normalized(view.eye() - pivot) : view.back;
    if (std::abs(dot(axis, eye)) > .24) {
        const auto direction = [&](Vec2 screen) {
            const Ray ray = view.ray(screen);
            const double denominator = dot(ray.direction, axis);
            if (std::abs(denominator) < 1e-8) return Vec3{};
            const double depth = dot(pivot - ray.origin, axis) / denominator;
            return depth < nearPlane ? Vec3{} : normalized(ray.origin + ray.direction * depth - pivot);
        };
        const Vec3 a = direction(start), b = direction(point);
        if (dot(a,a) > .5 && dot(b,b) > .5) return std::atan2(dot(cross(a,b), axis), dot(a,b));
    }
    return dot(point - start, tangent) / (40 + 9 * index);
}

void Editor::drawViewOverlay(const View& view) {
    const Rect r = view.rect;
    clip(r);
    const bool active = &view == &views[activeView];
    if (active && view.showRenderFrame) {
        const double fit = renderFrameFit(r, renderSettings.width, renderSettings.height);
        const double halfWidth = renderSettings.width * fit * .5;
        const double halfHeight = renderSettings.height * fit * .5;
        const Vec2 center = r.center();
        const double left = center.x - halfWidth, right = center.x + halfWidth;
        const double top = center.y - halfHeight, bottom = center.y + halfHeight;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(.025f, .04f, .06f, .65f);
        glRectd(r.x, r.y, r.x + r.w, top);
        glRectd(r.x, bottom, r.x + r.w, r.y + r.h);
        glRectd(r.x, top, left, bottom);
        glRectd(right, top, r.x + r.w, bottom);
        glDisable(GL_BLEND);
        constexpr Color frameColor{1.0f, .78f, .40f};
        const double x0 = std::max(left, r.x + 1.0), y0 = std::max(top, r.y + 1.0);
        const double x1 = std::min(right, r.x + r.w - 1.0), y1 = std::min(bottom, r.y + r.h - 1.0);
        line({x0, y0}, {x1, y0}, frameColor);
        line({x1, y0}, {x1, y1}, frameColor);
        line({x1, y1}, {x0, y1}, frameColor);
        line({x0, y1}, {x0, y0}, frameColor);
    }
    drawMotionPath(view);
    drawLightMarkers(view);
    if (meshEdit) drawComponentMarkers(view);
    const Vec2 origin{double(r.x + 43), double(r.y + r.h - 39)};
    for (int i = 0; i < 3; ++i) {
        const Vec2 direction{dot(axes[i], view.right), -dot(axes[i], view.up)};
        if (dot(direction, direction) < .01) {
            fill({int(origin.x) - 2, int(origin.y) - 2, 5, 5}, axisColors[i]);
            label(int(origin.x) + 6, int(origin.y) + 14, axisNames[i], axisColors[i]);
        } else {
            const Vec2 tip = origin + direction * 25;
            arrow(origin, tip, axisColors[i], 6, 1.5f);
            const Vec2 name = origin + direction * 34;
            label(int(name.x) - 4, int(name.y) + 5, axisNames[i], axisColors[i]);
        }
    }
    const Rect name = viewportNameArea(view);
    clip(name);
    fill(name, {.089f, .105f, .135f});
    label(name.x + 8, name.y + 16, viewName(view), active ? accent : heading);
    clip(r);
    const auto dropdown = [&](Rect button, const char* text, bool highlighted) {
        fill(button, highlighted ? Color{.16f, .30f, .43f} : Color{.115f, .15f, .20f});
        label(button.x + 6, button.y + 16, text, highlighted ? accent : heading);
        const Vec2 chevron{double(button.x + button.w - 9), button.y + 11.0};
        line(chevron + Vec2{-3, -1}, chevron + Vec2{0, 2}, muted);
        line(chevron + Vec2{0, 2}, chevron + Vec2{3, -1}, muted);
    };
    dropdown(viewportModeButton(view), viewportModeNames[int(view.mode)], view.mode != ViewportMode::Object);
    dropdown(viewportCameraButton(view), "Camera", view.camera >= 0);
    if (active) {
        fill({r.x, r.y, r.w, 1}, accent);
        fill({r.x, r.y + r.h - 1, r.w, 1}, accent);
        fill({r.x, r.y, 1, r.h}, accent);
        fill({r.x + r.w - 1, r.y, 1, r.h}, accent);
    }
    drawTransformHandles(view);
    if (drag.kind == DragKind::Marquee && drag.view >= 0 && &view == &views[drag.view]) {
        const auto box = marqueeBounds();
        if (std::max(box.w, box.h) >= 5) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(accent.r, accent.g, accent.b, .18f);
            glRecti(box.x, box.y, box.x + box.w, box.y + box.h);
            glDisable(GL_BLEND);
            color(accent);
            const double left = box.x, right = box.x + box.w;
            const double top = box.y, bottom = box.y + box.h;
            glBegin(GL_LINE_LOOP);
            vertex(Vec2{left, top}); vertex(Vec2{right, top});
            vertex(Vec2{right, bottom}); vertex(Vec2{left, bottom});
            glEnd();
        }
    }
}

void Editor::drawTransformHandles(const View& view) {
    if (activeTool() == Tool::Rotate) {
        if (!canRotateSelected() || (meshEdit && meshEdit->vertices.size() < 2)) return;
        const Vec3 pivot = meshEdit ? componentCenter() : models[selected].transform.position;
        if (view.depth(pivot) <= nearPlane * 2) return;
        glLineWidth(2);
        for (int i = 0; i < 3; ++i) {
            color(axisColors[i]);
            glBegin(GL_LINE_STRIP);
            for (Vec2 p : rotationRing(view, i)) vertex(p);
            glEnd();
        }
        glLineWidth(1);
        return;
    }
    const auto planes = planeHandles(view);
    for (int axis = 0; axis < 3; ++axis) {
        if (!planes[axis].visible) continue;
        const auto& points = planes[axis].corners;
        const Color tint = axisColors[axis];
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(tint.r, tint.g, tint.b, .32f);
        glBegin(GL_QUADS);
        for (Vec2 point : points) vertex(point);
        glEnd();
        glDisable(GL_BLEND);
        for (int i = 0; i < 4; ++i) line(points[i], points[(i+1) % 4], tint);
    }
    const auto viewHandles = handles(view);
    for (const int i : {2, 0, 1}) {
        const auto& handle = viewHandles[i];
        if (!handle.visible) continue;
        if (activeTool() != Tool::Scale) arrow(handle.start, handle.tip, axisColors[i], 9, 2);
        else {
            line(handle.start, handle.tip, axisColors[i], 2);
            fill({int(handle.tip.x) - 4, int(handle.tip.y) - 4, 9, 9}, axisColors[i]);
        }
    }
}

void Editor::drawLightMarkers(const View& view) {
    const auto guide = [&](Vec3 a, Vec3 b, Color tint) {
        if (view.depth(a) > nearPlane && view.depth(b) > nearPlane) line(view.project(a), view.project(b), tint);
    };
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = models[i];
        if (!objectVisible(models, i) || isGeometry(model.kind) || model.kind == ObjectKind::Group || i == view.camera) continue;
        const Transform& transform = model.transform;
        const Vec3 position = transform.position;
        if (view.depth(position) <= nearPlane) continue;
        if (isEmitter(model.kind)) {
            const auto center = view.project(position);
            diamond(center, 7, fogAccent);
            guide(position, position - transform.basis[1] * .65, fogAccent);
            label(int(center.x + 10), int(center.y - 4), "Emitter", fogAccent);
            continue;
        }
        if (isFog(model.kind)) {
            std::array<Vec3, 8> corners{};
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                const Vec3 local{(corner & 1) ? halfBox : -halfBox, (corner & 2) ? halfBox : -halfBox,
                                 (corner & 4) ? halfBox : -halfBox};
                corners[corner] = position + worldVector(transform, local);
            }
            const Color tint = i == selected ? fogAccent : Color{.31f, .43f, .52f};
            for (std::size_t corner = 0; corner < corners.size(); ++corner) {
                for (std::size_t axis = 1; axis < corners.size(); axis *= 2)
                    if (!(corner & axis)) guide(corners[corner], corners[corner | axis], tint);
            }
            const auto center = view.project(position);
            label(int(center.x + 7), int(center.y - 5), "Fog", tint);
            continue;
        }
        const Vec2 screen = view.project(position);
        const Color tint = model.kind == ObjectKind::Camera ? (i == selected ? accent : muted) :
            i == selected ? lightAccent : model.light.enabled ? Color{.82f, .72f, .48f} : muted;
        if (model.kind == ObjectKind::Camera) cameraIcon(screen, tint);
        else lightIcon(screen, model.kind, tint, 8);
        if (i != selected) continue;
        if (model.kind == ObjectKind::Camera) {
            const Vec3 end = position - transform.basis[2] * 2;
            const Vec3 x = transform.basis[0] * 1.0, y = transform.basis[1] * .8;
            const Vec3 frame[] = {end - x - y, end + x - y, end + x + y, end - x + y};
            for (int edge = 0; edge < 4; ++edge) {
                guide(position, frame[edge], tint);
                guide(frame[edge], frame[(edge + 1) % 4], tint);
            }
        } else if (model.kind == ObjectKind::Spot) {
            const double length = model.light.range > 0 ? std::min(3.0, model.light.range) : 3;
            const Vec3 end = position - transform.basis[2] * length;
            const double radius = length * std::tan(model.light.cone * pi / 360);
            const Vec3 x = transform.basis[0] * radius, y = transform.basis[1] * radius;
            for (int segment = 0; segment < 24; ++segment) {
                const double a = segment * pi / 12, b = (segment + 1) * pi / 12;
                const Vec3 edge = end + x * std::cos(a) + y * std::sin(a);
                guide(edge, end + x * std::cos(b) + y * std::sin(b), tint);
                if (segment % 6 == 0) guide(position, edge, tint);
            }
        } else if (model.kind == ObjectKind::Sky) {
            guide(position, position + transform.basis[1] * 2.5, tint);
        }
    }
}

void Editor::drawInterface() {
    const Color panel{.09f, .105f, .135f};
    const Color border{.17f, .20f, .25f};
    const int sidebar = width - sidebarWidth;
    fill({0, 0, width, headerHeight}, panel);
    fill({0, headerHeight, toolbarWidth, height - headerHeight}, panel);
    fill({sidebar, headerHeight, sidebarWidth, height - headerHeight}, panel);
    fill({0, headerHeight - 1, width, 1}, border);
    fill({toolbarWidth - 1, headerHeight, 1, height - headerHeight}, border);
    fill({sidebar, headerHeight, 1, height - headerHeight}, border);
    boxIcon(16, 8, accent);
    static const std::string brand = narrowUtf8(editorName);
    label(46, 23, brand);
    const Rect cursor = selectButton();
    const bool selecting = activeTool() == Tool::Select;
    fill(cursor, selecting ? Color{.18f, .32f, .47f} : Color{.14f, .19f, .25f});
    const Color cursorColor = selecting ? accent : heading;
    line({149, 12}, {149, 23}, cursorColor);
    line({149, 12}, {157, 19}, cursorColor);
    line({149, 23}, {152, 19}, cursorColor);
    line({152, 19}, {157, 19}, cursorColor);
    label(cursor.x + 24, cursor.y + 17, "Select", cursorColor);
    fill({222, 8, 1, 20}, border);
    const Rect effects = effectsButton();
    fill(effects, flyout == Flyout::Effects ? Color{.18f, .32f, .47f} : Color{.14f, .19f, .25f});
    label(effects.x + 9, effects.y + 17, "Effects", heading);
    line({double(effects.x + 64), effects.y + 10.0}, {double(effects.x + 68), effects.y + 14.0}, muted);
    line({double(effects.x + 68), effects.y + 14.0}, {double(effects.x + 72), effects.y + 10.0}, muted);
    const Rect dynamicsControl = dynamicsButton();
    const bool dynamicsActive = flyout == Flyout::Dynamics || dynamics.has_value();
    fill(dynamicsControl, dynamicsActive ? Color{.18f, .32f, .47f} : Color{.14f, .19f, .25f});
    label(dynamicsControl.x + 9, dynamicsControl.y + 17, "Dynamics", meshEdit ? muted : heading);
    const Vec2 chevron{double(dynamicsControl.x + dynamicsControl.w - 13), dynamicsControl.y + 12.0};
    line(chevron + Vec2{-4, -2}, chevron + Vec2{0, 2}, muted);
    line(chevron + Vec2{0, 2}, chevron + Vec2{4, -2}, muted);
    const Rect screen = fullscreenButton();
    fill(screen, {.14f, .19f, .25f});
    label(screen.x + 12, screen.y + 17, fullscreen ? "Exit full screen" : "Full screen (F11)");
    label(sidebar + 15, 77, "Scene");
    fill({sidebar + 12, 86, sidebarWidth - 24, 1}, border);

    const Rect button = toolbarButton();
    fill(button, flyout == Flyout::Creation ? Color{.18f, .32f, .47f} : Color{.14f, .19f, .25f});
    boxIcon(button.x + 7, button.y + 8, accent);
    line({34, 88}, {42, 88}, heading, 2);
    line({38, 84}, {38, 92}, heading, 2);
    const Rect lighting = lightingButton();
    fill(lighting, flyout == Flyout::Lighting ? Color{.31f, .25f, .16f} : Color{.14f, .19f, .25f});
    lightIcon(lighting.center(), ObjectKind::Omni, lightAccent, 7);
    const Rect transform = transformButton();
    fill(transform, flyout == Flyout::Transform ? Color{.18f, .32f, .47f} : Color{.14f, .19f, .25f});
    const Vec2 center = transform.center();
    arrow(center, center + Vec2{0, -12}, accent, 5, 1.5f);
    arrow(center, center + Vec2{12, 0}, accent, 5, 1.5f);
    line(center, center + Vec2{-8, 8}, accent, 1.5f);
    fill({int(center.x) - 2, int(center.y) - 2, 4, 4}, heading);
    const Rect modeling = modelingButton();
    const bool modelingActive = flyout == Flyout::Modeling || selectsFaces(tool);
    fill(modeling, modelingActive ? Color{.34f, .23f, .17f} : Color{.14f, .19f, .25f});
    modelingIcon(modeling.center(), modelingActive ? meshAccent : muted);

    if (models.empty()) {
        label(sidebar + 15, 118, "No models", muted);
    } else {
        const Rect list = modelList();
        glEnable(GL_SCISSOR_TEST);
        clip(list);
        const auto rows = sceneRows();
        const int end = std::min(int(rows.size()), listOffset + visibleRows());
        for (int row = listOffset; row < end; ++row) {
            const auto [index, depth] = rows[row];
            const int y = list.y + (row - listOffset) * modelRowHeight;
            const int x = sidebar + 14 + std::min(depth, 6) * 12;
            const bool active = std::find(selection.begin(), selection.end(), index) != selection.end();
            const auto& model = models[index];
            if (active) {
                fill({sidebar + 8, y, sidebarWidth - 16, 29},
                     index == selected ? Color{.16f, .28f, .39f} : Color{.14f, .23f, .32f});
                fill({sidebar + 8, y, 2, 29}, accent);
            }
            const Color icon = active ? accent : muted;
            if (model.kind == ObjectKind::Group) {
                const Vec2 center{double(x + 8), double(y + 14)};
                const Vec2 a = center + (model.expanded ? Vec2{-4, -2} : Vec2{-2, -4});
                const Vec2 b = center + (model.expanded ? Vec2{0, 2} : Vec2{2, 0});
                const Vec2 c = center + (model.expanded ? Vec2{4, -2} : Vec2{-2, 4});
                line(a, b, icon, 2);
                line(b, c, icon, 2);
            } else if (isGeometry(model.kind) || isFog(model.kind)) boxIcon(x + 3, y + 4, isFog(model.kind) ? fogAccent : icon);
            else if (model.kind == ObjectKind::Camera)
                cameraIcon({double(x + 13), double(y + 14)}, icon);
            else lightIcon({double(x + 12), double(y + 14)}, model.kind, lightAccent, 6);
            const bool visible = objectVisible(models, index);
            label(x + 32, y + 19, model.name + (visible ? "" : " (Hidden)"), visible ? heading : muted);
        }
        glDisable(GL_SCISSOR_TEST);
    }

    drawProperties();
    drawTimeline();
    fill({0, height - statusHeight, width, statusHeight}, panel);
    fill({0, height - statusHeight, width, 1}, border);
    const int statusY = height - 11;
    if (dynamics) {
        label(14, statusY, dynamics->playing ? "Dynamics  |  Playing" : "Dynamics  |  Paused", accent);
    } else if (meshEdit) {
        label(14, statusY, std::string(componentNames[int(meshEdit->mode)]) + "  |  " +
              toolNames[int(activeTool())] + "  |  Enter to apply  |  Esc to cancel  |  Alt + left drag to orbit", meshAccent);
    } else if (tool == Tool::Bridge) {
        label(14, statusY, "Bridge Faces  |  Click two faces on one mesh  |  Enter to join", meshAccent);
        if (!selectedFaces.empty()) label(sidebar + 15, statusY, std::to_string(selectedFaces.size()) + " selected", heading);
    } else if (tool == Tool::Extrude) {
        label(14, statusY, width < 860 ? "Extrude  |  Ctrl + click  |  Drag to extrude"
                                      : "Extrude  |  Ctrl + click faces  |  Drag faces or an axis  |  Esc to cancel", meshAccent);
        if (drag.view >= 0 && drag.kind == DragKind::Extrude && extrusion.changed) {
            char amount[64];
            std::snprintf(amount, sizeof(amount), "Distance %.2f", extrusion.distance);
            label(sidebar + 15, statusY, amount, heading);
        } else if (!selectedFaces.empty()) label(sidebar + 15, statusY, std::to_string(selectedFaces.size()) + " selected", heading);
    } else {
        label(14, statusY, std::string(toolNames[static_cast<int>(tool)]) +
              "  |  Alt + left drag to orbit  |  Wheel to zoom  |  Middle drag to pan", muted);
    }
    if (selected >= 0 && !selectsFaces(tool) && !meshEdit) {
        const Transform& transform = models[selected].transform;
        char location[100];
        if (tool == Tool::Scale) {
            std::snprintf(location, sizeof(location), "X %.2fx   Y %.2fx   Z %.2fx",
                          transform.size[0], transform.size[1], transform.size[2]);
        } else {
            const Vec3 p = transform.position;
            std::snprintf(location, sizeof(location), "X %.2f   Y %.2f   Z %.2f", p.x, p.y, p.z);
        }
        if (tool != Tool::Rotate) label(sidebar + 15, statusY, location, heading);
    }
    if (!fullscreen && !IsZoomed(window)) {
        const Rect grip = resizeGrip();
        const Vec2 corner{double(grip.x + grip.w - 5), double(grip.y + grip.h - 5)};
        for (double length : {5.0, 10.0, 15.0})
            line(corner - Vec2{length, 0}, corner - Vec2{0, length}, muted);
    }

    if (flyout == Flyout::Creation || flyout == Flyout::Crystals) {
        const Rect menu = boxMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        for (std::size_t i = 0; i < std::size(creationKinds); ++i) {
            const auto kind = creationKinds[i];
            const int y = menu.y + 36 * int(i);
            if (kind == ObjectKind::Camera) cameraIcon({double(menu.x + 21), double(y + 18)}, accent);
            else boxIcon(menu.x + 11, y + 9, accent);
            label(menu.x + 41, y + 24, objectNames[int(kind)]);
        }
        const int crystalsY = menu.y + 36 * int(std::size(creationKinds));
        label(menu.x + 41, crystalsY + 24, "Crystals  >");
    }
    if (flyout == Flyout::Crystals) {
        const auto menu = crystalMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        for (std::size_t i = 0; i < std::size(crystalKinds); ++i) {
            const int y = menu.y + 36 * int(i);
            diamond({menu.x + 20.0, y + 18.0}, 9, fogAccent);
            label(menu.x + 40, y + 24, objectNames[int(crystalKinds[i])]);
        }
    }
    if (flyout == Flyout::Transform) {
        const Rect menu = transformMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        for (int i = 0; i < 3; ++i) {
            const bool active = int(activeTool()) == i;
            if (active) fill({menu.x, menu.y + 36 * i, menu.w, 36}, {.18f, .32f, .47f});
            label(menu.x + 15, menu.y + 36 * i + 23, toolNames[i], active ? accent : toolAvailable(Tool(i)) ? heading : muted);
        }
    }
    if (flyout == Flyout::Lighting) {
        const Rect menu = lightingMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        for (int i = 0; i < 3; ++i) {
            const ObjectKind kind = static_cast<ObjectKind>(i + 1);
            const int y = menu.y + 36 * i;
            lightIcon({double(menu.x + 21), double(y + 17)}, kind, lightAccent, 6);
            label(menu.x + 41, y + 23, objectNames[i + 1]);
        }
    }
    if (flyout == Flyout::Effects) {
        const Rect menu = effectsMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        boxIcon(menu.x + 11, menu.y + 8, fogAccent);
        label(menu.x + 42, menu.y + 24, "Fog");
    }
    if (flyout == Flyout::Dynamics) {
        const Rect menu = dynamicsMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        const std::array labels{"Collision...", dynamics && dynamics->playing ? "Pause" : "Play", "Reset",
                                "Bake to Timeline...", "Water Emitter", "Water into Dish", "Keep Water Surface"};
        for (std::size_t i = 0; i < labels.size(); ++i)
            label(menu.x + 12, menu.y + 22 + int(i) * dynamicsMenuRowHeight, labels[i],
                  i == 2 && !dynamics ? muted : heading);
    }
    if (flyout == Flyout::Modeling) {
        const Rect menu = modelingMenu();
        fill({menu.x - 1, menu.y - 1, menu.w + 2, menu.h + 2}, border);
        fill(menu, {.15f, .20f, .27f});
        modelingIcon({double(menu.x + 21), double(menu.y + 18)}, meshAccent);
        label(menu.x + 42, menu.y + 24, "Extrude",
              activeTool() == Tool::Extrude ? meshAccent : toolAvailable(Tool::Extrude) ? heading : muted);
        label(menu.x + 42, menu.y + 58, "Cut (Difference)", !meshEdit && canCut() ? heading : muted);
        label(menu.x + 42, menu.y + 94, "Bridge Faces",
              activeTool() == Tool::Bridge ? meshAccent : meshEdit ? muted : heading);
    }
}

void Editor::drawProperties() {
    if (selected < 0) return;
    const Rect r = properties();
    const Model& model = models[selected];
    fill(r, {.105f, .126f, .160f});
    fill({r.x, r.y, r.w, 1}, {.22f, .26f, .32f});
    label(r.x + 14, r.y + 23, model.name + (model.hidden ? " (Hidden)" :
          lightSelected() && !model.light.enabled ? " (Off)" : ""));
    const Rect inspectArea = inspectButton();
    fill(inspectArea, {.155f, .198f, .25f});
    label(inspectArea.x + 5, inspectArea.y + 16, "Transform...");
    if (geometrySelected()) {
        for (int i = 0; i < 3; ++i) {
            const Rect button = componentButton(i);
            const bool active = meshEdit && i == int(meshEdit->mode);
            const Color tint = active ? meshAccent : heading;
            fill(button, active ? Color{.33f,.23f,.17f} : Color{.155f,.198f,.25f});
            const Vec2 c = button.center();
            if (i == int(ComponentMode::Vertex)) {
                fill({int(c.x)-3, int(c.y)-3, 6, 6}, tint);
            } else if (i == int(ComponentMode::Edge)) {
                line(c + Vec2{-6,5}, c + Vec2{6,-5}, tint, 2);
            } else {
                fill({int(c.x)-6, int(c.y)-6, 12, 12}, tint);
            }
        }
        if (model.kind == ObjectKind::Plane) {
            for (int i = 0; i < 2; ++i) {
                const Rect button = planeSegmentsButton(i);
                fill(button, {.155f,.198f,.25f});
                label(button.x + 5, button.y + 16,
                      std::string(i == 0 ? "X " : "Y ") + std::to_string(i == 0 ? model.planeSegmentsX : model.planeSegmentsY));
            }
        }
        const Rect texture = textureMeshButton();
        fill(texture, {.155f,.198f,.25f});
        label(texture.x + 8, texture.y + 16, "Edit Texture Mesh...");
        drawModifiers();
    }
    if (cameraSelected()) {
        const Rect setup = lightSetupButton();
        fill(setup, {.155f, .198f, .25f});
        label(setup.x + 7, setup.y + 16, "Camera view");
    }
    if (emitterSelected()) {
        const auto button = emitterSettingsButton();
        fill(button, {.155f, .198f, .25f});
        label(button.x + 7, button.y + 16, "Emitter settings...");
        return;
    }
    if (fogSelected()) {
        const auto& fog = model.fog;
        const std::array<std::string, 6> values{fog.style == FogStyle::Mist ? "Mist" : "Cloud",
            std::to_string(fog.density) + "%", std::to_string(fog.detail), std::to_string(fog.flow) + "%", "Color...",
            fogQualityNames[std::size_t(fog.quality)]};
        constexpr std::array names{"Type", "Density", "Noise scale", "Flow", "Tint", "Quality"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            const Rect button = fogOption(int(i));
            label(r.x + 14, button.y + 16, names[i]);
            fill(button, {.155f, .198f, .25f});
            label(button.x + 7, button.y + 16, values[i]);
        }
        const Rect tint = fogOption(4);
        fill({tint.x + tint.w - 23, tint.y + 4, 17, 15}, fog.color);
        const Rect shadows = fogShadowsButton();
        drawCheckbox(shadows, fog.shadows);
        label(shadows.x + 23, shadows.y + 15, "Shadows");
        return;
    }
    if (!lightSelected()) return;

    const Rect colorButton = lightColorButton();
    fill(colorButton, {.155f, .198f, .25f});
    label(colorButton.x + 6, colorButton.y + 16, "Color...");
    fill({colorButton.x + 56, colorButton.y + 5, 15, 13}, {.65f, .69f, .74f});
    fill({colorButton.x + 57, colorButton.y + 6, 13, 11}, model.light.color);
    const Rect setup = lightSetupButton();
    fill(setup, {.155f, .198f, .25f});
    label(setup.x + 5, setup.y + 16, "Light settings...");
    const Rect intensity = intensityButton();
    label(r.x + 14, r.y + 78, "Intensity");
    fill(intensity, {.155f, .198f, .25f});
    char power[32];
    std::snprintf(power, sizeof(power), "%.5g", model.light.power);
    label(intensity.x + 9, intensity.y + 16, power);
    const Rect toggle = shadowsButton();
    drawCheckbox(toggle, model.light.shadows);
    label(toggle.x + 23, toggle.y + 15, "Shadows");
    const Color controls = model.light.shadows ? heading : muted;
    label(r.x + 14, r.y + 134, "Bitmap", controls);
    for (int i = 0; i < int(std::size(bitmapSizes)); ++i) {
        const Rect option = bitmapOption(i);
        const bool active = model.light.shadowSize == bitmapSizes[i];
        fill(option, active && model.light.shadows ? Color{.18f, .32f, .47f} : Color{.14f, .17f, .22f});
        label(option.x + 10, option.y + 16, std::to_string(bitmapSizes[i]), controls);
    }
    const Rect custom = bitmapOption(3);
    const bool customSize = std::find(std::begin(bitmapSizes), std::end(bitmapSizes), model.light.shadowSize) == std::end(bitmapSizes);
    fill(custom, customSize && model.light.shadows ? Color{.18f, .32f, .47f} : Color{.14f, .17f, .22f});
    label(custom.x + 3, custom.y + 16, customSize ? std::to_string(model.light.shadowSize) : "...", controls);
    label(r.x + 14, r.y + 161, "Shadow transparency", controls);
    label(r.x + 163, r.y + 161, std::to_string(int(std::round(model.light.shadowTransparency * 100))) + "%", controls);
    const Rect track = transparencyTrack();
    const int x = track.x + 3 + int((track.w - 6) * model.light.shadowTransparency);
    fill({track.x + 3, track.y + 8, track.w - 6, 3}, {.21f, .27f, .34f});
    fill({x - 3, track.y + 3, 7, 13}, model.light.shadows ? accent : muted);
}

void Editor::drawModifiers() {
    const Rect panel = properties();
    fill({panel.x + 12, panel.y + 89, panel.w - 24, 1}, {.20f, .25f, .31f});
    label(panel.x + 14, panel.y + 105, "Modifiers");
    if (models[selected].evaluatedMesh) {
        label(panel.x + 126, panel.y + 105, std::to_string(modelMesh(models[selected]).faces.size()) + " faces", muted);
    }
    const Rect add = addModifierButton(), remove = removeModifierButton();
    const Color button{.155f, .198f, .25f};
    fill(add, button);
    label(add.x + 5, add.y + 16, "Add Modifier");
    fill(remove, button);
    label(remove.x + 8, remove.y + 16, "Remove", selectedModifier >= 0 ? heading : muted);
    const Rect list = modifiersList();
    fill(list, {.08f, .10f, .13f});
    const auto& modifiers = models[selected].modifiers;
    if (modifiers.empty()) {
        label(list.x + 7, list.y + 17, "No modifiers", muted);
        return;
    }
    constexpr int rowHeight = 22;
    for (int i = modifierListOffset; i < int(modifiers.size()) && i < modifierListOffset + list.h / rowHeight; ++i) {
        const int y = list.y + (i - modifierListOffset) * rowHeight;
        if (i == selectedModifier) fill({list.x + 1, y + 1, list.w - 2, rowHeight - 1}, {.15f, .29f, .42f});
        label(list.x + 8, y + 15, "Smooth", modifiers[i].active ? heading : muted);
        if (modifiers[i].active) label(list.x + 121, y + 15, std::to_string(modifiers[i].levels), accent);
    }
    if (selectedModifier < 0) return;
    const auto& modifier = modifiers[selectedModifier];
    const Rect active = activeModifierButton();
    fill({active.x, active.y + 5, 14, 14}, {.20f, .27f, .35f});
    if (modifier.active) {
        line({double(active.x + 3), double(active.y + 12)}, {double(active.x + 6), double(active.y + 16)}, accent, 2);
        line({double(active.x + 6), double(active.y + 16)}, {double(active.x + 12), double(active.y + 8)}, accent, 2);
    }
    label(active.x + 21, active.y + 16, "Active");
    const Rect levels = modifierLevelsButton();
    label(levels.x - 54, levels.y + 16, "Levels", muted);
    fill(levels, button);
    label(levels.x + 10, levels.y + 16, std::to_string(modifier.levels));
}

namespace {
constexpr Color keyColor{1.f, .72f, .31f};

void transportIcon(int index, Vec2 p, Color tint) {
    if (index == 3) {
        fill({int(p.x) - 5, int(p.y) - 5, 10, 10}, tint);
        return;
    }
    const double direction = index < 2 ? -1 : 1;
    const double offset = index == 0 || index == 5 ? -2 * direction : 0;
    color(tint);
    glBegin(GL_TRIANGLES);
    vertex(p + Vec2{-4 * direction + offset, -6});
    vertex(p + Vec2{6 * direction + offset, 0});
    vertex(p + Vec2{-4 * direction + offset, 6});
    glEnd();
    if (index != 2) {
        const double bar = direction * (index == 0 || index == 5 ? 6 : 8);
        line(p + Vec2{bar, -6}, p + Vec2{bar, 6}, tint, 2);
    }
}

int tickStep(int minimum) {
    int scale = 1;
    while (minimum > 10 * scale) scale *= 10;
    for (int factor : {1, 2, 5, 10}) {
        if (factor * scale >= minimum) return factor * scale;
    }
    return scale;
}
} // namespace

void Editor::drawTimeline() {
    const Rect panel = timelinePanel();
    const Color button{.14f, .18f, .24f};
    fill(panel, {.09f, .105f, .135f});
    fill({panel.x, panel.y, panel.w, 1}, {.21f, .26f, .34f});
    fill({panel.w - 1, panel.y, 1, panel.h}, {.17f, .20f, .25f});
    for (int i = 0; i < 6; ++i) {
        const Rect action = timelineTransport(i);
        fill(action, i == 2 && playing ? Color{.17f, .32f, .46f} : button);
        transportIcon(i, action.center(), i == 2 && playing ? accent : heading);
    }
    const auto* currentKey = currentTimelineKey();
    const bool hasKey = currentKey != nullptr;
    const Rect set = setKeyButton(), remove = removeKeyButton();
    fill(set, hasKey ? Color{.37f, .26f, .16f} : button);
    label(set.x + (compactTimeline() ? 4 : 8), set.y + 16,
          hasKey ? "Set key" : "Add key",
          selected >= 0 && models[selected].kind != ObjectKind::Group ? keyColor : muted);
    const Rect keyType = keyTypeButton();
    const auto interpolation = hasKey ? currentKey->interpolation : newKeyInterpolation;
    fill(keyType, button);
    label(keyType.x + (compactTimeline() ? 4 : 8), keyType.y + 16,
          interpolation == KeyInterpolation::Smooth ? "Smooth" : "Linear", heading);
    const Vec2 arrow{double(keyType.x + keyType.w - 9), keyType.y + 12.0};
    line(arrow + Vec2{-3, -1}, arrow + Vec2{0, 2}, muted);
    line(arrow + Vec2{0, 2}, arrow + Vec2{3, -1}, muted);
    fill(remove, button);
    line(remove.center() - Vec2{4, 4}, remove.center() + Vec2{4, 4}, hasKey ? keyColor : muted);
    line(remove.center() + Vec2{-4, 4}, remove.center() + Vec2{4, -4}, hasKey ? keyColor : muted);
    if (timelineWidth() > 595) label(404, set.y + 16, "Timeline", muted);
    const char* labels[] = {"FPS", "Length", "Frame"};
    const int values[] = {timeline.fps, timeline.frames, timeline.frame};
    for (int i = 0; i < 3; ++i) {
        const Rect r = timelineNumber(i);
        label(r.x - 51, r.y + 15, labels[i], muted);
        fill(r, button);
        label(r.x + 8, r.y + 15, std::to_string(values[i]), i == 2 ? accent : heading);
    }

    const Rect ruler = timelineRuler();
    fill(ruler, {.11f, .14f, .18f});
    const int last = timeline.frames - 1;
    const auto tickX = [&](int frame) {
        return ruler.x + 4 + (last > 0 ? int(std::round(double(frame) * (ruler.w - 9) / last)) : 0);
    };
    const int trackY = ruler.y + 29;
    fill({ruler.x + 4, trackY, ruler.w - 8, 2}, {.26f, .32f, .40f});
    const int ticks = std::max(1, ruler.w / 67);
    const int step = tickStep((last + ticks - 1) / ticks);
    for (int frame = 0; frame <= last; frame += step) {
        const int x = tickX(frame);
        fill({x, ruler.y + 17, 1, 11}, {.32f, .40f, .50f});
        const std::string number = std::to_string(frame);
        label(std::min(x + 2, ruler.x + ruler.w - int(number.size()) * 8 - 2), ruler.y + 13, number, muted);
    }
    if (selected >= 0) {
        int previousX = -100;
        for (const auto& key : models[selected].keys) {
            if (key.frame > last) break;
            const int x = tickX(key.frame);
            if (x <= previousX + 2) continue;
            diamond({double(x), double(trackY + 1)}, 4, keyColor);
            previousX = x;
        }
    }
    const int head = tickX(timeline.frame);
    fill({head, ruler.y + 16, 1, ruler.h - 16}, accent);
    color(accent);
    glBegin(GL_TRIANGLES);
    vertex(Vec2{double(head - 4), double(ruler.y + 16)});
    vertex(Vec2{double(head + 4), double(ruler.y + 16)});
    vertex(Vec2{double(head), double(ruler.y + 22)});
    glEnd();
}

void Editor::drawMotionPath(const View& view) {
    if (selected < 0 || !objectVisible(models, selected) || view.camera == selected) return;
    const Model& model = models[selected];
    if (model.keys.size() < 2 || model.keys.front().frame >= timeline.frames || timeline.frames < 2) return;
    const int last = timeline.frames - 1;
    Vec3 previous = model.keys.front().pose.position;
    const auto segment = [&](Vec3 end) {
        if (view.depth(previous) > nearPlane && view.depth(end) > nearPlane) {
            line(view.project(previous), view.project(end), {.66f, .47f, .25f});
        }
        previous = end;
    };
    const std::size_t stride = std::max<std::size_t>(1, model.keys.size() / 512);
    for (std::size_t i = 0; i < model.keys.size() && model.keys[i].frame <= last; i += stride) {
        const Vec3 p = model.keys[i].pose.position;
        segment(p);
        if (view.depth(p) > nearPlane) diamond(view.project(p), 3, keyColor);
    }
    segment(evaluatePose(model, last).position);
}

bool Editor::paintMaterials() {
    if (!context || !materialDc || materialWidth <= 0 || materialHeight <= 0) return false;
    const auto previousContext = wglGetCurrentContext();
    const auto previousDc = wglGetCurrentDC();
    if (!wglMakeCurrent(materialDc, context)) return false;
    glViewport(0, 0, materialWidth, materialHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glClearColor(.09f, .105f, .135f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, materialWidth, materialHeight, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    label(14, 24, "Click to edit  |  Right-click to apply", muted);
    const int end = std::min(int(materials.size()), (materialFirstRow + materialVisibleRows()) * materialColumns());
    for (int i = materialFirstRow * materialColumns(); i < end; ++i) {
        const Rect tile = materialTile(i);
        const bool assigned = selected >= 0 && models[selected].material == i;
        fill(tile, assigned ? accent : Color{.21f, .25f, .31f});
        fill({tile.x + 1, tile.y + 1, tile.w - 2, tile.h - 2}, {.13f, .16f, .21f});
        const Vec2 center{tile.center().x, double(tile.y + 27)};
        const bool crystal = isCrystalPreset(materials[i]->edit.preset);
        if (crystal) {
            for (const auto& facet : crystalPreview(materials[i]->edit)) {
                color(facet.color);
                glBegin(GL_POLYGON);
                for (std::size_t j = 0; j < facet.count; ++j) vertex(center + facet.corners[j] * 46);
                glEnd();
            }
        } else drawMaterialSphere(*materials[i], center, false);
        const Surface& surface = materials[i]->edit.surface;
        if (!crystal && surface.specular > 0 && (surface.shading == Shading::Phong || surface.shading == Shading::BlinnPhong)) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            drawMaterialSphere(*materials[i], center, true);
            glDisable(GL_BLEND);
        }
        const std::wstring name = presetName(materials[i]->edit.preset);
        label(tile.x + 9, tile.y + 65, name.empty() ? "Material " + std::to_string(i + 1)
                                                   : std::string(name.begin(), name.end()),
              assigned ? accent : heading);
    }
    const bool presented = SwapBuffers(materialDc) != FALSE;
    return wglMakeCurrent(previousDc, previousContext) && presented;
}

void Editor::paint() {
    if (!context || !width || !height) return;
    updateShadowMaps(drag.view >= 0 || playing || (dynamics && dynamics->playing));
    syncCameras();
    glClearColor(.065f, .074f, .1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    for (const View& view : views) drawView(view);
    overlay();
    for (const View& view : views) drawViewOverlay(view);
    glDisable(GL_SCISSOR_TEST);
    drawInterface();
    SwapBuffers(dc);
}

int Editor::viewAt(Vec2 p) const {
    for (int i = 0; i < int(views.size()); ++i) {
        if (views[i].rect.contains(p)) return i;
    }
    return -1;
}

int Editor::materialAt(Vec2 p) const {
    const int end = std::min(int(materials.size()), (materialFirstRow + materialVisibleRows()) * materialColumns());
    for (int i = materialFirstRow * materialColumns(); i < end; ++i) {
        if (materialTile(i).contains(p)) return i;
    }
    return -1;
}

void Editor::refreshMaterials(int reveal) {
    if (!materialWindow) return;
    const int columns = materialColumns(), page = materialVisibleRows();
    const int rows = (int(materials.size()) + columns - 1) / columns;
    if (reveal >= 0) {
        const int row = reveal / columns;
        materialFirstRow = std::clamp(materialFirstRow, std::max(0, row - page + 1), row);
    }
    materialFirstRow = std::clamp(materialFirstRow, 0, std::max(0, rows - page));
    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    scroll.nMax = std::max(0, rows - 1);
    scroll.nPage = UINT(page);
    scroll.nPos = materialFirstRow;
    SetScrollInfo(materialWindow, SB_VERT, &scroll, TRUE);
    InvalidateRect(materialWindow, nullptr, FALSE);
}

void Editor::showMaterials() {
    if (!materialWindow) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW klass{};
        klass.style = CS_OWNDC;
        klass.lpfnWndProc = materialWindowProc;
        klass.hInstance = instance;
        klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        klass.lpszClassName = L"VerdaMaterialsWindow";
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        const RECT area = monitor.rcWork;
        const int panelWidth = std::min(500L, area.right - area.left);
        const int panelHeight = std::min(700L, area.bottom - area.top);
        if (!CreateWindowExW(WS_EX_TOOLWINDOW, klass.lpszClassName, L"Scene Materials",
                             WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                             area.right - panelWidth, area.top + (area.bottom - area.top - panelHeight) / 2,
                             panelWidth, panelHeight, window, nullptr, instance, this)) {
            MessageBoxW(window, L"Could not open the Scene Materials window.", L"Materials", MB_OK | MB_ICONERROR);
            return;
        }
        refreshMaterials(selected >= 0 ? models[selected].material : -1);
    }
    ShowWindow(materialWindow, IsIconic(materialWindow) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(materialWindow);
}

LRESULT CALLBACK Editor::materialWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* editor = static_cast<Editor*>(creation->lpCreateParams);
        editor->materialWindow = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!editor) return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        editor->materialDc = GetDC(hwnd);
        return editor->materialDc && setGraphicsPixelFormat(editor->materialDc, GraphicsTarget::Window) ? 0 : -1;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {270, 220};
        return 0;
    }
    case WM_SIZE:
        editor->materialWidth = LOWORD(lParam);
        editor->materialHeight = HIWORD(lParam);
        editor->refreshMaterials();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        editor->paintMaterials();
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        const int index = editor->materialAt({double(GET_X_LPARAM(lParam)), double(GET_Y_LPARAM(lParam))});
        if (index >= 0) editor->openMaterial(index);
        return 0;
    }
    case WM_CONTEXTMENU:
        editor->materialMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    case WM_MOUSEWHEEL:
        editor->materialWheelDelta += GET_WHEEL_DELTA_WPARAM(wParam);
        editor->materialFirstRow -= editor->materialWheelDelta / WHEEL_DELTA;
        editor->materialWheelDelta %= WHEEL_DELTA;
        editor->refreshMaterials();
        return 0;
    case WM_VSCROLL: {
        const int page = editor->materialVisibleRows();
        switch (LOWORD(wParam)) {
        case SB_LINEUP: --editor->materialFirstRow; break;
        case SB_LINEDOWN: ++editor->materialFirstRow; break;
        case SB_PAGEUP: editor->materialFirstRow -= page; break;
        case SB_PAGEDOWN: editor->materialFirstRow += page; break;
        case SB_TOP: editor->materialFirstRow = 0; break;
        case SB_BOTTOM: editor->materialFirstRow = int(editor->materials.size()); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: {
            SCROLLINFO scroll{};
            scroll.cbSize = sizeof(scroll);
            scroll.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(hwnd, SB_VERT, &scroll)) editor->materialFirstRow = scroll.nTrackPos;
            break;
        }
        }
        editor->refreshMaterials();
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_NCDESTROY:
        if (editor->materialDc) ReleaseDC(hwnd, editor->materialDc);
        editor->materialDc = nullptr;
        editor->materialWindow = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool Editor::timelineClick(Vec2 point) {
    if (!timelinePanel().contains(point)) return false;
    flyout = Flyout::None;
    for (int i = 0; i < 6; ++i) {
        if (!timelineTransport(i).contains(point)) continue;
        if (i == 2) {
            startPlayback();
        } else {
            stopPlayback();
            if (i == 0) seekFrame(0);
            if (i == 1) seekFrame(timeline.frame - 1);
            if (i == 4) seekFrame(timeline.frame + 1);
            if (i == 5) seekFrame(timeline.frames - 1);
        }
        return true;
    }
    if (setKeyButton().contains(point) || removeKeyButton().contains(point)) {
        setKey(removeKeyButton().contains(point));
        return true;
    }
    if (keyTypeButton().contains(point)) {
        chooseKeyInterpolation();
        return true;
    }
    if (timelineRuler().contains(point)) {
        stopPlayback();
        scrubbingTimeline = true;
        scrubTo(point);
        SetCapture(window);
        return true;
    }
    if (timelineNumber(0).contains(point)) {
        stopPlayback();
        int fps = timeline.fps;
        if (editWholeNumber(window, L"Timeline FPS", L"Frames per second", fps, 1, maxTimelineFps) &&
            fps != timeline.fps) {
            remember();
            timeline.fps = fps;
        }
    } else if (timelineNumber(1).contains(point)) {
        stopPlayback();
        int count = timeline.frames;
        if (editWholeNumber(window, L"Timeline Length", L"Total frames", count, 1, maxTimelineFrames) &&
            count != timeline.frames) {
            remember();
            timeline.frames = count;
            seekFrame(timeline.frame);
        }
    } else if (timelineNumber(2).contains(point)) {
        stopPlayback();
        int frame = timeline.frame;
        if (editWholeNumber(window, L"Go to Frame", L"Frame number", frame, 0, timeline.frames - 1)) {
            seekFrame(frame);
        }
    }
    return true;
}

void Editor::scrubTo(Vec2 point) {
    const Rect ruler = timelineRuler();
    const double fraction = std::clamp((point.x - ruler.x - 4) / std::max(1, ruler.w - 9), 0.0, 1.0);
    seekFrame(int(std::round(fraction * (timeline.frames - 1))));
}

void Editor::seekFrame(int frame, bool refreshPose) {
    resetDynamics();
    frame = std::clamp(frame, 0, timeline.frames - 1);
    if (timeline.frame == frame && !refreshPose) return;
    timeline.frame = frame;
    for (int i = 0; i < int(models.size()); ++i) {
        Model& model = models[i];
        if (model.keys.empty()) continue;
        const Transform pose = evaluatePose(model, frame);
        if (model.kind != ObjectKind::Camera && !shadowMapsDirty && objectVisible(models, i)) {
            const auto differs = [](Vec3 a, Vec3 b) { return dot(a - b, a - b) > 1e-18; };
            shadowMapsDirty = differs(model.transform.position, pose.position) || model.transform.size != pose.size;
            for (int i = 0; i < 3 && !shadowMapsDirty; ++i) {
                shadowMapsDirty = differs(model.transform.basis[i], pose.basis[i]);
            }
        }
        model.transform = pose;
    }
    syncCameras();
    repaint();
}

const TransformKey* Editor::currentTimelineKey() const {
    return selected >= 0 ? findKey(models[selected], timeline.frame) : nullptr;
}

void Editor::chooseKeyInterpolation() {
    stopPlayback();
    const auto* key = currentTimelineKey();
    const auto current = key ? key->interpolation : newKeyInterpolation;
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    constexpr UINT linear = 1, smooth = 2;
    AppendMenuW(menu, MF_STRING, linear, L"Linear");
    AppendMenuW(menu, MF_STRING, smooth, L"Smooth");
    CheckMenuRadioItem(menu, linear, smooth, current == KeyInterpolation::Linear ? linear : smooth, MF_BYCOMMAND);
    const Rect button = keyTypeButton();
    POINT point{button.x, button.y + button.h};
    ClientToScreen(window, &point);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command != linear && command != smooth) return;
    newKeyInterpolation = command == linear ? KeyInterpolation::Linear : KeyInterpolation::Smooth;
    if (key && key->interpolation != newKeyInterpolation) {
        remember();
        setKeyInterpolation(models[selected], timeline.frame, newKeyInterpolation);
    }
    repaint();
}

void Editor::setKey(bool remove) {
    resetDynamics();
    stopPlayback();
    endDrag();
    if (selected < 0 || models[selected].kind == ObjectKind::Group) return;
    Model& model = models[selected];
    const TransformKey* key = currentTimelineKey();
    if (remove && !key) return;
    remember();
    if (remove) {
        removeKey(model, timeline.frame);
        model.transform = evaluatePose(model, timeline.frame);
        shadowMapsDirty = true;
        syncCameras();
    } else {
        storeKey(model, timeline.frame, key ? key->interpolation : newKeyInterpolation);
    }
    repaint();
}

void Editor::startPlayback() {
    if (playing) return;
    resetDynamics();
    endDrag();
    if (timeline.frames < 2) return;
    if (timeline.frame == timeline.frames - 1) seekFrame(0, true);
    else seekFrame(timeline.frame, true);
    playbackStartFrame = timeline.frame;
    playbackStart = GetTickCount64();
    playing = SetTimer(window, timelineTimer, 10, nullptr) != 0;
    repaint();
}

void Editor::stopPlayback() {
    if (!playing) return;
    KillTimer(window, timelineTimer);
    playing = false;
    repaint();
}

void Editor::tickPlayback() {
    if (!playing) return;
    const int last = timeline.frames - 1;
    const int frame = playbackFrame(playbackStartFrame, timeline.fps, GetTickCount64() - playbackStart, last);
    seekFrame(frame);
    if (frame == last) stopPlayback();
}

void Editor::configureDynamics() {
    resetDynamics();
    auto settings = dynamicsSettings;
    auto body = geometrySelected() ? models[selected].collision : CollisionSettings{};
    const bool hasSelection = geometrySelected();
    if (hasSelection && body.role == CollisionRole::Automatic) body.role = CollisionRole::Dynamic;
    if (!editCollision(window, hasSelection ? &body : nullptr, settings)) return;
    const auto applies = [&](int index) { return hasSelection && isGeometry(models[index].kind); };
    const bool bodiesChanged = std::ranges::any_of(selection, [&](int index) {
        return applies(index) && body != models[index].collision;
    });
    if (!bodiesChanged && settings == dynamicsSettings) return;
    remember();
    dynamicsSettings = settings;
    for (const int index : selection)
        if (applies(index)) models[index].collision = body;
    repaint();
}

DynamicsSimulation Editor::createDynamicsSimulation() const {
    bool hasDynamic = false;
    for (int i = 0; i < int(models.size()) && !hasDynamic; ++i)
        hasDynamic = isGeometry(models[i].kind) && models[i].collision.role == CollisionRole::Dynamic &&
                     objectVisible(models, i);
    return DynamicsSimulation(models, dynamicsSettings, hasDynamic ? std::span<const int>{} : std::span<const int>{selection});
}

void Editor::toggleDynamics() {
    stopPlayback();
    if (dynamics && dynamics->playing) {
        KillTimer(window, dynamicsTimer);
        dynamics->playing = false;
    } else {
        if (!dynamics) {
            auto simulation = createDynamicsSimulation();
            FluidSimulation fluid(models, dynamicsSettings);
            if (!simulation.hasBodies() && !fluid.active()) {
                MessageBoxW(window, L"Select a mesh, mark it as Dynamic, or add an emitter.",
                            L"Dynamics", MB_OK | MB_ICONINFORMATION);
                return;
            }
            std::vector<std::pair<std::size_t, Vec3>> original;
            for (const auto index : simulation.movingModels())
                original.emplace_back(index, models[index].transform.position);
            dynamics = DynamicsPreview{std::move(simulation), std::move(fluid), std::move(original),
                                       models.size(), materials.size()};
        }
        dynamics->lastTick = GetTickCount64();
        dynamics->playing = SetTimer(window, dynamicsTimer, dynamicsTimerIntervalMs, nullptr) != 0;
        if (!dynamics->playing)
            MessageBoxW(window, L"Could not start the dynamics timer.", L"Dynamics", MB_OK | MB_ICONERROR);
    }
    repaint();
}

void Editor::resetDynamics() {
    if (!dynamics) return;
    KillTimer(window, dynamicsTimer);
    for (const auto& [index, position] : dynamics->original) models[index].transform.position = position;
    models.resize(dynamics->modelCount);
    materials.resize(dynamics->materialCount);
    dynamics.reset();
    refreshMaterials();
    shadowMapsDirty = true;
    repaint();
}

void Editor::tickDynamics() {
    if (!dynamics || !dynamics->playing) return;
    constexpr double maxRealtimeAdvance = .1;
    const auto now = GetTickCount64();
    const double elapsed = std::min(maxRealtimeAdvance, double(now - dynamics->lastTick) / 1000);
    dynamics->lastTick = now;
    dynamics->simulation.advance(elapsed);
    dynamics->simulation.apply(models);
    if (dynamics->fluid.active()) {
        dynamics->fluid.advance(elapsed, models);
        constexpr ULONGLONG surfaceIntervalMs = 85;
        if (now - dynamics->lastSurface >= surfaceIntervalMs) {
            dynamics->lastSurface = now;
            auto mesh = dynamics->fluid.surface();
            if (mesh.faces.empty()) models.resize(dynamics->modelCount);
            else {
                if (models.size() == dynamics->modelCount) {
                    if (dynamics->waterMaterial < 0) {
                        for (std::size_t i = 0; i < materials.size(); ++i) {
                            if (materials[i]->edit.preset != MaterialPreset::ClearWater) continue;
                            dynamics->waterMaterial = int(i);
                            break;
                        }
                        if (dynamics->waterMaterial < 0 && materials.size() < 256) {
                            auto water = std::make_shared<Material>(Color{1, 1, 1});
                            water->edit = makePreset(MaterialPreset::ClearWater);
                            dynamics->waterMaterial = int(materials.size());
                            materials.push_back(std::move(water));
                        }
                    }
                    Model water;
                    water.kind = ObjectKind::Mesh;
                    water.name = "Water preview";
                    water.material = dynamics->waterMaterial;
                    models.push_back(std::move(water));
                }
                models.back().mesh = std::move(mesh);
            }
        }
    }
    shadowMapsDirty = true;
    repaint();
}

void Editor::addEmitter(bool withDish) {
    resetDynamics();
    if (models.size() + (withDish ? 2 : 1) > maxSceneObjects) return;
    remember();
    if (withDish) {
        Model dish;
        dish.kind = ObjectKind::Mesh;
        dish.name = "Water Dish";
        dish.mesh = createWaterDish();
        dish.collision.role = CollisionRole::Static;
        dish.meshShading = MeshShading::Smooth;
        rebuildModifiers(dish);
        models.push_back(std::move(dish));
    }
    Model source;
    source.kind = ObjectKind::Emitter;
    source.name = "Water Emitter";
    source.transform.position = {0, 2.8, 0};
    source.transform.size = {.26, .25, .26};
    models.push_back(std::move(source));
    select(int(models.size()) - 1);
    setTool(Tool::Move);
    shadowMapsDirty = true;
}

void Editor::configureEmitter() {
    if (!emitterSelected()) return;
    auto settings = models[selected].emitter;
    if (!editWaterEmitter(window, settings) || settings == models[selected].emitter) return;
    remember();
    models[selected].emitter = settings;
    repaint();
}

void Editor::keepWaterSurface() {
    if (!dynamics || models.size() <= dynamics->modelCount) return;
    Model water = std::move(models.back());
    const auto material = water.material >= int(dynamics->materialCount) ? materials[water.material] : MaterialSlot{};
    resetDynamics();
    if (models.size() >= maxSceneObjects) return;
    remember();
    if (material) materials.push_back(material);
    water.name = "Water Surface";
    water.meshShading = MeshShading::Smooth;
    water.collision.role = CollisionRole::Ignore;
    rebuildModifiers(water);
    models.push_back(std::move(water));
    select(int(models.size()) - 1);
    refreshMaterials();
    repaint();
}

void Editor::bakeDynamics() {
    resetDynamics();
    stopPlayback();
    auto simulation = createDynamicsSimulation();
    if (!simulation.hasBodies()) {
        MessageBoxW(window, L"Select a visible mesh or set its Collision role to Dynamic.",
                    L"Dynamics", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const int first = timeline.frame;
    constexpr std::size_t maxBakedKeys = 100'000;
    constexpr int suggestedSeconds = 5;
    const int maxFrames = int(maxBakedKeys / simulation.movingModels().size());
    const int lastLimit = std::min(timeline.frames - 1, first + maxFrames - 1);
    if (lastLimit <= first) {
        MessageBoxW(window, L"Move the playhead earlier or increase the timeline length.",
                    L"Bake Dynamics", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int last = std::min(lastLimit, first + timeline.fps * suggestedSeconds);
    if (!editWholeNumber(window, L"Bake Dynamics", L"Last frame", last, first + 1, lastLimit)) return;
    remember();
    for (int frame = first; frame <= last; ++frame) {
        if (frame != first) simulation.advance(1.0 / timeline.fps);
        simulation.apply(models);
        for (const auto index : simulation.movingModels()) storeKey(models[index], frame);
    }
    seekFrame(first, true);
}

bool uploadTexture(const TextureImage& image, GLuint& texture) {
    texture = 0;
    if (image.pixels.empty()) return true;
    auto rgba = image.pixels;
    for (std::size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    const GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);
    if (texture && error == GL_NO_ERROR) return true;
    if (texture) glDeleteTextures(1, &texture);
    texture = 0;
    return false;
}

class BitmapOpenGL {
public:
    ~BitmapOpenGL() {
        if (context) {
            if (wglGetCurrentContext() == context) {
                if (framebuffer) deleteFramebuffers(1, &framebuffer);
                if (attachments[0]) deleteRenderbuffers(GLsizei(attachments.size()), attachments.data());
                wglMakeCurrent(previousDC, previousContext);
            }
            wglDeleteContext(context);
        }
        if (surface) {
            if (dc) ReleaseDC(surface, dc);
            DestroyWindow(surface);
        } else {
            if (oldBitmap) SelectObject(dc, oldBitmap);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    }

    bool open(int width, int height) {
        if (activeGraphicsBackend() == GraphicsBackend::Vulkan) return openFramebuffer(width, height);
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) return false;
        oldBitmap = SelectObject(dc, bitmap);
        if (!oldBitmap || oldBitmap == HGDI_ERROR) { oldBitmap = nullptr; return false; }
        context = createGraphicsContext(dc, GraphicsTarget::Bitmap);
        previousDC = wglGetCurrentDC();
        previousContext = wglGetCurrentContext();
        return context && wglMakeCurrent(dc, context);
    }

private:
    template<class T> static bool loadExtension(T& slot, const char* name) {
        const auto address = wglGetProcAddress(name);
        const auto code = reinterpret_cast<std::uintptr_t>(address);
        if (code <= 3 || code == std::numeric_limits<std::uintptr_t>::max()) return false;
        slot = graphicsFunction<T>(address);
        return true;
    }

    bool openFramebuffer(int width, int height) {
        constexpr GLenum framebufferTarget = 0x8D40, renderbufferTarget = 0x8D41;
        constexpr GLenum colorAttachment = 0x8CE0, depthAttachment = 0x8D00;
        constexpr GLenum rgb8 = 0x8058, depth24 = 0x81A6, framebufferComplete = 0x8CD5;
        constexpr GLenum maxRenderbuffer = 0x84E8;
        WNDCLASSW wc{};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"3D1OffscreenGraphics";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        surface = CreateWindowW(wc.lpszClassName, L"", WS_POPUP, 0, 0, 32, 32, nullptr, nullptr, wc.hInstance, nullptr);
        if (!surface) return false;
        dc = GetDC(surface);
        if (!dc) return false;
        context = createGraphicsContext(dc, GraphicsTarget::Window);
        previousDC = wglGetCurrentDC();
        previousContext = wglGetCurrentContext();
        if (!context || !wglMakeCurrent(dc, context)) return false;
        void (APIENTRY *genFramebuffers)(GLsizei, GLuint*) = nullptr;
        void (APIENTRY *bindFramebuffer)(GLenum, GLuint) = nullptr;
        GLenum (APIENTRY *checkFramebuffer)(GLenum) = nullptr;
        void (APIENTRY *genRenderbuffers)(GLsizei, GLuint*) = nullptr;
        void (APIENTRY *bindRenderbuffer)(GLenum, GLuint) = nullptr;
        void (APIENTRY *renderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
        void (APIENTRY *attachRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
        if (!loadExtension(genFramebuffers, "glGenFramebuffers") ||
            !loadExtension(bindFramebuffer, "glBindFramebuffer") ||
            !loadExtension(checkFramebuffer, "glCheckFramebufferStatus") ||
            !loadExtension(deleteFramebuffers, "glDeleteFramebuffers") ||
            !loadExtension(genRenderbuffers, "glGenRenderbuffers") ||
            !loadExtension(bindRenderbuffer, "glBindRenderbuffer") ||
            !loadExtension(renderbufferStorage, "glRenderbufferStorage") ||
            !loadExtension(attachRenderbuffer, "glFramebufferRenderbuffer") ||
            !loadExtension(deleteRenderbuffers, "glDeleteRenderbuffers")) return false;
        GLint limit = 0;
        glGetIntegerv(maxRenderbuffer, &limit);
        if (width > limit || height > limit) return false;
        genFramebuffers(1, &framebuffer);
        bindFramebuffer(framebufferTarget, framebuffer);
        genRenderbuffers(GLsizei(attachments.size()), attachments.data());
        const GLenum formats[] = {rgb8, depth24};
        const GLenum points[] = {colorAttachment, depthAttachment};
        for (std::size_t i = 0; i < attachments.size(); ++i) {
            bindRenderbuffer(renderbufferTarget, attachments[i]);
            renderbufferStorage(renderbufferTarget, formats[i], width, height);
            attachRenderbuffer(framebufferTarget, points[i], renderbufferTarget, attachments[i]);
        }
        return framebuffer && checkFramebuffer(framebufferTarget) == framebufferComplete && glGetError() == GL_NO_ERROR;
    }

    HWND surface = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    HGLRC context = nullptr;
    HDC previousDC = nullptr;
    HGLRC previousContext = nullptr;
    GLuint framebuffer = 0;
    std::array<GLuint, 2> attachments{};
    void (APIENTRY *deleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (APIENTRY *deleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
};

bool addRasterMaterial(const MaterialEdit& edit, std::vector<MaterialSlot>& materials) {
    auto material = std::make_shared<Material>(Color{1, 1, 1});
    material->edit = edit;
    if (!uploadTexture(edit.image, material->texture)) return false;
    materials.push_back(std::move(material));
    return true;
}

bool renderRasterFrame(const View& view, const std::vector<Model>& models,
                       const std::vector<MaterialSlot>& materials, const std::vector<ShadowMap>& shadows,
                       std::vector<unsigned char>& pixels) {
    const int width = view.rect.w, height = view.rect.h;
    GLint limits[2]{};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, limits);
    if (width > limits[0] || height > limits[1]) return false;
    glViewport(0, 0, width, height);
    glClearColor(.10f, .12f, .16f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    view.applyCamera();
    drawModelScene(view, models, materials, shadows);
    pixels.resize(std::size_t(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) return false;
    for (std::size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    return true;
}

bool hasVisibleFog(const std::vector<Model>& models) {
    for (int i = 0; i < int(models.size()); ++i) {
        if (isFog(models[i].kind) && models[i].fog.density > 0 && objectVisible(models, i)) return true;
    }
    return false;
}

bool Editor::verifyRasterInitialization() {
    layout(760, 540);
    MaterialEdit material{Surface{{.75f, .8f, .9f}}, {}};
    material.image = {2, 2, {}, {0,0,255,255, 0,255,0,255, 255,0,0,255, 255,255,255,255}};
    std::vector<MaterialSlot> testMaterials;
    if (!addRasterMaterial(material, testMaterials)) return false;
    materials[0] = std::move(testMaterials[0]);
    for (int i = 0; i < 10; ++i) {
        addGeometry(ObjectKind::Box);
        models.back().material = 0;
    }
    addLight(ObjectKind::Omni);
    models.back().light.shadows = true;
    models.back().light.shadowSize = 64;
    paint();
    glFinish();
    if (glGetError() != GL_NO_ERROR) return false;
    BitmapOpenGL target;
    if (!target.open(128, 96)) return false;
    std::vector<MaterialSlot> targetMaterials;
    if (!addRasterMaterial(material, targetMaterials)) return false;
    std::vector<unsigned char> pixels;
    return renderRasterFrame(renderView(views[0], 128, 96), models, targetMaterials, shadowMaps, pixels);
}

struct VideoScene {
    View view;
    VideoSettings video;
    RenderSettings settings;
    std::vector<Model> models;
    TraceScene trace;
    std::vector<std::size_t> geometry, lights, volumes;
    BitmapOpenGL raster;
    std::vector<MaterialSlot> rasterMaterials;
    std::vector<ShadowMap> shadowMaps;
    bool rasterReady = false;
    int timelineFps;
    DynamicsSettings dynamics;
    std::optional<FluidSimulation> waterFlow;
    int waterMaterial = -1;
    bool waterAttached = false;

    VideoScene(View viewport, VideoSettings output, RenderSettings rendering, int fps, DynamicsSettings dynamics)
        : view(viewport), video(std::move(output)), settings(rendering), timelineFps(fps), dynamics(dynamics) {}

    bool drawFrame(int frame, RenderJob& output, std::wstring& error) {
        if (waterAttached) models.pop_back();
        waterAttached = false;
        for (auto& model : models) {
            if (!model.keys.empty()) model.transform = evaluatePose(model, frame);
        }
        if (!waterFlow) waterFlow.emplace(models, dynamics);
        if (waterFlow->active()) {
            waterFlow->advance(1.0 / timelineFps, models);
            auto mesh = waterFlow->surface();
            if (!mesh.faces.empty()) {
                Model water;
                water.kind = ObjectKind::Mesh;
                water.material = waterMaterial;
                water.mesh = std::move(mesh);
                models.push_back(std::move(water));
                waterAttached = true;
            }
        }
        View camera = view;
        const int cameraIndex = video.camera >= 0 ? video.camera : camera.camera;
        if (cameraIndex >= 0) {
            const Transform& transform = models[cameraIndex].transform;
            camera.camera = cameraIndex;
            camera.right = transform.basis[0];
            camera.up = transform.basis[1];
            camera.back = transform.basis[2];
            camera.perspective = true;
            if (video.camera >= 0) camera.zoom = 1;
            camera.target = transform.position - camera.back * (15 / camera.zoom);
        }
        if (video.camera >= 0) camera.rect = {0, 0, video.width, video.height};
        else camera = renderView(camera, video.width, video.height);
        if (settings.mode == RenderMode::Software) {
            if (!drawRaster(camera, output, error)) return false;
            if (!settings.caustics && !hasVisibleFog(models)) return true;
        }
        if (output.cancel.load(std::memory_order_relaxed)) return false;

        trace.camera = {camera.eye(), camera.right, camera.up, camera.back, camera.focal(),
                        camera.orthoScale(), camera.perspective};
        trace.timeSeconds = double(frame) / timelineFps;
        if (trace.width == 0) {
            trace.width = video.width;
            trace.height = video.height;
            trace.samples = settings.samples;
            trace.causticPhotons = settings.caustics ? settings.causticPhotons : 0;
            const auto sceneModels = models.size() - (waterAttached ? 1 : 0);
            for (std::size_t i = 0; i < sceneModels; ++i) {
                const Model& model = models[i];
                if (!objectVisible(models, int(i))) continue;
                if (isGeometry(model.kind)) { geometry.push_back(i); trace.boxes.push_back(model); }
                else if (isLight(model.kind)) { lights.push_back(i); trace.lights.push_back(model); }
                else if (isFog(model.kind)) { volumes.push_back(i); trace.volumes.push_back(model); }
            }
        }
        for (std::size_t i = 0; i < geometry.size(); ++i) trace.boxes[i].transform = models[geometry[i]].transform;
        for (std::size_t i = 0; i < lights.size(); ++i) trace.lights[i].transform = models[lights[i]].transform;
        for (std::size_t i = 0; i < volumes.size(); ++i) trace.volumes[i].transform = models[volumes[i]].transform;
        trace.boxes.resize(geometry.size());
        if (waterAttached) trace.boxes.push_back(models.back());
        renderGlobal(trace, output);
        if (output.failed) error = L"Could not render frame " + std::to_wstring(frame) + L".";
        return !output.failed && !output.cancel.load(std::memory_order_relaxed);
    }

    bool drawRaster(const View& camera, RenderJob& output, std::wstring& error) {
        if (!rasterReady) {
            if (!raster.open(video.width, video.height)) {
                error = L"Could not create the off-screen video renderer.";
                return false;
            }
            for (const auto& edit : trace.materials) {
                if (!addRasterMaterial(edit, rasterMaterials)) {
                    error = L"Could not upload a texture for the video render.";
                    return false;
                }
            }
            rasterReady = true;
        }
        buildShadowMaps(models, shadowMaps);
        if (output.cancel.load(std::memory_order_relaxed)) return false;
        if (!renderRasterFrame(camera, models, rasterMaterials, shadowMaps, output.pixels)) {
            error = L"The graphics backend could not read the rendered video frame.";
            return false;
        }
        return true;
    }
};

VideoSources Editor::videoSources() const {
    VideoSources sources{timeline.frames, timeline.fps, {}};
    sources.cameras.push_back({-1, L"Active viewport (" + widenUtf8(viewName(views[activeView])) + L")"});
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (models[i].kind == ObjectKind::Camera) sources.cameras.push_back({int(i), widenUtf8(models[i].name)});
    }
    return sources;
}

VideoFrameRenderer Editor::takeVideoSnapshot(const VideoSettings& settings) const {
    if (settings.camera >= 0 && (settings.camera >= int(models.size()) ||
        models[settings.camera].kind != ObjectKind::Camera ||
        widenUtf8(models[settings.camera].name) != settings.cameraName)) return {};
    auto snapshot = std::make_shared<VideoScene>(views[activeView], settings, renderSettings, timeline.fps, dynamicsSettings);
    snapshot->models = models;
    if (dynamics) {
        snapshot->models.resize(dynamics->modelCount);
        for (const auto& [index, position] : dynamics->original)
            snapshot->models[index].transform.position = position;
    }
    const auto materialCount = dynamics ? dynamics->materialCount : materials.size();
    for (std::size_t i = 0; i < materialCount; ++i) {
        snapshot->trace.materials.push_back(materials[i]->edit);
        if (snapshot->waterMaterial < 0 && materials[i]->edit.preset == MaterialPreset::ClearWater)
            snapshot->waterMaterial = int(i);
    }
    if (snapshot->waterMaterial < 0 &&
        std::ranges::any_of(snapshot->models, [](const Model& model) { return isEmitter(model.kind); })) {
        snapshot->waterMaterial = int(snapshot->trace.materials.size());
        snapshot->trace.materials.push_back(makePreset(MaterialPreset::ClearWater));
    }
    return [snapshot = std::move(snapshot)](int frame, RenderJob& output, std::wstring& error) {
        return snapshot->drawFrame(frame, output, error);
    };
}

void Editor::showVideoPanel() {
    if (!videoPanel) {
        videoPanel = std::make_unique<VideoPanel>(window, [this] { return videoSources(); },
                                                  [this](const VideoSettings& settings) { return takeVideoSnapshot(settings); });
    }
    videoPanel->show();
}

void Editor::addLibraryMaterial() {
    if (materialWindow) EnableWindow(materialWindow, FALSE);
    const MaterialPreset preset = chooseMaterialPreset(window);
    if (materialWindow) EnableWindow(materialWindow, TRUE);
    if (preset == MaterialPreset::None) return;
    if (materials.size() >= 256) {
        MessageBoxW(window, L"The scene supports up to 256 materials.", L"Material Library", MB_OK);
        return;
    }
    auto material = std::make_shared<Material>(Color{1, 1, 1});
    material->edit = makePreset(preset);
    if (!uploadTexture(material->edit.image, material->texture)) {
        MessageBoxW(window, L"OpenGL could not load the material.", L"Material Library", MB_OK | MB_ICONERROR);
        return;
    }
    remember();
    materials.push_back(std::move(material));
    showMaterials();
    refreshMaterials(int(materials.size()) - 1);
    repaint();
}

void Editor::openMaterial(int index) {
    if (drag.view >= 0) return;
    resetDynamics();
    if (index >= int(materials.size())) return;
    stopPlayback();
    const Material& material = *materials[index];
    MaterialEdit result;
    EnableWindow(window, FALSE);
    const bool accepted = editMaterial(materialWindow, index + 1, material.edit, result);
    EnableWindow(window, TRUE);
    if (!accepted) return;
    auto replacement = std::make_shared<Material>(Color{1, 1, 1});
    if (!uploadTexture(result.image, replacement->texture)) {
        MessageBoxW(materialWindow, L"OpenGL could not load the texture.", L"Material Editor", MB_OK | MB_ICONERROR);
        return;
    }
    replacement->edit = std::move(result);
    remember();
    materials[index] = std::move(replacement);
    refreshMaterials();
    repaint();
}

void Editor::materialMenu(POINT screen) {
    if (drag.view >= 0) return;
    POINT client = screen;
    ScreenToClient(materialWindow, &client);
    const int index = materialAt({double(client.x), double(client.y)});
    if (index < 0) return;
    resetDynamics();
    if (index >= int(materials.size())) return;

    stopPlayback();
    flyout = Flyout::None;
    repaint();
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    constexpr UINT apply = 1;
    const bool boxSelected = geometrySelected() && !meshEdit;
    AppendMenuW(menu, MF_STRING | (boxSelected ? MF_ENABLED : MF_GRAYED), apply, L"Apply");
    const BOOL command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y,
                                        0, materialWindow, nullptr);
    DestroyMenu(menu);
    if (command == apply && boxSelected && models[selected].material != index) {
        remember();
        models[selected].material = index;
        refreshMaterials();
        repaint();
    }
}

void Editor::toggleFullscreen() {
    finishWindowResize();
    endDrag();
    flyout = Flyout::None;
    constexpr UINT frameChanged = SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER;
    if (!fullscreen) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        windowedPlacement.length = sizeof(windowedPlacement);
        if (!GetWindowPlacement(window, &windowedPlacement) ||
            !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        windowedStyle = GetWindowLongPtrW(window, GWL_STYLE);
        fullscreen = true;
        SetWindowLongPtrW(window, GWL_STYLE, windowedStyle & ~WS_OVERLAPPEDWINDOW);
        const RECT r = monitor.rcMonitor;
        SetWindowPos(window, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, frameChanged);
    } else {
        fullscreen = false;
        SetWindowLongPtrW(window, GWL_STYLE, windowedStyle);
        SetWindowPlacement(window, &windowedPlacement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, frameChanged | SWP_NOMOVE | SWP_NOSIZE);
    }
    repaint();
}

void Editor::setTool(Tool value) {
    if (!toolAvailable(value)) return;
    endDrag();
    if (!meshEdit && tool == Tool::Extrude && (value <= Tool::Rotate || value == Tool::Select) &&
        !selectedFaces.empty()) {
        startMeshEdit(ComponentMode::Polygon);
        tool = value;
    }
    if (meshEdit) meshEdit->tool = value;
    else {
        tool = value;
        selectedFaces.clear();
    }
    flyout = Flyout::None;
    updateTransformMenu();
    repaint();
}

void Editor::updateTransformMenu() const {
    const HMENU editMenu = GetSubMenu(GetMenu(window), 1);
    if (editMenu) {
        const Tool current = activeTool();
        if (selectsFaces(current) || current == Tool::Select) {
            for (UINT id = moveCommand; id <= rotateCommand; ++id) CheckMenuItem(editMenu, id, MF_UNCHECKED);
        } else {
            CheckMenuRadioItem(editMenu, moveCommand, rotateCommand, moveCommand + int(current), MF_BYCOMMAND);
        }
        for (UINT i = 0; i < 3; ++i) {
            EnableMenuItem(editMenu, moveCommand+i, MF_BYCOMMAND | (toolAvailable(Tool(i)) ? MF_ENABLED : MF_GRAYED));
        }
        EnableMenuItem(editMenu, extrudeCommand, MF_BYCOMMAND | (toolAvailable(Tool::Extrude) ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(editMenu, extrudeCommand, current == Tool::Extrude ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(editMenu, bridgeCommand, current == Tool::Bridge ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(editMenu, selectCommand, current == Tool::Select ? MF_CHECKED : MF_UNCHECKED);
        EnableMenuItem(editMenu, bridgeCommand, MF_BYCOMMAND | (!meshEdit ? MF_ENABLED : MF_GRAYED));
    }
}

void Editor::renderActiveView() {
    if (!context || width < 2 || height < 2) return;
    syncCameras();
    if (views[activeView].rect.w < 2 || views[activeView].rect.h < 2) return;
    if (drag.view >= 0 && drag.kind == DragKind::Extrude) endDrag();
    stopRender();
    const View view = renderView(views[activeView], renderSettings.width, renderSettings.height);
    if (renderSettings.mode == RenderMode::GlobalIllumination) {
        startRayRender(view);
        return;
    }
    updateShadowMaps();
    RenderFrame frame{view.rect.w, view.rect.h, {}, {}};
    const auto render = [&] {
        BitmapOpenGL raster;
        if (!raster.open(frame.width, frame.height)) return false;
        std::vector<MaterialSlot> rasterMaterials;
        for (const auto& material : materials)
            if (!addRasterMaterial(material->edit, rasterMaterials)) return false;
        return renderRasterFrame(view, models, rasterMaterials, shadowMaps, frame.pixels);
    };
    if (!render()) {
        MessageBoxW(window, L"The graphics backend could not render this view.", L"Rendering", MB_OK | MB_ICONERROR);
        return;
    }
    if (renderSettings.caustics || hasVisibleFog(models)) {
        startRayRender(view, std::move(frame));
        return;
    }
    renderFrame = std::move(frame);
    showRenderWindow(view, false);
}

void Editor::showRenderWindow(const View& view, bool global) {
    const std::string name = viewName(view);
    const std::wstring title = L"Render - " + widenUtf8(name) +
                               (global ? L" - Global Illumination" : L"") +
                               (renderSettings.caustics ? L" - Caustics" : L"");
    if (!renderWindow) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW renderClass{};
        renderClass.style = CS_HREDRAW | CS_VREDRAW;
        renderClass.lpfnWndProc = renderWindowProc;
        renderClass.hInstance = instance;
        renderClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        renderClass.lpszClassName = L"3D1RenderWindow";
        if (!RegisterClassW(&renderClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

        RECT bounds{};
        GetWindowRect(window, &bounds);
        const int w = std::min(int(bounds.right - bounds.left), std::max(520, renderFrame.width));
        const int h = std::min(int(bounds.bottom - bounds.top), std::max(380, renderFrame.height));
        const HMENU menu = CreateMenu();
        const HMENU file = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, savePngCommand, L"&Save PNG...\tCtrl+S");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
        renderWindow = CreateWindowExW(0, renderClass.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
            (bounds.left + bounds.right - w) / 2, (bounds.top + bounds.bottom - h) / 2,
            w, h, window, menu, instance, &renderFrame);
        if (!renderWindow) DestroyMenu(menu);
    }
    if (!renderWindow) return;
    SetWindowTextW(renderWindow, title.c_str());
    ShowWindow(renderWindow, IsIconic(renderWindow) ? SW_RESTORE : SW_SHOW);
    SetActiveWindow(renderWindow);
    InvalidateRect(renderWindow, nullptr, FALSE);
}

void Editor::startRayRender(View view, RenderFrame raster) {
    renderJobGlobal = raster.pixels.empty();
    TraceScene scene;
    scene.camera = {view.eye(), view.right, view.up, view.back, view.focal(), view.orthoScale(), view.perspective};
    scene.width = view.rect.w;
    scene.height = view.rect.h;
    scene.samples = renderSettings.samples;
    scene.causticPhotons = renderSettings.caustics ? renderSettings.causticPhotons : 0;
    scene.timeSeconds = double(timeline.frame) / timeline.fps;
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = models[i];
        if (!objectVisible(models, i)) continue;
        if (isGeometry(model.kind)) scene.boxes.push_back(model);
        else if (isLight(model.kind)) scene.lights.push_back(model);
        else if (isFog(model.kind) && model.fog.density > 0) scene.volumes.push_back(model);
    }
    for (const auto& material : materials) scene.materials.push_back(material->edit);
    renderJobFog = !scene.volumes.empty();
    renderFrame = {scene.width, scene.height, {},
                   renderJobGlobal ? L"Preparing Global Illumination..."
                                   : scene.causticPhotons > 0 ? L"Tracing caustic photons..." : L"Rendering fog...",
                   renderJobGlobal ? 0 : -1, false};
    showRenderWindow(view, renderJobGlobal);
    if (!renderWindow) return;
    try {
        renderJob = std::make_shared<RenderJob>();
        renderJob->pixels = std::move(raster.pixels);
        if (renderJobGlobal) renderJob->preview = std::make_unique<RenderPreview>();
        renderWorker = std::thread([snapshot = std::move(scene), job = renderJob] {
            renderGlobal(snapshot, *job);
        });
        if (!SetTimer(window, renderTimer, renderPreviewIntervalMs, nullptr)) {
            stopRender();
            renderFrame.status = L"Could not track the render.";
        }
    } catch (...) {
        renderJob.reset();
        renderFrame.status = L"Could not start the render.";
    }
    InvalidateRect(renderWindow, nullptr, FALSE);
}

void Editor::pollRender() {
    if (!renderJob) return;
    if (renderJob->done.load(std::memory_order_acquire)) {
        if (renderJob->failed) {
            renderFrame.status = L"The render could not finish.";
            renderFrame.pixels.clear();
        } else {
            renderFrame.pixels = std::move(renderJob->pixels);
            renderFrame.complete = true;
            if (renderJobGlobal) renderFrame.progress = 100;
        }
        stopRender();
    } else if (renderJobGlobal) {
        try {
            if (!copyRenderPreview(renderFrame, *renderJob)) return;
        } catch (...) {
            stopRender();
            renderFrame.pixels.clear();
            renderFrame.status = L"Could not display the render preview.";
        }
    } else if (renderJob->tracingPhotons.load(std::memory_order_relaxed)) {
        renderFrame.status = L"Tracing caustic photons...";
    } else {
        const int progress = 100 * renderJob->rows.load(std::memory_order_relaxed) / renderFrame.height;
        const std::wstring stage = renderJobFog ? L"Rendering fog... " : L"Applying caustics... ";
        renderFrame.status = stage + std::to_wstring(progress) + L"%";
    }
    if (renderWindow) InvalidateRect(renderWindow, nullptr, FALSE);
}

void Editor::stopRender() {
    if (window) KillTimer(window, renderTimer);
    if (renderJob) renderJob->cancel.store(true, std::memory_order_relaxed);
    if (renderWorker.joinable()) renderWorker.join();
    renderJob.reset();
}

void Editor::addGeometry(ObjectKind kind) {
    if (kind == ObjectKind::Camera) { addCamera(true); return; }
    if (models.size() >= maxSceneObjects) return;
    std::vector<Bounds> bounds;
    int count = 0;
    for (const Model& model : models) {
        if (isGeometry(model.kind)) bounds.push_back(modelBounds(model));
        if (model.kind == kind) ++count;
    }
    const int index = int(bounds.size());
    Vec3 position{};
    for (int slot = 0; slot <= index + 40; ++slot) {
        const int column = (slot % 5 + 1) / 2 * (slot % 2 ? 1 : -1);
        const int row = (slot / 5 + 1) / 2 * ((slot / 5) % 2 ? 1 : -1);
        position = {column * 1.4, 0, row * 1.4};
        const auto overlaps = [position](const Bounds& b) {
            return position.x + halfBox > b.min.x && position.x - halfBox < b.max.x &&
                   position.y + halfBox > b.min.y && position.y - halfBox < b.max.y &&
                   position.z + halfBox > b.min.z && position.z - halfBox < b.max.z;
        };
        if (std::none_of(bounds.begin(), bounds.end(), overlaps)) break;
        if (slot == index + 40) {
            for (const Bounds& b : bounds) position.x = std::max(position.x, b.max.x + 1);
        }
    }
    remember();
    models.push_back({Transform{position}, std::string(objectNames[int(kind)]) + " " + std::to_string(count + 1),
                      -1, kind, {}, createPrimitive(kind)});
    shadowMapsDirty = true;
    select(int(models.size()) - 1);
}

std::string Editor::viewName(const View& view) const {
    std::string name = view.camera >= 0 ? models[view.camera].name : view.name;
    if (view.orbited) name += " (Orbit)";
    return name;
}

void Editor::syncCameras() {
    for (View& view : views) {
        if (view.camera < 0) continue;
        const Transform& transform = models[view.camera].transform;
        view.right = transform.basis[0];
        view.up = transform.basis[1];
        view.back = transform.basis[2];
        view.target = transform.position - view.back * (15 / view.zoom);
        view.perspective = true;
    }
}

void Editor::addCamera(bool selectNew) {
    if (models.size() >= maxSceneObjects) return;
    const int count = int(std::count_if(models.begin(), models.end(), [](const Model& model) {
        return model.kind == ObjectKind::Camera;
    }));
    const View& view = views[0];
    Transform transform{view.eye() + view.right * (count * 2.0), {{view.right, view.up, view.back}}};
    if (selectNew) remember();
    models.push_back({transform, "Camera " + std::to_string(count + 1), -1, ObjectKind::Camera, {}});
    shadowMapsDirty = true;
    if (selectNew) select(int(models.size()) - 1);
}

void Editor::lookThroughCamera() {
    if (cameraSelected()) assignCamera(activeView, selected);
}

void Editor::assignCamera(int viewIndex, int camera) {
    View& view = views[viewIndex];
    if (camera >= 0 && view.camera == camera) return;
    remember();
    if (camera < 0) {
        resetViewportCamera(viewIndex);
    } else {
        view.camera = camera;
        view.zoom = 1;
        view.orbited = false;
    }
    syncCameras();
    repaint();
}

void Editor::resetViewportCamera(int viewIndex) {
    View& view = views[viewIndex];
    View reset = defaultViews[viewIndex];
    reset.rect = view.rect;
    reset.showRenderFrame = view.showRenderFrame;
    reset.mode = view.mode;
    view = reset;
}

void Editor::chooseViewportMode(int viewIndex) {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    View& view = views[viewIndex];
    for (int i = 0; i < int(std::size(viewportModeNames)); ++i) {
        const auto name = widenUtf8(viewportModeNames[i]);
        AppendMenuW(menu, MF_STRING | (i == int(view.mode) ? MF_CHECKED : 0), i + 1, name.c_str());
    }
    const Rect button = viewportModeButton(view);
    POINT position{button.x, button.y + button.h};
    ClientToScreen(window, &position);
    const UINT item = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, position.x, position.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (item > 0 && item <= std::size(viewportModeNames) && item != UINT(view.mode) + 1) {
        remember();
        view.mode = static_cast<ViewportMode>(item - 1);
    }
}

void Editor::chooseViewportCamera(int viewIndex) {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const auto& view = views[viewIndex];
    constexpr UINT defaultCommand = 1;
    AppendMenuW(menu, MF_STRING | (view.camera < 0 ? MF_CHECKED : 0), defaultCommand, L"Default view");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    bool any = false;
    for (std::size_t i = 0; i < models.size(); ++i) {
        if (models[i].kind != ObjectKind::Camera) continue;
        auto name = widenUtf8(models[i].name);
        for (std::size_t pos = 0; (pos = name.find(L'&', pos)) != std::wstring::npos; pos += 2) name.insert(pos, 1, L'&');
        AppendMenuW(menu, MF_STRING | (view.camera == int(i) ? MF_CHECKED : 0), i + 2, name.c_str());
        any = true;
    }
    if (!any) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No cameras");
    const Rect button = viewportCameraButton(view);
    POINT position{button.x + button.w, button.y + button.h};
    ClientToScreen(window, &position);
    const UINT item = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN, position.x, position.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (item == defaultCommand) assignCamera(viewIndex, -1);
    else if (item >= 2 && item - 2 < models.size() && models[item - 2].kind == ObjectKind::Camera) {
        assignCamera(viewIndex, int(item - 2));
    }
}

std::wstring pickFile(HWND owner, bool save, bool obj = false, const std::wstring& previous = {}) {
    std::array<wchar_t, 32768> path{};
    if (save && previous.size() < path.size()) std::copy(previous.begin(), previous.end(), path.begin());
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrFilter = obj ? L"Wavefront OBJ (*.obj)\0*.obj\0All files\0*.*\0"
                            : L"3D1 Scene (*.3d1)\0*.3d1\0All files\0*.*\0";
    picker.lpstrFile = path.data();
    picker.nMaxFile = static_cast<DWORD>(path.size());
    picker.lpstrDefExt = obj ? L"obj" : L"3d1";
    picker.lpstrTitle = obj ? (save ? L"Export Wavefront OBJ" : L"Import Wavefront OBJ")
                            : (save ? L"Save Scene" : L"Open Scene");
    picker.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL accepted = save ? GetSaveFileNameW(&picker) : GetOpenFileNameW(&picker);
    return accepted ? std::wstring(path.data()) : std::wstring{};
}

bool Editor::saveScene(bool saveAs) {
    resetDynamics();
    endDrag();
    const std::wstring path = currentPath.empty() || saveAs ? pickFile(window, true, false, currentPath) : currentPath;
    if (path.empty()) return false;
    syncCameras();
    SceneDocument document;
    document.models = models;
    for (const auto& material : materials) document.materials.push_back(material->edit);
    for (std::size_t i = 0; i < views.size(); ++i) {
        const View& view = views[i];
        document.views[i] = {view.back, view.right, view.up, view.target, view.zoom,
                             view.perspective, view.orbited, view.camera, view.showRenderFrame, view.mode};
    }
    document.rendering = renderSettings;
    document.timeline = timeline;
    document.dynamics = dynamicsSettings;
    document.activeView = activeView;
    document.selected = selected;
    if (!writeScene(path.c_str(), document)) {
        MessageBoxW(window, L"Could not save the scene to this file.", L"Save Scene", MB_OK | MB_ICONERROR);
        return false;
    }
    currentPath = path;
    savedRevision = revision;
    updateTitle();
    return true;
}

void Editor::openScene() {
    resetDynamics();
    endDrag();
    const std::wstring path = pickFile(window, false);
    if (path.empty()) return;
    if (!confirmLeaveScene()) return;
    SceneDocument document;
    if (!readScene(path.c_str(), document)) {
        MessageBoxW(window, L"Could not read this 3D1 scene.", L"Open Scene", MB_OK | MB_ICONERROR);
        return;
    }
    std::vector<MaterialSlot> loaded;
    loaded.reserve(document.materials.size());
    for (auto& item : document.materials) {
        auto material = std::make_shared<Material>(Color{1, 1, 1});
        if (!uploadTexture(item.image, material->texture)) {
            MessageBoxW(window, L"Could not load the scene textures.", L"Open Scene", MB_OK | MB_ICONERROR);
            return;
        }
        material->edit = std::move(item);
        loaded.push_back(std::move(material));
    }
    stopRender();
    if (renderWindow) ShowWindow(renderWindow, SW_HIDE);
    undoHistory.clear();
    redoHistory.clear();
    materials = std::move(loaded);
    models = std::move(document.models);
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& from = document.views[i];
        View& to = views[i];
        to.back = from.back;
        to.right = from.right;
        to.up = from.up;
        to.target = from.target;
        to.zoom = from.zoom;
        to.perspective = from.perspective != 0;
        to.orbited = from.orbited != 0;
        to.camera = from.camera;
        to.showRenderFrame = from.showRenderFrame;
        to.mode = from.mode;
    }
    renderSettings = document.rendering;
    timeline = document.timeline;
    dynamicsSettings = document.dynamics;
    activeView = document.activeView;
    materialFirstRow = listOffset = 0;
    selected = -1;
    selectedModifier = -1;
    modifierListOffset = 0;
    selectedFaces.clear();
    shadowMapsDirty = true;
    setTool(Tool::Move);
    select(document.selected);
    currentPath = path;
    savedRevision = revision = ++nextRevision;
    updateTitle();
    repaint();
}

bool Editor::confirmLeaveScene() {
    if (revision == savedRevision) return true;
    const int choice = MessageBoxW(window, L"Save changes to the current scene?", editorName,
                                    MB_YESNOCANCEL | MB_ICONQUESTION);
    return choice == IDNO || (choice == IDYES && saveScene());
}

void Editor::newScene() {
    resetDynamics();
    endDrag();
    if (!confirmLeaveScene()) return;
    stopRender();
    if (renderWindow) ShowWindow(renderWindow, SW_HIDE);
    models.clear();
    materials = defaultMaterials();
    views = defaultViews;
    renderSettings = {};
    timeline = {};
    dynamicsSettings = {};
    selected = -1;
    selection.clear();
    activeView = materialFirstRow = listOffset = 0;
    refreshMaterials();
    layout(width, height);
    undoHistory.clear();
    redoHistory.clear();
    savedRevision = revision = ++nextRevision;
    currentPath.clear();
    addCamera(false);
    setTool(Tool::Move);
    updateTitle();
}

void Editor::importModels() {
    endDrag();
    const std::wstring path = pickFile(window, false, true);
    if (path.empty()) return;
    std::vector<Model> imported;
    std::wstring error;
    if (!importObj(path.c_str(), imported, error)) {
        MessageBoxW(window, error.c_str(), L"Import OBJ", MB_OK | MB_ICONERROR);
        return;
    }
    if (models.size() + imported.size() > maxSceneObjects) {
        const auto message = L"This scene would exceed " + std::to_wstring(maxSceneObjects) + L" objects.";
        MessageBoxW(window, message.c_str(), L"Import OBJ", MB_OK | MB_ICONWARNING);
        return;
    }
    remember();
    const int first = int(models.size());
    for (auto& object : imported) models.push_back(std::move(object));
    shadowMapsDirty = true;
    select(first);
    frameSelection();
    repaint();
}

void Editor::exportModels() {
    endDrag();
    const bool hasGeometry = std::any_of(models.begin(), models.end(), [&](const Model& model) {
        return isGeometry(model.kind) && objectVisible(models, int(&model - models.data()));
    });
    if (!hasGeometry) {
        MessageBoxW(window, L"There is no visible polygon geometry to export.", L"Export OBJ", MB_OK);
        return;
    }
    const std::wstring path = pickFile(window, true, true);
    if (!path.empty() && !exportObj(path.c_str(), models)) {
        MessageBoxW(window, L"Could not write the OBJ file.", L"Export OBJ", MB_OK | MB_ICONERROR);
    }
}

void Editor::updateTitle() const {
    const std::wstring document = currentPath.empty() ? L"Untitled" : std::filesystem::path(currentPath).filename().wstring();
    const std::wstring title = std::wstring(editorName) + L" - " + document + (revision == savedRevision ? L"" : L" *");
    SetWindowTextW(window, title.c_str());
}

Editor::SceneState Editor::sceneState() const {
    return {models, materials, views, renderSettings, timeline, dynamicsSettings, selected, activeView, revision,
            selection, selectedFaces};
}

void Editor::restore(SceneState&& state) {
    models = std::move(state.models);
    materials = std::move(state.materials);
    views = state.views;
    renderSettings = state.rendering;
    timeline = state.animation;
    dynamicsSettings = state.dynamics;
    revision = state.revision;
    activeView = state.activeView;
    selected = -1;
    selectedFaces.clear();
    layout(width, height);
    select(state.selected);
    selection = std::move(state.selection);
    if (selectsFaces(tool)) selectedFaces = std::move(state.faceSelection);
    shadowMapsDirty = true;
    updateTitle();
    repaint();
}

void Editor::record(SceneState&& before) {
    undoHistory.push_back(std::move(before));
    if (undoHistory.size() > undoHistoryLimit) undoHistory.erase(undoHistory.begin());
    std::size_t bytes = 0;
    std::size_t first = undoHistory.size() - 1;
    for (std::size_t i = undoHistory.size(); i-- > 0;) {
        for (const Model& model : undoHistory[i].models) {
            bytes += sizeof(Model) + model.mesh.vertices.size() * sizeof(Vec3) +
                     model.mesh.faces.size() * sizeof(MeshFace) + model.keys.size() * sizeof(TransformKey) +
                     model.defaultTextureUvs.size() * sizeof(std::array<Vec2,4>);
        }
        if (bytes > undoMeshBudgetBytes) break;
        first = i;
    }
    undoHistory.erase(undoHistory.begin(), undoHistory.begin() + first);
    redoHistory.clear();
    revision = ++nextRevision;
    updateTitle();
}

void Editor::remember() { record(sceneState()); }

void Editor::startInteraction() {
    interactionBefore = sceneState();
    interactionChanged = false;
}

void Editor::commitInteraction() {
    if (!interactionBefore) return;
    if (interactionChanged) record(std::move(*interactionBefore));
    interactionBefore.reset();
}

void Editor::undo(bool redo) {
    stopPlayback();
    resetDynamics();
    if (meshEdit) {
        undoMeshChange(redo);
        return;
    }
    endDrag();
    auto& source = redo ? redoHistory : undoHistory;
    auto& destination = redo ? undoHistory : redoHistory;
    if (source.empty()) return;
    destination.push_back(sceneState());
    SceneState state = std::move(source.back());
    source.pop_back();
    restore(std::move(state));
}

void Editor::recordMeshChange(MeshEditChange change) {
    auto& edit = *meshEdit;
    change.mode = edit.mode;
    change.components = edit.components;
    auto& history = edit.undoHistory;
    history.push_back(std::move(change));
    edit.redoHistory.clear();
    std::size_t first = history.size() - 1, bytes = 0;
    for (std::size_t i = history.size(); i-- > 0;) {
        const auto& entry = history[i];
        bytes += entry.components.size() * sizeof(std::size_t) + entry.positions.size() * sizeof(entry.positions.front()) +
                 entry.defaultUvs.size() * sizeof(entry.defaultUvs.front());
        if (entry.topology) bytes += entry.topology->vertices.size() * sizeof(Vec3) +
                                     entry.topology->faces.size() * sizeof(MeshFace);
        if (bytes > undoMeshBudgetBytes || history.size() - i > undoHistoryLimit) break;
        first = i;
    }
    history.erase(history.begin(), history.begin() + first);
}

void Editor::undoMeshChange(bool redo) {
    const bool activeEdit = drag.view >= 0 && (drag.kind == DragKind::Component || drag.kind == DragKind::Extrude);
    endDrag();
    if (activeEdit) {
        repaint();
        return;
    }
    auto& edit = *meshEdit;
    auto& source = redo ? edit.redoHistory : edit.undoHistory;
    auto& destination = redo ? edit.undoHistory : edit.redoHistory;
    if (source.empty()) return;
    auto change = std::move(source.back());
    source.pop_back();
    if (change.topology) {
        std::swap(edit.draft.mesh, *change.topology);
        std::swap(edit.draft.kind, change.kind);
        std::swap(edit.draft.defaultTextureUvs, change.defaultUvs);
    } else {
        for (auto& [index, position] : change.positions) std::swap(edit.draft.mesh.vertices[index], position);
    }
    startMeshEdit(change.mode);
    edit.components = change.components;
    refreshComponentVertices();
    destination.push_back(std::move(change));
    repaint();
}

void Editor::duplicate() {
    endDrag();
    if (selected < 0) return;
    const int original = selected;
    std::vector<int> copies(models.size(), -1);
    int next = int(models.size());
    for (int i = 0; i < int(copies.size()); ++i)
        if (i == original || isDescendant(models, i, original)) copies[i] = next++;
    if (next > int(maxSceneObjects)) return;
    remember();
    for (int i = 0; i < int(copies.size()); ++i) {
        if (copies[i] < 0) continue;
        Model clone = models[i];
        if (i != original) clone.parent = copies[clone.parent];
        clone.transform.position = clone.transform.position + axes[0] * 1.25;
        for (auto& key : clone.keys) key.pose.position = key.pose.position + axes[0] * 1.25;
        if (i == original) {
            clone.hidden = false;
            const std::string stem = clone.name.size() > 114 ? objectNames[int(clone.kind)] : clone.name;
            int suffix = 2;
            do {
                clone.name = stem + " " + std::to_string(suffix++);
            } while (std::any_of(models.begin(), models.end(), [&](const Model& model) { return model.name == clone.name; }));
        }
        models.push_back(std::move(clone));
    }
    shadowMapsDirty = true;
    select(copies[original]);
    repaint();
}

void Editor::deleteSelection() {
    endDrag();
    if (selected < 0) return;
    remember();
    const int old = selected;
    const auto roots = selectedRoots();
    std::vector<bool> removed(models.size());
    for (int i = 0; i < int(models.size()); ++i)
        removed[i] = std::any_of(roots.begin(), roots.end(), [&](int root) {
            return i == root || isDescendant(models, i, root);
        });
    removeModels(removed);
    select(models.empty() ? -1 : std::min(old, int(models.size()) - 1));
    repaint();
}

std::vector<int> Editor::removeModels(const std::vector<bool>& removed) {
    syncCameras();
    std::vector<int> remap(models.size(), -1);
    std::vector<Model> kept;
    kept.reserve(models.size());
    for (int i = 0; i < int(models.size()); ++i) {
        if (removed[i]) continue;
        remap[i] = int(kept.size());
        kept.push_back(std::move(models[i]));
    }
    for (Model& model : kept)
        if (model.parent >= 0) model.parent = remap[model.parent];
    for (View& view : views)
        if (view.camera >= 0) view.camera = remap[view.camera];
    models = std::move(kept);
    select(-1);
    selectedFaces.clear();
    shadowMapsDirty = true;
    return remap;
}

std::vector<int> Editor::selectedRoots() const {
    std::vector<bool> chosen(models.size());
    for (int index : selection) chosen[index] = true;
    std::vector<int> roots;
    for (int index : selection) {
        int parent = models[index].parent;
        while (parent >= 0 && !chosen[parent]) parent = models[parent].parent;
        if (parent < 0) roots.push_back(index);
    }
    return roots;
}

void Editor::groupSelection() {
    endDrag();
    const auto roots = selectedRoots();
    if (roots.size() < 2 || models.size() >= maxSceneObjects) return;
    Model group;
    group.kind = ObjectKind::Group;
    group.parent = models[roots.front()].parent;
    for (int index : roots) {
        group.transform.position = group.transform.position + models[index].transform.position;
        if (group.parent != models[index].parent) group.parent = -1;
    }
    group.transform.position = group.transform.position * (1.0 / roots.size());
    for (int suffix = 1; ; ++suffix) {
        group.name = "Group " + std::to_string(suffix);
        if (std::none_of(models.begin(), models.end(), [&](const Model& model) { return model.name == group.name; }))
            break;
    }
    remember();
    const int index = int(models.size());
    models.push_back(std::move(group));
    for (int root : roots) models[root].parent = index;
    shadowMapsDirty = true;
    select(index);
    repaint();
}

bool Editor::canUngroup() const {
    return std::any_of(selection.begin(), selection.end(), [&](int i) { return models[i].kind == ObjectKind::Group; });
}

void Editor::ungroupSelection() {
    endDrag();
    if (!canUngroup()) return;
    remember();
    std::vector<bool> removed(models.size());
    for (int i : selection) removed[i] = models[i].kind == ObjectKind::Group;
    std::vector<int> released;
    for (int i = 0; i < int(models.size()); ++i) {
        if (removed[i]) continue;
        int parent = models[i].parent;
        while (parent >= 0 && removed[parent]) {
            if (models[parent].hidden) models[i].hidden = true;
            parent = models[parent].parent;
        }
        if (parent != models[i].parent) {
            models[i].parent = parent;
            released.push_back(i);
        }
    }
    const auto remap = removeModels(removed);
    for (int i : released) select(remap[i], true);
    repaint();
}

bool Editor::canCut() const {
    return selection.size() == 2 && std::all_of(selection.begin(), selection.end(), [&](int i) {
        return isGeometry(models[i].kind) && objectVisible(models, i);
    });
}

void Editor::cutSelection() {
    endDrag();
    flyout = Flyout::None;
    repaint();
    if (!canCut()) return;
    const int target = selection.front(), cutter = selection.back();
    Mesh mesh;
    const CutResult result = subtractMesh(models[target], models[cutter], mesh);
    if (result != CutResult::Success) {
        const wchar_t* message = L"The objects need closed meshes with consistently oriented faces.";
        if (result == CutResult::NoOverlap) message = L"The selected objects do not overlap in volume.";
        if (result == CutResult::Empty) message = L"The cutter would remove the entire target.";
        if (result == CutResult::TooComplex) message = L"The meshes are too complex for this cut.";
        if (result == CutResult::Unstable) message = L"This cut did not produce a closed mesh. Try moving the cutter slightly.";
        MessageBoxW(window, message, L"Cut (Difference)", MB_OK | MB_ICONINFORMATION);
        return;
    }
    remember();
    Model& model = models[target];
    model.kind = ObjectKind::Mesh;
    model.mesh = std::move(mesh);
    model.defaultTextureUvs.clear();
    model.modifiers.clear();
    model.evaluatedMesh.reset();
    rebuildModifiers(model);
    models[cutter].hidden = true;
    select(-1);
    select(target);
    shadowMapsDirty = true;
    repaint();
}

void Editor::bridgeSelection() {
    flyout = Flyout::None;
    if (!geometrySelected() || selectedFaces.size() != 2) {
        if (tool != Tool::Bridge) setTool(Tool::Bridge);
        return;
    }
    Model candidate = models[selected];
    candidate.mesh = modelMesh(candidate, true);
    if (!bridgeFaces(candidate.mesh, candidate.transform, selectedFaces[0], selectedFaces[1])) {
        MessageBoxW(window, L"These faces cannot be bridged. Choose separate faces with room between their contours.",
                    L"Bridge Faces", MB_OK | MB_ICONINFORMATION);
        return;
    }
    candidate.kind = ObjectKind::Mesh;
    candidate.defaultTextureUvs.clear();
    if (!rebuildModifiers(candidate)) {
        MessageBoxW(window, L"The bridge exceeds the active Smooth modifier limit.", L"Bridge Faces", MB_OK | MB_ICONWARNING);
        return;
    }
    remember();
    models[selected] = std::move(candidate);
    selectedFaces.clear();
    shadowMapsDirty = true;
    repaint();
}

void Editor::rename() {
    endDrag();
    if (selected < 0) return;
    std::string name = models[selected].name;
    if (!editObjectName(window, name) || name == models[selected].name) return;
    remember();
    models[selected].name = std::move(name);
    repaint();
}

void Editor::changeVisibility() {
    endDrag();
    if (selected < 0) return;
    remember();
    models[selected].hidden = !models[selected].hidden;
    selectedFaces.clear();
    shadowMapsDirty = true;
    repaint();
}

void Editor::inspect() {
    endDrag();
    if (selected < 0) return;
    auto& model = models[selected];
    Transform transform = model.transform;
    if (!editObjectTransform(window, model.kind, transform)) return;
    remember();
    model.transform = transform;
    if (model.kind == ObjectKind::Group) transformGroup(selected, undoHistory.back().models);
    shadowMapsDirty = true;
    repaint();
}

void Editor::frameSelection() {
    if (selected < 0) return;
    View& view = views[activeView];
    const Model& model = models[selected];
    Bounds bounds = isGeometry(model.kind) || isFog(model.kind) ? modelBounds(model)
        : Bounds{model.transform.position - Vec3{.5, .5, .5}, model.transform.position + Vec3{.5, .5, .5}};
    if (model.kind == ObjectKind::Group) {
        for (int i = 0; i < int(models.size()); ++i) {
            if ((!isGeometry(models[i].kind) && !isFog(models[i].kind)) ||
                !objectVisible(models, i) || !isDescendant(models, i, selected)) continue;
            const Bounds child = modelBounds(models[i]);
            bounds.min = {std::min(bounds.min.x, child.min.x), std::min(bounds.min.y, child.min.y),
                          std::min(bounds.min.z, child.min.z)};
            bounds.max = {std::max(bounds.max.x, child.max.x), std::max(bounds.max.y, child.max.y),
                          std::max(bounds.max.z, child.max.z)};
        }
    }
    const Vec3 dimensions = bounds.max - bounds.min;
    const double diameter = std::max(1.0, std::sqrt(dot(dimensions, dimensions)));
    view.camera = -1;
    view.target = bounds.center();
    const double aspect = std::min(1.0, double(view.rect.w) / std::max(1, view.rect.h));
    view.zoom = std::clamp((view.perspective ? 7.8 * aspect : 7.8) / diameter, minViewZoom, maxViewZoom);
    repaint();
}

void Editor::sceneMenu(POINT screen, int row) {
    if (drag.view >= 0 || row < 0 || row >= int(models.size())) return;
    if (std::find(selection.begin(), selection.end(), row) == selection.end()) select(row);
    flyout = Flyout::None;
    repaint();
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, renameCommand, L"Rename\tF2");
    AppendMenuW(menu, MF_STRING, duplicateCommand, L"Duplicate\tCtrl+D");
    AppendMenuW(menu, MF_STRING, visibilityCommand, models[selected].hidden ? L"Show\tH" : L"Hide\tH");
    AppendMenuW(menu, MF_STRING, inspectCommand, L"Transform...");
    AppendMenuW(menu, MF_STRING, frameCommand, L"Frame Selection\tF");
    AppendMenuW(menu, MF_STRING | (selectedRoots().size() >= 2 ? MF_ENABLED : MF_GRAYED), groupCommand, L"Group");
    AppendMenuW(menu, MF_STRING | (canUngroup() ? MF_ENABLED : MF_GRAYED), ungroupCommand, L"Ungroup");
    AppendMenuW(menu, MF_STRING | (canCut() ? MF_ENABLED : MF_GRAYED), cutCommand, L"Cut (Difference)");
    if (isGeometry(models[row].kind)) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (models[row].kind == ObjectKind::Mesh && models[row].modifiers.empty()
                    ? MF_GRAYED : MF_ENABLED), convertMeshCommand, L"Convert to Mesh");
        const MeshShading shading = models[row].meshShading;
        AppendMenuW(menu, MF_STRING | (shading == MeshShading::Smooth ? MF_CHECKED : 0), shadeSmoothCommand, L"Shade Smooth");
        AppendMenuW(menu, MF_STRING | (shading == MeshShading::Flat ? MF_CHECKED : 0), shadeFlatCommand, L"Shade Flat");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, deleteCommand, L"Delete\tDel");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command) {
        if (command == convertMeshCommand || command == shadeSmoothCommand || command == shadeFlatCommand) select(row);
        SendMessageW(window, WM_COMMAND, command, 0);
    }
}

void Editor::convertToMesh() {
    if (!geometrySelected()) return;
    Model& model = models[selected];
    if (model.kind == ObjectKind::Mesh && model.modifiers.empty()) return;
    Mesh baked = modelMesh(model);
    remember();
    if (modelMesh(model, true).faces.size() != baked.faces.size()) model.defaultTextureUvs.clear();
    if (model.meshShading == MeshShading::Default && !baked.normals.empty()) model.meshShading = MeshShading::Smooth;
    baked.normals.clear();
    model.kind = ObjectKind::Mesh;
    model.mesh = std::move(baked);
    model.modifiers.clear();
    model.evaluatedMesh.reset();
    rebuildModifiers(model);
    selectedFaces.clear();
    selectedModifier = -1;
    modifierListOffset = 0;
    shadowMapsDirty = true;
    repaint();
}

void Editor::setMeshShading(MeshShading mode) {
    if (!geometrySelected() || models[selected].meshShading == mode) return;
    Model candidate = models[selected];
    candidate.meshShading = mode;
    if (!rebuildModifiers(candidate)) return;
    remember();
    models[selected] = std::move(candidate);
    repaint();
}

void Editor::addLight(ObjectKind kind) {
    if (models.size() >= maxSceneObjects) return;
    const int count = int(std::count_if(models.begin(), models.end(), [kind](const Model& model) {
        return model.kind == kind;
    }));
    Transform transform;
    Light light;
    if (kind == ObjectKind::Omni) {
        transform.position = {-2.0 + count * 1.5, 3.5, 2.5};
    } else if (kind == ObjectKind::Spot) {
        transform.position = {2.5 + count * 1.5, 4, 3};
        transform.basis[2] = normalized(transform.position);
        transform.basis[0] = normalized(cross(axes[1], transform.basis[2]));
        transform.basis[1] = cross(transform.basis[2], transform.basis[0]);
    } else {
        transform.position = {-3.5 + count * 1.5, 3, -2.5};
        light.power = .85;
        light.color = {.72f, .83f, 1.f};
    }
    remember();
    models.push_back({transform, std::string(objectNames[int(kind)]) + " " + std::to_string(count + 1),
                      -1, kind, light});
    shadowMapsDirty = true;
    select(int(models.size()) - 1);
}

void Editor::addFog() {
    if (models.size() >= maxSceneObjects) return;
    const auto count = std::ranges::count_if(models, [](const Model& model) { return isFog(model.kind); });
    constexpr std::array<double, 3> initialSize{5, 2.5, 5};
    Model volume;
    volume.kind = ObjectKind::Fog;
    volume.name = "Fog " + std::to_string(count + 1);
    volume.transform.position = {double(count) * 2, 1.25, 0};
    volume.transform.size = initialSize;
    remember();
    models.push_back(std::move(volume));
    select(int(models.size()) - 1);
    setTool(Tool::Move);
}

std::vector<Editor::SceneRow> Editor::sceneRows() const {
    std::vector<std::vector<int>> children(models.size() + 1);
    for (int i = 0; i < int(models.size()); ++i) children[models[i].parent + 1].push_back(i);
    std::vector<SceneRow> rows, pending;
    for (auto it = children.front().rbegin(); it != children.front().rend(); ++it) pending.push_back({*it, 0});
    while (!pending.empty()) {
        const SceneRow row = pending.back();
        pending.pop_back();
        rows.push_back(row);
        if (!models[row.index].expanded) continue;
        const auto& branch = children[row.index + 1];
        for (auto it = branch.rbegin(); it != branch.rend(); ++it) pending.push_back({*it, row.depth + 1});
    }
    return rows;
}

void Editor::select(int model, bool toggle) {
    if (toggle) {
        if (model < 0) return;
        const auto entry = std::find(selection.begin(), selection.end(), model);
        if (entry != selection.end()) selection.erase(entry);
        else selection.push_back(model);
        model = selection.empty() ? -1 : selection.back();
    } else {
        selection.clear();
        if (model >= 0) selection.push_back(model);
    }
    if (selected != model) {
        selectedFaces.clear();
        selectedModifier = model >= 0 && !models[model].modifiers.empty() ? 0 : -1;
        modifierListOffset = 0;
    }
    selected = model;
    if (selected >= 0)
        for (int parent = models[selected].parent; parent >= 0; parent = models[parent].parent)
            models[parent].expanded = true;
    const auto rows = sceneRows();
    listOffset = std::clamp(listOffset, 0, std::max(0, int(rows.size()) - visibleRows()));
    refreshMaterials(selected >= 0 ? models[selected].material : -1);
    if (selected < 0) return;
    const auto entry = std::find_if(rows.begin(), rows.end(), [&](SceneRow row) { return row.index == selected; });
    if (entry == rows.end()) return;
    const int row = int(entry - rows.begin());
    if (row < listOffset) listOffset = row;
    if (row >= listOffset + visibleRows()) listOffset = row - visibleRows() + 1;
}

void Editor::changeTransparency(Vec2 p) {
    const Rect track = transparencyTrack();
    const double value = std::clamp((p.x - track.x - 3) / (track.w - 6), 0.0, 1.0);
    models[selected].light.shadowTransparency = std::round(value * 100) / 100;
    if (interactionBefore) {
        interactionChanged = models[selected].light.shadowTransparency !=
                             interactionBefore->models[selected].light.shadowTransparency;
    }
}

void Editor::openTextureMeshEditor() {
    if (!geometrySelected()) return;
    const Model& current = models[selected];
    const Mesh& base = modelMesh(current, true);
    TextureUvs defaults = current.defaultTextureUvs;
    const bool startUnfolded = defaults.empty() && hasStackedTextureUvs(base);
    if (startUnfolded || (!defaults.empty() && hasStackedTextureUvs(base, &defaults))) {
        defaults = unfoldTextureUvs(base);
    } else if (defaults.empty()) {
        defaults.reserve(base.faces.size());
        for (const auto& face : base.faces) defaults.push_back(face.uv);
    }
    std::vector<TextureChoice> textures{{-1, nullptr, L"No texture"}};
    for (int i = 0; i < int(materials.size()); ++i) {
        const auto& edit = materials[i]->edit;
        if (edit.image.pixels.empty()) continue;
        textures.push_back({i, &edit, L"Material " + std::to_wstring(i+1) + L"  -  " + edit.image.filename});
    }
    const int original = current.material >= 0 && !materials[current.material]->edit.image.pixels.empty() ? current.material : -1;
    int texture = original;
    TextureUvs result;
    if (!editTextureMesh(window, base, current.transform, defaults, startUnfolded, textures, texture, result)) return;
    bool uvChanged = false;
    for (std::size_t i = 0; i < result.size() && !uvChanged; ++i) {
        for (std::size_t j = 0; j < base.faces[i].count; ++j) {
            const Vec2 a = result[i][j], b = base.faces[i].uv[j];
            if (a.x != b.x || a.y != b.y) { uvChanged = true; break; }
        }
    }
    if (!uvChanged && texture == original) return;
    Model candidate = current;
    if (uvChanged) {
        candidate.mesh = base;
        candidate.defaultTextureUvs = std::move(defaults);
        for (std::size_t i = 0; i < result.size(); ++i) candidate.mesh.faces[i].uv = result[i];
        if (!rebuildModifiers(candidate)) {
            MessageBoxW(window, L"Could not update the UV mesh with these modifiers.", L"Texture Mesh Editor",
                        MB_OK | MB_ICONERROR);
            return;
        }
    }
    MaterialSlot replacement;
    if (texture != original) {
        if (materials.size() >= 256) {
            MessageBoxW(window, L"The scene already has 256 materials.", L"Texture Mesh Editor", MB_OK | MB_ICONWARNING);
            return;
        }
        MaterialEdit edit{Surface{{.57f,.63f,.72f}}, {}};
        if (current.material >= 0) edit = materials[current.material]->edit;
        edit.image = texture >= 0 ? materials[texture]->edit.image : TextureImage{};
        edit.repeatX = texture >= 0 ? materials[texture]->edit.repeatX : 1;
        edit.repeatY = texture >= 0 ? materials[texture]->edit.repeatY : 1;
        auto next = std::make_shared<Material>(edit.surface.tint);
        if (!uploadTexture(edit.image, next->texture)) {
            MessageBoxW(window, L"OpenGL could not load the selected texture.", L"Texture Mesh Editor", MB_OK | MB_ICONERROR);
            return;
        }
        next->edit = std::move(edit);
        replacement = std::move(next);
    }
    remember();
    if (replacement) {
        candidate.material = int(materials.size());
        materials.push_back(std::move(replacement));
    }
    models[selected] = std::move(candidate);
    select(selected);
    repaint();
}

bool Editor::propertyClick(Vec2 p) {
    if (!properties().contains(p)) return false;
    if (inspectButton().contains(p)) { inspect(); return true; }
    if (emitterSelected()) {
        if (emitterSettingsButton().contains(p)) configureEmitter();
        return true;
    }
    if (geometrySelected()) {
        if (textureMeshButton().contains(p)) { openTextureMeshEditor(); return true; }
        for (int i = 0; i < 3; ++i) {
            if (componentButton(i).contains(p)) { startMeshEdit(ComponentMode(i)); return true; }
        }
        if (models[selected].kind == ObjectKind::Plane) {
            for (int i = 0; i < 2; ++i) {
                if (planeSegmentsButton(i).contains(p)) { setPlaneSegments(i); return true; }
            }
        }
        return modifierClick(p);
    }
    if (cameraSelected() && lightSetupButton().contains(p)) lookThroughCamera();
    if (fogSelected()) {
        auto& settings = models[selected].fog;
        auto updated = settings;
        bool changed = false;
        if (fogOption(0).contains(p)) {
            updated.style = settings.style == FogStyle::Mist ? FogStyle::Cloud : FogStyle::Mist;
            changed = true;
        } else if (fogOption(1).contains(p)) {
            changed = editWholeNumber(window, L"Fog Density", L"Density (%)",
                                      updated.density, minFogDensity, maxFogDensity) && updated.density != settings.density;
        } else if (fogOption(2).contains(p)) {
            changed = editWholeNumber(window, L"Fog Noise Scale", L"Noise scale",
                                      updated.detail, minFogDetail, maxFogDetail) && updated.detail != settings.detail;
        } else if (fogOption(3).contains(p)) {
            changed = editWholeNumber(window, L"Fog Flow", L"Flow (%)",
                                      updated.flow, minFogFlow, maxFogFlow) && updated.flow != settings.flow;
        } else if (fogOption(4).contains(p)) changed = editLightColor(window, updated.color);
        else if (fogOption(fogQualityRow).contains(p)) {
            const HMENU menu = CreatePopupMenu();
            if (!menu) return true;
            for (std::size_t i = 0; i < fogQualityMenuNames.size(); ++i) {
                const bool active = std::size_t(settings.quality) == i;
                AppendMenuW(menu, MF_STRING | (active ? MF_CHECKED : MF_UNCHECKED),
                            UINT_PTR(i + 1), fogQualityMenuNames[i]);
            }
            const auto button = fogOption(fogQualityRow);
            POINT anchor{button.x, button.y + button.h};
            ClientToScreen(window, &anchor);
            const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, anchor.x, anchor.y,
                                               0, window, nullptr);
            DestroyMenu(menu);
            if (choice != 0) {
                updated.quality = FogQuality(choice - 1);
                changed = updated.quality != settings.quality;
            }
        } else if (fogShadowsButton().contains(p)) {
            updated.shadows = !settings.shadows;
            changed = true;
        }
        if (changed) {
            remember();
            settings = updated;
        }
        return true;
    }
    if (!lightSelected()) return true;
    Model& model = models[selected];
    if (lightColorButton().contains(p)) {
        Color color = model.light.color;
        if (editLightColor(window, color)) {
            remember();
            model.light.color = color;
        }
    } else if (lightSetupButton().contains(p)) {
        Light light = model.light;
        if (editSceneLight(window, model.kind, light)) {
            remember();
            model.light = light;
            shadowMapsDirty = true;
        }
    } else if (intensityButton().contains(p)) {
        double power = model.light.power;
        if (editLightIntensity(window, power) && power != model.light.power) {
            remember();
            model.light.power = power;
        }
    } else if (shadowsButton().contains(p)) {
        remember();
        model.light.shadows = !model.light.shadows;
        shadowMapsDirty = true;
    } else if (model.light.shadows) {
        for (int i = 0; i < int(std::size(bitmapSizes)); ++i) {
            if (!bitmapOption(i).contains(p)) continue;
            if (model.light.shadowSize == bitmapSizes[i]) return true;
            remember();
            model.light.shadowSize = bitmapSizes[i];
            shadowMapsDirty = true;
            return true;
        }
        if (bitmapOption(3).contains(p)) {
            int size = model.light.shadowSize;
            if (editShadowSize(window, size) && size != model.light.shadowSize) {
                remember();
                model.light.shadowSize = size;
                shadowMapsDirty = true;
            }
            return true;
        }
        if (transparencyTrack().contains(p)) {
            startInteraction();
            adjustingTransparency = true;
            changeTransparency(p);
            SetCapture(window);
        }
    }
    return true;
}

bool Editor::commitModifiers(std::vector<SmoothModifier> modifiers) {
    Model& model = models[selected];
    Model updated = model;
    updated.modifiers = std::move(modifiers);
    if (!rebuildModifiers(updated)) {
        MessageBoxW(window, L"Smooth exceeds the mesh limit. Use fewer levels or disable another modifier.",
                    L"Smooth", MB_OK | MB_ICONWARNING);
        return false;
    }
    remember();
    model.modifiers = std::move(updated.modifiers);
    model.evaluatedMesh = std::move(updated.evaluatedMesh);
    shadowMapsDirty = true;
    return true;
}

bool Editor::modifierClick(Vec2 point) {
    auto modifiers = models[selected].modifiers;
    const Rect list = modifiersList();
    constexpr int rowHeight = 22;
    if (addModifierButton().contains(point)) {
        if (modifiers.size() < maxModifiers && chooseSmoothModifier(window)) {
            modifiers.push_back({});
            const int index = int(modifiers.size()) - 1;
            if (commitModifiers(std::move(modifiers))) {
                selectedModifier = index;
                modifierListOffset = std::max(0, index - list.h / rowHeight + 1);
            }
        }
    } else if (removeModifierButton().contains(point) && selectedModifier >= 0) {
        modifiers.erase(modifiers.begin() + selectedModifier);
        const int count = int(modifiers.size());
        if (commitModifiers(std::move(modifiers))) {
            selectedModifier = std::min(selectedModifier, count - 1);
            modifierListOffset = std::clamp(modifierListOffset, 0, std::max(0, count - list.h / rowHeight));
        }
    } else if (list.contains(point)) {
        const int row = int(point.y - list.y) / rowHeight;
        if (row < list.h / rowHeight && row + modifierListOffset < int(modifiers.size())) {
            selectedModifier = row + modifierListOffset;
        }
    } else if (selectedModifier >= 0 && activeModifierButton().contains(point)) {
        modifiers[selectedModifier].active = !modifiers[selectedModifier].active;
        commitModifiers(std::move(modifiers));
    } else if (selectedModifier >= 0 && modifierLevelsButton().contains(point)) {
        int levels = modifiers[selectedModifier].levels;
        if (editWholeNumber(window, L"Smooth Levels", L"Subdivision levels", levels, 1, maxSmoothLevels) &&
            levels != modifiers[selectedModifier].levels) {
            modifiers[selectedModifier].levels = levels;
            commitModifiers(std::move(modifiers));
        }
    }
    return true;
}

void Editor::mouseDown(Vec2 p, bool orbit, bool toggle, bool marquee) {
    if (drag.view >= 0 || adjustingTransparency || scrubbingTimeline) return;
    const int index = viewAt(p);
    if (orbit && index >= 0) {
        stopPlayback();
        flyout = Flyout::None;
        activeView = index;
        const View& view = views[index];
        drag = {};
        drag.view = index;
        drag.kind = DragKind::Orbit;
        drag.mouse = p;
        drag.initialBack = view.back;
        drag.initialRight = view.right;
        drag.initialUp = view.up;
        drag.wasOrbited = view.orbited;
        drag.initialCamera = view.camera;
        altUsedToOrbit = true;
        SetCapture(window);
        return;
    }
    if (dynamicsButton().contains(p)) {
        if (!meshEdit) flyout = flyout == Flyout::Dynamics ? Flyout::None : Flyout::Dynamics;
        return;
    }
    if (!meshEdit && flyout == Flyout::Dynamics && dynamicsMenu().contains(p)) {
        const int action = int(p.y - dynamicsMenu().y) / dynamicsMenuRowHeight;
        flyout = Flyout::None;
        switch (action) {
        case 0: configureDynamics(); break;
        case 1: toggleDynamics(); break;
        case 2: resetDynamics(); break;
        case 3: bakeDynamics(); break;
        case 4: addEmitter(false); break;
        case 5: addEmitter(true); break;
        case 6: keepWaterSurface(); break;
        }
        return;
    }
    resetDynamics();
    if (!timelinePanel().contains(p)) stopPlayback();
    if (selectButton().contains(p)) {
        setTool(Tool::Select);
        return;
    }
    if (effectsButton().contains(p)) {
        if (!meshEdit) flyout = flyout == Flyout::Effects ? Flyout::None : Flyout::Effects;
        return;
    }
    if (!meshEdit && flyout == Flyout::Effects && effectsMenu().contains(p)) {
        flyout = Flyout::None;
        addFog();
        return;
    }
    if (transformButton().contains(p)) {
        flyout = flyout == Flyout::Transform ? Flyout::None : Flyout::Transform;
        return;
    }
    if (flyout == Flyout::Transform && transformMenu().contains(p)) {
        setTool(static_cast<Tool>(int(p.y - transformMenu().y) / 36));
        return;
    }
    if (modelingButton().contains(p)) {
        flyout = flyout == Flyout::Modeling ? Flyout::None : Flyout::Modeling;
        return;
    }
    if (flyout == Flyout::Modeling && modelingMenu().contains(p)) {
        const int item = int(p.y - modelingMenu().y) / 36;
        if (item == 0) setTool(Tool::Extrude);
        else if (!meshEdit && item == 1) cutSelection();
        else if (!meshEdit) bridgeSelection();
        return;
    }
    if (meshEdit) {
        flyout = Flyout::None;
        for (int i = 0; i < 3; ++i) {
            if (componentButton(i).contains(p)) { startMeshEdit(ComponentMode(i)); return; }
        }
        if (index >= 0) {
            activeView = index;
            const auto& view = views[index];
            const bool header = viewportNameArea(view).contains(p) || viewportModeButton(view).contains(p) ||
                                viewportCameraButton(view).contains(p);
            if (marquee && !header) beginMarquee(index, p, toggle);
            else beginComponentDrag(index, p, header, toggle);
        }
        return;
    }
    if (fullscreenButton().contains(p)) {
        toggleFullscreen();
        return;
    }
    if (toolbarButton().contains(p)) {
        flyout = flyout == Flyout::Creation ? Flyout::None : Flyout::Creation;
        return;
    }
    if (lightingButton().contains(p)) {
        flyout = flyout == Flyout::Lighting ? Flyout::None : Flyout::Lighting;
        return;
    }
    if (flyout == Flyout::Crystals && crystalMenu().contains(p)) {
        flyout = Flyout::None;
        addGeometry(crystalKinds[int(p.y - crystalMenu().y) / 36]);
        return;
    }
    if ((flyout == Flyout::Creation || flyout == Flyout::Crystals) && boxMenu().contains(p)) {
        const auto item = std::size_t((p.y - boxMenu().y) / 36);
        if (item == std::size(creationKinds)) flyout = Flyout::Crystals;
        else {
            flyout = Flyout::None;
            addGeometry(creationKinds[item]);
        }
        return;
    }
    if (flyout == Flyout::Lighting && lightingMenu().contains(p)) {
        addLight(static_cast<ObjectKind>(1 + int(p.y - lightingMenu().y) / 36));
        flyout = Flyout::None;
        return;
    }
    if (timelineClick(p)) return;
    stopPlayback();
    flyout = Flyout::None;
    if (propertyClick(p)) return;
    const Rect list = modelList();
    if (list.contains(p)) {
        const int row = static_cast<int>(p.y - list.y) / modelRowHeight;
        const int index = row + listOffset;
        const auto rows = sceneRows();
        if (row < visibleRows() && index < int(rows.size())) {
            const auto [model, depth] = rows[index];
            const int x = list.x + 14 + std::min(depth, 6) * 12;
            if (!toggle && models[model].kind == ObjectKind::Group && p.x >= x && p.x < x + 20) {
                if (models[model].expanded && selected >= 0 && isDescendant(models, selected, model)) select(model);
                models[model].expanded = !models[model].expanded;
                listOffset = std::min(listOffset, std::max(0, int(sceneRows().size()) - visibleRows()));
            } else select(model, toggle);
        }
        return;
    }

    if (index < 0) return;
    activeView = index;
    const View& view = views[index];

    if (!toggle && (tool == Tool::Move || tool == Tool::Scale)) {
        const auto planes = planeHandles(view);
        for (int axis = 0; axis < 3; ++axis) {
            if (!hitPlaneHandle(planes[axis], p)) continue;
            const auto& transform = models[selected].transform;
            drag = {};
            drag.view = index;
            drag.plane = axis;
            drag.mouse = p;
            drag.initialModel = transform;
            startInteraction();
            SetCapture(window);
            return;
        }
    }

    double bestDistance = 49;
    int bestAxis = -1;
    Vec2 tangent{};
    const auto consider = [&](int axis, Vec2 start, Vec2 end, double minFraction) {
        if (toggle) return;
        const Vec2 direction = end - start;
        const double lengthSquared = dot(direction, direction);
        if (lengthSquared < 1e-6) return;
        const double fraction = dot(p - start, direction) / lengthSquared;
        if (minFraction > 0 && fraction < minFraction) return;
        const Vec2 offset = p - (start + direction * std::clamp(fraction, 0.0, 1.0));
        const double distance = dot(offset, offset);
        if (distance >= bestDistance) return;
        bestDistance = distance;
        bestAxis = axis;
        tangent = direction * (1 / std::sqrt(lengthSquared));
    };
    if (tool == Tool::Rotate) {
        if (!toggle) bestAxis = pickRotationRing(view, p, tangent);
    } else {
        const auto viewHandles = handles(view);
        for (int i = 0; i < 3; ++i) {
            const Handle& handle = viewHandles[i];
            if (handle.visible) consider(i, handle.start, handle.tip, .24);
        }
    }
    if (bestAxis >= 0) {
        if (tool == Tool::Extrude) {
            beginExtrusion(index, p, axes[bestAxis], true);
            return;
        }
        drag = {};
        drag.view = index;
        drag.axis = bestAxis;
        drag.mouse = p;
        drag.initialModel = models[selected].transform;
        drag.tangent = tangent;
        startInteraction();
        SetCapture(window);
        return;
    }
    if (viewportModeButton(view).contains(p)) {
        chooseViewportMode(index);
        return;
    }
    if (viewportCameraButton(view).contains(p)) {
        chooseViewportCamera(index);
        return;
    }
    if (viewportNameArea(view).contains(p)) return;

    const Ray ray = view.ray(p);
    double nearest = farPlane;
    int picked = -1;
    for (int i = 0; !selectsFaces(tool) && i < int(models.size()); ++i) {
        const Model& model = models[i];
        if (!objectVisible(models, i) || isGeometry(model.kind) || model.kind == ObjectKind::Group || i == view.camera) continue;
        const double depth = view.depth(model.transform.position);
        if (depth <= nearPlane || depth >= nearest) continue;
        const Vec2 delta = p - view.project(model.transform.position);
        if (dot(delta, delta) > 12 * 12) continue;
        nearest = depth;
        picked = i;
    }
    if (picked >= 0) {
        select(picked, toggle);
        return;
    }
    SurfaceHit hit{farPlane};
    for (int i = 0; i < int(models.size()); ++i) {
        const Model& model = models[i];
        if (!isGeometry(model.kind) || !objectVisible(models, i)) continue;
        const bool cage = selectsFaces(tool) && i == selected;
        const SurfaceHit result = model.mesh.faces.empty() && (cage || !model.evaluatedMesh)
            ? intersectBox(ray, model.transform, nearPlane, hit.distance)
            : ModelCollider(model, cage).intersect(ray, nearPlane, hit.distance);
        if (result.distance >= hit.distance) continue;
        hit = result;
        picked = i;
    }
    const int previouslySelected = selected;
    if (selectsFaces(tool) && picked >= 0) {
        select(picked);
        if (picked != previouslySelected && models[picked].evaluatedMesh)
            hit = ModelCollider(models[picked], true).intersect(ray, nearPlane, farPlane);
        if (hit.face == noFace) return;
        if (tool == Tool::Extrude) {
            pickExtrusionFace(index, p, hit.face, toggle);
            return;
        }
        if (selectedFaces.size() == 2 &&
            !std::binary_search(selectedFaces.begin(), selectedFaces.end(), hit.face)) selectedFaces.clear();
        const auto entry = std::lower_bound(selectedFaces.begin(), selectedFaces.end(), hit.face);
        if (entry != selectedFaces.end() && *entry == hit.face) selectedFaces.erase(entry);
        else selectedFaces.insert(entry, hit.face);
        return;
    }
    if (selectsFaces(tool) && toggle) return;
    if (picked < 0 && !selectsFaces(tool)) {
        for (int i = 0; i < int(models.size()); ++i) {
            if (!isFog(models[i].kind) || !objectVisible(models, i)) continue;
            const auto volume = intersectBox(ray, models[i].transform, nearPlane, hit.distance);
            if (volume.distance >= hit.distance) continue;
            hit = volume;
            picked = i;
        }
    }
    if (picked < 0 && cameraSelected()) return;
    select(picked, toggle);
}

void Editor::pickExtrusionFace(int index, Vec2 point, std::size_t face, bool toggle) {
    auto& faces = meshEdit ? meshEdit->components : selectedFaces;
    const auto entry = std::lower_bound(faces.begin(), faces.end(), face);
    const bool included = entry != faces.end() && *entry == face;
    if (toggle) {
        if (included) faces.erase(entry);
        else faces.insert(entry, face);
    } else if (!included) faces = {face};
    if (meshEdit) refreshComponentVertices();
    if (toggle) return;

    const auto& model = extrusionModel();
    Vec3 normal{};
    for (auto selectedFace : faces) {
        const auto polygon = faceGeometry(model, selectedFace, true);
        normal = normal + cross(polygon.u, polygon.triangle ? polygon.v : polygon.diagonal);
        if (!polygon.triangle) normal = normal + cross(polygon.diagonal, polygon.v);
    }
    normal = normalized(normal);
    if (dot(normal,normal) < .5) normal = faceGeometry(model, face, true).normal;
    beginExtrusion(index, point, normal);
}

void Editor::beginExtrusion(int index, Vec2 point, Vec3 direction, bool axisDrag) {
    const View& view = views[index];
    const Model& model = extrusionModel();
    const Vec3 center = meshEdit ? componentCenter() : extrusionCenter();
    const double depth = view.depth(center);
    if (depth <= nearPlane) return;

    drag = {};
    drag.view = index;
    drag.kind = DragKind::Extrude;
    drag.mouse = point;
    extrusion = {};
    extrusion.localDirection = localVector(model.transform, direction);
    extrusion.signedDirection = axisDrag;
    const double scale = view.perspective ? view.focal() / depth : view.orthoScale();
    const double towardCamera = dot(direction, view.back);
    Vec2 projected = Vec2{dot(direction, view.right), -dot(direction, view.up)} * scale;
    if (view.perspective) {
        projected = projected + (view.project(center) - view.rect.center()) * (towardCamera / depth);
    }
    const double length = std::sqrt(dot(projected, projected));
    if (length < scale * .26) {
        drag.tangent = {0, -1};
        extrusion.pixelsPerUnit = scale;
    } else {
        drag.tangent = projected * (1 / length);
        extrusion.pixelsPerUnit = length;
        if (view.perspective) extrusion.perspectiveFactor = towardCamera / depth;
    }
    if (towardCamera > 0 || (axisDrag && towardCamera < 0)) {
        double nearest = depth;
        for (const auto index : extrusionFaces()) {
            const auto face = faceGeometry(model, index, true);
            const auto corners = face.corners();
            for (std::size_t i = 0; i < face.count(); ++i) nearest = std::min(nearest, view.depth(corners[i]));
        }
        const double limit = std::clamp((nearest - nearPlane * 2) / std::abs(towardCamera), 0.0, 100.0);
        if (towardCamera > 0) extrusion.maximum = limit;
        else extrusion.minimum = -limit;
    }
    if (!meshEdit) {
        updateShadowMaps();
        startInteraction();
    }
    SetCapture(window);
}

void Editor::updateExtrusion(Vec2 delta) {
    const double pixels = dot(delta, drag.tangent);
    const double projected = pixels / extrusion.pixelsPerUnit;
    const double distance = projected / std::max(.1, 1 + projected * extrusion.perspectiveFactor);
    extrusion.distance = std::clamp(distance, extrusion.signedDirection ? extrusion.minimum : 0.0, extrusion.maximum);
    if (std::abs(pixels) < 3 || std::abs(extrusion.distance) < .0001) extrusion.distance = 0;
    Model& model = meshEdit ? meshEdit->draft : models[selected];
    if (extrusion.distance == 0) {
        if (extrusion.changed) {
            model.mesh = std::move(extrusion.before);
            if (meshEdit) refreshComponentVertices();
        }
        extrusion.changed = false;
        return;
    }
    const Vec3 offset = extrusion.localDirection * extrusion.distance;
    if (!extrusion.changed) {
        if (extrusion.unavailable) return;
        Mesh result = modelMesh(model, true);
        if (!prepareExtrusion(result, extrusionFaces(), extrusion.topology)) {
            extrusion.unavailable = true;
            return;
        }
        extrusion.before = std::move(model.mesh);
        model.mesh = std::move(result);
        extrusion.changed = true;
        if (meshEdit) refreshComponentVertices();
    }
    for (const auto& moved : extrusion.topology.moved) model.mesh.vertices[moved.index] = moved.original + offset;
}

void Editor::finishExtrusion(bool commit) {
    if (drag.view < 0 || drag.kind != DragKind::Extrude) return;
    const wchar_t* failure = extrusion.unavailable
        ? L"Select a region with a boundary and a manifold mesh within the editor limits." : nullptr;
    if (extrusion.changed) {
        Model& model = meshEdit ? meshEdit->draft : models[selected];
        if (commit) {
            const auto wallStart = meshEdit ? extrusion.before.faces.size()
                                            : modelMesh(interactionBefore->models[selected], true).faces.size();
            for (std::size_t i = wallStart; i < model.mesh.faces.size(); ++i) {
                const auto& face = model.mesh.faces[i];
                const auto& points = model.mesh.vertices;
                const Vec3 origin = points[face.vertices[0]];
                const Vec3 area = cross(points[face.vertices[1]] - origin, points[face.vertices[face.count-1]] - origin);
                if (dot(area,area) > 1e-20) continue;
                failure = L"That direction collapses a side of the extrusion. Try another axis or drag the faces.";
                break;
            }
            if (!failure && !orientExtrusion(model.mesh, extrusion.topology))
                failure = L"That direction gives the extrusion zero volume. Try another axis or drag the faces.";
            if (!failure && !meshEdit && !rebuildModifiers(model))
                failure = L"Extruding these faces would exceed the Smooth mesh limit.";
        }
        if (commit && !failure) {
            if (meshEdit) {
                MeshEditChange change{};
                change.topology = std::move(extrusion.before);
                change.kind = model.kind;
                change.defaultUvs = std::move(model.defaultTextureUvs);
                recordMeshChange(std::move(change));
            }
            if (model.kind == ObjectKind::Plane) model.kind = ObjectKind::Mesh;
            model.defaultTextureUvs.clear();
            if (meshEdit) refreshComponentVertices();
            else {
                shadowMapsDirty = true;
                interactionChanged = true;
            }
        } else {
            model.mesh = std::move(extrusion.before);
            if (meshEdit) refreshComponentVertices();
        }
    }
    extrusion = {};
    if (commit && failure) MessageBoxW(window, failure, L"Extrude", MB_OK | MB_ICONWARNING);
}

void Editor::setPlaneSegments(int axis) {
    Model candidate = models[selected];
    int& count = axis == 0 ? candidate.planeSegmentsX : candidate.planeSegmentsY;
    const int previous = count;
    if (!editWholeNumber(window, L"Plane Segments", axis == 0 ? L"X segments" : L"Y segments",
                         count, 1, maxPlaneSegments) || count == previous) return;
    candidate.mesh = createPlane(candidate.planeSegmentsX, candidate.planeSegmentsY);
    candidate.defaultTextureUvs.clear();
    if (!rebuildModifiers(candidate)) {
        MessageBoxW(window, L"This grid exceeds the active Smooth modifier limit.", L"Plane Segments", MB_OK | MB_ICONWARNING);
        return;
    }
    remember();
    models[selected] = std::move(candidate);
    selectedFaces.clear();
    shadowMapsDirty = true;
}

void Editor::startMeshEdit(ComponentMode mode) {
    if (!geometrySelected() || !objectVisible(models, selected)) return;
    flyout = Flyout::None;
    if (meshEdit && meshEdit->mode == mode) return;
    endDrag();
    if (!meshEdit) {
        MeshEdit edit{};
        edit.draft = models[selected];
        edit.draft.mesh = modelMesh(models[selected], true);
        edit.draft.mesh.normals.clear();
        if (tool == Tool::Select) edit.tool = Tool::Select;
        if (mode == ComponentMode::Polygon && tool == Tool::Extrude) {
            edit.components = selectedFaces;
            edit.tool = Tool::Extrude;
        }
        meshEdit = std::move(edit);
    } else meshEdit->components.clear();
    meshEdit->mode = mode;
    if (mode == ComponentMode::Edge) {
        auto& edges = meshEdit->edges;
        edges.clear();
        for (const auto& face : meshEdit->draft.mesh.faces) {
            for (std::size_t i = 0; i < face.count; ++i) {
                const auto a = face.vertices[i], b = face.vertices[(i + 1) % face.count];
                edges.push_back({std::min(a,b), std::max(a,b)});
            }
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    }
    refreshComponentVertices();
    if (!toolAvailable(meshEdit->tool)) meshEdit->tool = Tool::Move;
    updateTransformMenu();
}

void Editor::finishMeshEdit(bool commit) {
    if (commit) finishExtrusion(true);
    endDrag(commit);
    if (!meshEdit) return;
    const auto& original = modelMesh(models[selected], true);
    const auto& current = meshEdit->draft.mesh;
    const bool changed = original.faces.size() != current.faces.size() ||
        !std::equal(original.vertices.begin(), original.vertices.end(), current.vertices.begin(), current.vertices.end(),
        [](Vec3 a, Vec3 b) { const Vec3 delta = a - b; return dot(delta,delta) <= 1e-16; });
    if (commit && changed) {
        Model& draft = meshEdit->draft;
        for (const auto& face : draft.mesh.faces) {
            const auto& vertices = draft.mesh.vertices;
            const Vec3 a = vertices[face.vertices[0]], b = vertices[face.vertices[1]];
            const Vec3 c = vertices[face.vertices[2]], d = vertices[face.vertices[face.count - 1]];
            const auto collapsed = [](Vec3 normal) { return dot(normal,normal) <= 1e-20; };
            if (collapsed(cross(b-a,d-a)) || (face.count == 4 &&
                (collapsed(cross(b-a,c-a)) || collapsed(cross(c-a,d-a))))) {
                MessageBoxW(window, L"Move the component away from the collapsed face, or cancel with Esc.",
                            L"Mesh Edit", MB_OK | MB_ICONWARNING);
                return;
            }
        }
        draft.kind = ObjectKind::Mesh;
        if (!rebuildModifiers(draft)) {
            MessageBoxW(window, L"These edits exceed the active Smooth modifier limit.", L"Mesh Edit", MB_OK | MB_ICONWARNING);
            return;
        }
        remember();
        models[selected] = std::move(draft);
        shadowMapsDirty = true;
    }
    meshEdit.reset();
    flyout = Flyout::None;
    selectedFaces.clear();
    updateTransformMenu();
    repaint();
}

Vec3 Editor::componentCenter() const {
    const auto& edit = *meshEdit;
    Vec3 center{};
    for (const auto vertex : edit.vertices) center = center + edit.draft.mesh.vertices[vertex];
    return edit.draft.transform.position + worldVector(edit.draft.transform, center * (1.0 / edit.vertices.size()));
}

void Editor::refreshComponentVertices() {
    auto& edit = *meshEdit;
    if (edit.mode == ComponentMode::Vertex) {
        edit.vertices = edit.components;
        return;
    }
    edit.vertices.clear();
    for (const auto component : edit.components) {
        if (edit.mode == ComponentMode::Edge) {
            const auto edge = edit.edges[component];
            edit.vertices.insert(edit.vertices.end(), edge.begin(), edge.end());
        } else {
            const auto& face = edit.draft.mesh.faces[component];
            edit.vertices.insert(edit.vertices.end(), face.vertices.begin(), face.vertices.begin() + face.count);
        }
    }
    std::sort(edit.vertices.begin(), edit.vertices.end());
    edit.vertices.erase(std::unique(edit.vertices.begin(), edit.vertices.end()), edit.vertices.end());
}

void Editor::beginComponentDrag(int index, Vec2 point, bool handlesOnly, bool toggle) {
    auto& edit = *meshEdit;
    const auto& view = views[index];
    const auto& mesh = edit.draft.mesh;
    const auto world = [&](std::size_t id) {
        return edit.draft.transform.position + worldVector(edit.draft.transform, mesh.vertices[id]);
    };
    const auto begin = [&] {
        edit.original.clear();
        edit.original.reserve(edit.vertices.size());
        for (const auto vertex : edit.vertices) edit.original.push_back(mesh.vertices[vertex]);
        drag = {};
        drag.view = index;
        drag.kind = DragKind::Component;
        drag.mouse = point;
        drag.componentPivot = componentCenter();
        SetCapture(window);
    };
    if (!toggle && !edit.vertices.empty() && edit.tool == Tool::Rotate) {
        Vec2 tangent;
        const int axis = pickRotationRing(view, point, tangent);
        if (axis >= 0) {
            begin();
            drag.axis = axis;
            drag.tangent = tangent;
            return;
        }
    } else if (!toggle && !edit.vertices.empty()) {
        const auto planes = planeHandles(view);
        for (int axis = 0; axis < 3; ++axis) {
            if (!hitPlaneHandle(planes[axis], point)) continue;
            begin();
            drag.plane = axis;
            return;
        }
        const auto arrows = handles(view);
        int axis = -1;
        double nearest = 49;
        for (int i = 0; i < 3; ++i) {
            const auto& arrow = arrows[i];
            if (!arrow.visible) continue;
            const Vec2 span = arrow.tip - arrow.start;
            const double along = dot(point - arrow.start, span) / dot(span, span);
            if (along < .24) continue;
            const Vec2 gap = point - (arrow.start + span * std::min(along, 1.0));
            const double distance = dot(gap, gap);
            if (distance < nearest) { nearest = distance; axis = i; }
        }
        if (axis >= 0) {
            if (edit.tool == Tool::Extrude) {
                beginExtrusion(index, point, axes[axis], true);
                return;
            }
            begin();
            drag.axis = axis;
            const Vec2 span = arrows[axis].tip - arrows[axis].start;
            drag.tangent = span * (1 / std::sqrt(dot(span,span)));
            return;
        }
    }
    if (handlesOnly) return;
    std::size_t picked = noFace;
    double depth = farPlane, best = 81;
    std::optional<ModelCollider> surface;
    const bool seeThrough = view.mode == ViewportMode::Wireframe ||
        (view.mode == ViewportMode::Software && edit.draft.material >= 0 &&
            materialOptics(materials[edit.draft.material]->edit).transmission > 0);
    const auto consider = [&](std::size_t component, std::size_t a, std::size_t b) {
        const Vec3 A = world(a), B = world(b);
        if (view.depth(A) <= nearPlane || view.depth(B) <= nearPlane) return;
        const Vec2 start = view.project(A), end = view.project(B), span = end - start;
        const double length = dot(span, span);
        const double t = length > 1e-9 ? std::clamp(dot(point-start,span) / length, 0.0, 1.0) : 0;
        const Vec2 offset = point - start - span * t;
        const double distance = dot(offset, offset);
        const double at = view.depth(A), bt = view.depth(B);
        const double along = view.perspective ? (t / bt) / ((1-t) / at + t / bt) : t;
        const double candidateDepth = view.depth(A + (B-A) * along);
        if (distance > best || (distance >= best - 1e-6 && candidateDepth >= depth)) return;
        if (!seeThrough) {
            if (!surface) surface.emplace(edit.draft,true);
            const auto hit = surface->intersect(view.ray(start + span*t), nearPlane, candidateDepth - 1e-4);
            if (hit.face != noFace) return;
        }
        best = distance;
        depth = candidateDepth;
        picked = component;
    };
    if (edit.mode == ComponentMode::Vertex) {
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) consider(i,i,i);
    } else if (edit.mode == ComponentMode::Edge) {
        for (std::size_t i = 0; i < edit.edges.size(); ++i) consider(i, edit.edges[i][0], edit.edges[i][1]);
    } else {
        const Ray ray = view.ray(point);
        const auto hit = ModelCollider(edit.draft, true).intersect(ray, nearPlane, farPlane);
        if (hit.face != noFace) {
            picked = hit.face;
            depth = view.depth(ray.origin + ray.direction * hit.distance);
        }
    }
    if (picked == noFace) {
        beginMarquee(index, point, toggle);
        return;
    }
    if (edit.tool == Tool::Extrude) {
        pickExtrusionFace(index, point, picked, toggle);
        return;
    }
    const auto entry = std::lower_bound(edit.components.begin(), edit.components.end(), picked);
    const bool included = entry != edit.components.end() && *entry == picked;
    if (toggle) {
        if (included) edit.components.erase(entry);
        else edit.components.insert(entry, picked);
    } else if (!included) edit.components = {picked};
    refreshComponentVertices();
    if (toggle || edit.tool != Tool::Move) return;
    begin();
    edit.unitsPerPixel = view.perspective ? depth / view.focal() : 1 / view.orthoScale();
}

void Editor::drawComponentMarkers(const View& view) {
    const auto& edit = *meshEdit;
    const auto screen = [&](std::size_t i) -> std::optional<Vec2> {
        const Vec3 point = edit.draft.transform.position + worldVector(edit.draft.transform, edit.draft.mesh.vertices[i]);
        if (view.depth(point) <= nearPlane) return {};
        return view.project(point);
    };
    const auto segment = [&](std::size_t first, std::size_t second) {
        const auto a = screen(first), b = screen(second);
        if (a && b) line(*a,*b,heading,2);
    };
    if (edit.mode == ComponentMode::Edge) {
        for (const auto index : edit.components) segment(edit.edges[index][0], edit.edges[index][1]);
    } else if (edit.mode == ComponentMode::Polygon) {
        for (const auto index : edit.components) {
            const auto& face = edit.draft.mesh.faces[index];
            for (std::size_t i = 0; i < face.count; ++i)
                segment(face.vertices[i], face.vertices[(i + 1) % face.count]);
        }
    }
}

void Editor::beginMarquee(int index, Vec2 point, bool additive) {
    drag = {};
    drag.view = index;
    drag.kind = DragKind::Marquee;
    drag.mouse = drag.marqueeEnd = point;
    drag.additiveMarquee = additive;
    SetCapture(window);
}

Rect Editor::marqueeBounds() const {
    const Rect view = views[drag.view].rect;
    const int x = std::clamp(int(drag.marqueeEnd.x), view.x, view.x + view.w - 1);
    const int y = std::clamp(int(drag.marqueeEnd.y), view.y, view.y + view.h - 1);
    const int startX = int(drag.mouse.x), startY = int(drag.mouse.y);
    return {std::min(x, startX), std::min(y, startY), std::abs(x - startX) + 1, std::abs(y - startY) + 1};
}

bool segmentOverlapsRect(Vec2 a, Vec2 b, const Rect& box) {
    double first = 0, last = 1;
    const auto clip = [&](double origin, double direction, double low, double high) {
        if (std::abs(direction) < 1e-10) return origin >= low && origin <= high;
        double enter = (low - origin) / direction, exit = (high - origin) / direction;
        if (enter > exit) std::swap(enter, exit);
        first = std::max(first, enter);
        last = std::min(last, exit);
        return first <= last;
    };
    return clip(a.x, b.x - a.x, box.x, box.x + box.w) && clip(a.y, b.y - a.y, box.y, box.y + box.h);
}

bool overlapsMarquee(const View& view, const Rect& box, std::array<Vec3, 12> corners, std::size_t count) {
    // Clip to the depth range before projecting edges and faces that cross the camera.
    const auto clipDepth = [&](double depth, bool keepFarther) {
        std::array<Vec3, 12> clipped{};
        std::size_t out = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto next = (i + 1) % count;
            const double a = view.depth(corners[i]) - depth, b = view.depth(corners[next]) - depth;
            const bool inside = keepFarther ? a >= 0 : a <= 0;
            if (inside) clipped[out++] = corners[i];
            if (inside != (keepFarther ? b >= 0 : b <= 0))
                clipped[out++] = corners[i] + (corners[next] - corners[i]) * (a / (a - b));
        }
        corners = clipped;
        count = out;
    };
    clipDepth(nearPlane, true);
    clipDepth(farPlane, false);
    if (!count) return false;

    std::array<Vec2, 12> points{};
    for (std::size_t i = 0; i < count; ++i) {
        points[i] = view.project(corners[i]);
        if (box.contains(points[i])) return true;
    }
    if (count == 1) return false;
    bool inside = false;
    const auto center = box.center();
    for (std::size_t i = 0; i < count; ++i) {
        const Vec2 a = points[i], b = points[(i + 1) % count];
        if (segmentOverlapsRect(a, b, box)) return true;
        if ((a.y > center.y) != (b.y > center.y) &&
            center.x < a.x + (b.x - a.x) * (center.y - a.y) / (b.y - a.y)) inside = !inside;
    }
    return inside;
}

void Editor::finishMarquee() {
    auto& edit = *meshEdit;
    const auto box = marqueeBounds();
    std::vector<std::size_t> hits;
    if (std::max(box.w, box.h) >= 5) {
        const auto& view = views[drag.view];
        const auto& mesh = edit.draft.mesh;
        const auto& transform = edit.draft.transform;
        const auto world = [&](std::size_t i) { return transform.position + worldVector(transform, mesh.vertices[i]); };
        if (edit.mode == ComponentMode::Vertex) {
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                const Vec3 point = world(i);
                const double depth = view.depth(point);
                if (depth >= nearPlane && depth <= farPlane && box.contains(view.project(point))) hits.push_back(i);
            }
        } else if (edit.mode == ComponentMode::Edge) {
            for (std::size_t i = 0; i < edit.edges.size(); ++i) {
                const auto& edge = edit.edges[i];
                if (overlapsMarquee(view, box, {world(edge[0]), world(edge[1])}, 2)) hits.push_back(i);
            }
        } else {
            for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
                const auto& face = mesh.faces[i];
                std::array<Vec3, 12> corners{};
                for (std::size_t j = 0; j < face.count; ++j) corners[j] = world(face.vertices[j]);
                if (overlapsMarquee(view, box, corners, face.count)) hits.push_back(i);
            }
        }
    }
    if (drag.additiveMarquee) {
        std::vector<std::size_t> selection;
        std::set_union(edit.components.begin(), edit.components.end(), hits.begin(), hits.end(),
                       std::back_inserter(selection));
        edit.components = std::move(selection);
    } else edit.components = std::move(hits);
    refreshComponentVertices();
}

void Editor::mouseMove(Vec2 p) {
    if (drag.view < 0) return;
    if (drag.kind == DragKind::Marquee) {
        drag.marqueeEnd = p;
        return;
    }
    View& view = views[drag.view];
    const Vec2 delta = p - drag.mouse;
    if (drag.kind == DragKind::Pan) {
        view.camera = dot(delta, delta) == 0 ? drag.initialCamera : -1;
        const double scale = view.perspective ? view.focal() / (15 / view.zoom) : view.orthoScale();
        view.target = drag.initialTarget - view.right * (delta.x / scale) + view.up * (delta.y / scale);
        return;
    }
    if (drag.kind == DragKind::Orbit) {
        if (dot(delta, delta) == 0) {
            view.camera = drag.initialCamera;
            view.back = drag.initialBack;
            view.right = drag.initialRight;
            view.up = drag.initialUp;
            view.orbited = drag.wasOrbited;
            return;
        }
        constexpr Vec3 worldUp{0, 1, 0};
        view.camera = -1;
        Vec3 flat = drag.initialBack - worldUp * dot(drag.initialBack, worldUp);
        if (dot(flat, flat) < 1e-10) flat = cross(drag.initialRight, worldUp);
        const Vec3 heading = rotated(normalized(flat), worldUp, -delta.x * .009);
        const double elevation = std::clamp(std::asin(std::clamp(drag.initialBack.y, -1.0, 1.0)) + delta.y * .009,
                                           -pi * .497, pi * .497);
        view.back = heading * std::cos(elevation) + worldUp * std::sin(elevation);
        view.right = normalized(cross(worldUp, view.back));
        view.up = cross(view.back, view.right);
        view.orbited = true;
        return;
    }
    if (drag.kind == DragKind::Extrude) {
        updateExtrusion(delta);
        return;
    }
    if (drag.kind == DragKind::Component) {
        auto& edit = *meshEdit;
        const Transform& transform = edit.draft.transform;
        if (edit.tool == Tool::Scale) {
            const auto factors = dragScaleFactors(view, drag.componentPivot, axes, drag.axis, drag.plane, delta, drag.tangent);
            const Vec3 pivot = localVector(transform, drag.componentPivot - transform.position);
            for (std::size_t i = 0; i < edit.vertices.size(); ++i) {
                const Vec3 world = worldVector(transform, edit.original[i] - pivot);
                const Vec3 scaled{world.x * factors[0], world.y * factors[1], world.z * factors[2]};
                edit.draft.mesh.vertices[edit.vertices[i]] = pivot + localVector(transform,scaled);
            }
            return;
        }
        if (edit.tool == Tool::Rotate) {
            const Vec3 axis = axes[drag.axis];
            const double angle = rotationDragAngle(view, drag.componentPivot, drag.axis, drag.mouse, p, drag.tangent);
            const Vec3 localPivot = localVector(transform, drag.componentPivot - transform.position);
            for (std::size_t i = 0; i < edit.vertices.size(); ++i) {
                const Vec3 world = worldVector(transform, edit.original[i] - localPivot);
                edit.draft.mesh.vertices[edit.vertices[i]] = localPivot + localVector(transform, rotated(world, axis, angle));
            }
            return;
        }
        Vec3 displacement;
        if (drag.plane >= 0) {
            displacement = planeMoveOffset(view, drag.componentPivot, drag.plane, delta);
        } else if (drag.axis >= 0) {
            displacement = axisMoveOffset(view, drag.componentPivot, axes[drag.axis], delta);
        } else {
            displacement = (view.right * delta.x - view.up * delta.y) * edit.unitsPerPixel;
        }
        const Vec3 offset = localVector(transform, displacement);
        for (std::size_t i = 0; i < edit.vertices.size(); ++i)
            edit.draft.mesh.vertices[edit.vertices[i]] = edit.original[i] + offset;
        return;
    }

    Transform& transform = models[selected].transform;
    const bool group = models[selected].kind == ObjectKind::Group;
    const auto updateTransform = [&] {
        if (group && interactionBefore) transformGroup(selected, interactionBefore->models);
        const auto changed = [](Vec3 a, Vec3 b) { const Vec3 d = a-b; return dot(d,d) > 1e-16; };
        interactionChanged = transform.size != drag.initialModel.size ||
            changed(transform.position, drag.initialModel.position) ||
            !std::equal(transform.basis.begin(), transform.basis.end(), drag.initialModel.basis.begin(),
                        [&](Vec3 a,Vec3 b) { return !changed(a,b); });
    };
    shadowMapsDirty = true;
    const Vec3 initial = drag.initialModel.position;
    if (tool == Tool::Scale) {
        const auto factors = dragScaleFactors(view, initial, drag.initialModel.basis.data(), drag.axis,
                                              drag.plane, delta, drag.tangent, group);
        for (int i = 0; i < 3; ++i)
            transform.size[i] = std::clamp(drag.initialModel.size[i] * factors[i], minObjectScale, maxObjectScale);
        updateTransform();
        return;
    }
    if (drag.plane >= 0) {
        transform.position = initial + planeMoveOffset(view, initial, drag.plane, delta);
        updateTransform();
        return;
    }

    const Vec3 axis = axes[drag.axis];
    if (tool == Tool::Rotate) {
        const double angle = rotationDragAngle(view, initial, drag.axis, drag.mouse, p, drag.tangent);
        for (int i = 0; i < 3; ++i) transform.basis[i] = rotated(drag.initialModel.basis[i], axis, angle);
        updateTransform();
        return;
    }

    transform.position = initial + axisMoveOffset(view, initial, axis, delta);
    updateTransform();
}

void Editor::transformGroup(int index, const std::vector<Model>& before) {
    const Transform& initial = before[index].transform;
    Transform& target = models[index].transform;
    double ratio = target.size[0] / initial.size[0];
    std::vector<int> descendants;
    for (int i = 0; i < int(models.size()); ++i)
        if (isDescendant(models, i, index)) descendants.push_back(i);
    if (ratio != 1) {
        double minimum = 0, maximum = maxObjectScale / initial.size[0];
        const auto limit = [&](const Transform& pose) {
            for (double size : pose.size) {
                minimum = std::max(minimum, minObjectScale / size);
                maximum = std::min(maximum, maxObjectScale / size);
            }
        };
        for (int i : descendants) {
            if (!isGeometry(before[i].kind) && !isFog(before[i].kind) && before[i].kind != ObjectKind::Group) continue;
            limit(before[i].transform);
            for (const auto& key : before[i].keys) limit(key.pose);
        }
        ratio = std::clamp(ratio, std::min(1.0, minimum), std::max(1.0, maximum));
        target.size.fill(std::clamp(initial.size[0] * ratio, minObjectScale, maxObjectScale));
    }
    const auto rotate = [&](Vec3 vector) {
        return target.basis[0] * dot(vector, initial.basis[0]) +
               target.basis[1] * dot(vector, initial.basis[1]) +
               target.basis[2] * dot(vector, initial.basis[2]);
    };
    const auto apply = [&](Transform pose, bool scale) {
        pose.position = target.position + rotate(pose.position - initial.position) * ratio;
        for (Vec3& direction : pose.basis) direction = rotate(direction);
        if (scale)
            for (double& size : pose.size) size = std::clamp(size * ratio, minObjectScale, maxObjectScale);
        return pose;
    };
    for (int i : descendants) {
        const bool scale = isGeometry(before[i].kind) || isFog(before[i].kind) || before[i].kind == ObjectKind::Group;
        models[i].transform = apply(before[i].transform, scale);
        for (std::size_t key = 0; key < models[i].keys.size(); ++key)
            models[i].keys[key].pose = apply(before[i].keys[key].pose, scale);
    }
}

void Editor::panStart(Vec2 p) {
    if (drag.view >= 0) return;
    const int index = viewAt(p);
    if (index < 0) return;
    stopPlayback();
    flyout = Flyout::None;
    activeView = index;
    drag = {};
    drag.view = index;
    drag.kind = DragKind::Pan;
    drag.mouse = p;
    drag.initialTarget = views[index].target;
    drag.initialCamera = views[index].camera;
    SetCapture(window);
}

void Editor::cancelDrag() {
    if (drag.view < 0) return;
    View& view = views[drag.view];
    switch (drag.kind) {
    case DragKind::Transform:
        break;
    case DragKind::Pan:
        view.target = drag.initialTarget;
        view.camera = drag.initialCamera;
        break;
    case DragKind::Orbit:
        view.back = drag.initialBack;
        view.right = drag.initialRight;
        view.up = drag.initialUp;
        view.orbited = drag.wasOrbited;
        view.camera = drag.initialCamera;
        break;
    case DragKind::Extrude:
    case DragKind::Component:
    case DragKind::Marquee:
        break;
    }
    endDrag();
}

void Editor::endDrag(bool commitComponent) {
    const bool editorDrag = drag.view >= 0 || adjustingTransparency || scrubbingTimeline;
    if (drag.view >= 0 && drag.kind == DragKind::Component && meshEdit) {
        auto& edit = *meshEdit;
        MeshEditChange change{};
        for (std::size_t i = 0; i < edit.vertices.size(); ++i) {
            const auto index = edit.vertices[i];
            auto& position = edit.draft.mesh.vertices[index];
            if (!commitComponent) position = edit.original[i];
            else {
                const Vec3 delta = position - edit.original[i];
                if (dot(delta, delta) > 1e-16) change.positions.emplace_back(index, edit.original[i]);
            }
        }
        if (!change.positions.empty()) recordMeshChange(std::move(change));
    }
    finishExtrusion(false);
    drag.view = -1;
    adjustingTransparency = false;
    scrubbingTimeline = false;
    if (interactionBefore) {
        if (interactionChanged) restore(std::move(*interactionBefore));
        interactionBefore.reset();
    }
    // Windows also captures this window while its frame is being resized.
    if (editorDrag && GetCapture() == window) ReleaseCapture();
}

void Editor::resizeWindow(POINT cursor) {
    const int edge = resize.edge;
    const LONG dx = cursor.x - resize.origin.x;
    const LONG dy = cursor.y - resize.origin.y;
    RECT bounds = resize.bounds;
    if (edge == HTLEFT || edge == HTTOPLEFT || edge == HTBOTTOMLEFT)
        bounds.left = std::min(bounds.left + dx, bounds.right - minWindowWidth);
    if (edge == HTRIGHT || edge == HTTOPRIGHT || edge == HTBOTTOMRIGHT)
        bounds.right = std::max(bounds.right + dx, bounds.left + minWindowWidth);
    if (edge == HTTOP || edge == HTTOPLEFT || edge == HTTOPRIGHT)
        bounds.top = std::min(bounds.top + dy, bounds.bottom - minWindowHeight);
    if (edge == HTBOTTOM || edge == HTBOTTOMLEFT || edge == HTBOTTOMRIGHT)
        bounds.bottom = std::max(bounds.bottom + dy, bounds.top + minWindowHeight);
    SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Editor::finishWindowResize() {
    if (resize.edge == HTNOWHERE) return;
    resize.edge = HTNOWHERE;
    if (GetCapture() == window) ReleaseCapture();
}

void Editor::wheel(Vec2 p, double steps) {
    if (drag.view >= 0) return;
    if (geometrySelected() && modifiersList().contains(p)) {
        const int maxOffset = std::max(0, int(models[selected].modifiers.size()) - modifiersList().h / 22);
        modifierListOffset = std::clamp(modifierListOffset - int(std::round(steps)), 0, maxOffset);
        return;
    }
    if (modelList().contains(p)) {
        listOffset = std::clamp(listOffset - static_cast<int>(std::round(steps * 3)),
                                0, std::max(0, int(sceneRows().size()) - visibleRows()));
        return;
    }
    const int index = viewAt(p);
    if (index < 0 || flyout != Flyout::None) return;
    View& view = views[index];
    view.zoom = std::clamp(view.zoom * std::pow(1.16, steps), minViewZoom, maxViewZoom);
}

LRESULT Editor::onMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto point = [lParam] {
        return Vec2{double(GET_X_LPARAM(lParam)), double(GET_Y_LPARAM(lParam))};
    };
    const auto mouse = [lParam] { return POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; };
    switch (message) {
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
        if (hit != HTCLIENT || fullscreen || IsZoomed(hwnd) || GetCapture() == hwnd) return hit;
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (ScreenToClient(hwnd, &client) && resizeGrip().contains({double(client.x), double(client.y)}))
            return HTBOTTOMRIGHT;
        return hit;
    }
    case WM_NCLBUTTONDOWN:
        if (!fullscreen && !IsZoomed(hwnd) && wParam >= HTLEFT && wParam <= HTBOTTOMRIGHT) {
            RECT bounds{};
            if (!GetWindowRect(hwnd, &bounds)) break;
            endDrag();
            resize = {int(wParam), mouse(), bounds};
            SetCapture(hwnd);
            if (GetCapture() != hwnd) resize.edge = HTNOWHERE;
            else return 0;
        }
        break;
    case WM_NCLBUTTONUP:
        if (resize.edge != HTNOWHERE) {
            resizeWindow(mouse());
            finishWindowResize();
            return 0;
        }
        break;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            if (resize.edge == HTNOWHERE) endDrag();
            layout(LOWORD(lParam), HIWORD(lParam));
            repaint();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {minWindowWidth, minWindowHeight};
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT region{};
        BeginPaint(window, &region);
        paint();
        EndPaint(window, &region);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        mouseDown(point(), (GetKeyState(VK_MENU) & 0x8000) != 0,
                  (wParam & MK_CONTROL) != 0, (wParam & MK_SHIFT) != 0);
        repaint();
        return 0;
    case WM_MBUTTONDOWN:
        if (resize.edge != HTNOWHERE) return 0;
        panStart(point());
        repaint();
        return 0;
    case WM_MOUSEMOVE:
        if (resize.edge != HTNOWHERE) {
            POINT cursor = mouse();
            if (ClientToScreen(hwnd, &cursor)) resizeWindow(cursor);
        } else if (scrubbingTimeline) {
            scrubTo(point());
        } else if (adjustingTransparency) {
            changeTransparency(point());
            repaint();
        } else if (drag.view >= 0) {
            mouseMove(point());
            repaint();
        }
        return 0;
    case WM_LBUTTONUP:
        if (resize.edge != HTNOWHERE) {
            POINT cursor = mouse();
            if (ClientToScreen(hwnd, &cursor)) resizeWindow(cursor);
            finishWindowResize();
        } else if (scrubbingTimeline) {
            scrubTo(point());
            endDrag();
        } else if (adjustingTransparency) {
            changeTransparency(point());
            commitInteraction();
            endDrag();
            repaint();
        } else if (drag.view >= 0 && drag.kind == DragKind::Marquee) {
            mouseMove(point());
            finishMarquee();
            endDrag();
            repaint();
        } else if (drag.view >= 0 && drag.kind != DragKind::Pan) {
            mouseMove(point());
            finishExtrusion(true);
            commitInteraction();
            endDrag(true);
            repaint();
        }
        return 0;
    case WM_MBUTTONUP:
        if (drag.view >= 0 && drag.kind == DragKind::Pan) {
            mouseMove(point());
            endDrag();
            repaint();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &client);
        wheel({double(client.x), double(client.y)}, GET_WHEEL_DELTA_WPARAM(wParam) / double(WHEEL_DELTA));
        repaint();
        return 0;
    }
    case WM_CONTEXTMENU: {
        if (meshEdit) return 0;
        resetDynamics();
        stopPlayback();
        POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (screen.x == -1 && screen.y == -1) {
            if (selected < 0) return 0;
            const Rect list = modelList();
            const auto rows = sceneRows();
            const auto entry = std::find_if(rows.begin(), rows.end(), [&](SceneRow row) { return row.index == selected; });
            const int row = std::clamp(int(entry - rows.begin()) - listOffset, 0, visibleRows() - 1);
            screen = {list.x + 35, list.y + 16 + row * modelRowHeight};
            ClientToScreen(window, &screen);
            sceneMenu(screen, selected);
            return 0;
        }
        POINT client = screen;
        ScreenToClient(window, &client);
        const Rect list = modelList();
        if (list.contains({double(client.x), double(client.y)})) {
            const int slot = (client.y - list.y) / modelRowHeight;
            const auto rows = sceneRows();
            if (slot < visibleRows() && slot + listOffset < int(rows.size()))
                sceneMenu(screen, rows[slot + listOffset].index);
        }
        return 0;
    }
    case WM_INITMENUPOPUP: {
        const HMENU menu = reinterpret_cast<HMENU>(wParam);
        if (menu == GetSubMenu(GetMenu(window), 1)) {
            const bool canUndo = meshEdit ? !meshEdit->undoHistory.empty() : !undoHistory.empty();
            const bool canRedo = meshEdit ? !meshEdit->redoHistory.empty() : !redoHistory.empty();
            EnableMenuItem(menu, undoCommand, MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, redoCommand, MF_BYCOMMAND | (canRedo ? MF_ENABLED : MF_GRAYED));
            for (UINT command : {renameCommand, duplicateCommand, deleteCommand, inspectCommand, visibilityCommand, frameCommand}) {
                EnableMenuItem(menu, command, MF_BYCOMMAND | (selected < 0 ? MF_GRAYED : MF_ENABLED));
            }
            return 0;
        }
        if (menu == GetSubMenu(GetMenu(window), 2)) {
            EnableMenuItem(menu, assignCameraCommand, MF_BYCOMMAND | (cameraSelected() ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, groupCommand, MF_BYCOMMAND | (selectedRoots().size() >= 2 ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, ungroupCommand, MF_BYCOMMAND | (canUngroup() ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, cutCommand, MF_BYCOMMAND | (canCut() ? MF_ENABLED : MF_GRAYED));
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) != renderCommand) resetDynamics();
        if (LOWORD(wParam) == materialWindowCommand) { showMaterials(); return 0; }
        if (LOWORD(wParam) == undoCommand || LOWORD(wParam) == redoCommand) {
            undo(LOWORD(wParam) == redoCommand);
            return 0;
        }
        if (LOWORD(wParam) == selectCommand) { setTool(Tool::Select); return 0; }
        if (meshEdit) {
            if (LOWORD(wParam) == moveCommand) setTool(Tool::Move);
            if (LOWORD(wParam) == scaleCommand) setTool(Tool::Scale);
            if (LOWORD(wParam) == rotateCommand) setTool(Tool::Rotate);
            if (LOWORD(wParam) == extrudeCommand) setTool(Tool::Extrude);
            return 0;
        }
        stopPlayback();
        switch (LOWORD(wParam)) {
        case moveCommand: setTool(Tool::Move); return 0;
        case scaleCommand: setTool(Tool::Scale); return 0;
        case rotateCommand: setTool(Tool::Rotate); return 0;
        case extrudeCommand: setTool(Tool::Extrude); return 0;
        case libraryCommand: addLibraryMaterial(); return 0;
        case newSceneCommand: newScene(); return 0;
        case openSceneCommand: openScene(); return 0;
        case saveSceneCommand: saveScene(); return 0;
        case saveAsCommand: saveScene(true); return 0;
        case importObjCommand: importModels(); return 0;
        case exportObjCommand: exportModels(); return 0;
        case duplicateCommand: duplicate(); return 0;
        case deleteCommand: deleteSelection(); return 0;
        case renameCommand: rename(); return 0;
        case visibilityCommand: changeVisibility(); return 0;
        case inspectCommand: inspect(); return 0;
        case frameCommand: frameSelection(); return 0;
        case groupCommand: groupSelection(); return 0;
        case ungroupCommand: ungroupSelection(); return 0;
        case cutCommand: cutSelection(); return 0;
        case bridgeCommand: bridgeSelection(); return 0;
        case convertMeshCommand: convertToMesh(); return 0;
        case shadeSmoothCommand: setMeshShading(MeshShading::Smooth); return 0;
        case shadeFlatCommand: setMeshShading(MeshShading::Flat); return 0;
        case assignCameraCommand: lookThroughCamera(); return 0;
        case renderCommand: renderActiveView(); return 0;
        case renderVideoCommand: showVideoPanel(); return 0;
        case viewportSettingsCommand: {
            View& view = views[activeView];
            bool show = view.showRenderFrame;
            if (editViewportSettings(window, show) && show != view.showRenderFrame) {
                remember();
                view.showRenderFrame = show;
                repaint();
            }
            return 0;
        }
        case configureGraphicsCommand: {
            const auto preferred = preferredGraphicsBackend();
            bool vulkan = preferred == GraphicsBackend::Vulkan;
            const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const wchar_t* active = L"Current backend: OpenGL";
            if (activeGraphicsBackend() == GraphicsBackend::Vulkan)
                active = renderer && std::string_view(renderer).find("llvmpipe") != std::string_view::npos
                    ? L"Current backend: Vulkan (Lavapipe CPU)" : L"Current backend: Vulkan (Mesa Zink)";
            if (!editGraphicsConfiguration(window, vulkan, active)) return 0;
            const auto requested = vulkan ? GraphicsBackend::Vulkan : GraphicsBackend::OpenGL;
            if (requested == preferred) return 0;
            if (vulkan && GetFileAttributesW((executableDirectory() / L"vulkan" / L"opengl32.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
                MessageBoxW(window, L"The bundled Mesa Zink runtime is missing.", L"Configure Graphics", MB_OK | MB_ICONWARNING);
            } else if (!saveGraphicsBackend(requested)) {
                MessageBoxW(window, L"Could not save the graphics setting.", L"Configure Graphics", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        case setupRenderCommand: {
            auto settings = renderSettings;
            if (setupRender(window, settings) &&
                (settings.samples != renderSettings.samples || settings.mode != renderSettings.mode ||
                 settings.caustics != renderSettings.caustics || settings.causticPhotons != renderSettings.causticPhotons ||
                 settings.width != renderSettings.width || settings.height != renderSettings.height)) {
                remember();
                renderSettings = settings;
                repaint();
            }
            return 0;
        }
        case exitCommand: SendMessageW(window, WM_CLOSE, 0, 0); return 0;
        case resetViewCommand: {
            endDrag();
            resetViewportCamera(activeView);
            repaint();
            return 0;
        }
        }
        break;
    case WM_TIMER:
        if (wParam == renderTimer) { pollRender(); return 0; }
        if (wParam == timelineTimer) { tickPlayback(); return 0; }
        if (wParam == dynamicsTimer) { tickDynamics(); return 0; }
        break;
    case cancelRenderMessage:
        stopRender();
        return 0;
    case WM_SYSKEYDOWN:
        if (wParam == VK_MENU && !(lParam & (1L << 30))) altUsedToOrbit = false;
        break;
    case WM_SYSKEYUP:
    case WM_KEYUP:
        // After Alt + drag, releasing Alt should leave focus in the viewport.
        if (wParam == VK_MENU && altUsedToOrbit) {
            altUsedToOrbit = false;
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (resize.edge != HTNOWHERE) {
            if (wParam == VK_ESCAPE) finishWindowResize();
            return 0;
        }
        if (dynamics && wParam == VK_ESCAPE) {
            resetDynamics();
            return 0;
        }
        if (dynamics && wParam == VK_SPACE) {
            if (!(lParam & (1L << 30))) toggleDynamics();
            return 0;
        }
        if ((wParam == 'Z' || wParam == 'Y') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (!(lParam & (1L << 30)))
                undo(wParam == 'Y' || (GetKeyState(VK_SHIFT) & 0x8000));
            return 0;
        }
        if (meshEdit) {
            if (wParam == VK_ESCAPE && drag.view >= 0) {
                cancelDrag();
                repaint();
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_ESCAPE) finishMeshEdit(wParam == VK_RETURN);
            return 0;
        }
        if (tool == Tool::Bridge && wParam == VK_RETURN) {
            if (!(lParam & (1L << 30))) bridgeSelection();
            return 0;
        }
        if (!(lParam & (1L << 30))) {
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            UINT command = 0;
            if (control) {
                switch (wParam) {
                case 'N': command = newSceneCommand; break;
                case 'O': command = openSceneCommand; break;
                case 'S': command = shift ? saveAsCommand : saveSceneCommand; break;
                case 'D': command = duplicateCommand; break;
                }
            } else {
                switch (wParam) {
                case VK_DELETE: command = deleteCommand; break;
                case VK_F2: command = renameCommand; break;
                case 'H': command = visibilityCommand; break;
                case 'F': command = frameCommand; break;
                case 'K': setKey(shift); return 0;
                case VK_SPACE: if (playing) stopPlayback(); else startPlayback(); return 0;
                case VK_LEFT: stopPlayback(); seekFrame(timeline.frame - 1); return 0;
                case VK_RIGHT: stopPlayback(); seekFrame(timeline.frame + 1); return 0;
                case VK_HOME: stopPlayback(); seekFrame(0); return 0;
                case VK_END: stopPlayback(); seekFrame(timeline.frames - 1); return 0;
                }
            }
            if (command) {
                SendMessageW(window, WM_COMMAND, command, 0);
                return 0;
            }
        }
        if (wParam == VK_F9) {
            if (!(lParam & (1L << 30))) renderActiveView();
            return 0;
        }
        if (wParam == VK_F11) {
            if (!(lParam & (1L << 30))) toggleFullscreen();
            return 0;
        }
        if (wParam != VK_ESCAPE) break;
        if (playing) { stopPlayback(); return 0; }
        if (fullscreen && drag.view < 0 && !adjustingTransparency &&
            !scrubbingTimeline && flyout == Flyout::None) {
            toggleFullscreen();
            return 0;
        }
        cancelDrag();
        if (tool == Tool::Bridge) selectedFaces.clear();
        if (adjustingTransparency || scrubbingTimeline) endDrag();
        flyout = Flyout::None;
        repaint();
        return 0;
    case WM_CANCELMODE:
        finishWindowResize();
        endDrag();
        flyout = Flyout::None;
        repaint();
        return 0;
    case WM_CAPTURECHANGED:
        resize.edge = HTNOWHERE;
        endDrag();
        repaint();
        return 0;
    case WM_CLOSE:
        resetDynamics();
        stopPlayback();
        finishWindowResize();
        if (meshEdit) finishMeshEdit(false);
        endDrag();
        if (confirmLeaveScene()) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        release();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    const auto editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
        if (editor->init(window)) return 0;
        editor->release();
        return -1;
    }
    if (editor && message != WM_NCCREATE) return editor->onMessage(window, message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR args, int show) {
    SetProcessDPIAware();
    const std::string_view arguments(args);
    const bool probe = arguments == "--probe-vulkan";
    StartupSplash splash(!probe && show != SW_HIDE);
    const auto preferred = arguments == "--opengl" ? GraphicsBackend::OpenGL
        : (probe || arguments == "--vulkan") ? GraphicsBackend::Vulkan : preferredGraphicsBackend();
    if (probe) SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if ((!probe && preferred == GraphicsBackend::Vulkan && !findWorkingVulkanDriver()) || !initializeGraphics(preferred)) {
        if (probe) return 1;
        splash.finish();
        if (preferred == GraphicsBackend::OpenGL || !initializeGraphics(GraphicsBackend::OpenGL)) {
            MessageBoxW(nullptr, L"Could not load a graphics backend.", editorName, MB_ICONERROR);
            return 1;
        }
        MessageBoxW(nullptr, L"Could not start Vulkan through Mesa Zink. This session will use OpenGL.",
                    editorName, MB_ICONWARNING);
    }
    const wchar_t className[] = L"3D1EditorWindow";
    WNDCLASSW windowClass{};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass)) return 1;

    Editor editor;
    if (probe) {
        const HWND window = CreateWindowExW(0, className, L"", WS_OVERLAPPEDWINDOW, 0, 0, 760, 540,
                                            nullptr, nullptr, instance, &editor);
        if (!window) return 1;
        const bool ready = editor.verifyRasterInitialization();
        DestroyWindow(window);
        return ready ? 0 : 1;
    }
    const HMENU menu = CreateMenu();
    const HMENU file = CreatePopupMenu();
    const HMENU edit = CreatePopupMenu();
    const HMENU objects = CreatePopupMenu();
    const HMENU materials = CreatePopupMenu();
    const HMENU viewport = CreatePopupMenu();
    const HMENU rendering = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, newSceneCommand, L"&New Scene\tCtrl+N");
    AppendMenuW(file, MF_STRING, openSceneCommand, L"&Open Scene...\tCtrl+O");
    AppendMenuW(file, MF_STRING, saveSceneCommand, L"&Save Scene\tCtrl+S");
    AppendMenuW(file, MF_STRING, saveAsCommand, L"Save Scene &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, importObjCommand, L"&Import OBJ...");
    AppendMenuW(file, MF_STRING, exportObjCommand, L"&Export OBJ...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, exitCommand, L"E&xit");
    AppendMenuW(edit, MF_STRING, undoCommand, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, redoCommand, L"&Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, renameCommand, L"Re&name\tF2");
    AppendMenuW(edit, MF_STRING, duplicateCommand, L"&Duplicate\tCtrl+D");
    AppendMenuW(edit, MF_STRING, visibilityCommand, L"&Hide / Show\tH");
    AppendMenuW(edit, MF_STRING, deleteCommand, L"De&lete\tDel");
    AppendMenuW(edit, MF_STRING, inspectCommand, L"&Transform...");
    AppendMenuW(edit, MF_STRING, frameCommand, L"&Frame Selection\tF");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, selectCommand, L"&Select");
    AppendMenuW(edit, MF_STRING, moveCommand, L"&Move");
    AppendMenuW(edit, MF_STRING, scaleCommand, L"&Scale");
    AppendMenuW(edit, MF_STRING, rotateCommand, L"&Rotate");
    AppendMenuW(edit, MF_STRING, extrudeCommand, L"E&xtrude");
    AppendMenuW(edit, MF_STRING, bridgeCommand, L"&Bridge Faces");
    CheckMenuRadioItem(edit, moveCommand, rotateCommand, moveCommand, MF_BYCOMMAND);
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, resetViewCommand, L"Reset active &view");
    AppendMenuW(objects, MF_STRING, assignCameraCommand, L"&Assign selected camera");
    AppendMenuW(objects, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(objects, MF_STRING, groupCommand, L"&Group");
    AppendMenuW(objects, MF_STRING, ungroupCommand, L"&Ungroup");
    AppendMenuW(objects, MF_STRING, cutCommand, L"&Cut (Difference)");
    AppendMenuW(materials, MF_STRING, materialWindowCommand, L"&Scene Materials...");
    AppendMenuW(materials, MF_STRING, libraryCommand, L"&Library...");
    AppendMenuW(viewport, MF_STRING, viewportSettingsCommand, L"&Viewport Settings...");
    AppendMenuW(viewport, MF_STRING, configureGraphicsCommand, L"&Configure...");
    AppendMenuW(rendering, MF_STRING, renderCommand, L"&Render\tF9");
    AppendMenuW(rendering, MF_STRING, renderVideoCommand, L"Render to &Video...");
    AppendMenuW(rendering, MF_STRING, setupRenderCommand, L"&Setup Render...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(objects), L"O&bjects");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(materials), L"&Materials");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(viewport), L"&Viewport");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rendering), L"&Rendering");
    const HWND window = CreateWindowExW(0, className, editorName, WS_OVERLAPPEDWINDOW,
                                        CW_USEDEFAULT, CW_USEDEFAULT, 1260, 820, nullptr,
                                        menu, instance, &editor);
    if (!window) {
        splash.finish();
        MessageBoxW(nullptr, L"Could not open the editor window.", editorName, MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, show);
    UpdateWindow(window);
    splash.finish();
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (editor.dispatchVideoDialog(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
