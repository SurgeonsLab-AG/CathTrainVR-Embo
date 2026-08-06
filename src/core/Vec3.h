#pragma once
#include <cmath>
#include <algorithm>

namespace cn {

/// Minimal 3-vector. Deliberately Qt-free so the geometry core can be unit
/// tested / run head-less (see tools/centerline_cli.cpp) without linking Qt.
struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3 &operator-=(const Vec3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float &operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    float dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3 &o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float lengthSquared() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSquared()); }

    Vec3 normalized() const {
        const float l = length();
        return l > 1e-20f ? (*this) / l : Vec3(0.f, 0.f, 0.f);
    }
};

inline Vec3 operator*(float s, const Vec3 &v) { return v * s; }

inline float distance(const Vec3 &a, const Vec3 &b) { return (a - b).length(); }

inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }

/// Axis-aligned bounding box, used for view framing and the spatial hash.
struct Box3 {
    Vec3 lo{ 1e30f,  1e30f,  1e30f};
    Vec3 hi{-1e30f, -1e30f, -1e30f};

    void extend(const Vec3 &p) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    bool valid() const { return hi.x >= lo.x; }
    Vec3 center() const { return (lo + hi) * 0.5f; }
    Vec3 size() const { return hi - lo; }
    float diagonal() const { return valid() ? (hi - lo).length() : 0.f; }
};

/// Build any unit vector perpendicular to `n`. Used to seed the Frenet-ish
/// frame for catheter tube meshing and tip deflection.
inline Vec3 anyPerpendicular(const Vec3 &n) {
    const Vec3 a = (std::fabs(n.x) < 0.9f) ? Vec3(1.f, 0.f, 0.f) : Vec3(0.f, 1.f, 0.f);
    return n.cross(a).normalized();
}

/// Rotate `v` about unit axis `axis` by `angle` radians (Rodrigues).
inline Vec3 rotateAbout(const Vec3 &v, const Vec3 &axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + axis.cross(v) * s + axis * (axis.dot(v) * (1.f - c));
}

} // namespace cn
