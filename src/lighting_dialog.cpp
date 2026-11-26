#include "lighting_dialog.h"
#include "text_encoding.h"

#include <commdlg.h>

#include <array>
#include <cwchar>

namespace {
constexpr int firstMode = 100;
constexpr int samplesId = 102;
constexpr int causticsId = 103;
constexpr int photonsId = 104;
constexpr int resolutionId = 105, renderWidthId = 106, renderHeightId = 107, frameId = 108;
constexpr int openGLId = 150, vulkanId = 151;
constexpr int powerId = 110;
constexpr int rangeId = 111;
constexpr int radiusId = 112;
constexpr int coneId = 113;
constexpr int colorId = 120;
constexpr int enabledId = 121;
constexpr int transformId = 130;
constexpr int modifiersId = 140;

HWND add(HWND parent, const wchar_t* kind, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id = 0, DWORD ex = 0) {
    const HWND child = CreateWindowExW(ex, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    return child;
}

void layout(HWND dialog, const wchar_t* title, int width, int height) {
    RECT size{0, 0, width, height};
    AdjustWindowRectEx(&size, static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_EXSTYLE)));
    RECT owner{};
    GetWindowRect(GetWindow(dialog, GW_OWNER), &owner);
    const int w = size.right - size.left, h = size.bottom - size.top;
    SetWindowTextW(dialog, title);
    SetWindowPos(dialog, nullptr, (owner.left + owner.right - w) / 2,
                 (owner.top + owner.bottom - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    add(dialog, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, width - 177, height - 42, 76, 27, IDOK);
    add(dialog, L"BUTTON", L"Cancel", WS_TABSTOP, width - 92, height - 42, 76, 27, IDCANCEL);
}

INT_PTR runDialog(HWND owner, DLGPROC proc, LPARAM value) {
    struct alignas(4) {
        DLGTEMPLATE dialog{};
        WORD menu = 0, windowClass = 0, title = 0;
    } data;
    data.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    data.dialog.cx = 200;
    data.dialog.cy = 120;
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &data.dialog, owner, proc, value);
}

bool checked(HWND dialog, int id) { return IsDlgButtonChecked(dialog, id) == BST_CHECKED; }

void refreshModes(HWND dialog) {
    const bool software = checked(dialog, firstMode);
    const bool global = checked(dialog, firstMode + 1);
    EnableWindow(GetDlgItem(dialog, firstMode), !global);
    EnableWindow(GetDlgItem(dialog, firstMode + 1), !software);
    EnableWindow(GetDlgItem(dialog, samplesId), global);
    EnableWindow(GetDlgItem(dialog, photonsId), checked(dialog, causticsId));
    EnableWindow(GetDlgItem(dialog, IDOK), software || global);
}

bool readNumber(HWND dialog, int id, double low, double high, double& value, bool integer = false);

struct ResolutionPreset { const wchar_t* name; int width, height; };
constexpr ResolutionPreset resolutions[] = {
    {L"640 x 480 (VGA)", 640, 480},
    {L"1280 x 720 (HD)", 1280, 720},
    {L"1920 x 1080 (Full HD)", 1920, 1080},
    {L"2560 x 1440 (QHD)", 2560, 1440},
    {L"3840 x 2160 (4K UHD)", 3840, 2160},
};

void selectResolution(HWND dialog) {
    int index = 0;
    for (const auto& item : resolutions) {
        ++index;
        if (GetDlgItemInt(dialog, renderWidthId, nullptr, FALSE) == UINT(item.width) &&
            GetDlgItemInt(dialog, renderHeightId, nullptr, FALSE) == UINT(item.height)) {
            SendDlgItemMessageW(dialog, resolutionId, CB_SETCURSEL, index, 0);
            return;
        }
    }
    SendDlgItemMessageW(dialog, resolutionId, CB_SETCURSEL, 0, 0);
}

INT_PTR CALLBACK renderSetupProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* settings = reinterpret_cast<RenderSettings*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        layout(dialog, L"Setup Render", 420, 354);
        add(dialog, L"BUTTON", L"Software Rendering", BS_AUTOCHECKBOX | WS_TABSTOP,
            17, 18, 345, 24, firstMode);
        add(dialog, L"BUTTON", L"Global Illumination", BS_AUTOCHECKBOX | WS_TABSTOP,
            17, 48, 345, 24, firstMode + 1);
        add(dialog, L"STATIC", L"Samples per pixel", 0, 44, 86, 130, 22);
        const std::wstring text = std::to_wstring(settings->samples);
        add(dialog, L"EDIT", text.c_str(), ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            178, 81, 110, 25, samplesId, WS_EX_CLIENTEDGE);
        add(dialog, L"STATIC", L"1 - 65536", 0, 299, 86, 100, 22);
        add(dialog, L"BUTTON", L"Caustics", BS_AUTOCHECKBOX | WS_TABSTOP,
            17, 116, 145, 24, causticsId);
        CheckDlgButton(dialog, causticsId, settings->caustics ? BST_CHECKED : BST_UNCHECKED);
        add(dialog, L"STATIC", L"Caustic photons", 0, 44, 151, 130, 22);
        add(dialog, L"EDIT", std::to_wstring(settings->causticPhotons).c_str(),
            ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 178, 146, 110, 25, photonsId, WS_EX_CLIENTEDGE);
        add(dialog, L"STATIC", L"5000 - 500000", 0, 299, 151, 113, 22);
        add(dialog, L"STATIC", L"Resolution", 0, 17, 204, 112, 22);
        const HWND presets = add(dialog, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                 136, 199, 260, 160, resolutionId);
        SendMessageW(presets, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom"));
        for (const auto& item : resolutions)
            SendMessageW(presets, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name));
        add(dialog, L"STATIC", L"Width (px)", 0, 17, 244, 112, 22);
        add(dialog, L"EDIT", std::to_wstring(settings->width).c_str(), ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            136, 239, 80, 25, renderWidthId, WS_EX_CLIENTEDGE);
        add(dialog, L"STATIC", L"Height (px)", 0, 234, 244, 85, 22);
        add(dialog, L"EDIT", std::to_wstring(settings->height).c_str(), ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
            316, 239, 80, 25, renderHeightId, WS_EX_CLIENTEDGE);
        selectResolution(dialog);
        CheckDlgButton(dialog, firstMode + static_cast<int>(settings->mode), BST_CHECKED);
        refreshModes(dialog);
        SetFocus(GetDlgItem(dialog, firstMode + static_cast<int>(settings->mode)));
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    switch (LOWORD(wParam)) {
    case firstMode: case firstMode + 1: case causticsId:
        refreshModes(dialog);
        return TRUE;
    case resolutionId:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            const auto index = SendDlgItemMessageW(dialog, resolutionId, CB_GETCURSEL, 0, 0);
            if (index > 0 && index <= LRESULT(std::size(resolutions))) {
                const auto& item = resolutions[index - 1];
                SetDlgItemInt(dialog, renderWidthId, item.width, FALSE);
                SetDlgItemInt(dialog, renderHeightId, item.height, FALSE);
            } else SetFocus(GetDlgItem(dialog, renderWidthId));
        }
        return TRUE;
    case renderWidthId: case renderHeightId:
        if (HIWORD(wParam) == EN_CHANGE) selectResolution(dialog);
        return TRUE;
    case IDOK: {
        if (!checked(dialog, firstMode) && !checked(dialog, firstMode + 1)) return TRUE;
        auto& settings = *reinterpret_cast<RenderSettings*>(GetWindowLongPtrW(dialog, DWLP_USER));
        const bool global = checked(dialog, firstMode + 1);
        const bool caustics = checked(dialog, causticsId);
        double samples = settings.samples;
        double photons = settings.causticPhotons;
        double width, height;
        if (global && !readNumber(dialog, samplesId, 1, maxRenderSamples, samples, true)) return TRUE;
        if (caustics && !readNumber(dialog, photonsId, minCausticPhotons, maxCausticPhotons, photons, true)) return TRUE;
        if (!readNumber(dialog, renderWidthId, minRenderDimension, maxRenderDimension, width, true) ||
            !readNumber(dialog, renderHeightId, minRenderDimension, maxRenderDimension, height, true)) return TRUE;
        settings.samples = int(samples);
        settings.mode = global ? RenderMode::GlobalIllumination : RenderMode::Software;
        settings.caustics = caustics;
        settings.causticPhotons = int(photons);
        settings.width = int(width);
        settings.height = int(height);
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK viewportSettingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        layout(dialog, L"Viewport Settings", 340, 105);
        add(dialog, L"BUTTON", L"Show render frame", BS_AUTOCHECKBOX | WS_TABSTOP, 17, 17, 295, 24, frameId);
        CheckDlgButton(dialog, frameId, *reinterpret_cast<bool*>(lParam) ? BST_CHECKED : BST_UNCHECKED);
        SetFocus(GetDlgItem(dialog, frameId));
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(wParam) == IDOK) {
        *reinterpret_cast<bool*>(GetWindowLongPtrW(dialog, DWLP_USER)) = checked(dialog, frameId);
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

struct GraphicsDraft { bool useVulkan; const wchar_t* activeBackend; };

INT_PTR CALLBACK graphicsSetupProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        const auto& draft = *reinterpret_cast<GraphicsDraft*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        layout(dialog, L"Configure Graphics", 390, 182);
        add(dialog, L"BUTTON", L"Using OpenGL", BS_CHECKBOX | WS_TABSTOP, 17, 15, 320, 24, openGLId);
        add(dialog, L"BUTTON", L"Using Vulkan", BS_CHECKBOX | WS_TABSTOP, 17, 44, 320, 24, vulkanId);
        CheckDlgButton(dialog, draft.useVulkan ? vulkanId : openGLId, BST_CHECKED);
        add(dialog, L"STATIC", draft.activeBackend,
            0, 17, 80, 348, 19);
        add(dialog, L"STATIC", L"Changes take effect after restarting Verda 3D.", 0, 17, 108, 358, 21);
        SetFocus(GetDlgItem(dialog, draft.useVulkan ? vulkanId : openGLId));
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    switch (LOWORD(wParam)) {
    case openGLId: case vulkanId:
        CheckDlgButton(dialog, openGLId, LOWORD(wParam) == openGLId ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, vulkanId, LOWORD(wParam) == vulkanId ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    case IDOK:
        reinterpret_cast<GraphicsDraft*>(GetWindowLongPtrW(dialog, DWLP_USER))->useVulkan = checked(dialog, vulkanId);
        EndDialog(dialog, IDOK);
        return TRUE;
    case IDCANCEL:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

struct LightDraft {
    ObjectKind kind;
    Light light;
};

void number(HWND dialog, const wchar_t* label, int id, int y, double value) {
    wchar_t text[40]{};
    std::swprintf(text, std::size(text), L"%.10g", value);
    add(dialog, L"STATIC", label, 0, 17, y + 4, 210, 22);
    add(dialog, L"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL, 222, y, 119, 25, id, WS_EX_CLIENTEDGE);
}

bool readNumber(HWND dialog, int id, double low, double high, double& value, bool integer) {
    wchar_t input[64]{};
    GetDlgItemTextW(dialog, id, input, std::size(input));
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(input, &end);
    if (end == input || *end || !std::isfinite(parsed) || parsed < low || parsed > high ||
        (integer && std::floor(parsed) != parsed)) {
        wchar_t error[96]{};
        std::swprintf(error, std::size(error), integer ? L"Enter a whole number from %.0f to %.0f."
                                                      : L"Enter a value from %.6g to %.6g.", low, high);
        MessageBoxW(dialog, error, L"Invalid value", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, id));
        return false;
    }
    value = parsed;
    return true;
}

struct NumberDraft {
    const wchar_t* title;
    const wchar_t* label;
    double current, low, high;
    bool integer;
};

INT_PTR CALLBACK numberProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto& draft = *reinterpret_cast<NumberDraft*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        layout(dialog, draft.title, 358, 98);
        number(dialog, draft.label, powerId, 14, draft.current);
        if (draft.integer) {
            const HWND input = GetDlgItem(dialog, powerId);
            SetWindowLongPtrW(input, GWL_STYLE, GetWindowLongPtrW(input, GWL_STYLE) | ES_NUMBER);
        }
        SetFocus(GetDlgItem(dialog, powerId));
        SendDlgItemMessageW(dialog, powerId, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        auto& draft = *reinterpret_cast<NumberDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (readNumber(dialog, powerId, draft.low, draft.high, draft.current, draft.integer)) EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK modifierPickerProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG: {
        layout(dialog, L"Add Modifier", 310, 160);
        const HWND list = add(dialog, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY,
                              16, 16, 278, 78, modifiersId, WS_EX_CLIENTEDGE);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Smooth"));
        SetFocus(list);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (LOWORD(wParam) == IDOK || (LOWORD(wParam) == modifiersId && HIWORD(wParam) == LBN_DBLCLK)) {
            if (SendDlgItemMessageW(dialog, modifiersId, LB_GETCURSEL, 0, 0) != LB_ERR) EndDialog(dialog, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK renameProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        layout(dialog, L"Rename Object", 372, 99);
        auto& name = *reinterpret_cast<std::string*>(lParam);
        const HWND input = add(dialog, L"EDIT", widenUtf8(name).c_str(), WS_TABSTOP | ES_AUTOHSCROLL,
                               16, 15, 339, 25, powerId, WS_EX_CLIENTEDGE);
        SendMessageW(input, EM_SETLIMITTEXT, 128, 0);
        SetFocus(input);
        SendMessageW(input, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        wchar_t text[129]{};
        GetDlgItemTextW(dialog, powerId, text, std::size(text));
        const std::wstring value = text;
        const auto start = value.find_first_not_of(L" \t");
        const auto stop = value.find_last_not_of(L" \t");
        auto name = start == std::wstring::npos ? std::string{} : narrowUtf8(value.substr(start, stop - start + 1));
        if (name.empty() || name.size() > 128) {
            MessageBoxW(dialog, L"Enter a name of up to 128 UTF-8 bytes.", L"Rename Object", MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        *reinterpret_cast<std::string*>(GetWindowLongPtrW(dialog, DWLP_USER)) = std::move(name);
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

struct TransformDraft { ObjectKind kind; Transform transform; };

INT_PTR CALLBACK transformProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        const auto& draft = *reinterpret_cast<TransformDraft*>(lParam);
        const auto& transform = draft.transform;
        const double ry = std::asin(std::clamp(-transform.basis[0].z, -1.0, 1.0));
        const bool regular = std::abs(std::cos(ry)) > 1e-7;
        const double rx = regular ? std::atan2(transform.basis[1].z, transform.basis[2].z) : 0;
        const double rz = regular ? std::atan2(transform.basis[0].y, transform.basis[0].x)
                                  : std::atan2(-transform.basis[1].x, transform.basis[1].y);
        const double values[][3] = {{transform.position.x, transform.position.y, transform.position.z},
            {rx * 180 / pi, ry * 180 / pi, rz * 180 / pi},
            {transform.size[0], transform.size[1], transform.size[2]}};
        const int rows = isGeometry(draft.kind) ? 3 : draft.kind == ObjectKind::Omni ? 1 : 2;
        layout(dialog, L"Object Transform", 413, 93 + rows * 36);
        const wchar_t* headers[] = {L"X", L"Y", L"Z"};
        const wchar_t* labels[] = {L"Position", L"Rotation (deg)", L"Scale"};
        for (int i = 0; i < 3; ++i) add(dialog, L"STATIC", headers[i], 0, 135 + i * 91, 14, 75, 20);
        for (int row = 0; row < rows; ++row) {
            add(dialog, L"STATIC", labels[row], 0, 17, 44 + row * 36, 112, 20);
            for (int column = 0; column < 3; ++column) {
                wchar_t text[48]{};
                std::swprintf(text, std::size(text), L"%.12g", values[row][column]);
                add(dialog, L"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL, 135 + column * 91,
                    40 + row * 36, 81, 25, transformId + row * 3 + column, WS_EX_CLIENTEDGE);
            }
        }
        SetFocus(GetDlgItem(dialog, transformId));
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        auto& draft = *reinterpret_cast<TransformDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
        const int rows = isGeometry(draft.kind) ? 3 : draft.kind == ObjectKind::Omni ? 1 : 2;
        double values[3][3]{};
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < 3; ++column) {
                const double low = row == 0 ? -100000 : row == 1 ? -3600 : .05;
                const double high = row == 0 ? 100000 : row == 1 ? 3600 : 100;
                if (!readNumber(dialog, transformId + row * 3 + column, low, high, values[row][column])) return TRUE;
            }
        }
        auto& transform = draft.transform;
        transform.position = {values[0][0], values[0][1], values[0][2]};
        if (rows > 1) {
            for (int i = 0; i < 3; ++i) {
                Vec3 axis = axes[i];
                for (int j = 0; j < 3; ++j) axis = rotated(axis, axes[j], values[1][j] * pi / 180);
                transform.basis[i] = axis;
            }
        }
        if (rows > 2) transform.size = {values[2][0], values[2][1], values[2][2]};
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void colorLabel(HWND dialog, Color color) {
    wchar_t text[32]{};
    std::swprintf(text, std::size(text), L"Color  #%02X%02X%02X", unsigned(color.r * 255 + .5f),
                  unsigned(color.g * 255 + .5f), unsigned(color.b * 255 + .5f));
    SetDlgItemTextW(dialog, colorId, text);
}

INT_PTR CALLBACK lightSetupProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) SetWindowLongPtrW(dialog, DWLP_USER, lParam);
    auto* draft = reinterpret_cast<LightDraft*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!draft) return FALSE;
    const bool sky = draft->kind == ObjectKind::Sky;
    const bool spot = draft->kind == ObjectKind::Spot;
    Light& light = draft->light;
    if (message == WM_INITDIALOG) {
        const int height = sky ? 155 : spot ? 259 : 224;
        layout(dialog, sky ? L"Sky Light Settings" : spot ? L"Spot Light Settings" : L"Omni Light Settings",
               358, height);
        add(dialog, L"BUTTON", L"Enabled", BS_AUTOCHECKBOX | WS_TABSTOP, 17, 16, 120, 27, enabledId);
        CheckDlgButton(dialog, enabledId, light.enabled ? BST_CHECKED : BST_UNCHECKED);
        add(dialog, L"BUTTON", L"", WS_TABSTOP, 203, 15, 138, 28, colorId);
        colorLabel(dialog, light.color);
        number(dialog, L"Intensity", powerId, 57, light.power);
        if (!sky) {
            number(dialog, L"Range (0 = unlimited)", rangeId, 92, light.range);
            number(dialog, L"Radius (GI soft shadows)", radiusId, 127, light.radius);
        }
        if (spot) number(dialog, L"Cone angle (degrees)", coneId, 162, light.cone);
        SetFocus(GetDlgItem(dialog, powerId));
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    switch (LOWORD(wParam)) {
    case colorId: {
        if (editLightColor(dialog, light.color)) colorLabel(dialog, light.color);
        return TRUE;
    }
    case IDOK:
        if (!readNumber(dialog, powerId, 0, 10000, light.power) ||
            (!sky && (!readNumber(dialog, rangeId, 0, 500, light.range) ||
                      !readNumber(dialog, radiusId, 0, 25, light.radius))) ||
            (spot && !readNumber(dialog, coneId, 1, 160, light.cone))) return TRUE;
        light.enabled = checked(dialog, enabledId);
        EndDialog(dialog, IDOK);
        return TRUE;
    case IDCANCEL: EndDialog(dialog, IDCANCEL); return TRUE;
    }
    return FALSE;
}
} // namespace

bool setupRender(HWND owner, RenderSettings& settings) {
    return runDialog(owner, renderSetupProc, reinterpret_cast<LPARAM>(&settings)) == IDOK;
}

bool editViewportSettings(HWND owner, bool& showRenderFrame) {
    return runDialog(owner, viewportSettingsProc, reinterpret_cast<LPARAM>(&showRenderFrame)) == IDOK;
}

bool editGraphicsConfiguration(HWND owner, bool& useVulkan, const wchar_t* activeBackend) {
    GraphicsDraft draft{useVulkan, activeBackend};
    if (runDialog(owner, graphicsSetupProc, reinterpret_cast<LPARAM>(&draft)) != IDOK) return false;
    useVulkan = draft.useVulkan;
    return true;
}

bool editSceneLight(HWND owner, ObjectKind kind, Light& light) {
    LightDraft draft{kind, light};
    if (runDialog(owner, lightSetupProc, reinterpret_cast<LPARAM>(&draft)) != IDOK) return false;
    light = draft.light;
    return true;
}

bool editLightIntensity(HWND owner, double& intensity) {
    NumberDraft draft{L"Light Intensity", L"Intensity (0 - 10000)", intensity, 0, 10000, false};
    if (runDialog(owner, numberProc, reinterpret_cast<LPARAM>(&draft)) != IDOK) return false;
    intensity = draft.current;
    return true;
}

bool editLightColor(HWND owner, Color& color) {
    static std::array<COLORREF, 16> custom{};
    CHOOSECOLORW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.rgbResult = RGB(int(color.r * 255 + .5f), int(color.g * 255 + .5f), int(color.b * 255 + .5f));
    picker.lpCustColors = custom.data();
    picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&picker)) return false;
    color = {GetRValue(picker.rgbResult) / 255.f, GetGValue(picker.rgbResult) / 255.f,
             GetBValue(picker.rgbResult) / 255.f};
    return true;
}

bool editShadowSize(HWND owner, int& size) {
    NumberDraft draft{L"Custom Shadow Bitmap", L"Pixels per side (16 - 1024)",
                      double(size), minShadowSize, maxShadowSize, true};
    if (runDialog(owner, numberProc, reinterpret_cast<LPARAM>(&draft)) != IDOK) return false;
    size = int(draft.current);
    return true;
}

bool editObjectName(HWND owner, std::string& name) {
    return runDialog(owner, renameProc, reinterpret_cast<LPARAM>(&name)) == IDOK;
}

bool editObjectTransform(HWND owner, ObjectKind kind, Transform& transform) {
    TransformDraft draft{kind, transform};
    if (runDialog(owner, transformProc, reinterpret_cast<LPARAM>(&draft)) != IDOK) return false;
    transform = draft.transform;
    return true;
}

bool editWholeNumber(HWND owner, const wchar_t* title, const wchar_t* label, int& value, int low, int high) {
    NumberDraft draft{title, label, double(value), double(low), double(high), true};
    if (runDialog(owner, numberProc, reinterpret_cast<LPARAM>(&draft)) != IDOK) return false;
    value = int(draft.current);
    return true;
}

bool chooseSmoothModifier(HWND owner) { return runDialog(owner, modifierPickerProc, 0) == IDOK; }
