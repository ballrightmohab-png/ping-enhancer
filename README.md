# Adaptive Aim — experimental LeviLauncher companion

Native C++20 aim-assist-only source for LeviLaunchroid (Android ARM64).
**ARM64 package compiled successfully; in-game behavior is not yet tested.**
Requires a matching **BedrockTools** runtime.

## Download the built mod

[Download AdaptiveAim-arm64.zip](https://github.com/ballrightmohab-png/ping-enhancer/actions/runs/39804512546/artifacts/12436797466)
(GitHub sign-in may be required; Actions artifacts expire.) Extract
`AdaptiveAim.levipack` from the ZIP and import it into LeviLauncher. This is an
experimental build, not a promise of compatibility or ban safety.
No undetectability, anti-cheat bypass, or ban-safety claims. Use only where the
world/server owner permits assistance.

## Features

- One module in LeviLauncher's native mod menu, using the same `ModuleBuilder`
  and `ConfigType::Toggle` API used by BedrockTools.
- Disabled by default; head toggle off means torso, on means head.
- **Aim strength (%)** slider from 0–100 (default 50). Zero applies no assist;
  higher values scale the smoothed turn up to the maximum-turn cap.
- Adaptive exponential slowdown near the target; bounded turn speed, narrow
  configurable cone and short configurable range.
- Independent **Target players** (default on) and **Target mobs** (default off)
  switches; invisible targets excluded; no auto-click, reach,
  packet manipulation, ESP, or extra gameplay modules in this library.
- Solid-voxel obstruction check; pauses for chat/container screens reported by
  BedrockTools. No actor pointers kept between ticks.

## Build

Requirements: Android NDK r28c, xmake, Python 3, Git; an Android ARM64 device for
validation. The build requires network access for upstream dependencies.

```sh
sh scripts/fetch-sdk.sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c
xmake -y
# Substitute the actual path printed by xmake:
python3 scripts/package.py build/android/arm64-v8a/release/libAdaptiveAim.so build/AdaptiveAim.levipack
```

Enable BedrockTools first, then import/enable AdaptiveAim.
Open LeviLauncher's mod menu → **Adaptive Aim** to see **Target players**,
**Target mobs**, and **Aim strength (%)**, plus the head/adaptive controls.
The menu now registers even when the runtime is missing or not ready; actual
assistance still fails closed. After enabling matching BedrockTools, switch the
Adaptive Aim menu toggle off and back on to retry attaching the runtime.
No dependency ordering is assumed or silently bypassed.
Settings currently reset when the module is disabled/reloaded; custom on-disk
persistence is not implemented.

## Compatibility / limitations

- SDK pinned to BedrockTools `b0ed3e9a0e5cb598dc5c93f22e5f159de80d5d95`
  (its manifest declares Minecraft `1.26.5X.X`). Preloader build pinned to
  `5d108da350ce3e7f13f51415f81e65344ecd5d14`.
- Matching API ABI alone does **not** guarantee matching game layouts. Do not
  use with another BedrockTools/game build without porting and device testing.
- Direct calls to upstream version-specific actor/turn signatures need actual
  device verification. GitHub Actions successfully compiled and linked this
  module with NDK r28c and packaged it as a LeviPack. Minecraft runtime/gameplay
  validation remains unverified. Successful build: Actions run `39804512546`.
- Obstruction checks use solid-block voxels, not exact collision shapes. Glass,
  fluids, partial blocks, and entities may differ from visual line of sight.
- Mobs use the same category bit (2) as upstream BedrockTools Hitbox, not simply
  "anything that is not a player"; players obey only the players switch.
- Head/torso points are bounding-box fractions, not skeletal hitboxes. No team,
  friend, spectator/death filtering or target-lock hysteresis yet.
- The narrowest angular candidate is selected every tick; crowded targets may
  cause switching. Other camera mods can change the applied turn behavior.
- Only chat and container events gate aim; pause/menu coverage requires further
  device testing. Assistance runs while enabled, not only while attacking.
- BedrockTools itself includes other modules: disable those in its own menu if
  you want only aim assistance active. This is a companion, not a standalone SDK fork.

## Host tests

```sh
mkdir -p build
c++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/aim_test.cpp -o build/aim_test
./build/aim_test
python3 -m unittest discover -s tests -p 'test_*.py'
```

Math/settings tests cover yaw wrap, headward pitch, invalid coordinates, speed caps,
zero error, update-frequency equivalence, independent target switches and slider
validation. The CI host menu test uses real upstream menu headers and a stub
Android lifecycle to check controls remain registered without BedrockTools,
including toggling and reload. These do not validate Minecraft
ABI calls or in-game camera behavior.

## Upstream references and license

Studied [QYCottage/BedrockTools](https://github.com/QYCottage/BedrockTools),
particularly `src/launcher/ModuleMenu.cpp`, `include/bedrocktools/Api.hpp`, SDK
wrappers, and its packaging script; and
[LiteLDev/preloader-android](https://github.com/LiteLDev/preloader-android).
SDK headers are fetched separately and not vendored. The pinned BedrockTools LICENSE file is MIT (its README says GPL-3.0,
which is inconsistent). The included LICENSE preserves its MIT notice.
Review upstream licensing before distributing modified SDK code.

## Version 0.2.0

Fixes missing menu controls when the runtime is unavailable at module enable.
Adds independent player/mob switches and a 0–100% strength slider. Controls
register before dependency attachment; switching on retries attachment.
The prior 0.1.0 download above does not contain these fixes; use the newest
successful **Build Adaptive Aim** workflow artifact for 0.2.0.
