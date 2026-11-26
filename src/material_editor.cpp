#include "material_editor.h"
#include "material_library.h"

#include <commctrl.h>
#include <commdlg.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>
#include <utility>

namespace {
struct ComRelease {
    void operator()(IUnknown* object) const { object->Release(); }
};

template<class T>
using ComPtr = std::unique_ptr<T, ComRelease>;

int textureSize(UINT source) {
    int size = 1;
    while (size < maxTextureDimension && static_cast<UINT>(size) < source) size *= 2;
    return size;
}

bool readTexture(const wchar_t* path, TextureImage& image) {
    IWICImagingFactory* rawFactory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, reinterpret_cast<void**>(&rawFactory)))) {
        return false;
    }
    const ComPtr<IWICImagingFactory> factory(rawFactory);
    IWICBitmapDecoder* rawDecoder = nullptr;
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnDemand, &rawDecoder))) {
        return false;
    }
    const ComPtr<IWICBitmapDecoder> decoder(rawDecoder);
    IWICBitmapFrameDecode* rawFrame = nullptr;
    if (FAILED(decoder->GetFrame(0, &rawFrame))) return false;
    const ComPtr<IWICBitmapFrameDecode> frame(rawFrame);
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return false;

    TextureImage loaded;
    loaded.width = textureSize(width);
    loaded.height = textureSize(height);
    IWICBitmapScaler* rawScaler = nullptr;
    if (FAILED(factory->CreateBitmapScaler(&rawScaler))) return false;
    const ComPtr<IWICBitmapScaler> scaler(rawScaler);
    if (FAILED(scaler->Initialize(frame.get(), loaded.width, loaded.height,
                                  WICBitmapInterpolationModeFant))) return false;
    IWICFormatConverter* rawConverter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&rawConverter))) return false;
    const ComPtr<IWICFormatConverter> converter(rawConverter);
    if (FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom))) return false;
    const UINT stride = loaded.width * 4;
    loaded.pixels.resize(stride * loaded.height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(loaded.pixels.size()),
                                     loaded.pixels.data()))) return false;
    loaded.filename = path;
    loaded.filename.erase(0, loaded.filename.find_last_of(L"\\/") + 1);
    image = std::move(loaded);
    return true;
}

constexpr int browseId = 101;
constexpr int colorId = 102;
constexpr int shadingId = 103;
constexpr int clearTextureId = 104;
constexpr int repeatXId = 105, repeatYId = 106;
constexpr RECT previewArea{16, 35, 295, 316};
constexpr RECT swatchArea{450, 74, 511, 96};
constexpr int sliderTop = 116;
constexpr int sliderSpacing = 39;
constexpr double pi = 3.141592653589793;
constexpr const wchar_t* shadingNames[] = {L"Blinn-Phong", L"Phong", L"Lambert", L"Unlit"};
struct SurfaceSlider { const wchar_t* label; int Surface::* value; int min; int max; };
constexpr SurfaceSlider sliders[] = {
    {L"Ambient", &Surface::ambient, 0, 100},
    {L"Diffuse", &Surface::diffuse, 0, 100},
    {L"Specular", &Surface::specular, 0, 100},
    {L"Shininess", &Surface::shininess, 1, 128},
    {L"Emission", &Surface::emission, 0, 100},
};
struct OpticsSlider { const wchar_t* label; double Optics::* value; int min; int max; };
constexpr OpticsSlider opticsSliders[] = {
    {L"Transmission", &Optics::transmission, 0, 100},
    {L"Refraction IOR", &Optics::index, 100, 250},
    {L"Caustic strength", &Optics::causticStrength, 0, 400},
};

COLORREF colorRef(Color color) {
    return RGB(int(color.r * 255 + .5f), int(color.g * 255 + .5f), int(color.b * 255 + .5f));
}

HWND control(HWND parent, const wchar_t* type, const wchar_t* label, DWORD style,
             int x, int y, int w, int h, int id = 0);

struct MaterialDialog {
    MaterialEdit edit;
    HWND fileLabel = nullptr;
    HWND clearButton = nullptr;
    HWND shadingList = nullptr;
    std::array<HWND, std::size(sliders)> tracks{};
    std::array<HWND, std::size(sliders)> values{};
    std::array<HWND, std::size(opticsSliders)> opticsTracks{};
    std::array<HWND, std::size(opticsSliders)> opticsValues{};
    std::array<COLORREF, 16> customColors{};
    bool accepted = false;
    bool finished = false;

    void createControls(HWND window) {
        control(window, L"STATIC", L"Material preview", 0, 16, 14, 200, 19);
        control(window, L"STATIC", L"Shading model", 0, 315, 19, 190, 19);
        shadingList = control(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
                              315, 38, 280, 155, shadingId);
        for (const auto* name : shadingNames) {
            SendMessageW(shadingList, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        }
        SendMessageW(shadingList, CB_SETCURSEL, static_cast<WPARAM>(edit.surface.shading), 0);
        control(window, L"BUTTON", L"Base color...", WS_TABSTOP, 315, 71, 121, 28, colorId);

        for (int i = 0; i < int(std::size(sliders)); ++i) {
            const auto& slider = sliders[i];
            const int y = sliderTop + i * sliderSpacing;
            control(window, L"STATIC", slider.label, 0, 315, y, 98, 20);
            tracks[i] = control(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS,
                                407, y - 4, 149, 31);
            values[i] = control(window, L"STATIC", L"", SS_RIGHT, 558, y, 37, 20);
            SendMessageW(tracks[i], TBM_SETRANGE, FALSE, MAKELPARAM(slider.min, slider.max));
            SendMessageW(tracks[i], TBM_SETPOS, TRUE, edit.surface.*(slider.value));
        }
        const Optics defaults = materialOptics(edit);
        for (int i = 0; i < int(std::size(opticsSliders)); ++i) {
            const auto& slider = opticsSliders[i];
            const int y = 310 + i * 34;
            control(window, L"STATIC", slider.label, 0, 315, y, 109, 20);
            opticsTracks[i] = control(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS,
                                      425, y - 4, 126, 31);
            opticsValues[i] = control(window, L"STATIC", L"", SS_RIGHT, 555, y, 40, 20);
            SendMessageW(opticsTracks[i], TBM_SETRANGE, FALSE, MAKELPARAM(slider.min, slider.max));
            SendMessageW(opticsTracks[i], TBM_SETPOS, TRUE, int(std::round(defaults.*(slider.value) * 100)));
        }
        updateSurface(window);
        control(window, L"BUTTON", L"Load texture...", WS_TABSTOP, 16, 329, 121, 28, browseId);
        clearButton = control(window, L"BUTTON", L"Remove", WS_TABSTOP, 145, 329, 73, 28, clearTextureId);
        fileLabel = control(window, L"STATIC", L"", SS_ENDELLIPSIS | SS_NOPREFIX,
                            16, 367, 279, 19);
        control(window, L"STATIC", L"Repeat X", 0, 16, 401, 66, 20);
        control(window, L"EDIT", std::to_wstring(edit.repeatX).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER,
                83, 397, 55, 24, repeatXId);
        control(window, L"STATIC", L"Repeat Y", 0, 153, 401, 66, 20);
        control(window, L"EDIT", std::to_wstring(edit.repeatY).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER,
                221, 397, 55, 24, repeatYId);
        SendDlgItemMessageW(window, repeatXId, EM_SETLIMITTEXT, 3, 0);
        SendDlgItemMessageW(window, repeatYId, EM_SETLIMITTEXT, 3, 0);
        updateTexture(window);
        control(window, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 429, 432, 76, 28, IDOK);
        control(window, L"BUTTON", L"Cancel", WS_TABSTOP, 519, 432, 76, 28, IDCANCEL);
    }

    bool updateRepeat(HWND window) {
        BOOL validX = FALSE, validY = FALSE;
        const UINT x = GetDlgItemInt(window, repeatXId, &validX, FALSE);
        const UINT y = GetDlgItemInt(window, repeatYId, &validY, FALSE);
        if (!validX || !validY || x < 1 || x > maxTextureRepeat || y < 1 || y > maxTextureRepeat) return false;
        edit.repeatX = int(x);
        edit.repeatY = int(y);
        InvalidateRect(window, &previewArea, FALSE);
        return true;
    }

    void updateSurface(HWND window) {
        const auto shading = SendMessageW(shadingList, CB_GETCURSEL, 0, 0);
        if (shading != CB_ERR) edit.surface.shading = static_cast<Shading>(shading);
        for (int i = 0; i < int(std::size(sliders)); ++i) {
            int& value = edit.surface.*(sliders[i].value);
            value = static_cast<int>(SendMessageW(tracks[i], TBM_GETPOS, 0, 0));
            SetWindowTextW(values[i], std::to_wstring(value).c_str());
            const bool glossControl = sliders[i].value == &Surface::specular ||
                                      sliders[i].value == &Surface::shininess;
            const bool enabled = edit.surface.shading != Shading::Unlit &&
                                 (!glossControl || edit.surface.shading != Shading::Lambert);
            EnableWindow(tracks[i], enabled);
            EnableWindow(values[i], enabled);
        }
        Optics optics = materialOptics(edit);
        for (int i = 0; i < int(std::size(opticsSliders)); ++i) {
            const int position = static_cast<int>(SendMessageW(opticsTracks[i], TBM_GETPOS, 0, 0));
            optics.*(opticsSliders[i].value) = position / 100.0;
            wchar_t text[16]{};
            if (i == 1) std::swprintf(text, std::size(text), L"%.2f", position / 100.0);
            else std::swprintf(text, std::size(text), L"%d%%", position);
            SetWindowTextW(opticsValues[i], text);
            EnableWindow(opticsTracks[i], i == 0 || SendMessageW(opticsTracks[0], TBM_GETPOS, 0, 0) > 0);
        }
        edit.optics = optics;
        InvalidateRect(window, &previewArea, FALSE);
    }

    void updateTexture(HWND window) {
        const bool loaded = !edit.image.pixels.empty();
        SetWindowTextW(fileLabel, loaded ? edit.image.filename.c_str() : L"No texture loaded");
        EnableWindow(clearButton, loaded);
        InvalidateRect(window, &previewArea, FALSE);
    }

    void chooseColor(HWND window) {
        CHOOSECOLORW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = window;
        picker.rgbResult = colorRef(edit.surface.tint);
        picker.lpCustColors = customColors.data();
        picker.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (!ChooseColorW(&picker)) return;
        edit.surface.tint = {GetRValue(picker.rgbResult) / 255.f, GetGValue(picker.rgbResult) / 255.f,
                             GetBValue(picker.rgbResult) / 255.f};
        InvalidateRect(window, &previewArea, FALSE);
        InvalidateRect(window, &swatchArea, FALSE);
    }

    void browse(HWND window) {
        std::array<wchar_t, 32768> path{};
        OPENFILENAMEW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = window;
        picker.lpstrFilter = L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0"
                             L"All files\0*.*\0";
        picker.lpstrFile = path.data();
        picker.nMaxFile = static_cast<DWORD>(path.size());
        picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&picker)) return;

        if (!readTexture(path.data(), edit.image)) {
            MessageBoxW(window, L"Windows could not read this image.", L"Material Editor",
                        MB_OK | MB_ICONERROR);
            return;
        }
        updateTexture(window);
    }

    void draw(HWND window) const {
        PAINTSTRUCT region{};
        const HDC dc = BeginPaint(window, &region);
        const HBRUSH swatch = CreateSolidBrush(colorRef(edit.surface.tint));
        FillRect(dc, &swatchArea, swatch);
        DeleteObject(swatch);
        FrameRect(dc, &swatchArea, GetSysColorBrush(COLOR_3DSHADOW));
        if (RectVisible(dc, &previewArea)) {
            if (isCrystalPreset(edit.preset)) {
                FillRect(dc, &previewArea, GetSysColorBrush(COLOR_3DSHADOW));
                drawCrystalPreview(dc, previewArea, edit);
            } else drawSphere(dc);
        }
        EndPaint(window, &region);
    }

    void drawSphere(HDC dc) const {
        constexpr int side = 268;
        constexpr double radius = side * .45;
        std::vector<DWORD> pixels(side * side, RGB(49, 39, 32));
        const Vec3 light = normalized({-.45, .65, 1});
        const TextureImage& image = edit.image;
        const Color tint = edit.surface.tint;
        const auto optics = materialOptics(edit);
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                const double x = (col + .5 - side * .5) / radius;
                const double y = (side * .5 - row - .5) / radius;
                const double zSquared = 1 - x * x - y * y;
                if (zSquared <= 0) continue;
                const double z = std::sqrt(zSquared);
                const Lighting lighting = illuminate(edit.surface, {x, y, z}, light, {0, 0, 1});
                Color texel{1, 1, 1};
                if (!image.pixels.empty()) {
                    const double u = std::atan2(x, z) / pi + .5;
                    const double v = .5 - std::asin(y) / pi;
                    const auto* pixel = texturePixel(edit, {u, v});
                    texel = {pixel[2] / 255.f, pixel[1] / 255.f, pixel[0] / 255.f};
                }
                const double reflectance = (optics.index - 1) / (optics.index + 1);
                const float transparent = float(optics.transmission * (1 - reflectance * reflectance) * .75);
                const auto shade = [&](float channel, float texelChannel) {
                    const float diffuse = std::min(1.f, channel * lighting.base) * texelChannel;
                    return std::min(1.f, diffuse * (1 - transparent) + channel * .19f * transparent + lighting.highlight);
                };
                const Color shaded{shade(tint.r, texel.r), shade(tint.g, texel.g), shade(tint.b, texel.b)};
                pixels[row * side + col] = RGB(int(shaded.b * 255), int(shaded.g * 255), int(shaded.r * 255));
            }
        }
        FillRect(dc, &previewArea, GetSysColorBrush(COLOR_BTNFACE));
        FrameRect(dc, &previewArea, GetSysColorBrush(COLOR_3DSHADOW));
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = side;
        bitmap.bmiHeader.biHeight = -side;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(dc, (previewArea.left + previewArea.right - side) / 2,
                          (previewArea.top + previewArea.bottom - side) / 2, side, side, 0, 0, 0,
                          side, pixels.data(), &bitmap, DIB_RGB_COLORS);
    }
};

HWND control(HWND parent, const wchar_t* type, const wchar_t* label, DWORD style,
             int x, int y, int w, int h, int id) {
    const HWND item = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                     parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
    SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return item;
}

LRESULT CALLBACK dialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* dialog = reinterpret_cast<MaterialDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE:
        dialog->createControls(window);
        return 0;
    case WM_HSCROLL:
        dialog->updateSurface(window);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case browseId:
            dialog->browse(window);
            return 0;
        case clearTextureId:
            dialog->edit.image = {};
            dialog->updateTexture(window);
            return 0;
        case colorId:
            dialog->chooseColor(window);
            return 0;
        case shadingId:
            if (HIWORD(wParam) == CBN_SELCHANGE) dialog->updateSurface(window);
            return 0;
        case repeatXId: case repeatYId:
            if (HIWORD(wParam) == EN_CHANGE) dialog->updateRepeat(window);
            return 0;
        case IDOK:
            if (!dialog->updateRepeat(window)) {
                MessageBoxW(window, L"Repeat X and Repeat Y must be between 1 and 256.", L"Material Editor",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            dialog->accepted = true;
            [[fallthrough]];
        case IDCANCEL:
            dialog->finished = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        dialog->finished = true;
        return 0;
    case WM_PAINT:
        dialog->draw(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

bool editMaterial(HWND owner, int number, const MaterialEdit& current, MaterialEdit& resultEdit) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return false;
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t className[] = L"3D1MaterialWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = dialogProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        CoUninitialize();
        return false;
    }

    MaterialDialog dialog{current};
    constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT frame{0, 0, 612, 477};
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const std::wstring title = L"Material Editor - Material " + std::to_wstring(number);
    const HWND window = CreateWindowExW(exStyle, className, title.c_str(), style,
                                        (ownerRect.left + ownerRect.right - width) / 2,
                                        (ownerRect.top + ownerRect.bottom - height) / 2,
                                        width, height, owner, nullptr, instance, &dialog);
    if (window) {
        EnableWindow(owner, FALSE);
        ShowWindow(window, SW_SHOW);
        SetFocus(GetDlgItem(window, browseId));
        MSG message{};
        BOOL result = TRUE;
        while (!dialog.finished && (result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                dialog.finished = true;
            } else if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        EnableWindow(owner, TRUE);
        DestroyWindow(window);
        SetActiveWindow(owner);
        if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
    }
    CoUninitialize();
    if (dialog.accepted) resultEdit = std::move(dialog.edit);
    return dialog.accepted;
}
