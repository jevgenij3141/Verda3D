#include "startup_splash.h"
#include "startup_assets.h"
#include "scene_mesh.h"

#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <new>

namespace {
constexpr int artWidth = 740, artHeight = 402;
constexpr ULONGLONG introDuration = 1900;

struct StartupArt {
    UINT width = 0, height = 0;
    std::vector<unsigned char> pixels;
};

struct ReleaseCom { void operator()(IUnknown* value) const { value->Release(); } };

StartupArt loadStartupArt() {
    constexpr WORD rawResourceType = 10;
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(VERDA_STARTUP_ART), MAKEINTRESOURCEW(rawResourceType));
    if (!resource) return {};
    const auto bytes = SizeofResource(module, resource);
    const auto memory = LoadResource(module, resource);
    auto* data = static_cast<BYTE*>(LockResource(memory));
    if (!data || !bytes) return {};
    IWICImagingFactory* rawFactory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&rawFactory)))) return {};
    const std::unique_ptr<IWICImagingFactory, ReleaseCom> factory(rawFactory);
    IWICStream* rawStream = nullptr;
    if (FAILED(factory->CreateStream(&rawStream))) return {};
    const std::unique_ptr<IWICStream, ReleaseCom> stream(rawStream);
    if (FAILED(stream->InitializeFromMemory(data, bytes))) return {};
    IWICBitmapDecoder* rawDecoder = nullptr;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                &rawDecoder))) return {};
    const std::unique_ptr<IWICBitmapDecoder, ReleaseCom> decoder(rawDecoder);
    IWICBitmapFrameDecode* rawFrame = nullptr;
    if (FAILED(decoder->GetFrame(0, &rawFrame))) return {};
    const std::unique_ptr<IWICBitmapFrameDecode, ReleaseCom> frame(rawFrame);
    IWICFormatConverter* rawConverter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&rawConverter))) return {};
    const std::unique_ptr<IWICFormatConverter, ReleaseCom> converter(rawConverter);
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                                      0, WICBitmapPaletteTypeCustom))) return {};
    StartupArt art;
    if (FAILED(converter->GetSize(&art.width, &art.height)) || art.width == 0 || art.height == 0 ||
        art.width > 2048 || art.height > 2048) return {};
    const UINT size = art.width * art.height * 4;
    art.pixels.resize(size);
    if (FAILED(converter->CopyPixels(nullptr, art.width * 4, size, art.pixels.data()))) return {};
    return art;
}

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

void title(HDC dc, double seconds) {
    const int saved = SaveDC(dc);
    const HFONT font = CreateFontW(-64,0,0,0,FW_HEAVY,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI Black");
    SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);
    const int count = int(std::wcslen(editorName));
    constexpr int x = 40, y = 126;
    for (int depth = 8; depth > 0; --depth) {
        SetTextColor(dc,RGB(115-depth*8,24-depth,66-depth*3));
        TextOutW(dc,x+depth,y+depth,editorName,count);
    }
    const bool path = BeginPath(dc) && TextOutW(dc,x,y,editorName,count) && EndPath(dc) &&
                      SelectClipPath(dc,RGN_AND);
    if (path) {
        const int highlight = x + int(std::clamp(seconds / 1.5, 0.0, 1.0) * 265);
        for (int row = y; row < y + 76; ++row) {
            const double height = (row-y)/75.0;
            const double light = .70 + .30 * std::abs(std::cos(pi * (height - .1) * 1.55));
            block(dc,{x,row,x+328,row+1},RGB(int(255*light),int(224*light),int(243*light)));
        }
        const int gleam = SaveDC(dc);
        IntersectClipRect(dc,highlight-1,y+3,highlight+3,y+63);
        block(dc,{highlight-1,y+3,highlight+3,y+63},RGB(255,245,250));
        RestoreDC(dc,gleam);
    } else {
        SetTextColor(dc,RGB(246,232,247));
        TextOutW(dc,x,y,editorName,count);
    }
    RestoreDC(dc,saved);
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
        const double light = .29 + .71 * std::max(0.0, dot(normal,sun));
        const Vec3 reflection = normalized(sun + normalized({.72,.45,1}));
        const double specular = std::pow(std::max(0.0, dot(normal, reflection)), 11) * .67;
        const auto channel = [&](double c) { return int(std::lerp(c * light, .97, specular) * 255); };
        Facet facet{{}, int(face.count), 0, RGB(channel(color.r),channel(color.g),channel(color.b))};
        for (std::size_t j = 0; j < face.count; ++j) {
            const auto point = posed[face.vertices[j]];
            facet.points[j] = project(point);
            facet.depth += dot(point,Vec3{.72,.45,1}) / face.count;
        }
        faces.push_back(facet);
    }
}

void atmosphere(HDC dc, double seconds) {
    constexpr int w = artWidth / 2, h = artHeight / 2;
    std::vector<std::uint32_t> pixels(w * h);
    const auto cloud = [](double x, double y, double cx, double cy, double width, double height) {
        const double u = (x-cx)/width, v = (y-cy)/height;
        const double density = std::max(0.0, 1 - u*u - v*v);
        return density*density*density;
    };
    for (int py = 0; py < h; ++py) {
        const double y = py * 2;
        for (int px = 0; px < w; ++px) {
            const double x = px * 2 - seconds * 11;
            const double top = cloud(x,y,480,116,139,70) + cloud(x,y,555,83,117,64) +
                               cloud(x,y,620,118,101,49) + cloud(x,y,679,151,101,56);
            const double mist = cloud(x,y,520,308,282,47) + cloud(x,y,665,264,198,35);
            const double halo = cloud(x,y,526,218,227,174);
            const auto channel = [](double v) { return std::uint32_t(std::clamp(v, 0.0, 255.0)); };
            const auto r = channel(9 + 38*halo + 45*top + 27*mist);
            const auto g = channel(13 + 13*halo + 52*top + 34*mist);
            const auto b = channel(23 + 26*halo + 77*top + 52*mist);
            pixels[py*w + px] = (r << 16) | (g << 8) | b;
        }
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = w;
    bitmap.bmiHeader.biHeight = -h;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc,0,0,artWidth,artHeight,0,0,w,h,pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
}

void drawFallbackScene(HDC dc, double seconds) {
    atmosphere(dc, seconds);
    for (int i = -5; i <= 5; ++i) {
        const double grid = i*.50;
        const COLORREF ink = i == 0 ? RGB(76,102,137) : RGB(36,52,78);
        stroke(dc, project({grid,0,-2.5}), project({grid,0,2.5}), ink);
        stroke(dc, project({-2.5,0,grid}), project({2.5,0,grid}), ink);
    }
    static const Mesh ruby = createExtraPrimitive(ObjectKind::CrystalBrilliant);
    static const Mesh emerald = createExtraPrimitive(ObjectKind::CrystalStep);
    static const Mesh sapphire = createExtraPrimitive(ObjectKind::CrystalOctahedron);
    static const Mesh amethyst = createExtraPrimitive(ObjectKind::CrystalPoint);
    const double entrance = 1 - std::clamp(seconds/.62,0.0,1.0);
    const double lift = entrance*entrance*.55;
    std::vector<Facet> faces;
    shape(faces, emerald, {-.41,.38+lift,.63}, {.94,.78,.94}, .24 + seconds*.37, {.14f,.92f,.55f});
    shape(faces, sapphire, {1.12,.53+lift,.24}, {.82,1.08,.82}, seconds*.43, {.27f,.51f,1});
    shape(faces, amethyst, {.29,.69+lift,1.57}, {.62,1.2,.67}, -.53 + seconds*.28, {.73f,.38f,1});
    shape(faces, ruby, {.10,1.61+lift+.12*std::sin(seconds*3.4),-.28},
          {1.52,1.66,1.52}, .35 + seconds*.84, {1,.09f,.23f});
    std::sort(faces.begin(),faces.end(),[](const Facet& a,const Facet& b) { return a.depth < b.depth; });
    const HPEN pen = CreatePen(PS_SOLID,1,RGB(45,21,53));
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
    const auto sparkle = project({.49,1.98+lift,-.1});
    const int shine = 4 + int((1 + std::sin(seconds * 6)) * 4);
    stroke(dc,{sparkle.x-shine,sparkle.y},{sparkle.x+shine,sparkle.y},RGB(255,173,195));
    stroke(dc,{sparkle.x,sparkle.y-shine},{sparkle.x,sparkle.y+shine},RGB(255,227,234));
}

void drawIntro(HDC dc, double seconds, bool ready, const StartupArt& art) {
    if (art.pixels.empty()) drawFallbackScene(dc, seconds);
    else {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = LONG(art.width);
        bitmap.bmiHeader.biHeight = -LONG(art.height);
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetStretchBltMode(dc, HALFTONE);
        const double entrance = 1 - std::clamp(seconds / 1.25, 0.0, 1.0);
        const int inset = int(artWidth * .018 * entrance * entrance);
        StretchDIBits(dc,-inset,-inset/2,artWidth+inset*2,artHeight+inset,0,0,art.width,art.height,
                      art.pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
    }
    title(dc, seconds);
    block(dc,{47,202,162,204},RGB(227,60,104));
    caption(dc, L"v" VERDA_VERSION, 47, 214, 17, FW_SEMIBOLD, RGB(219,156,185));
    caption(dc, ready ? L"Ready" : L"Initializing graphics", 46, 328, 14, FW_NORMAL, RGB(167,178,199));
    block(dc,{46,354,239,357},RGB(43,48,76));
    if (ready) block(dc,{46,354,239,357},RGB(237,101,143));
    else {
        const int x = 46 + int(std::fmod(seconds*117,146.0));
        block(dc,{x,354,x+47,357},RGB(237,101,143));
    }
}

struct SplashState { ULONGLONG start = GetTickCount64(); bool ready = false; StartupArt artwork; };

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
            drawIntro(buffer,(GetTickCount64()-state->start)/1000.0,state->ready,state->artwork);
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
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(com)) {
        try { state.artwork = loadStartupArt(); } catch (const std::bad_alloc&) {}
        CoUninitialize();
    }
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
