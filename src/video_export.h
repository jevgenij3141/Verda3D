#pragma once

#include "global_renderer.h"

#include <functional>
#include <memory>
#include <mutex>

constexpr int minVideoSize = 64;
constexpr int maxVideoWidth = 3840;
constexpr int maxVideoHeight = 2160;
constexpr int maxVideoFps = 120;

struct VideoSettings {
    int first = 0, last = 0;
    int fps = 24;
    int width = 1280, height = 720;
    int camera = -1;
    std::wstring cameraName;
    std::wstring filename;
    bool replaceExisting = false;

    int frameCount() const { return last - first + 1; }
};

using VideoFrameRenderer = std::function<bool(int frame, RenderJob& output, std::wstring& error)>;

struct VideoJob {
    RenderJob frame;
    std::atomic<int> completed{0};
    std::atomic<bool> done{false};
    std::wstring error;
    bool saved = false;
    std::mutex previewMutex;
    std::shared_ptr<const std::vector<unsigned char>> preview;
};

void exportVideo(const VideoSettings& settings, const VideoFrameRenderer& renderer, VideoJob& job) noexcept;
