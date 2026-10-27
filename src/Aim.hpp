#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace aim {
struct Vec3 { float x, y, z; };
struct Angles { float pitch, yaw; };
// Camera input is a different ABI boundary from actor pitch/yaw storage.
struct LookDelta { float first, second; };
inline LookDelta toLookDelta(Angles delta, bool yawFirst) {
    return yawFirst ? LookDelta{delta.yaw,delta.pitch} : LookDelta{delta.pitch,delta.yaw};
}
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// Segment traversal with a finite end parameter: do not overshoot negative-axis
// endpoints that sit exactly on a voxel boundary.
template<class IsSolid>
bool clearVoxels(Vec3 a,Vec3 b,IsSolid solid) {
    if(!finite(a) || !finite(b)) return false;
    for(float coordinate:{a.x,a.y,a.z,b.x,b.y,b.z})
        if(std::abs(coordinate)>32000000.f) return false;
    int cell[3]={(int)std::floor(a.x),(int)std::floor(a.y),(int)std::floor(a.z)};
    int end[3]={(int)std::floor(b.x),(int)std::floor(b.y),(int)std::floor(b.z)};
    float origin[3]={a.x,a.y,a.z}, d[3]={b.x-a.x,b.y-a.y,b.z-a.z};
    int sign[3]; float next[3],stride[3];
    for(int i=0;i<3;++i) {
        sign[i]=d[i]>0?1:-1;
        stride[i]=d[i]==0?INFINITY:std::abs(1/d[i]);
        next[i]=d[i]==0?INFINITY:((cell[i]+(sign[i]>0?1:0))-origin[i])/d[i];
    }
    for(int n=0;n<128;++n) {
        if(solid(cell[0],cell[1],cell[2])) return false;
        if(cell[0]==end[0] && cell[1]==end[1] && cell[2]==end[2]) return true;
        int axis=next[0]<next[1]?0:1;
        if(next[2]<next[axis]) axis=2;
        if(next[axis]>=1.f) return !solid(end[0],end[1],end[2]);
        cell[axis]+=sign[axis]; next[axis]+=stride[axis];
    }
    return false;
}
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
    const float scale = std::min(alpha,std::clamp(speed,5.0f,720.0f)*dt/distance);
    return {e.pitch*scale,e.yaw*scale};
}
}
