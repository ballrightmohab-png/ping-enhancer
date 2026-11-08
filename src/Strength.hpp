#pragma once
#include "CameraSmoother.hpp"

namespace aim {
// 10..90: progressively quicker tracking. 100: full large-angle correction, without
// large-turn slowdown or the smoothing reservoir. Target filters/calibration stay
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
    // Ignore sub-pixel-scale feedback noise instead of reversing every tick.
    const float distance=magnitude(error);
    if(distance<.12f) { smoother.reset(); return {}; }
    if(level>=1) {
        smoother.reset();
        // Crossing through a target can flip yaw by ~180 degrees in one tick.
        // Do not apply the gradual-turn ceiling to maximum-strength tracking.
        // Close to alignment, ease correction so tiny gain/pose errors do not
        // bounce the crosshair. Blend continuously to full response at 2 degrees.
        const float t=std::clamp(distance/2.f,0.f,1.f);
        const float blend=t*t*(3.f-2.f*t);
        const float gentle=std::clamp(1.f-std::exp(-14.f*dt),.15f,.75f);
        const float gain=gentle+(1.f-gentle)*blend;
        return {error.pitch*gain,error.yaw*gain};
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
