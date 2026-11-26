#include "startup_splash.h"
#include "scene_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace {
constexpr int artWidth = 740, artHeight = 402;
constexpr ULONGLONG introDuration = 1900;

POINT project(Vec3 p) {
    return {LONG(492 + 69 * (p.x - .72 * p.z)), LONG(279 + 25 * (p.x + p.z) - 78 * p.y)};
}

void stroke(HDC dc, POINT a, POINT b, COLORREF color, int width = 1) {
    const HPEN pen = CreatePen(PS_SOLID, width, color);
    const auto old = SelectObject(dc, pen);
    MoveToEx(dc, a.x, a.y, nullptr);
    LineTo(dc, b.x, b.y);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void block(HDC dc, RECT bounds, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void caption(HDC dc, const wchar_t* text, int x, int y, int size, int weight, COLORREF color) {
    const HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text, int(std::wcslen(text)));
    SelectObject(dc, old);
    DeleteObject(font);
}

struct Facet {
    std::array<POINT,4> points;
    int count;
    double depth;
    COLORREF color;
};

void shape(std::vector<Facet>& faces, const Mesh& mesh, Vec3 position, Vec3 dimensions, double angle, Color color) {
    const Vec3 sun = normalized(Vec3{-.8,1.6,1.1});
    std::vector<Vec3> posed;
    posed.reserve(mesh.vertices.size());
    for (Vec3 p : mesh.vertices)
        posed.push_back(position + rotated({p.x*dimensions.x,p.y*dimensions.y,p.z*dimensions.z},axes[1],angle));
    for (const auto& face : mesh.faces) {
        const auto a = posed[face.vertices[0]], b = posed[face.vertices[1]];
        const auto c = posed[face.vertices[face.count-1]];
        const Vec3 normal = normalized(cross(b-a,c-a));
        if (dot(normal, Vec3{.72,.45,1}) <= 0) continue;
        const double light = .50 + .50 * std::max(0.0, dot(normal,sun));
        Facet facet{{}, int(face.count), 0, RGB(int(color.r*light*255),int(color.g*light*255),int(color.b*light*255))};
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto point = posed[face.vertices[j]];
            facet.points[j] = project(point);
            facet.depth += dot(point,Vec3{.72,.45,1}) / face.count;
        }
        faces.push_back(facet);
    }
}

void plant(HDC dc, Vec3 base, double height, double sway, bool warm) {
    Vec3 previous = base;
    const COLORREF stem = warm ? RGB(135,178,126) : RGB(70,148,108);
    for (int i = 1; i <= 8; ++i) {
        const double t = i / 8.0;
        const Vec3 node = base + Vec3{sway*t*t,height*t,.16*t};
        stroke(dc, project(previous), project(node), stem, 2);
        previous = node;
        if (i < 3) continue;
        const double sign = i % 2 ? -1 : 1;
        const Vec3 tip = node + Vec3{sign*(.49-.025*i),.16 + .017*i,sign*.16};
        const Vec3 bend = (node+tip)*.5;
        POINT leaf[] = {project(node),project(bend+Vec3{sign*.05,.13,-.04}),
                        project(tip),project(bend+Vec3{-sign*.02,-.08,.04})};
        const HBRUSH brush = CreateSolidBrush(warm ? (i%2 ? RGB(185,214,127) : RGB(118,187,123))
                                                  : (i%2 ? RGB(60,170,127) : RGB(116,207,150)));
        const auto old = SelectObject(dc,brush);
        const auto oldPen = SelectObject(dc,GetStockObject(NULL_PEN));
        Polygon(dc,leaf,4);
        SelectObject(dc,oldPen);
        SelectObject(dc,old);
        DeleteObject(brush);
        stroke(dc,project(node),project(tip),stem);
    }
}

void drawIntro(HDC dc, double seconds, bool ready) {
    for (int y = 0; y < artHeight; y += 4) {
        const double t = double(y)/artHeight;
        block(dc,{0,y,artWidth,y+4},RGB(10+int(7*t),24+int(13*t),24+int(9*t)));
    }
    for (int i = -5; i <= 5; ++i) {
        const double grid = i*.50;
        const COLORREF ink = i == 0 ? RGB(72,124,103) : RGB(41,80,68);
        stroke(dc, project({grid,0,-2.5}), project({grid,0,2.5}), ink);
        stroke(dc, project({-2.5,0,grid}), project({2.5,0,grid}), ink);
    }
    plant(dc,{1.38,0,-.52},1.74,.28 + std::sin(seconds*2)*.11,false);
    plant(dc,{.95,0,-1.12},1.41,-.20 + std::sin(seconds*2.2)*.10,true);
    plant(dc,{-1.12,0,-.67},1.07,-.26 + std::sin(seconds*1.8)*.08,false);

    static const Mesh box = modelMesh(Model{},true);
    static const Mesh cylinder = createPrimitive(ObjectKind::Cylinder);
    static const Mesh pyramid = createPrimitive(ObjectKind::Pyramid);
    static const Mesh octahedron{
        {{0,.70,0},{.55,0,0},{0,0,.55},{-.55,0,0},{0,0,-.55},{0,-.70,0}},
        {{{0,2,1,1},{},3},{{0,3,2,2},{},3},{{0,4,3,3},{},3},{{0,1,4,4},{},3},
         {{5,1,2,2},{},3},{{5,2,3,3},{},3},{{5,3,4,4},{},3},{{5,4,1,1},{},3}}
    };
    const double entrance = 1 - std::clamp(seconds/.62,0.0,1.0);
    const double lift = entrance*entrance*.55;
    std::vector<Facet> faces;
    shape(faces, box, {-.52,.27+lift,.45}, {.94,.54,.85}, .24 + seconds*.28, {.94f,.63f,.35f});
    shape(faces, cylinder, {.66,.43+lift,-.18}, {.91,.86,.91}, seconds*.31, {.40f,.80f,.64f});
    shape(faces, pyramid, {.42,.24+lift,.92}, {.62,.48,.67}, -.53 + seconds*.28, {.64f,.65f,.96f});
    shape(faces, octahedron, {-.04,1.56+lift+.12*std::sin(seconds*3.4),-.23},
          {.92,1,.92}, .35 + seconds*.84, {.83f,.72f,.98f});
    std::sort(faces.begin(),faces.end(),[](const Facet& a,const Facet& b) { return a.depth < b.depth; });
    const HPEN pen = CreatePen(PS_SOLID,1,RGB(32,61,55));
    const auto previous = SelectObject(dc,pen);
    for (const auto& face : faces) {
        const HBRUSH brush = CreateSolidBrush(face.color);
        const auto old = SelectObject(dc,brush);
        Polygon(dc,face.points.data(),face.count);
        SelectObject(dc,old);
        DeleteObject(brush);
    }
    SelectObject(dc,previous);
    DeleteObject(pen);
    plant(dc,{-1.61,0,1.04},.63,-.20 + std::sin(seconds*2)*.07,true);
    plant(dc,{1.52,0,1.19},.85,.28 + std::sin(seconds*2.7)*.07,false);

    caption(dc, editorName, 43, 132, 53, FW_SEMIBOLD, RGB(221,240,222));
    caption(dc, L"3D model editor", 46, 198, 19, FW_NORMAL, RGB(159,187,167));
    caption(dc, ready ? L"Ready" : L"Initializing graphics", 46, 328, 14, FW_NORMAL, RGB(162,189,168));
    block(dc,{46,354,239,357},RGB(38,78,65));
    if (ready) block(dc,{46,354,239,357},RGB(133,225,171));
    else {
        const int x = 46 + int(std::fmod(seconds*117,146.0));
        block(dc,{x,354,x+47,357},RGB(133,225,171));
    }
}

struct SplashState { ULONGLONG start = GetTickCount64(); bool ready = false; };

LRESULT CALLBACK splashProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_LBUTTONDOWN || message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        const auto* state = reinterpret_cast<const SplashState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        PAINTSTRUCT region{};
        const HDC output = BeginPaint(window,&region);
        RECT area{};
        GetClientRect(window,&area);
        const HDC buffer = CreateCompatibleDC(output);
        const HBITMAP bitmap = CreateCompatibleBitmap(output,area.right,area.bottom);
        if (buffer && bitmap && state) {
            const auto old = SelectObject(buffer,bitmap);
            SetMapMode(buffer,MM_ANISOTROPIC);
            SetWindowExtEx(buffer,artWidth,artHeight,nullptr);
            SetViewportExtEx(buffer,area.right,area.bottom,nullptr);
            drawIntro(buffer,(GetTickCount64()-state->start)/1000.0,state->ready);
            SetMapMode(buffer,MM_TEXT);
            BitBlt(output,0,0,area.right,area.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,old);
        }
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        EndPaint(window,&region);
        return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

void showSplash(HANDLE ready) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = splashProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr,IDC_ARROW);
    wc.lpszClassName = L"VerdaStartupSplash";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    RECT area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0)) return;
    const double scale = std::min({1.0,(area.right-area.left)*.88/artWidth,(area.bottom-area.top)*.88/artHeight});
    const int w = int(artWidth*scale), h = int(artHeight*scale);
    SplashState state;
    const HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
        editorName, WS_POPUP, (area.left+area.right-w)/2,(area.top+area.bottom-h)/2,
        w,h,nullptr,nullptr,wc.hInstance,&state);
    if (!window) return;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    auto frame = state.start;
    while (IsWindow(window)) {
        const DWORD count = state.ready ? 0 : 1;
        const DWORD event = MsgWaitForMultipleObjects(count,count ? &ready : nullptr,FALSE,33,QS_ALLINPUT);
        if (count && event == WAIT_OBJECT_0) state.ready = true;
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&message);
        const auto now = GetTickCount64();
        if (state.ready && now-state.start >= introDuration) break;
        if (now-frame >= 30) {
            InvalidateRect(window,nullptr,FALSE);
            frame = now;
        }
    }
    if (IsWindow(window)) DestroyWindow(window);
}
} // namespace

StartupSplash::StartupSplash(bool enabled) {
    if (!enabled) return;
    ready = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!ready) return;
    try { animation = std::thread(showSplash,ready); }
    catch (...) { CloseHandle(ready); ready = nullptr; }
}

StartupSplash::~StartupSplash() { finish(); }

void StartupSplash::finish() {
    if (!ready) return;
    SetEvent(ready);
    if (animation.joinable()) animation.join();
    CloseHandle(ready);
    ready = nullptr;
}
