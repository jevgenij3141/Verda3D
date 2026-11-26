#pragma once

#include "video_export.h"

#include <thread>

struct VideoCameraChoice {
    int model;
    std::wstring name;
};

struct VideoSources {
    int frames, fps;
    std::vector<VideoCameraChoice> cameras;
};

class VideoPanel {
public:
    using QuerySources = std::function<VideoSources()>;
    using Snapshot = std::function<VideoFrameRenderer(const VideoSettings&)>;

    VideoPanel(HWND owner, QuerySources sources, Snapshot snapshot);
    ~VideoPanel();
    void show();
    bool dispatch(MSG& message);

private:
    static INT_PTR CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK previewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    void createSettings();
    void refreshCameras(bool initial);
    void browse();
    void process();
    void poll();
    void setBusy(bool busy);
    void showPreview();
    void paintPreview() const;

    HWND owner;
    HWND settingsWindow = nullptr;
    HWND previewWindow = nullptr;
    QuerySources querySources;
    Snapshot takeSnapshot;
    VideoSources sources{};
    VideoSettings current;
    std::shared_ptr<VideoJob> job;
    std::thread worker;
    bool busy = false;
    int shownFrames = 0;
    std::shared_ptr<const std::vector<unsigned char>> preview;
};
