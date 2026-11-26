#pragma once

#include <cmath>

struct Vec2 {
    double x, y;
    Vec2 operator+(Vec2 v) const { return {x + v.x, y + v.y}; }
    Vec2 operator-(Vec2 v) const { return {x - v.x, y - v.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
};

struct Vec3 {
    double x, y, z;
    Vec3 operator+(Vec3 v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(Vec3 v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalized(Vec3 v) {
    const double length = std::sqrt(dot(v, v));
    return length > 1e-10 ? v * (1 / length) : Vec3{};
}
inline Vec3 rotated(Vec3 v, Vec3 axis, double radians) {
    const double sine = std::sin(radians), cosine = std::cos(radians);
    return v * cosine + cross(axis, v) * sine + axis * (dot(axis, v) * (1 - cosine));
}
