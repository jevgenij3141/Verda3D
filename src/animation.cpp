#include "animation.h"

namespace {
struct Quaternion {
    double w, x, y, z;
    Quaternion operator+(Quaternion q) const { return {w + q.w, x + q.x, y + q.y, z + q.z}; }
    Quaternion operator*(double s) const { return {w * s, x * s, y * s, z * s}; }
};

double dot(Quaternion a, Quaternion b) {
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

Quaternion normalize(Quaternion q) { return q * (1 / std::sqrt(dot(q, q))); }

Quaternion toQuaternion(const std::array<Vec3, 3>& m) {
    const double trace = m[0].x + m[1].y + m[2].z;
    if (trace > 0) {
        const double s = 2 * std::sqrt(trace + 1);
        return normalize({s * .25, (m[1].z - m[2].y) / s, (m[2].x - m[0].z) / s, (m[0].y - m[1].x) / s});
    }
    if (m[0].x >= m[1].y && m[0].x >= m[2].z) {
        const double s = 2 * std::sqrt(1 + m[0].x - m[1].y - m[2].z);
        return normalize({(m[1].z - m[2].y) / s, s * .25, (m[1].x + m[0].y) / s, (m[2].x + m[0].z) / s});
    }
    if (m[1].y >= m[2].z) {
        const double s = 2 * std::sqrt(1 + m[1].y - m[0].x - m[2].z);
        return normalize({(m[2].x - m[0].z) / s, (m[1].x + m[0].y) / s, s * .25, (m[2].y + m[1].z) / s});
    }
    const double s = 2 * std::sqrt(1 + m[2].z - m[0].x - m[1].y);
    return normalize({(m[0].y - m[1].x) / s, (m[2].x + m[0].z) / s, (m[2].y + m[1].z) / s, s * .25});
}

Quaternion interpolate(Quaternion a, Quaternion b, double weight) {
    double alignment = dot(a, b);
    if (alignment < 0) { b = b * -1; alignment = -alignment; }
    if (alignment > .9995) return normalize(a * (1 - weight) + b * weight);
    const double angle = std::acos(std::clamp(alignment, -1.0, 1.0));
    return (a * std::sin((1 - weight) * angle) + b * std::sin(weight * angle)) * (1 / std::sin(angle));
}

Vec3 rotate(Vec3 direction, Quaternion orientation) {
    const Vec3 imaginary{orientation.x, orientation.y, orientation.z};
    const Vec3 skew = cross(imaginary, direction) * 2;
    return direction + skew * orientation.w + cross(imaginary, skew);
}

template<class Keys>
auto keyPosition(Keys& keys, int frame) {
    return std::lower_bound(keys.begin(), keys.end(), frame,
        [](const TransformKey& key, int value) { return key.frame < value; });
}
} // namespace

const TransformKey* findKey(const Model& model, int frame) {
    const auto key = keyPosition(model.keys, frame);
    return key != model.keys.end() && key->frame == frame ? &*key : nullptr;
}

void storeKey(Model& model, int frame, KeyInterpolation interpolation) {
    const auto key = keyPosition(model.keys, frame);
    const TransformKey value{frame, model.transform, interpolation};
    if (key != model.keys.end() && key->frame == frame) *key = value;
    else model.keys.insert(key, value);
}

void setKeyInterpolation(Model& model, int frame, KeyInterpolation interpolation) {
    const auto key = keyPosition(model.keys, frame);
    if (key != model.keys.end() && key->frame == frame) key->interpolation = interpolation;
}

void removeKey(Model& model, int frame) {
    const auto key = keyPosition(model.keys, frame);
    if (key != model.keys.end() && key->frame == frame) model.keys.erase(key);
}

Transform evaluatePose(const Model& model, int frame) {
    if (model.keys.empty()) return model.transform;
    const auto next = keyPosition(model.keys, frame);
    if (next == model.keys.end()) return model.keys.back().pose;
    if (next == model.keys.begin() || next->frame == frame) return next->pose;
    const auto& previous = *(next - 1);
    const double t = double(frame - previous.frame) / (next->frame - previous.frame);
    // A smooth key sets the speed to zero at its own endpoint.
    double weight = t;
    if (previous.interpolation == KeyInterpolation::Smooth) weight -= t * (1 - t) * (1 - t);
    if (next->interpolation == KeyInterpolation::Smooth) weight += t * t * (1 - t);
    const Transform& a = previous.pose;
    const Transform& b = next->pose;
    const Quaternion orientation = interpolate(toQuaternion(a.basis), toQuaternion(b.basis), weight);
    Transform pose;
    pose.position = a.position + (b.position - a.position) * weight;
    for (int i = 0; i < 3; ++i) {
        pose.size[i] = a.size[i] + (b.size[i] - a.size[i]) * weight;
        pose.basis[i] = rotate(axes[i], orientation);
    }
    return pose;
}

int playbackFrame(int start, int fps, unsigned long long elapsedMilliseconds, int last) {
    const auto remaining = static_cast<unsigned long long>(last - start);
    if (elapsedMilliseconds > remaining * 1000 / fps + 1) return last;
    return start + static_cast<int>(std::min(remaining, elapsedMilliseconds * fps / 1000));
}
