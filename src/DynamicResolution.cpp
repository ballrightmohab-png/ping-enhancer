#include "DynamicResolution.hpp"

#include "Config.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace levifsr {
namespace {

// Number of frames between two evaluations. Evaluating every frame would make
// the controller react to single frame hitching.
constexpr int kEvaluationInterval = 16;

// Minimum number of samples before a decision is allowed.
constexpr std::size_t kMinSamples = 8;

// Sliding window of the frame times that drive the decision.
constexpr std::size_t kWindowSize = 32;

// Consecutive evaluations that have to agree before the scale moves.
constexpr int kSlowEvaluationsToDrop = 2;
constexpr int kFastEvaluationsToRaise = 3;

// Cooldown in evaluations after a change. Recovery is throttled harder than
// reduction so a temporary performance gain does not immediately raise the
// resolution and cause a new stutter.
constexpr int kCooldownAfterRaise = 3;
constexpr int kCooldownAfterDrop = 1;

} // namespace

void DynamicResolutionController::configure(bool enabled, int targetFps,
                                             int minScale, int maxScale,
                                             int currentScale) {
  mEnabled = enabled;
  mTargetFps = std::clamp(targetFps, limits::kMinFps, limits::kMaxFps);
  mMinScale = std::clamp(minScale, limits::kMinRenderScale,
                         limits::kMaxRenderScale);
  mMaxScale = std::clamp(maxScale, limits::kMinRenderScale,
                         limits::kMaxRenderScale);
  if (mMinScale > mMaxScale) {
    std::swap(mMinScale, mMaxScale);
  }
  mCurrentScale = std::clamp(currentScale, mMinScale, mMaxScale);

  mSamples.clear();
  mPercentile = 0.0;
  mFramesSinceEvaluation = 0;
  mConsecutiveTooSlow = 0;
  mConsecutiveFast = 0;
  mCooldown = 0;
  mLastDirection = 0;
}

void DynamicResolutionController::submitFrame(double frameTimeMs) {
  if (!mEnabled || frameTimeMs <= 0.0) {
    return;
  }

  mSamples.push_back(frameTimeMs);
  if (mSamples.size() > kWindowSize) {
    mSamples.pop_front();
  }

  if (++mFramesSinceEvaluation < kEvaluationInterval ||
      mSamples.size() < kMinSamples) {
    return;
  }
  mFramesSinceEvaluation = 0;
  evaluate();
}

void DynamicResolutionController::submitSyntheticFrame(double frameTimeMs) {
  submitFrame(frameTimeMs);
}

void DynamicResolutionController::evaluate() {
  std::vector<double> sorted(mSamples.begin(), mSamples.end());
  std::sort(sorted.begin(), sorted.end());

  // 75th percentile: a couple of slow frames do not move the decision.
  const std::size_t index = static_cast<std::size_t>(
      static_cast<double>(sorted.size()) * 0.75 + 0.5);
  mPercentile = sorted[std::min(index, sorted.size() - 1)];

  const double targetMs =
      1000.0 / static_cast<double>(std::max(1, mTargetFps));

  // Hysteresis: the "good" band is 0.85x - 1.08x of the target frame time.
  if (mPercentile > targetMs * 1.08) {
    ++mConsecutiveTooSlow;
    mConsecutiveFast = 0;
  } else if (mPercentile < targetMs * 0.85) {
    ++mConsecutiveFast;
    mConsecutiveTooSlow = 0;
  } else {
    mConsecutiveTooSlow = 0;
    mConsecutiveFast = 0;
  }

  if (mCooldown > 0) {
    --mCooldown;
    return;
  }

  int direction = 0;
  if (mConsecutiveTooSlow >= kSlowEvaluationsToDrop) {
    direction = -1;
  } else if (mConsecutiveFast >= kFastEvaluationsToRaise) {
    direction = 1;
  }
  if (direction == 0) {
    return;
  }

  const int next = std::clamp(mCurrentScale + direction * limits::kScaleStep,
                              mMinScale, mMaxScale);
  if (next == mCurrentScale) {
    return;
  }

  mCurrentScale = next;
  mLastDirection = direction;
  ++mAdjustments;
  if (direction > 0) {
    ++mScaleIncreased;
  } else {
    ++mScaleDecreased;
  }

  mConsecutiveTooSlow = 0;
  mConsecutiveFast = 0;
  mCooldown = direction > 0 ? kCooldownAfterRaise : kCooldownAfterDrop;

  // Re-measure from scratch after a change, the old samples belong to the
  // previous resolution.
  mSamples.clear();
}

} // namespace levifsr
