#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace aim {
struct Vec3 { float x, y, z; };
struct Angles { float pitch, yaw; };
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline float wrap(float x) { return std::remainder(x, 360.0f); }
inline std::optional<Angles> towards(Vec3 eye, Vec3 point) {
    if (!finite(eye) || !finite(point)) return {};
    const float x = point.x-eye.x, y = point.y-eye.y, z = point.z-eye.z;
    const float horizontal = std::hypot(x,z);
    if (std::hypot(horizontal,y) < 0.001f) return {};
    constexpr float deg = 180.0f / 3.14159265358979323846f;
    return Angles{-std::atan2(y,horizontal)*deg, std::atan2(-x,z)*deg};
}
inline Angles error(Angles current, Angles target) {
    return {target.pitch-current.pitch, wrap(target.yaw-current.yaw)};
}
inline float magnitude(Angles a) { return std::hypot(a.pitch,a.yaw); }
// Exponential convergence with a vector speed cap; independent of update frequency.
inline Angles step(Angles e, float dt, float response, float speed, bool adaptive) {
    if (!std::isfinite(e.pitch) || !std::isfinite(e.yaw) || !std::isfinite(dt) || dt <= 0) return {};
    dt = std::min(dt, 0.1f); // Never catch up with a large snap after a stall.
    const float distance = magnitude(e);
    if (distance < 0.05f) return {};
    const float gain = adaptive ? std::clamp(distance/6.0f,0.25f,1.0f) : 1.0f;
    const float alpha = 1.0f-std::exp(-std::clamp(response,1.0f,20.0f)*gain*dt);
    const float scale = std::min(alpha,std::clamp(speed,5.0f,120.0f)*dt/distance);
    return {e.pitch*scale,e.yaw*scale};
}
}
