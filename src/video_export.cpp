#include "video_export.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <filesystem>

namespace {
struct ComRelease { void operator()(IUnknown* object) const { object->Release(); } };
template<class T> using ComPtr = std::unique_ptr<T, ComRelease>;
constexpr LONGLONG ticksPerSecond = 10000000;

std::wstring failure(std::wstring text, HRESULT result) {
    wchar_t detail[24]{};
    std::swprintf(detail, std::size(detail), L" (0x%08lX)", static_cast<unsigned long>(result));
    return text + detail;
}

HRESULT describeVideo(IMFMediaType* media, const VideoSettings& settings, const GUID& format) {
    HRESULT hr = media->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = media->SetGUID(MF_MT_SUBTYPE, format);
    if (SUCCEEDED(hr)) hr = media->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(media, MF_MT_FRAME_SIZE, settings.width, settings.height);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(media, MF_MT_FRAME_RATE, settings.fps, 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(media, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = media->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    if (SUCCEEDED(hr)) hr = media->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    if (SUCCEEDED(hr)) hr = media->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    if (SUCCEEDED(hr)) hr = media->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    return hr;
}

HRESULT createWriter(const wchar_t* path, const VideoSettings& settings,
                     ComPtr<IMFSinkWriter>& writer, DWORD& stream) {
    IMFAttributes* rawAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&rawAttributes, 2);
    const ComPtr<IMFAttributes> attributes(rawAttributes);
    if (SUCCEEDED(hr)) hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    IMFSinkWriter* rawWriter = nullptr;
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(path, nullptr, attributes.get(), &rawWriter);
    writer.reset(rawWriter);

    IMFMediaType* rawOutput = nullptr;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&rawOutput);
    const ComPtr<IMFMediaType> output(rawOutput);
    if (SUCCEEDED(hr)) hr = describeVideo(output.get(), settings, MFVideoFormat_H264);
    const auto bitrate = std::clamp(std::int64_t(settings.width) * settings.height * settings.fps / 6,
                                     std::int64_t(350000), std::int64_t(55000000));
    if (SUCCEEDED(hr)) hr = output->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate));
    if (SUCCEEDED(hr)) hr = writer->AddStream(output.get(), &stream);

    IMFMediaType* rawInput = nullptr;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&rawInput);
    const ComPtr<IMFMediaType> input(rawInput);
    if (SUCCEEDED(hr)) hr = describeVideo(input.get(), settings, MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) hr = input->SetUINT32(MF_MT_DEFAULT_STRIDE, settings.width);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(stream, input.get(), nullptr);
    if (SUCCEEDED(hr)) hr = writer->BeginWriting();
    return hr;
}

void convertNv12(const std::vector<unsigned char>& bgra, int width, int height, BYTE* data) {
    auto* uv = data + std::size_t(width) * height;
    for (int y = 0; y < height; y += 2) {
        const auto* top = bgra.data() + std::size_t(height - 1 - y) * width * 4;
        const auto* bottom = top - std::size_t(width) * 4;
        auto* luma = data + std::size_t(y) * width;
        auto* chroma = uv + std::size_t(y / 2) * width;
        for (int x = 0; x < width; x += 2) {
            int red = 0, green = 0, blue = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const auto* row = dy ? bottom : top;
                for (int dx = 0; dx < 2; ++dx) {
                    const auto* pixel = row + (x + dx) * 4;
                    const int r = pixel[2], g = pixel[1], b = pixel[0];
                    luma[dy * width + x + dx] = BYTE(16 + ((47 * r + 157 * g + 16 * b + 128) >> 8));
                    red += r;
                    green += g;
                    blue += b;
                }
            }
            chroma[x] = BYTE((131072 - 26 * red - 87 * green + 112 * blue + 512) >> 10);
            chroma[x + 1] = BYTE((131072 + 112 * red - 102 * green - 10 * blue + 512) >> 10);
        }
    }
}

HRESULT writeFrame(IMFSinkWriter* writer, DWORD stream, const VideoSettings& settings,
                   int frame, const std::vector<unsigned char>& pixels) {
    const DWORD length = settings.width * settings.height * 3 / 2;
    IMFMediaBuffer* rawBuffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(length, &rawBuffer);
    const ComPtr<IMFMediaBuffer> buffer(rawBuffer);
    BYTE* data = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        convertNv12(pixels, settings.width, settings.height, data);
        hr = buffer->Unlock();
    }
    if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(length);
    IMFSample* rawSample = nullptr;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&rawSample);
    const ComPtr<IMFSample> sample(rawSample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.get());
    const LONGLONG time = LONGLONG(frame) * ticksPerSecond / settings.fps;
    const LONGLONG next = LONGLONG(frame + 1) * ticksPerSecond / settings.fps;
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(time);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(next - time);
    if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.get());
    return hr;
}

void encode(const VideoSettings& settings, const VideoFrameRenderer& renderer,
            VideoJob& job, const wchar_t* temporary) {
    ComPtr<IMFSinkWriter> writer;
    DWORD stream = 0;
    HRESULT hr = createWriter(temporary, settings, writer, stream);
    if (FAILED(hr)) {
        job.error = failure(L"Could not start the Windows H.264 encoder. Try a smaller resolution or FPS.", hr);
        return;
    }

    const std::size_t bytes = std::size_t(settings.width) * settings.height * 4;
    for (int frame = 0; frame < settings.frameCount(); ++frame) {
        if (job.frame.cancel.load(std::memory_order_relaxed)) return;
        job.frame.pixels.clear();
        job.frame.rows.store(0, std::memory_order_relaxed);
        job.frame.failed = false;
        if (!renderer(settings.first + frame, job.frame, job.error)) return;
        if (job.frame.cancel.load(std::memory_order_relaxed)) return;
        if (job.frame.pixels.size() != bytes) {
            job.error = L"The renderer returned an incomplete frame.";
            return;
        }
        hr = writeFrame(writer.get(), stream, settings, frame, job.frame.pixels);
        if (FAILED(hr)) {
            job.error = failure(L"Could not encode frame " + std::to_wstring(settings.first + frame) + L".", hr);
            return;
        }
        auto preview = std::make_shared<const std::vector<unsigned char>>(std::move(job.frame.pixels));
        {
            std::lock_guard<std::mutex> lock(job.previewMutex);
            job.preview = std::move(preview);
            job.completed.store(frame + 1, std::memory_order_release);
        }
    }
    if (job.frame.cancel.load(std::memory_order_relaxed)) return;
    hr = writer->Finalize();
    writer.reset();
    if (FAILED(hr)) {
        job.error = failure(L"Could not finalize the MP4 file.", hr);
        return;
    }
    if (job.frame.cancel.load(std::memory_order_relaxed)) return;
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (settings.replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary, settings.filename.c_str(), flags)) {
        job.error = failure(L"Could not save the finished MP4 file.", HRESULT_FROM_WIN32(GetLastError()));
    } else job.saved = true;
}
} // namespace

void exportVideo(const VideoSettings& settings, const VideoFrameRenderer& renderer, VideoJob& job) noexcept {
    wchar_t temporary[MAX_PATH]{};
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT mf = E_FAIL;
    try {
        const bool valid = settings.first >= 0 && settings.last >= settings.first && settings.last <= 99999 &&
            settings.fps >= 1 && settings.fps <= maxVideoFps &&
            settings.width >= minVideoSize && settings.width <= maxVideoWidth && settings.width % 2 == 0 &&
            settings.height >= minVideoSize && settings.height <= maxVideoHeight && settings.height % 2 == 0;
        if (!valid) job.error = L"Invalid video dimensions, FPS, or frame range.";
        else if (FAILED(com)) job.error = failure(L"Could not start Windows media services.", com);
        else {
            mf = MFStartup(MF_VERSION);
            if (FAILED(mf)) job.error = failure(L"Could not start Windows Media Foundation.", mf);
            else {
                const auto directory = std::filesystem::path(settings.filename).parent_path();
                if (!GetTempFileNameW(directory.c_str(), L"3DV", 0, temporary)) {
                    job.error = failure(L"Could not create a temporary file in the output folder.",
                                        HRESULT_FROM_WIN32(GetLastError()));
                } else encode(settings, renderer, job, temporary);
            }
        }
    } catch (...) {
        job.error = L"Video rendering ran out of resources.";
    }
    if (*temporary) DeleteFileW(temporary);
    if (SUCCEEDED(mf)) MFShutdown();
    if (SUCCEEDED(com)) CoUninitialize();
    job.done.store(true, std::memory_order_release);
}
