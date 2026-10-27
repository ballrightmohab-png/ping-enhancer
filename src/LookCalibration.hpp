#pragma once
#include "Aim.hpp"

namespace aim {
// Learn a 2x2 input->view-angle response instead of guessing axis order/sign/scale.
// Uses tiny isolated automatic probes; manual look invalidates a pending sample.
class LookCalibration {
    enum class Phase { First, WaitFirst, Second, WaitSecond, Ready, Failed } phase=Phase::First;
    Angles baseline{}, column0{}, column1{};
    double sentAt=0, lastManual=-1e20;
    static constexpr float probe=.2f;
public:
    void reset() { *this=LookCalibration{}; }
    bool ready() const { return phase==Phase::Ready; }
    bool failed() const { return phase==Phase::Failed; }
    void cancelPending() { if(!ready() && !failed()) phase=Phase::First; }
    std::optional<LookDelta> update(Angles view,double now,bool manualLook) {
        if(!std::isfinite(view.pitch) || !std::isfinite(view.yaw) || !std::isfinite(now)) {
            phase=Phase::Failed; return {};
        }
        if(ready() || failed()) return {};
        if(manualLook) { phase=Phase::First; lastManual=now; return {}; }
        if(now-lastManual<.25) return {};
        if(phase==Phase::First || phase==Phase::Second) {
            baseline=view; sentAt=now;
            if(phase==Phase::First) { phase=Phase::WaitFirst; return LookDelta{probe,0}; }
            phase=Phase::WaitSecond; return LookDelta{0,probe};
        }
        const auto change=error(baseline,view);
        if(magnitude(change)>12.f || now-sentAt>.8) { phase=Phase::Failed; return {}; }
        // Wait for deferred camera updates to settle before estimating gain.
        if(now-sentAt<.2 || magnitude(change)<.01f) return {};
        Angles column{change.pitch/probe,change.yaw/probe};
        const float gain=magnitude(column);
        if(gain<.025f || gain>50.f) { phase=Phase::Failed; return {}; }
        if(phase==Phase::WaitFirst) { column0=column; phase=Phase::Second; return {}; }
        column1=column;
        const float det=column0.pitch*column1.yaw-column1.pitch*column0.yaw;
        const float norms=magnitude(column0)*magnitude(column1);
        // Reject nearly parallel/degenerate axes: never invert a noisy matrix.
        phase=std::abs(det)>.5f*norms?Phase::Ready:Phase::Failed;
        return {};
    }
    std::optional<LookDelta> encode(Angles desired,float nativeLimit=10.f) const {
        if(!ready() || !std::isfinite(desired.pitch) || !std::isfinite(desired.yaw) ||
           !std::isfinite(nativeLimit) || nativeLimit<=0 || nativeLimit>4096.f) return {};
        const float det=column0.pitch*column1.yaw-column1.pitch*column0.yaw;
        LookDelta input{(column1.yaw*desired.pitch-column1.pitch*desired.yaw)/det,
            (-column0.yaw*desired.pitch+column0.pitch*desired.yaw)/det};
        if(!std::isfinite(input.first) || !std::isfinite(input.second)) return {};
        // Smooth assistance keeps the original 10-unit cap. Maximum tracking
        // can request a full calibrated reversal, still with a finite ceiling.
        const float size=std::hypot(input.first,input.second);
        if(!std::isfinite(size)) return {};
        if(size>nativeLimit) { input.first*=nativeLimit/size; input.second*=nativeLimit/size; }
        return input;
    }
};
}
