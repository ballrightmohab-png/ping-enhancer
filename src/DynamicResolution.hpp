// LeviFSR AI - adaptive resolution controller.
//
// Pure logic with no graphics dependencies, so it can be unit tested on the
// host without an Android device.
//
// Design goals that matter for gameplay:
//   * a single slow frame must never change the render scale,
//   * the scale moves in steps with a cooldown, so it cannot oscillate,
//   * recovery is slower than reduction (asymmetric), because a scale that
//     grows too fast causes a new stutter,
//   * the user selected bounds are always respected.

#pragma once

#include <cstddef>
#include <deque>

namespace levifsr {

class DynamicResolutionController {
public:
  // Re-initialises the controller. `currentScale` is the scale the renderer is
  // using right now.
  void configure(bool enabled, int targetFps, int minScale, int maxScale,
                 int currentScale);

  // Feeds one measured frame time in milliseconds.
  void submitFrame(double frameTimeMs);

  // Test hook: identical to submitFrame(), kept separate so that a test can
  // document that the value is synthetic.
  void submitSyntheticFrame(double frameTimeMs);

  [[nodiscard]] int currentScale() const { return mCurrentScale; }
  [[nodiscard]] bool enabled() const { return mEnabled; }
  [[nodiscard]] int targetFps() const { return mTargetFps; }
  [[nodiscard]] int minScale() const { return mMinScale; }
  [[nodiscard]] int maxScale() const { return mMaxScale; }

  // 75th percentile of the recent frame times, the value the decision is
  // based on.
  [[nodiscard]] double percentileFrameTime() const { return mPercentile; }
  [[nodiscard]] std::size_t sampleCount() const { return mSamples.size(); }

  // Counters used by the diagnostics and by the tests.
  [[nodiscard]] int adjustments() const { return mAdjustments; }
  [[nodiscard]] int scaleIncreased() const { return mScaleIncreased; }
  [[nodiscard]] int scaleDecreased() const { return mScaleDecreased; }
  [[nodiscard]] bool scaleIncreasedLast() const { return mLastDirection > 0; }
  [[nodiscard]] bool scaleDecreasedLast() const { return mLastDirection < 0; }
  [[nodiscard]] int cooldown() const { return mCooldown; }

private:
  void evaluate();

  bool mEnabled = false;
  int mTargetFps = 60;
  int mMinScale = 50;
  int mMaxScale = 100;
  int mCurrentScale = 100;

  std::deque<double> mSamples;
  double mPercentile = 0.0;

  int mFramesSinceEvaluation = 0;
  int mConsecutiveTooSlow = 0;
  int mConsecutiveFast = 0;
  int mCooldown = 0;
  int mAdjustments = 0;
  int mScaleIncreased = 0;
  int mScaleDecreased = 0;
  int mLastDirection = 0;
};

} // namespace levifsr
