#pragma once
#include "Aim.hpp"

namespace aim {
// Adapted from frame-gen CameraSmoother::processTurnDelta (exponential mode),
// revision a223a14b0fe678b3d992b2248ecb3d31a42fce0a. No GL/framegen dependency.
// State belongs to automatic correction only; physical look is never filtered.
class CameraSmoother {
    Angles reservoir{}, previous{};
public:
    void reset() { reservoir={}; previous={}; }
    Angles pending() const { return reservoir; }
    Angles advance(Angles input,float dt,float smoothness,float maxSpeed) {
        if(!std::isfinite(input.pitch) || !std::isfinite(input.yaw) ||
           !std::isfinite(dt) || !std::isfinite(smoothness) || !std::isfinite(maxSpeed) || dt<=0) {
            reset(); return {};
        }
        if(dt>.12f) { reset(); return {}; } // No residual catch-up after a stall.
        dt=std::clamp(dt,.0005f,.05f);
        reservoir.pitch+=input.pitch; reservoir.yaw+=input.yaw;
        const float s=std::clamp(smoothness/100.f,0.f,.95f);
        const float tau=.004f+.065f*std::pow(s,1.35f);
        const float alpha1=std::clamp(1.f-std::exp(-dt/std::max(.001f,tau)),.05f,1.f);
        const float alpha2=std::clamp(1.f-std::exp(-dt/std::max(.001f,tau*.65f)),.10f,1.f);
        Angles result{
            previous.pitch+alpha2*(reservoir.pitch*alpha1-previous.pitch),
            previous.yaw+alpha2*(reservoir.yaw*alpha1-previous.yaw)};
        // Targeting has stronger bounds than raw-input smoothing: never emit an
        // inertial step opposite to, or larger than, the current reservoir.
        auto bounded=[](float value,float remaining) {
            return remaining>=0?std::clamp(value,0.f,remaining):std::clamp(value,remaining,0.f);
        };
        result={bounded(result.pitch,reservoir.pitch),bounded(result.yaw,reservoir.yaw)};
        const float length=magnitude(result);
        const float limit=std::clamp(maxSpeed,0.f,720.f)*dt;
        if(length>limit && length>0) {
            const float scale=limit/length; result.pitch*=scale; result.yaw*=scale;
        }
        reservoir.pitch-=result.pitch; reservoir.yaw-=result.yaw;
        previous=result;
        return result;
    }
};
}
