#pragma once
#include "CameraSmoother.hpp"

namespace aim {
// 10..90: progressively quicker tracking. 100: strongest bounded lock, without
// adaptive slowdown or the smoothing reservoir. Target filters/calibration stay
// outside this function and are unchanged by strength.
inline Angles strengthCorrection(Angles error,float dt,float strength,float baseSpeed,
                                 bool adaptive,bool smoothing,float smoothness,
                                 CameraSmoother& smoother) {
    if(!std::isfinite(strength) || !std::isfinite(dt) || dt<=0 || dt>.2f ||
       !std::isfinite(error.pitch) || !std::isfinite(error.yaw)) {
        smoother.reset(); return {};
    }
    const float level=std::clamp(strength/100.f,0.f,1.f);
    if(level<=0) { smoother.reset(); return {}; }
    if(level>=1) {
        smoother.reset();
        const float distance=magnitude(error);
        if(distance<.05f) return {};
        const float scale=std::min(1.f,720.f*std::min(dt,.1f)/distance);
        return {error.pitch*scale,error.yaw*scale};
    }
    const auto pending=smoothing?smoother.pending():Angles{};
    const Angles remaining{error.pitch-pending.pitch,error.yaw-pending.yaw};
    const float speed=baseSpeed*(1.f+3.f*level*level);
    auto delta=step(remaining,dt,10.f+10.f*level,speed,adaptive);
    delta.pitch*=level; delta.yaw*=level;
    if(smoothing) delta=smoother.advance(delta,dt,smoothness*(1.f-level),speed*level);
    else smoother.reset();
    return delta;
}
}
