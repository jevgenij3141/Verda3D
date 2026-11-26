#include "video_dialog.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>

namespace {
enum Control {
    cameraId = 101, firstId, lastId, fpsId, widthId, heightId, fileId, browseId,
    processId, progressId, statusId, previewId, cancelId
};
constexpr UINT_PTR pollTimer = 22;
constexpr int settingsWidth = 550, settingsHeight = 326;

HWND addControl(HWND parent, const wchar_t* kind, const wchar_t* name, DWORD style,
                int x, int y, int width, int height, int id = 0, DWORD ex = 0) {
    HWND control = CreateWindowExW(ex, kind, name, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
        parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    return control;
}

void number(HWND parent, int id, int x, int y, int value) {
    addControl(parent, L"EDIT", std::to_wstring(value).c_str(), WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
               x, y, 91, 25, id, WS_EX_CLIENTEDGE);
}

bool readNumber(HWND parent, int id, int lowest, int highest, int& value, const wchar_t* name) {
    wchar_t input[64]{};
    const int length = GetDlgItemTextW(parent, id, input, std::size(input));
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(input, &end, 10);
    if (length && end != input && !*end && parsed >= lowest && parsed <= highest) {
        value = int(parsed);
        return true;
    }
    const std::wstring text = std::wstring(name) + L" must be between " + std::to_wstring(lowest) +
                              L" and " + std::to_wstring(highest) + L".";
    MessageBoxW(parent, text.c_str(), L"Render to Video", MB_OK | MB_ICONWARNING);
    const HWND field = GetDlgItem(parent, id);
    SetFocus(field);
    SendMessageW(field, EM_SETSEL, 0, -1);
    return false;
}

std::wstring readPath(HWND dialog) {
    const HWND field = GetDlgItem(dialog, fileId);
    std::wstring path(GetWindowTextLengthW(field) + 1, L'\0');
    const int length = GetWindowTextW(field, path.data(), static_cast<int>(path.size()));
    path.resize(length);
    const auto first = path.find_first_not_of(L" \t");
    if (first == path.npos) return {};
    path = path.substr(first, path.find_last_not_of(L" \t") - first + 1);
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
    return path;
}
} // namespace

VideoPanel::VideoPanel(HWND ownerWindow, QuerySources sourcesProvider, Snapshot snapshotProvider)
    : owner(ownerWindow), querySources(std::move(sourcesProvider)), takeSnapshot(std::move(snapshotProvider)) {}

VideoPanel::~VideoPanel() {
    if (job) job->frame.cancel.store(true, std::memory_order_relaxed);
    if (worker.joinable()) worker.join();
    if (previewWindow) DestroyWindow(previewWindow);
    if (settingsWindow) DestroyWindow(settingsWindow);
}

void VideoPanel::show() {
    if (!settingsWindow) {
        struct alignas(4) {
            DLGTEMPLATE dialog{};
            WORD menu = 0, windowClass = 0, title = 0;
        } data;
        data.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
        data.dialog.cx = 250;
        data.dialog.cy = 190;
        settingsWindow = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), &data.dialog, owner,
                                                     settingsProc, reinterpret_cast<LPARAM>(this));
        if (!settingsWindow) {
            MessageBoxW(owner, L"Could not open the video settings.", L"Render to Video", MB_OK | MB_ICONERROR);
            return;
        }
    } else if (!busy) refreshCameras(false);
    ShowWindow(settingsWindow, SW_SHOW);
    SetActiveWindow(settingsWindow);
}

bool VideoPanel::dispatch(MSG& message) {
    return settingsWindow && IsWindowVisible(settingsWindow) &&
           (message.hwnd == settingsWindow || IsChild(settingsWindow, message.hwnd)) &&
           IsDialogMessageW(settingsWindow, &message);
}

INT_PTR CALLBACK VideoPanel::settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) SetWindowLongPtrW(window, DWLP_USER, lParam);
    auto* panel = reinterpret_cast<VideoPanel*>(GetWindowLongPtrW(window, DWLP_USER));
    if (!panel) return FALSE;
    switch (message) {
    case WM_INITDIALOG:
        panel->settingsWindow = window;
        panel->createSettings();
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case browseId: panel->browse(); return TRUE;
        case processId: case IDOK: panel->process(); return TRUE;
        case previewId: panel->showPreview(); return TRUE;
        case cancelId:
            if (panel->busy) {
                panel->job->frame.cancel.store(true, std::memory_order_relaxed);
                EnableWindow(GetDlgItem(window, cancelId), FALSE);
            }
            return TRUE;
        case IDCANCEL: ShowWindow(window, SW_HIDE); return TRUE;
        }
        break;
    case WM_TIMER:
        if (wParam == pollTimer) { panel->poll(); return TRUE; }
        break;
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return TRUE;
    case WM_DESTROY:
        panel->settingsWindow = nullptr;
        return TRUE;
    }
    return FALSE;
}

void VideoPanel::createSettings() {
    INITCOMMONCONTROLSEX init{sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&init);
    const HWND dialog = settingsWindow;
    SetWindowTextW(dialog, L"Render to Video");
    RECT frame{0, 0, settingsWidth, settingsHeight};
    AdjustWindowRectEx(&frame, DWORD(GetWindowLongPtrW(dialog, GWL_STYLE)), FALSE,
                       DWORD(GetWindowLongPtrW(dialog, GWL_EXSTYLE)));
    RECT parent{};
    GetWindowRect(owner, &parent);
    const int w = frame.right - frame.left, h = frame.bottom - frame.top;
    SetWindowPos(dialog, nullptr, (parent.left + parent.right - w) / 2,
                 (parent.top + parent.bottom - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    addControl(dialog, L"STATIC", L"Camera", 0, 17, 22, 112, 20);
    addControl(dialog, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
               129, 17, 402, 210, cameraId);
    refreshCameras(true);
    addControl(dialog, L"STATIC", L"Start frame", 0, 17, 63, 109, 20);
    number(dialog, firstId, 129, 58, 0);
    addControl(dialog, L"STATIC", L"End frame", 0, 283, 63, 105, 20);
    number(dialog, lastId, 400, 58, sources.frames - 1);
    addControl(dialog, L"STATIC", L"FPS", 0, 17, 100, 109, 20);
    number(dialog, fpsId, 129, 95, std::min(sources.fps, maxVideoFps));
    addControl(dialog, L"STATIC", L"Width × Height", 0, 17, 137, 109, 20);
    number(dialog, widthId, 129, 132, current.width);
    addControl(dialog, L"STATIC", L"×", 0, 230, 137, 20, 20);
    number(dialog, heightId, 253, 132, current.height);
    addControl(dialog, L"STATIC", L"Output (.mp4)", 0, 17, 180, 112, 20);
    addControl(dialog, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 129, 175, 316, 25, fileId, WS_EX_CLIENTEDGE);
    addControl(dialog, L"BUTTON", L"Browse...", WS_TABSTOP, 455, 175, 76, 25, browseId);
    addControl(dialog, L"BUTTON", L"Process", BS_DEFPUSHBUTTON | WS_TABSTOP, 17, 235, 90, 27, processId);
    addControl(dialog, PROGRESS_CLASSW, L"", PBS_SMOOTH, 121, 239, 410, 19, progressId);
    addControl(dialog, L"STATIC", L"", SS_ENDELLIPSIS | SS_NOPREFIX, 17, 277, 330, 29, statusId);
    addControl(dialog, L"BUTTON", L"Preview", WS_TABSTOP, 358, 273, 82, 26, previewId);
    addControl(dialog, L"BUTTON", L"Cancel job", WS_TABSTOP, 449, 273, 82, 26, cancelId);
    setBusy(false);
}

void VideoPanel::refreshCameras(bool initial) {
    const HWND combo = GetDlgItem(settingsWindow, cameraId);
    const auto old = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    int oldIndex = -1;
    std::wstring oldName;
    if (!initial && old >= 0 && std::size_t(old) < sources.cameras.size()) {
        oldIndex = sources.cameras[old].model;
        oldName = sources.cameras[old].name;
    }
    sources = querySources();
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selection = 0;
    for (std::size_t i = 0; i < sources.cameras.size(); ++i) {
        const auto& camera = sources.cameras[i];
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(camera.name.c_str()));
        if (camera.model == oldIndex && camera.name == oldName) selection = int(i);
    }
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

void VideoPanel::browse() {
    if (busy) return;
    std::array<wchar_t, 32768> filename{};
    OPENFILENAMEW options{};
    options.lStructSize = sizeof(options);
    options.hwndOwner = settingsWindow;
    options.lpstrFile = filename.data();
    options.nMaxFile = DWORD(filename.size());
    options.lpstrFilter = L"MPEG-4 video (*.mp4)\0*.mp4\0";
    options.lpstrDefExt = L"mp4";
    options.lpstrTitle = L"Save Rendered Video";
    options.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&options)) SetDlgItemTextW(settingsWindow, fileId, filename.data());
}

void VideoPanel::process() {
    if (busy) return;
    const HWND dialog = settingsWindow;
    VideoSettings next;
    const int limit = querySources().frames - 1;
    if (!readNumber(dialog, firstId, 0, limit, next.first, L"Start frame") ||
        !readNumber(dialog, lastId, next.first, limit, next.last, L"End frame") ||
        !readNumber(dialog, fpsId, 1, maxVideoFps, next.fps, L"FPS") ||
        !readNumber(dialog, widthId, minVideoSize, maxVideoWidth, next.width, L"Width") ||
        !readNumber(dialog, heightId, minVideoSize, maxVideoHeight, next.height, L"Height")) return;
    if (next.width % 2 || next.height % 2) {
        MessageBoxW(dialog, L"Width and height must both be even.", L"Render to Video", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto camera = SendDlgItemMessageW(dialog, cameraId, CB_GETCURSEL, 0, 0);
    if (camera < 0 || std::size_t(camera) >= sources.cameras.size()) return;
    next.camera = sources.cameras[camera].model;
    next.cameraName = sources.cameras[camera].name;

    const std::wstring requested = readPath(dialog);
    if (requested.empty()) {
        MessageBoxW(dialog, L"Choose an output file first.", L"Render to Video", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, fileId));
        return;
    }
    try {
        auto path = std::filesystem::absolute(requested);
        if (!path.has_extension()) path += L".mp4";
        if (_wcsicmp(path.extension().c_str(), L".mp4") != 0) {
            MessageBoxW(dialog, L"Use the .mp4 extension for the output file.", L"Render to Video", MB_OK | MB_ICONWARNING);
            return;
        }
        next.filename = path.wstring();
    } catch (...) {
        MessageBoxW(dialog, L"The output path is invalid.", L"Render to Video", MB_OK | MB_ICONWARNING);
        return;
    }
    const DWORD attributes = GetFileAttributesW(next.filename.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            MessageBoxW(dialog, L"Choose a file rather than a directory.", L"Render to Video", MB_OK | MB_ICONWARNING);
            return;
        }
        if (MessageBoxW(dialog, L"Replace the existing video after this render finishes?", L"Render to Video",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
        next.replaceExisting = true;
    }
    try {
        auto renderer = takeSnapshot(next);
        if (!renderer) {
            refreshCameras(false);
            MessageBoxW(dialog, L"The camera list changed. Select a camera and try again.", L"Render to Video",
                        MB_OK | MB_ICONWARNING);
            return;
        }
        if (worker.joinable()) worker.join();
        auto task = std::make_shared<VideoJob>();
        if (!SetTimer(dialog, pollTimer, 100, nullptr)) {
            MessageBoxW(dialog, L"Could not start the progress timer.", L"Render to Video", MB_OK | MB_ICONERROR);
            return;
        }
        worker = std::thread([next, task, renderer = std::move(renderer)] { exportVideo(next, renderer, *task); });
        job = std::move(task);
        current = std::move(next);
    } catch (...) {
        KillTimer(dialog, pollTimer);
        MessageBoxW(dialog, L"Could not start the video render.", L"Render to Video", MB_OK | MB_ICONERROR);
        return;
    }
    preview.reset();
    shownFrames = 0;
    busy = true;
    setBusy(true);
    SetDlgItemTextW(dialog, fileId, current.filename.c_str());
    SendDlgItemMessageW(dialog, progressId, PBM_SETRANGE32, 0, current.frameCount() * 100);
    SendDlgItemMessageW(dialog, progressId, PBM_SETPOS, 0, 0);
    SetDlgItemTextW(dialog, statusId, L"Starting video render...");
    showPreview();
}

void VideoPanel::setBusy(bool running) {
    for (int id : {cameraId, firstId, lastId, fpsId, widthId, heightId, fileId, browseId, processId}) {
        EnableWindow(GetDlgItem(settingsWindow, id), !running);
    }
    EnableWindow(GetDlgItem(settingsWindow, cancelId), running);
    EnableWindow(GetDlgItem(settingsWindow, previewId), running || preview != nullptr);
}

void VideoPanel::poll() {
    if (!busy || !job) return;
    const bool done = job->done.load(std::memory_order_acquire);
    int completed = job->completed.load(std::memory_order_acquire);
    if (completed != shownFrames) {
        std::lock_guard<std::mutex> lock(job->previewMutex);
        preview = job->preview;
        shownFrames = completed = job->completed.load(std::memory_order_relaxed);
        if (previewWindow) InvalidateRect(previewWindow, nullptr, FALSE);
    }
    const int count = current.frameCount();
    const bool cancelled = job->frame.cancel.load(std::memory_order_relaxed);
    const int partial = completed == count ? 0 :
        std::clamp(99 * job->frame.rows.load(std::memory_order_relaxed) / current.height, 0, 99);
    SendDlgItemMessageW(settingsWindow, progressId, PBM_SETPOS, completed * 100 + partial, 0);
    if (done) {
        KillTimer(settingsWindow, pollTimer);
        if (worker.joinable()) worker.join();
        busy = false;
        setBusy(false);
        if (cancelled && !job->saved) SetDlgItemTextW(settingsWindow, statusId, L"Video render cancelled.");
        else if (!job->error.empty()) {
            SetDlgItemTextW(settingsWindow, statusId, L"Video rendering failed.");
            MessageBoxW(settingsWindow, job->error.c_str(), L"Render to Video", MB_OK | MB_ICONERROR);
        } else if (job->saved) {
            SendDlgItemMessageW(settingsWindow, progressId, PBM_SETPOS, count * 100, 0);
            const std::wstring status = L"Saved " + std::to_wstring(count) + L" frames to MP4.";
            SetDlgItemTextW(settingsWindow, statusId, status.c_str());
        } else {
            SetDlgItemTextW(settingsWindow, statusId, L"Video rendering did not finish.");
        }
        if (previewWindow) InvalidateRect(previewWindow, nullptr, FALSE);
        return;
    }
    const std::wstring status = cancelled ? L"Cancelling video render..." :
        completed == count ? L"Finalizing MP4..." :
        L"Rendering frame " + std::to_wstring(current.first + completed) + L"  (" +
        std::to_wstring(completed) + L" / " + std::to_wstring(count) + L" encoded)";
    SetDlgItemTextW(settingsWindow, statusId, status.c_str());
}

void VideoPanel::showPreview() {
    if (!busy && !preview) return;
    if (!previewWindow) {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW info{};
        info.style = CS_HREDRAW | CS_VREDRAW;
        info.lpfnWndProc = previewProc;
        info.hInstance = instance;
        info.hCursor = LoadCursor(nullptr, IDC_ARROW);
        info.lpszClassName = L"3D1VideoPreview";
        if (!RegisterClassW(&info) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        previewWindow = CreateWindowExW(0, info.lpszClassName, L"Video Render Preview", WS_OVERLAPPEDWINDOW,
                                        CW_USEDEFAULT, CW_USEDEFAULT, 560, 382, owner, nullptr, instance, this);
        if (!previewWindow) return;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(settingsWindow, MONITOR_DEFAULTTONEAREST), &monitor)) {
            const auto& area = monitor.rcWork;
            RECT settings{};
            GetWindowRect(settingsWindow, &settings);
            const int panelWidth = settings.right - settings.left;
            const int previewWidth = std::min(640, std::max(450, int(area.right - area.left) - panelWidth - 50));
            if (area.right - area.left >= panelWidth + previewWidth + 24) {
                const int left = (area.left + area.right - panelWidth - previewWidth - 12) / 2;
                const int top = std::max(int(area.top) + 12, int(settings.top));
                SetWindowPos(settingsWindow, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                SetWindowPos(previewWindow, nullptr, left + panelWidth + 12, top, previewWidth, 382,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
    ShowWindow(previewWindow, SW_SHOWNOACTIVATE);
    InvalidateRect(previewWindow, nullptr, FALSE);
}

LRESULT CALLBACK VideoPanel::previewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* panel = reinterpret_cast<VideoPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!panel) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_PAINT: panel->paintPreview(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: panel->previewWindow = nullptr; return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(window); return 0; }
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void VideoPanel::paintPreview() const {
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(previewWindow, &paint);
    RECT area{};
    GetClientRect(previewWindow, &area);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    if (preview) {
        const int imageHeight = std::max(1, int(area.bottom) - 25);
        const double scale = std::min(double(area.right) / current.width, double(imageHeight) / current.height);
        const int w = int(current.width * scale), h = int(current.height * scale);
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = current.width;
        bitmap.bmiHeader.biHeight = current.height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        SetStretchBltMode(dc, HALFTONE);
        StretchDIBits(dc, (area.right - w) / 2, (imageHeight - h) / 2, w, h, 0, 0, current.width, current.height,
                      preview->data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
        area.top = imageHeight;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 231, 246));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    const std::wstring text = preview ? L"Frame " + std::to_wstring(current.first + shownFrames - 1) :
                                         busy ? L"Waiting for the first rendered frame..." : L"No frame was rendered.";
    DrawTextW(dc, text.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(previewWindow, &paint);
}
