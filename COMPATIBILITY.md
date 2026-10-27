# Compatibility investigation: Minecraft 1.26.51 / LeviLauncher 1.5.27

## Verified

- LeviLaunchroid release tag `v1.5.27` points to commit
  `a8531393d3a336812748a9bb45582a3d9791af7b`.
- Its `app/src/main/cpp/preloader` gitlink points to
  `5d108da350ce3e7f13f51415f81e65344ecd5d14`, exactly the revision pinned in
  this project's CMakeLists.txt and xmake.lua. No preloader update is needed
  merely to align with launcher 1.5.27.
- The standalone mod's existing CI build passed Android ARM64 compilation,
  launcher-backend host tests, and the check for a BedrockTools binary dependency.
  See [run 39810045427](https://github.com/ballrightmohab-png/ping-enhancer/actions/runs/39810045427).
  That result does **not** establish game compatibility.

Source for the launcher revision:
https://github.com/LiteLDev/LeviLaunchroid/tree/a8531393d3a336812748a9bb45582a3d9791af7b/app/src/main/cpp

## Reported failure in 0.4.0

The user reports `ContainerScreenControllerDtor` does not resolve on their
Minecraft 1.26.51 installation. Current runtime requires that function to track
closing container screens. Therefore this build is **not compatible/working on
that installation**, regardless of successful compilation.

The destructor has not been located in the user's binary. Version 0.5.0 does
not guess its address or weaken its pattern; it stops depending on that function.

## Version 0.5.0 alternate menu-state path

LeviLauncher v1.5.27's own rules resolve `ClientInstance::isShowingMenu` through
RTTI name `14ClientInstance`, slot 151 for Minecraft >= 1.26.50. The mod now uses
the same exported launcher vtable resolver and hooks that query, plus normal tick.
The four container/chat lifecycle byte signatures have been removed. Unknown UI
state pauses assistance until a real query returns false. A failed RTTI lookup
still blocks installation; a failed tick hook rolls back the menu hook.

Source evidence for the slot/type rule:
https://github.com/LiteLDev/LeviLaunchroid/blob/a8531393d3a336812748a9bb45582a3d9791af7b/resources/preloader/preloader_signature_rules_source.json
https://github.com/LiteLDev/preloader-android/blob/5d108da350ce3e7f13f51415f81e65344ecd5d14/src/pl/runtime/GameHooks.cpp

Host tests cover this alternate path and simulated target-to-turn behavior.
**Actual query frequency, menu coverage, native layouts and camera movement on
Minecraft 1.26.51 remain unverified without the binary/device.**

## Required evidence for a real fix

1. Obtain the user's installed ARM64 `libminecraftpe.so`, or the APK/split APK
   containing `lib/arm64-v8a/libminecraftpe.so` (user-supplied copy, not a random
   game download). Do not commit the APK or native binary to Git.
2. Run the audit below. Record its SHA-256, each pattern's match count and the
   candidate ELF virtual addresses. A bare version string does not uniquely
   identify an exact Android binary.
3. Verify the RTTI-resolved menu query and the six remaining function patterns;
   require unique executable matches. Verify function identities/layouts,
   not just byte counts.
4. Rebuild and test on a device with Minecraft 1.26.51 + LeviLauncher 1.5.27:
   start/stop/reload, open/close inventory and chat, aim at a chicken and player,
   check independent switches/strength zero, and observe actual camera movement.
   A test mock cannot substitute for these device checks.

## Offline signature audit

```sh
python3 scripts/audit_game.py /path/to/libminecraftpe.so --output build/game-audit.json
# Or:
python3 scripts/audit_game.py /path/to/installed-arm64.apk --output build/game-audit.json
```

Exit 0 means all bundled patterns have exactly one 4-byte-aligned match within
executable ELF load segments; exit 1 means a missing/ambiguous signature; exit 2
means bad input. The tool extracts only the ARM64 library from an APK, reports its
SHA-256, does not execute game code, and does not download anything. Even exit 0
is **not** proof of correct function identity, layouts or successful gameplay.

## Version 0.6.0 idle tracking status

- Idle requests still originate from game ticks; no physical-look gate is added.
- The camera command mapping defaults to yaw-first and is configurable. This is
  a testable integration hypothesis, not a verified Android 1.26.51 ABI claim.
- The host deferred-camera model now verifies two-axis convergence and follow-up
  tracking of a moving chicken-sized entity with no look events.
- A real controller bug was reproduced and fixed: voxel traversal could overshoot
  a negative-direction grid-aligned endpoint and falsely report an obstruction.
- Immediate actor rotation checks were replaced by later-tick observation. Actual
  rendered camera movement on the user's installation remains unverified.

## Version 0.7.0 frame-gen camera adaptation

The user's frame-gen branch provides a real turn detour and an exponential
residual-reservoir smoother (not merely image interpolation). That structure is
now adapted here, with pitch-first default matching its source. Automatic ticks
supply commands through the captured original callback; physical input is not
filtered. No original-callback call is considered proof of camera movement.
The target/control/signature/device uncertainties described above remain.

## Version 0.8.0 — user reports movement but wrong aim in 0.7.0

The camera-turn entry point now has user-reported movement, but direction/target
alignment was incorrect. The controller now uses the embedded game pick ray
instead of actor rotation, and auto-calibrates axis order/sign/scale from bounded
probe responses. The new HitResult ray-direction offset (12) follows the public
HitResult field declaration, alongside the existing SDK start/wrapper offsets.
It still needs Android 1.26.51 verification. Its use as centre-screen feedback
requires first-person crosshair control mode; touch tap-picking is not assumed
to provide a centre ray. New synthetic tests deliberately separate camera and
body orientation, use swapped/inverted/scaled axes and assert ray/target overlap.
These are controller tests, not a rendered Android crosshair verification.

## Version 0.8.1 — strict pick-ray regression

Device report: 0.8.0 paused with "use first-person crosshair controls" despite
those controls already being active. Invalid ray data was incorrectly treated as
proof of the wrong control mode. The strict dependency is removed: an unavailable
ray selects first-person actor rotation with bounded response calibration.
Status identifies the feedback source. Regression tests cover all-zero pick-ray
tracking and frozen fallback rotation; actual device camera/rotation agreement
remains unverified. This supersedes 0.8.0's invalid-ray hard pause.

## Version 0.9.0 — user-confirmed movement/alignment baseline

The user reports 0.8.1 "works good". Preserve that runtime/layout/calibration
path. Only the HUD draw submission and strength controller/menu are changed:
10–100 integer slider, stronger response curve, 100 labeled AIM BOT! and using
bounded strongest-lock tracking without smoothing/slowdown. No new signatures,
layout constants, target types or gameplay modules are introduced.

## Version 0.9.2 — pass-through turn latency at maximum strength

User clarified the issue is slow camera reversal through an enemy, not switching
enemies. Maximum strength now requests full angular correction and lifts the
calibrated native-input ceiling from 10 to 4096 for that mode only. Lower-strength
turn limits, SDK layouts and hooks are unchanged. New host regressions cross the
enemy in both directions with head/torso targets and require alignment after a
single deferred camera update. Actual Android large-turn response is unverified;
small-probe calibration cannot prove perfectly linear response at every amplitude.
