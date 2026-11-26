#include "material_library.h"
#include "scene_mesh.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {
constexpr const wchar_t* names[] = {L"Ocean", L"Glass", L"Ground", L"Brick", L"Ice", L"Wood",
                                    L"Steel", L"Concrete", L"Marble", L"Sand", L"Checker",
                                    L"Clear Water", L"River Water", L"Ruby", L"Emerald", L"Sapphire", L"Amethyst", L"Amber"};
static_assert(std::size(names) == presetCount);

double hash(int x, int y) {
    auto value = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return double(value ^ (value >> 16)) / 4294967295.0;
}

double noise(double x, double y) {
    const int i = int(std::floor(x)), j = int(std::floor(y));
    const double s = x - i, t = y - j;
    const double u = s * s * (3 - 2 * s), v = t * t * (3 - 2 * t);
    return (hash(i, j) * (1 - u) + hash(i + 1, j) * u) * (1 - v) +
           (hash(i, j + 1) * (1 - u) + hash(i + 1, j + 1) * u) * v;
}

Color mix(Color a, Color b, double value) {
    const float t = float(std::clamp(value, 0.0, 1.0));
    return {a.r * (1 - t) + b.r * t, a.g * (1 - t) + b.g * t, a.b * (1 - t) + b.b * t};
}

Color pattern(MaterialPreset preset, double u, double v) {
    const double n = noise(u * 8, v * 8), fine = noise(u * 115, v * 115);
    switch (preset) {
    case MaterialPreset::Ocean: {
        const double wave = std::sin(56 * u + 15 * v + 7 * n) * .5 +
                            std::sin(42 * v - 16 * u + 4 * n) * .25;
        return mix({.025f, .16f, .28f}, {.32f, .80f, .90f}, .34 + wave * .35 + .14 * fine);
    }
    case MaterialPreset::Ground:
        return mix({.17f, .095f, .045f}, {.63f, .49f, .29f}, n * .7 + fine * .35);
    case MaterialPreset::Brick: {
        const int row = int(v * 8);
        const double bx = u * 4 + .5 * (row % 2);
        const double x = bx - std::floor(bx), y = v * 8 - row;
        if (x < .029 || x > .971 || y < .05 || y > .95)
            return mix({.47f, .43f, .38f}, {.77f, .72f, .63f}, fine);
        const double variation = hash(int(bx), row) * .7 + fine * .36;
        return mix({.36f, .105f, .058f}, {.76f, .34f, .19f}, variation);
    }
    case MaterialPreset::Ice: {
        const double veins = std::pow(std::abs(std::sin(22 * u + 18 * v + n * 12)), 18);
        return mix({.31f, .59f, .74f}, {.90f, .98f, 1.f}, .25 + veins * .58 + fine * .15);
    }
    case MaterialPreset::Wood:
        return mix({.23f, .105f, .043f}, {.74f, .46f, .23f},
                   .5 + .4 * std::sin(117 * u + 4 * noise(u * 7, v * 1.5)) + .1 * fine);
    case MaterialPreset::Steel:
        return mix({.42f, .48f, .53f}, {.83f, .86f, .90f}, .2 + .64 * noise(u * 170, v * 4));
    case MaterialPreset::Concrete:
        return mix({.37f, .38f, .38f}, {.75f, .74f, .71f}, .35 * n + .65 * fine);
    case MaterialPreset::Marble:
        return mix({.22f, .25f, .31f}, {.91f, .91f, .88f},
                   1 - .85 * std::pow(std::abs(std::sin(17 * u + 11 * v + 7 * n)), 12));
    case MaterialPreset::Sand:
        return mix({.52f, .36f, .17f}, {.93f, .79f, .54f},
                   .45 + .25 * std::sin(23 * u + 17 * v + n * 3) + .3 * fine);
    case MaterialPreset::Checker:
        return (int(u * 8) + int(v * 8)) % 2 ? Color{.12f, .14f, .18f} : Color{.88f, .89f, .91f};
    default: return {1, 1, 1};
    }
}

constexpr int presetList = 110;
INT_PTR CALLBACK libraryProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_MEASUREITEM && wParam == presetList) {
        reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = 31;
        return TRUE;
    }
    if (message == WM_DRAWITEM && wParam == presetList) {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item.itemID >= presetCount) return TRUE;
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        FillRect(item.hDC, &item.rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        const auto preset = MaterialPreset(item.itemID + 1);
        const auto rect = item.rcItem;
        if (isCrystalPreset(preset))
            drawCrystalPreview(item.hDC, {rect.left + 3, rect.top + 1, rect.left + 33, rect.bottom - 1}, makePreset(preset));
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        RECT title{rect.left + 38, rect.top, rect.right, rect.bottom};
        DrawTextW(item.hDC, presetName(preset), -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (item.itemState & ODS_FOCUS) DrawFocusRect(item.hDC, &item.rcItem);
        return TRUE;
    }
    if (message == WM_INITDIALOG) {
        SetWindowTextW(dialog, L"Material Library");
        const HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            13, 13, 238, 127, dialog, reinterpret_cast<HMENU>(presetList), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        for (const auto* name : names) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SetFocus(list);
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == presetList && HIWORD(wParam) == LBN_SELCHANGE) {
        const auto selected = SendDlgItemMessageW(dialog, presetList, LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR) EndDialog(dialog, selected + 1);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

const wchar_t* presetName(MaterialPreset preset) {
    return preset == MaterialPreset::None ? L"" : names[std::size_t(preset) - 1];
}

Vec3 shadeNormal(MaterialPreset preset, Vec3 normal, Vec3 point) {
    if (!usesSurfaceRipples(preset)) return normal;
    const Vec3 tangent = normalized(cross(std::abs(normal.y) > .94 ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, normal));
    const Vec3 bitangent = cross(normal, tangent);
    const double x = dot(point, tangent), y = dot(point, bitangent);
    const double strength = preset == MaterialPreset::Ocean ? .15 : preset == MaterialPreset::ClearWater ? .018 :
                            preset == MaterialPreset::RiverWater ? .095 : .07;
    const double u = std::sin(x * 14 + y * 3 + 1.4 * std::sin(y * 9));
    const double v = std::sin(y * 18 - x * 6) * .75;
    return normalized(normal + tangent * (strength * u) + bitangent * (strength * v));
}

bool usesSurfaceRipples(MaterialPreset preset) {
    return preset == MaterialPreset::Ocean || preset == MaterialPreset::Ice ||
           preset == MaterialPreset::ClearWater || preset == MaterialPreset::RiverWater;
}

bool isCrystalPreset(MaterialPreset preset) {
    return preset >= MaterialPreset::Ruby && preset <= MaterialPreset::Amber;
}

MaterialEdit makePreset(MaterialPreset preset) {
    MaterialEdit material;
    material.preset = preset;
    Surface& surface = material.surface;
    switch (preset) {
    case MaterialPreset::Ocean: surface.diffuse = 45; surface.specular = 85; surface.shininess = 96; break;
    case MaterialPreset::Glass: surface.tint = {.87f, .97f, 1}; surface.diffuse = 30; surface.specular = 90; surface.shininess = 120; break;
    case MaterialPreset::Ice: surface.diffuse = 55; surface.specular = 70; surface.shininess = 80; break;
    case MaterialPreset::Steel: surface.diffuse = 58; surface.specular = 95; surface.shininess = 105; break;
    case MaterialPreset::Marble: surface.diffuse = 72; surface.specular = 55; surface.shininess = 55; break;
    case MaterialPreset::Wood: surface.specular = 22; surface.shininess = 24; break;
    case MaterialPreset::ClearWater: surface.tint = {.94f, .99f, 1}; break;
    case MaterialPreset::RiverWater: surface.tint = {.66f, .89f, .92f}; break;
    case MaterialPreset::Ruby: surface.tint = {.90f, .08f, .18f}; break;
    case MaterialPreset::Emerald: surface.tint = {.075f, .78f, .33f}; break;
    case MaterialPreset::Sapphire: surface.tint = {.12f, .31f, .95f}; break;
    case MaterialPreset::Amethyst: surface.tint = {.61f, .30f, .91f}; break;
    case MaterialPreset::Amber: surface.tint = {.99f, .58f, .14f}; break;
    default: surface.shading = Shading::Lambert; surface.specular = 0; break;
    }
    if (isCrystalPreset(preset) || preset == MaterialPreset::ClearWater || preset == MaterialPreset::RiverWater) {
        surface.shading = Shading::BlinnPhong;
        surface.diffuse = 20;
        surface.specular = 100;
        surface.shininess = preset == MaterialPreset::RiverWater ? 72 : 125;
        return material;
    }
    if (preset == MaterialPreset::Glass || preset == MaterialPreset::None) return material;
    auto& image = material.image;
    image.width = image.height = 256;
    image.filename = presetName(preset);
    image.pixels.resize(std::size_t(image.width) * image.height * 4);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const Color color = pattern(preset, (x + .5) / image.width, (y + .5) / image.height);
            auto* pixel = image.pixels.data() + (y * image.width + x) * 4;
            pixel[0] = static_cast<unsigned char>(color.b * 255);
            pixel[1] = static_cast<unsigned char>(color.g * 255);
            pixel[2] = static_cast<unsigned char>(color.r * 255);
            pixel[3] = 255;
        }
    }
    return material;
}

std::vector<CrystalFacet> crystalPreview(const MaterialEdit& material) {
    constexpr std::array cuts{ObjectKind::CrystalBrilliant, ObjectKind::CrystalStep, ObjectKind::CrystalOctahedron,
                              ObjectKind::CrystalPoint, ObjectKind::CrystalBrilliant};
    if (!isCrystalPreset(material.preset)) return {};
    const auto mesh = createExtraPrimitive(cuts[std::size_t(material.preset) - std::size_t(MaterialPreset::Ruby)]);
    const Vec3 view = normalized({.37,.42,1});
    const Vec3 right = normalized(cross(axes[1], view)), up = cross(view, right);
    const Vec3 key = normalized({-.48,.83,.65});
    const auto half = normalized(view + key);
    std::vector<CrystalFacet> result;
    for (const auto& face : mesh.faces) {
        const auto point = mesh.vertices[face.vertices[0]];
        const auto normal = normalized(cross(mesh.vertices[face.vertices[1]] - point,
                                            mesh.vertices[face.vertices[2]] - point));
        if (dot(normal, view) <= 0) continue;
        CrystalFacet facet{};
        facet.count = face.count;
        for (std::size_t i = 0; i < face.count; ++i) {
            const auto p = mesh.vertices[face.vertices[i]];
            facet.corners[i] = {dot(p, right), -dot(p, up)};
        }
        const double highlight = std::pow(std::max(0.0, dot(half,normal)), 8) * .82;
        const double illumination = .35 + .65 * std::max(0.0, dot(normal,key));
        const auto tint = material.surface.tint;
        facet.color = mix({float(tint.r * illumination), float(tint.g * illumination),
                           float(tint.b * illumination)}, {.94f,.97f,1}, highlight);
        result.push_back(facet);
    }
    return result;
}

void drawCrystalPreview(HDC dc, RECT area, const MaterialEdit& material) {
    const double size = .94 * std::min(area.right - area.left, area.bottom - area.top);
    const Vec2 center{(area.left + area.right) * .5, (area.top + area.bottom) * .5};
    const auto saved = SaveDC(dc);
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    for (const auto& facet : crystalPreview(material)) {
        std::array<POINT, 4> points{};
        for (std::size_t i = 0; i < facet.count; ++i) {
            const auto p = center + facet.corners[i] * size;
            points[i] = {LONG(std::round(p.x)), LONG(std::round(p.y))};
        }
        const auto c = facet.color;
        SetDCBrushColor(dc, RGB(int(c.r * 255), int(c.g * 255), int(c.b * 255)));
        Polygon(dc, points.data(), int(facet.count));
    }
    RestoreDC(dc, saved);
}

MaterialPreset chooseMaterialPreset(HWND owner) {
    struct alignas(4) {
        DLGTEMPLATE dialog{};
        WORD menu = 0, windowClass = 0, title = 0;
    } data;
    data.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    data.dialog.cx = 177;
    data.dialog.cy = 90;
    const auto choice = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &data.dialog, owner, libraryProc, 0);
    return choice > 0 ? static_cast<MaterialPreset>(choice) : MaterialPreset::None;
}
