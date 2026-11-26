#include "image_output.h"

#include <commdlg.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>

namespace {
struct ReleaseCom { void operator()(IUnknown* value) const { value->Release(); } };
template<class T> using ComPtr = std::unique_ptr<T, ReleaseCom>;

bool encode(const wchar_t* filename, int width, int height, const std::vector<unsigned char>& bottomUp) {
    IWICImagingFactory* rawFactory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(&rawFactory)))) return false;
    const ComPtr<IWICImagingFactory> factory(rawFactory);
    IWICStream* rawStream = nullptr;
    if (FAILED(factory->CreateStream(&rawStream))) return false;
    const ComPtr<IWICStream> stream(rawStream);
    if (FAILED(stream->InitializeFromFilename(filename, GENERIC_WRITE))) return false;
    IWICBitmapEncoder* rawEncoder = nullptr;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &rawEncoder))) return false;
    const ComPtr<IWICBitmapEncoder> encoder(rawEncoder);
    if (FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) return false;
    IWICBitmapFrameEncode* rawFrame = nullptr;
    if (FAILED(encoder->CreateNewFrame(&rawFrame, nullptr))) return false;
    const ComPtr<IWICBitmapFrameEncode> frame(rawFrame);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width, height)) ||
        FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return false;

    const UINT stride = width * 4;
    std::vector<unsigned char> row(stride);
    for (int y = height - 1; y >= 0; --y) {
        const auto source = bottomUp.begin() + std::size_t(y) * stride;
        std::copy_n(source, stride, row.begin());
        for (UINT alpha = 3; alpha < stride; alpha += 4) row[alpha] = 255;
        if (FAILED(frame->WritePixels(1, stride, stride, row.data()))) return false;
    }
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
} // namespace

bool saveRenderPng(HWND owner, int width, int height, std::vector<unsigned char> bottomUp) {
    if (width <= 0 || height <= 0 || bottomUp.size() != std::size_t(width) * height * 4) return false;
    std::array<wchar_t, 32768> target{};
    OPENFILENAMEW picker{};
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = owner;
    picker.lpstrFilter = L"PNG image (*.png)\0*.png\0";
    picker.lpstrDefExt = L"png";
    picker.lpstrFile = target.data();
    picker.nMaxFile = static_cast<DWORD>(target.size());
    picker.lpstrTitle = L"Save Render";
    picker.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&picker)) return true;

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return false;
    const std::filesystem::path path(target.data());
    wchar_t temporary[MAX_PATH]{};
    bool saved = false;
    if (GetTempFileNameW(path.parent_path().c_str(), L"PNG", 0, temporary)) {
        try {
            saved = encode(temporary, width, height, bottomUp) &&
                    MoveFileExW(temporary, target.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        } catch (...) {
            saved = false;
        }
        if (!saved) DeleteFileW(temporary);
    }
    CoUninitialize();
    return saved;
}
