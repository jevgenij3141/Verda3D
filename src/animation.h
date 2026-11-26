#pragma once

#include "scene.h"

constexpr int maxTimelineFrames = 100000;
constexpr int maxTimelineFps = 240;

struct TimelineSettings {
    int fps = 24;
    int frames = 120;
    int frame = 0;
};

const TransformKey* findKey(const Model& model, int frame);
void storeKey(Model& model, int frame, KeyInterpolation interpolation = KeyInterpolation::Linear);
void setKeyInterpolation(Model& model, int frame, KeyInterpolation interpolation);
void removeKey(Model& model, int frame);
Transform evaluatePose(const Model& model, int frame);
int playbackFrame(int start, int fps, unsigned long long elapsedMilliseconds, int last);
