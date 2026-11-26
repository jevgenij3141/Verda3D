#include "texture_mesh_editor.h"
#include "scene_mesh.h"

#include <windowsx.h>

#include <cmath>
#include <cstdint>
#include <numeric>

namespace {
constexpr int textureId = 100, firstToolId = 110, fitId = 114, restoreId = 120, unfoldId = 121;
constexpr int canvasTop = 77, footer = 46;
enum class Tool { Move, ScaleX, ScaleY, Rotate };
constexpr const wchar_t* toolNames[] = {L"Move", L"Scale X", L"Scale Y", L"Rotate"};

double wedge(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

bool containsUv(const std::array<Vec2,4>& polygon, std::size_t count, Vec2 point) {
    bool inside = false;
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vec2 a = polygon[i], b = polygon[j];
        if ((a.y > point.y) == (b.y > point.y)) continue;
        if (point.x < a.x + (b.x - a.x) * (point.y - a.y) / (b.y - a.y)) inside = !inside;
    }
    return inside;
}

Vec2 transformUv(Tool tool, Vec2 point, Vec2 pivot, Vec2 from, Vec2 to) {
    const Vec2 delta = to - from;
    if (delta.x == 0 && delta.y == 0) return point;
    const Vec2 local = point - pivot;
    switch (tool) {
    case Tool::Move: return point + delta;
    case Tool::ScaleX: return {pivot.x + local.x * std::exp(std::clamp(delta.x * 3, -8.0, 8.0)), point.y};
    case Tool::ScaleY: return {point.x, pivot.y + local.y * std::exp(std::clamp(-delta.y * 3, -8.0, 8.0))};
    case Tool::Rotate: {
        const Vec2 a = from - pivot, b = to - pivot;
        const double angle = dot(a,a) > 1e-5 ? std::atan2(wedge(a,b),dot(a,b)) : delta.x * 6;
        const double c = std::cos(angle), s = std::sin(angle);
        return pivot + Vec2{c * local.x - s * local.y, s * local.x + c * local.y};
    }
    }
    return point;
}

HWND addControl(HWND window, const wchar_t* type, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id = 0) {
    const HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        window, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    return child;
}

struct UvWindow {
    const Mesh& mesh;
    const TextureUvs& defaults;
    const std::vector<TextureChoice>& textures;
    TextureUvs uvs, dragStart;
    std::vector<unsigned char> selected;
    std::vector<std::size_t> affected;
    std::vector<std::uint32_t> backdrop;
    HWND window = nullptr;
    Tool tool = Tool::Move;
    int choice = 0, width = 0, height = 0;
    std::size_t picked = 0, cycle = 0;
    Vec2 lastPick{-100,-100}, origin{}, scale{1,1}, start{}, panStart{}, pivot{};
    double zoom = 1;
    bool paintingDirty = true, dragging = false, panning = false, accepted = false, finished = false;

    UvWindow(const Mesh& mesh, const TextureUvs& defaults, bool startWithDefaults,
             const std::vector<TextureChoice>& textures, int material)
        : mesh(mesh), defaults(defaults), textures(textures), selected(mesh.faces.size()) {
        if (startWithDefaults) uvs = defaults;
        else {
            uvs.reserve(mesh.faces.size());
            for (const auto& face : mesh.faces) uvs.push_back(face.uv);
        }
        for (int i = 0; i < int(textures.size()); ++i)
            if (textures[i].material == material) choice = i;
    }

    RECT canvas() const { return {12, canvasTop, std::max(13, width - 12), std::max(canvasTop + 1, height - footer)}; }
    bool onCanvas(Vec2 p) const { const RECT r = canvas(); return PtInRect(&r, {LONG(p.x), LONG(p.y)}); }
    Vec2 fromScreen(Vec2 point) const { return {(point.x-origin.x)/scale.x, (point.y-origin.y)/scale.y}; }
    Vec2 toScreen(Vec2 uv) const { return {origin.x + uv.x*scale.x, origin.y + uv.y*scale.y}; }

    void create() {
        addControl(window, L"STATIC", L"Texture", 0, 13, 17, 55, 20);
        const HWND list = addControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     69, 12, 380, 280, textureId);
        for (const auto& item : textures)
            SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name.c_str()));
        SendMessageW(list, CB_SETCURSEL, choice, 0);
        for (int i = 0; i < int(std::size(toolNames)); ++i)
            addControl(window, L"BUTTON", toolNames[i], BS_RADIOBUTTON | WS_TABSTOP,
                       13 + i * 78, 46, 74, 23, firstToolId+i);
        CheckRadioButton(window, firstToolId, firstToolId+3, firstToolId);
        addControl(window, L"BUTTON", L"Fit UVs", WS_TABSTOP, 338, 43, 93, 27, fitId);
        addControl(window, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 78, 27, IDOK);
        addControl(window, L"BUTTON", L"Cancel", WS_TABSTOP, 0, 0, 78, 27, IDCANCEL);
    }

    void invalidate(bool background = false) {
        paintingDirty = paintingDirty || background;
        InvalidateRect(window, nullptr, FALSE);
    }

    void fit() {
        const auto r = canvas();
        const auto* material = textures[choice].edit;
        const double aspect = material ? std::clamp(double(material->image.width) / material->image.height, .25, 4.0) : 1;
        Vec2 low{}, high{1,1};
        for (std::size_t i = 0; i < uvs.size(); ++i) {
            for (std::size_t j = 0; j < mesh.faces[i].count; ++j) {
                const auto uv = uvs[i][j];
                low = {std::min(low.x,uv.x), std::min(low.y,uv.y)};
                high = {std::max(high.x,uv.x), std::max(high.y,uv.y)};
            }
        }
        const Vec2 halfSpan = high * .5 - low * .5;
        const double size = .42 * std::min(double(r.right-r.left) / aspect / halfSpan.x,
                                           double(r.bottom-r.top) / halfSpan.y);
        zoom = 1;
        scale = {size * aspect, size};
        const Vec2 center = low * .5 + high * .5;
        origin = {(r.left+r.right)*.5 - center.x*scale.x, (r.top+r.bottom)*.5 - center.y*scale.y};
        invalidate(true);
    }

    void resize(int w, int h) {
        width = w; height = h;
        MoveWindow(GetDlgItem(window, IDOK), width-179, height-35, 78, 27, TRUE);
        MoveWindow(GetDlgItem(window, IDCANCEL), width-91, height-35, 78, 27, TRUE);
        fit();
    }

    void pick(Vec2 point) {
        const Vec2 uv = fromScreen(point);
        std::vector<std::size_t> hits;
        for (std::size_t i = uvs.size(); i-- > 0;)
            if (containsUv(uvs[i], mesh.faces[i].count, uv)) hits.push_back(i);
        const Vec2 delta = point - lastPick;
        cycle = dot(delta,delta) <= 16 ? cycle + 1 : 0;
        lastPick = point;
        if (hits.empty()) {
            std::fill(selected.begin(), selected.end(), false);
            picked = 0;
        } else {
            const auto i = hits[cycle % hits.size()];
            selected[i] = !selected[i];
            picked = i + 1;
        }
        invalidate();
    }

    void begin(Vec2 point) {
        affected.clear();
        for (std::size_t i = 0; i < selected.size(); ++i)
            if (selected[i]) affected.push_back(i);
        if (affected.empty()) {
            affected.resize(uvs.size());
            std::iota(affected.begin(), affected.end(), 0);
        }
        if (affected.empty()) return;
        Vec2 low = uvs[affected.front()][0], high = low;
        dragStart.clear();
        dragStart.reserve(affected.size());
        for (const auto i : affected) {
            dragStart.push_back(uvs[i]);
            for (std::size_t j = 0; j < mesh.faces[i].count; ++j) {
                const auto uv = uvs[i][j];
                low = {std::min(low.x,uv.x), std::min(low.y,uv.y)};
                high = {std::max(high.x,uv.x), std::max(high.y,uv.y)};
            }
        }
        pivot = low * .5 + high * .5;
        start = fromScreen(point);
        dragging = true;
        SetCapture(window);
    }

    void move(Vec2 point) {
        if (panning) {
            origin = panStart + point - start;
            invalidate(true);
        } else if (dragging) {
            const auto current = fromScreen(point);
            for (std::size_t i = 0; i < affected.size(); ++i) {
                const auto index = affected[i];
                for (std::size_t j = 0; j < mesh.faces[index].count; ++j) {
                    const auto uv = transformUv(tool, dragStart[i][j], pivot, start, current);
                    if (std::isfinite(uv.x) && std::isfinite(uv.y))
                        uvs[index][j] = {std::clamp(uv.x,-1e6,1e6), std::clamp(uv.y,-1e6,1e6)};
                }
            }
            lastPick = {-100,-100};
            invalidate();
        }
    }

    void drawBackdrop(HDC dc, RECT r) {
        const int w = r.right-r.left, h = r.bottom-r.top;
        if (paintingDirty) {
            backdrop.resize(std::size_t(w) * h);
            const auto* material = textures[choice].edit;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const Vec2 uv = fromScreen({double(r.left+x)+.5,double(r.top+y)+.5});
                    const bool primary = uv.x >= 0 && uv.y >= 0 && uv.x < 1 && uv.y < 1;
                    const auto* pixel = material ? texturePixel(*material, uv) : nullptr;
                    const int checker = ((x/16 + y/16) % 2) ? 50 : 65;
                    const int shade = primary ? 100 : 46;
                    const int red = (pixel ? pixel[2] : checker) * shade / 100;
                    const int green = (pixel ? pixel[1] : checker) * shade / 100;
                    const int blue = (pixel ? pixel[0] : checker) * shade / 100;
                    backdrop[std::size_t(y)*w+x] = (red << 16) | (green << 8) | blue;
                }
            }
            paintingDirty = false;
        }
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
        bitmap.bmiHeader.biWidth = w;
        bitmap.bmiHeader.biHeight = -h;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(dc, r.left, r.top, w, h, 0, 0, 0, h, backdrop.data(), &bitmap, DIB_RGB_COLORS);
    }

    void draw(HDC dc) {
        RECT entire{0,0,width,height};
        FillRect(dc, &entire, GetSysColorBrush(COLOR_BTNFACE));
        const auto r = canvas();
        drawBackdrop(dc,r);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, r.left,r.top,r.right,r.bottom);
        const auto screenPoint = [&](Vec2 uv) {
            const auto p = toScreen(uv);
            return POINT{LONG(std::clamp(p.x,-1e7,1e7)),LONG(std::clamp(p.y,-1e7,1e7))};
        };
        const HPEN border = CreatePen(PS_DOT,1,RGB(175,184,189));
        SelectObject(dc,border);
        const std::array<POINT,5> frame{screenPoint({0,0}),screenPoint({1,0}),screenPoint({1,1}),
                                        screenPoint({0,1}),screenPoint({0,0})};
        Polyline(dc,frame.data(),int(frame.size()));
        const HPEN ordinary = CreatePen(PS_SOLID,1,RGB(102,223,242));
        const HPEN highlighted = CreatePen(PS_SOLID,2,RGB(255,169,54));
        for (bool active : {false,true}) {
            SelectObject(dc,active ? highlighted : ordinary);
            for (std::size_t i = 0; i < uvs.size(); ++i) {
                if (bool(selected[i]) != active) continue;
                std::array<POINT,5> corners{};
                const auto count = mesh.faces[i].count;
                for (std::size_t j = 0; j < count; ++j) corners[j] = screenPoint(uvs[i][j]);
                corners[count] = corners[0];
                Polyline(dc,corners.data(),int(count+1));
            }
        }
        RestoreDC(dc,saved);
        DeleteObject(border); DeleteObject(ordinary); DeleteObject(highlighted);
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        const auto count = std::count(selected.begin(),selected.end(),1);
        const auto status = count > 0 ? std::to_wstring(count) + L" selected  |  Face " + std::to_wstring(picked) +
            L"  |  Ctrl + right click to toggle faces"
            : std::wstring(L"Drag to edit all faces  |  Ctrl + right click selects faces  |  Wheel zooms, middle drag pans");
        RECT text{13,height-footer+9,width-185,height-4};
        DrawTextW(dc,status.c_str(),-1,&text,DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
};

LRESULT CALLBACK uvProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* state = static_cast<UvWindow*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    auto* state = reinterpret_cast<UvWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (!state) return DefWindowProcW(window,message,wParam,lParam);
    const Vec2 point{double(GET_X_LPARAM(lParam)),double(GET_Y_LPARAM(lParam))};
    switch (message) {
    case WM_CREATE: state->create(); return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {550,375}; return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) state->resize(LOWORD(lParam),HIWORD(lParam));
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id >= firstToolId && id <= firstToolId+3) {
            state->tool = Tool(id-firstToolId);
            CheckRadioButton(window,firstToolId,firstToolId+3,id);
        } else if (id == restoreId) {
            state->uvs = state->defaults;
            state->fit();
        } else if (id == unfoldId) {
            state->uvs = unfoldTextureUvs(state->mesh);
            state->fit();
        } else if (id == fitId) state->fit();
        else if (id == textureId && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto choice = SendDlgItemMessageW(window,textureId,CB_GETCURSEL,0,0);
            if (choice != CB_ERR) { state->choice = int(choice); state->fit(); }
        } else if (id == IDOK || id == IDCANCEL) {
            state->accepted = id == IDOK;
            state->finished = true;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (state->onCanvas(point) && !state->panning) { SetFocus(window); state->begin(point); }
        return 0;
    case WM_RBUTTONDOWN:
        if ((wParam & MK_CONTROL) && state->onCanvas(point) && !state->dragging && !state->panning) state->pick(point);
        return 0;
    case WM_MBUTTONDOWN:
        if (!state->dragging && state->onCanvas(point)) {
            state->start = point;
            state->panStart = state->origin;
            state->panning = true;
            SetCapture(window);
        }
        return 0;
    case WM_MOUSEMOVE: state->move(point); return 0;
    case WM_LBUTTONUP: case WM_MBUTTONUP:
        if ((message == WM_LBUTTONUP && state->dragging) || (message == WM_MBUTTONUP && state->panning)) {
            state->move(point);
            state->dragging = state->panning = false;
            if (GetCapture() == window) ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED: state->dragging = state->panning = false; return 0;
    case WM_MOUSEWHEEL: {
        if (state->dragging || state->panning) return 0;
        POINT client{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        ScreenToClient(window,&client);
        const Vec2 anchor{double(client.x),double(client.y)};
        if (!state->onCanvas(anchor)) return 0;
        const double zoom = std::clamp(state->zoom * std::pow(1.2,GET_WHEEL_DELTA_WPARAM(wParam)/120.0),.12,16.0);
        const double factor = zoom/state->zoom;
        state->zoom = zoom;
        state->origin = anchor + (state->origin-anchor)*factor;
        state->scale = state->scale*factor;
        state->invalidate(true);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC target = BeginPaint(window,&paint);
        if (state->width > 0 && state->height > 0) {
            const HDC buffer = CreateCompatibleDC(target);
            const HBITMAP bitmap = CreateCompatibleBitmap(target,state->width,state->height);
            if (buffer && bitmap) {
                const auto old = SelectObject(buffer,bitmap);
                state->draw(buffer);
                BitBlt(target,0,0,state->width,state->height,buffer,0,0,SRCCOPY);
                SelectObject(buffer,old);
            } else state->draw(target);
            if (bitmap) DeleteObject(bitmap);
            if (buffer) DeleteDC(buffer);
        }
        EndPaint(window,&paint);
        return 0;
    }
    case WM_CLOSE: state->finished = true; return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
} // namespace

bool hasStackedTextureUvs(const Mesh& mesh, const TextureUvs* layout) {
    if (mesh.faces.size() < 2 || (layout && layout->size() != mesh.faces.size())) return false;
    const auto uv = [&](std::size_t i) -> const std::array<Vec2,4>& {
        return layout ? (*layout)[i] : mesh.faces[i].uv;
    };
    const auto count = mesh.faces.front().count;
    const auto& reference = uv(0);
    for (std::size_t i = 1; i < mesh.faces.size(); ++i) {
        if (mesh.faces[i].count != count) return false;
        const auto& corners = uv(i);
        std::array<bool,4> used{};
        for (std::size_t a = 0; a < count; ++a) {
            std::size_t b = 0;
            while (b < count && (used[b] || std::abs(reference[a].x-corners[b].x) > 1e-9 ||
                                  std::abs(reference[a].y-corners[b].y) > 1e-9)) ++b;
            if (b == count) return false;
            used[b] = true;
        }
    }
    return true;
}

TextureUvs unfoldTextureUvs(const Mesh& mesh) {
    TextureUvs result(mesh.faces.size());
    const auto& box = modelMesh(Model{}, true);
    const bool boxTopology = mesh.vertices.size() == box.vertices.size() && mesh.faces.size() == box.faces.size() &&
        std::equal(mesh.faces.begin(), mesh.faces.end(), box.faces.begin(), [](const MeshFace& a, const MeshFace& b) {
            return a.count == b.count && a.vertices == b.vertices;
        });
    if (boxTopology) {
        // The front face is the center, and the back folds out from the right face.
        constexpr Vec2 net[][4] = {
            {{4,2},{4,1},{3,1},{3,2}}, {{1,2},{2,2},{2,1},{1,1}},
            {{0,2},{1,2},{1,1},{0,1}}, {{3,2},{3,1},{2,1},{2,2}},
            {{1,3},{2,3},{2,2},{1,2}}, {{1,0},{1,1},{2,1},{2,0}}
        };
        for (std::size_t i = 0; i < result.size(); ++i)
            for (std::size_t j = 0; j < 4; ++j) result[i][j] = Vec2{.04,.155} + net[i][j] * .23;
        return result;
    }
    if (result.empty()) return result;
    const int columns = int(std::ceil(std::sqrt(double(result.size()))));
    const int rows = (int(result.size()) + columns - 1) / columns;
    const double cell = 1.0 / columns;
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const auto& face = mesh.faces[i];
        const Vec3 origin = mesh.vertices[face.vertices[0]];
        const Vec3 right = normalized(mesh.vertices[face.vertices[1]] - origin);
        Vec3 normal{};
        for (std::size_t j = 1; j+1 < face.count; ++j)
            normal = normal + cross(mesh.vertices[face.vertices[j]]-origin, mesh.vertices[face.vertices[j+1]]-origin);
        const Vec3 up = normalized(cross(right, normal));
        Vec2 low{}, high{};
        auto& corners = result[i];
        for (std::size_t j = 0; j < face.count; ++j) {
            const Vec3 p = mesh.vertices[face.vertices[j]] - origin;
            corners[j] = {dot(p,right), dot(p,up)};
            low = {std::min(low.x,corners[j].x), std::min(low.y,corners[j].y)};
            high = {std::max(high.x,corners[j].x), std::max(high.y,corners[j].y)};
        }
        const Vec2 center{(i % columns + .5) * cell, (i / columns + .5) * cell + (1 - rows * cell) * .5};
        const double scale = .88 * cell / std::max({high.x-low.x, high.y-low.y, 1e-12});
        for (std::size_t j = 0; j < face.count; ++j) corners[j] = center + (corners[j] - (low+high)*.5) * scale;
    }
    return result;
}

bool editTextureMesh(HWND owner, const Mesh& mesh, const TextureUvs& defaults, bool startWithDefaults,
                     const std::vector<TextureChoice>& textures, int& material, TextureUvs& result) {
    if (mesh.faces.empty() || defaults.size() != mesh.faces.size() || textures.empty()) return false;
    constexpr wchar_t className[] = L"3D1TextureMeshEditor";
    WNDCLASSW wc{};
    wc.lpfnWndProc = uvProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr,IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    UvWindow state(mesh,defaults,startWithDefaults,textures,material);
    HMENU menu = CreateMenu(), edit = CreatePopupMenu();
    AppendMenuW(edit,MF_STRING,restoreId,L"&Restore");
    AppendMenuW(edit,MF_STRING,unfoldId,L"&Unfold");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(edit),L"&Edit");
    RECT r{};
    GetWindowRect(owner,&r);
    constexpr int w = 870, h = 690;
    const HWND window = CreateWindowExW(WS_EX_CONTROLPARENT,className,L"Texture Mesh Editor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,(r.left+r.right-w)/2,(r.top+r.bottom-h)/2,
        w,h,owner,menu,wc.hInstance,&state);
    if (!window) { DestroyMenu(menu); return false; }
    EnableWindow(owner,FALSE);
    ShowWindow(window,SW_SHOW);
    SetFocus(window);
    MSG message{};
    BOOL status = TRUE;
    while (!state.finished && (status = GetMessageW(&message,nullptr,0,0)) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            SendDlgItemMessageW(window,textureId,CB_GETDROPPEDSTATE,0,0) == FALSE) state.finished = true;
        else if (!IsDialogMessageW(window,&message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner,TRUE);
    DestroyWindow(window);
    SetActiveWindow(owner);
    if (status == 0) PostQuitMessage(int(message.wParam));
    if (!state.accepted) return false;
    result = std::move(state.uvs);
    material = textures[state.choice].material;
    return true;
}
