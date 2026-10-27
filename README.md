# Adaptive Aim 0.9.2 — standalone LeviLauncher mod

**BedrockTools is no longer required, installed, loaded, fetched or queried.**
This is a native Android ARM64 mod for LeviLaunchroid. It uses the launcher's
own preloader interface for its menu, signature scans and hook chains.

## Download / install

Use **AdaptiveAim-arm64** from the latest successful
[Build Adaptive Aim run](https://github.com/ballrightmohab-png/ping-enhancer/actions?query=workflow%3A%22Build+Adaptive+Aim%22+branch%3Aarena%2Fc393d2bc-ping-enhancer).
Extract `AdaptiveAim.levipack` from the artifact ZIP, replace the old Adaptive Aim
package, and restart Minecraft through LeviLauncher. You can disable/uninstall
BedrockTools; this mod does not call it. GitHub sign-in may be required for artifacts.

**Compatibility is version-specific.** The current bundled layout constants and
byte patterns originate from a source snapshot whose manifest declares Minecraft
`1.26.5X.X`. This is not a promise of support for all game versions. Missing
signatures prevent hook installation; matching signatures alone do not guarantee
matching layouts. Actual Android gameplay must still be verified on-device.
No undetectability or ban-safety claim. Use only where assistance is permitted.

## Controls

Open the launcher mod menu → **Auto Target Tracking**:

- Master switch: disabled by default.
- **Target players**: on by default.
- **Target mobs**: off by default. Enable for chickens/other mobs.
- **Aim strength (100 = AIM BOT!)**: integer slider, 10–100, default 50.
  10–90 progressively increases tracking speed/response and reduces smoothing
  lag. 100 selects the strongest bounded lock and bypasses adaptive slowdown
  and smoothing. The launcher still displays a numeric value; the control label
  identifies 100 as **AIM BOT!**. Use the master switch to turn assistance off.
- **Aim at head**: off = torso, on = head (bounding-box fractions, not skeletons).
- **Adaptive slowdown**: slows near the aim point.
- **Aim cone**: 1–180°, default 60, measured in both pitch and yaw.
- **Range**: 1–6 blocks, default 4.
- **Auto-calibrate camera axes**: on by default. Two small isolated probes learn
  axis order, sign and gain from changes in the active view feedback. Keep the camera
  still briefly during calibration. Missing/unstable response pauses calibration.
- **Camera input: yaw first (advanced)**: only used with auto-calibration OFF; off by default, matching frame-gen
  source's pitch-first convention; on selects yaw-first. Android axis order is not yet device-verified. If it
  moves on the wrong axis, turn the master off before changing this option.
- **Frame-gen camera smoothing**: on by default; filters automatic corrections
  using frame-gen's exponential reservoir approach.
- **Camera smoothness**: 0–95, default 50.
- **Base turn speed (below 100 strength)**: 5–90, default 70. The strength
  curve boosts this baseline at higher settings. At 100, the controller requests
  the full angular correction each feedback update instead of applying a gradual
  degrees-per-second limit. A finite calibrated native-input ceiling still applies.

No auto-clicking, reach, packets, ESP or extra gameplay modules. The Mob category
bit distinguishes mobs from items/projectiles. Players follow only the players
switch, even when they also carry the Mob category.
Settings reset on native mod disable/reload; custom disk persistence is not implemented.

## Standalone implementation

`src/game/Runtime.hpp` scans `libminecraftpe.so` using the launcher scanner.
It owns **three hooks**: local-player normal tick, `LocalPlayerApplyTurnDelta`,
and `ClientInstance::isShowingMenu`.
The latter is resolved through RTTI slot **151**, using the same rule shipped in
LeviLauncher **v1.5.27** for Minecraft **1.26.50+**. There is **no container-screen
destructor signature or container/chat lifecycle hook requirement** anymore.

Menu-query return values are preserved. Aim starts paused until the game reports
that no menu is showing; unknown menu state is not treated as gameplay. Hook
installation rolls back on failure and disable removes all three hooks. There is no
BedrockTools event bus, API lookup or dependency.

The monitor retries incomplete scans/installation every two seconds while the
module is enabled at the native lifecycle level. The live HUD distinguishes
missing signatures, absent world ticks, disabled filters, range/cone rejection,
solid obstructions and a commanded turn with no immediate camera change.
Camera changes may be deferred by the game or other mods. Response checks now
observe player rotation on subsequent ticks, not immediately after the turn call.
Neither a submitted command nor an actor rotation change proves rendered-camera
movement.

## Quick chicken check

Enable the master switch and **Target mobs**; set strength to 100 and cone to 60.
Stand 2–3 blocks from a chicken in clear view, aim near it, and close the menu.
If it does not turn, report the live status and exact Minecraft and LeviLauncher
versions. Looking horizontally above a small mob can put it outside the aim cone.
Do not try to compensate for incompatible game layouts with slider settings.

## Build (Android NDK r28c)

Requires CMake 3.22+, Ninja, Git, Python 3 and Android NDK r28c. The launcher
preloader source and its own dependencies are fetched during configuration.
**No BedrockTools checkout or SDK download is involved.**

```sh
cmake -S . -B build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android --target AdaptiveAim --parallel 2
python3 scripts/package.py build/android/libAdaptiveAim.so build/AdaptiveAim.levipack
```

Alternative xmake configuration is provided in `xmake.lua`.
Preloader is pinned to `5d108da350ce3e7f13f51415f81e65344ecd5d14`.

## Tests and limitations

```sh
mkdir -p build
c++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/aim_test.cpp -o build/aim_test
./build/aim_test
python3 -m unittest discover -s tests -p 'test_*.py'
# After CMake fetched the launcher headers:
for test in menu hooks runtime; do
  c++ -std=c++20 -Wall -Wextra -Werror -Itests/stubs \
    -Ibuild/android/_deps/preloader-src/include \
    tests/${test}_test.cpp -ldl -pthread -o build/${test}_test
  ./build/${test}_test
done
```

Host tests fake the launcher scanner/hook backend, not another mod's API. They
exercise late game availability, missing signatures, hook rollback, menu-query return values, unknown-UI-state pausing, cleanup/re-enable and chicken-sized targeting through the installed
tick detour. They also cover independent target toggles, zero strength, range/cone
and obstruction rejection, yaw wrapping and turn caps. CI checks the Android
ELF dynamic table/symbols for an unwanted BedrockTools dependency.

These are **not real-device compatibility tests**. The collision check uses
solid voxels, not exact shapes; glass/partial blocks may differ from visibility.
There is no friend/team/spectator/death filter or target-lock hysteresis. Nearby
crowded targets may switch. Menu pausing depends on the game invoking `isShowingMenu`; actual coverage and
query frequency still need device validation. Other camera mods may
modify the turn behavior. No broad version support or gameplay success claimed.

## Credits

Bundled layout constants and byte patterns were adapted from BedrockTools
revision `b0ed3e9a0e5cb598dc5c93f22e5f159de80d5d95`, but that project is no longer a
dependency. See `THIRD_PARTY_NOTICES.md` and the preserved MIT notice in LICENSE.

## 0.5.0 workaround for the reported missing destructor

The user reported `ContainerScreenControllerDtor` missing on Minecraft 1.26.51,
LeviLauncher 1.5.27. **0.4.0 cannot aim on that installation.** Version 0.5.0
removes all four container/chat lifecycle signatures and uses the launcher's
RTTI-backed menu-state path instead. That removes this particular startup
requirement without just disabling the menu guard.

Host tests check that missing menu-state lookup still prevents installation,
unknown UI state pauses aim, true/false results pause/resume assistance, and a
chicken-sized target reaches the turn routine through the installed tick hook.
These are **not proof of real-game compatibility or camera movement**.

See `COMPATIBILITY.md` for findings and the remaining device checks.
`scripts/audit_game.py` audits the six byte signatures in a user-supplied ARM64
Minecraft `.so` or APK. It does not yet audit the RTTI lookup or function identity.

## 0.6.0 — automatic idle tracking (experimental)

Tracking is driven by game ticks and **never requires a mouse/touch/look event**.
It computes fresh target points every tick and keeps a 2° preference for a
still-valid tracked entity. The retained target identity is only compared against
the fresh actor list, never dereferenced across ticks. Menus, master off, strength
zero and both target switches off stop issuing requests. There is no auto-attack.

This version separates camera-input mapping from actor pitch/yaw storage. The
new default sends yaw first and pitch second; an advanced toggle permits the
previous ordering. Research supports treating those as different boundaries,
but the exact Android mapping still needs device verification. The standalone
native tick-call camera path remains **experimental**, not a proven idle camera
integration for every 1.26.51 binary. It is not secretly switched to swipe-only.

A regression test found and fixed the solid-voxel ray traversal overshooting a
negative-axis endpoint on a block boundary (for example a mob centered at x=-1).
That bug could incorrectly reject a target in unobstructed air.

The expanded host test models camera commands applied on the *next* tick and
checks that both yaw and pitch converge toward a chicken-sized entity, continue
following it when it moves across the view, and stop at master-off, all with
zero look-input events. This validates the controller **under that declared
model**, not the real Minecraft camera ABI. No direct rotation-field write was
added to production; only the test model updates its synthetic rotation.

## 0.7.0 — camera path adapted from your frame-gen repo

Reference: `ballrightmohab-png/frame-gen`, branch `arena/01a0f331-frame-gen`,
revision `a223a14b0fe678b3d992b2248ecb3d31a42fce0a`, `src/CameraSmoother.cpp/.hpp`.
Main did not contain that implementation. Source was adapted at your request;
this does not add a FrameGen binary/runtime dependency.

- Added a turn hook with a captured original callback, following frame-gen's
  detour/original pattern. Physical mouse/touch input passes through unchanged.
- Idle corrections are generated on game ticks, processed by a two-stage
  exponential residual-reservoir smoother, then forwarded through the captured
  original turn callback. There is **no mouse-motion gate** and no recursive
  re-entry through our own hook.
- The controller subtracts already-pending correction before generating another
  step, caps the released turn speed and cancels residuals at menu entry, target
  loss/replacement, config changes, stalls and master-off. Cancellation discards
  residuals rather than flushing an unwanted final turn.
- Pitch-first is the new default, matching frame-gen's `Vec2 {pitch,yaw}` source
  convention. This is not independently verified against your Android binary;
  the advanced axis toggle still exists.
- Did not copy frame interpolation, image warping, FOV/perspective hooks or broad
  fallback byte patterns. Those do not establish actual entity tracking and
  would introduce more unverified entry points.

For the first test, disable FrameGen's camera smoothing and other camera/zoom
mods to avoid overlapping smoothing/hook effects. Enable Auto Target Tracking,
Target mobs, strength 100 and cone 60, stand 2–3 blocks from a chicken in open
space and leave your mouse untouched. Read the live status if it does not move.

Host tests cover smoothing/residual conservation, bounded release, reset/stall
cancellation, unchanged physical input, one original callback per automatic
request, stopped-runtime rejection and deferred idle target following. **These
still do not validate actual rendered camera movement on your device.**

## 0.8.0 — crosshair feedback and camera-response calibration

The user confirmed 0.7.0 moved the camera but did not aim at the entity. This
revision replaces the assumption that body rotation equals the camera view:

- Reads the embedded game `HitResult` ray's origin/direction (`Level + 456`,
  start @0, direction @12). These are version-specific layout assumptions;
  direction validity and proximity to the player are checked. It does not need
  an existing entity hit to form a direction. Version 0.8.0 paused on invalid rays;
  **0.8.1 supersedes that behaviour with a calibrated first-person fallback**.
- Measures the target point from that ray origin, and angular error from its
  direction. Torso/head target points remain inside the entity bounding box.
- Default auto-calibration submits two tiny isolated native-input probes and
  estimates a 2x2 input-to-view response matrix. This handles swapped axes,
  inverted signs and gain differences rather than guessing from source names.
  Physical look invalidates a pending calibration sample; unknown/degenerate
  or absent responses stop probing rather than issuing uncontrolled turns.
- A stagnant view during normal tracking latches a pause; toggle the master off/on
  to retry. Touch/mouse input itself is forwarded unchanged; calibration does not
  require it, and automatic tracking still runs while the user is idle.

**Touch test:** use **first-person, centre-crosshair aiming controls**, not the
classic tap-to-interact cursor. The stored pick ray must correspond to the
centre crosshair when used. This mod does not force a control scheme or prove
that every Android touch mode supplies that same ray; arbitrary tap-cursor
targeting and third-person parallax are not claimed as supported. In 0.8.1,
unusable ray data selects a first-person rotation fallback. If that feedback
also fails to respond, calibration pauses rather than issuing repeated probes.

Host tests now freeze the synthetic body at the WRONG angle while updating a
separate camera/view ray. With swapped, inverted, differently-scaled input axes,
the calibrated controller converges until the view ray intersects a chicken's
bounding box, including after physical look input and in head/torso modes.
Additional tests check eight sign/order combinations, response timeouts, manual
look invalidation, and invalid-ray pausing. **Actual device alignment remains
unverified until the user tests this build.**

## 0.8.1 — missing pick ray no longer blocks all movement

The user reports the 0.8.0 "use first-person crosshair controls" message despite
already using those controls. That message was an incorrect inference from
invalid ray data, NOT a real control-mode check. The strict ray requirement
caused a no-movement regression.

On unusable ray data, 0.8.1 now uses the player's first-person pitch/yaw component
and estimates eye origin from player bounds. Axis/sign/gain calibration still
applies by default; this is not a return to the uncalibrated 0.7 controller.
The selected fallback remains active until a menu/config/master/world reset to
avoid repeatedly changing coordinate sources as pick data becomes available.
Status text identifies `[view ray]` or `[first-person rotation fallback]`.

A new runtime regression converges onto a chicken with an **all-zero pick ray**,
using swapped, inverted, differently-scaled camera input. It also verifies that
stale rotation causes one bounded probe followed by calibration failure, rather
than uncontrolled turning. Android alignment is still device-unverified; if
actor rotation is not camera-aligned on this build, the fallback cannot promise
centre-crosshair alignment. No automatic third-person/control-mode detection is
implemented.

## 0.9.0 — confirmed working baseline, clean HUD and stronger strength control

The user confirmed 0.8.1 works well. This update keeps its target selection,
ray/first-person fallback, calibration and native turn bridge intact.

- Removed the top-left `Aim: ...` draw submission entirely, including startup,
  calibration and error text. The background hook retry monitor still runs.
- Strength is now an integer slider from 10 to 100, labeled
  `Aim strength (100 = AIM BOT!)`. Intermediate values remain possible; the
  legacy launcher slider does not provide custom per-value text or decade ticks.
- 10–90 boosts both turn response and speed progressively and reduces smoothing
  delay. 100 uses the current angular error directly, bounded to 720°/second,
  bypassing adaptive slowdown and reservoir smoothing. It is strongest tracking,
  not an unlimited teleport/snap: native input remains capped by calibration.
- Range, aim cone, visibility, target switches, head/torso choice, pitch bounds,
  idle tracking, calibration and stale-feedback safeguards remain in effect.
  There is still no auto-attack.

Tests verify monotonically increasing decade strengths, bounded maximum lock,
zero-strength/stall handling, no residual smoothing at 100, no HUD text, and the
existing view-ray and missing-ray first-person targeting regressions.

## 0.9.1 — sticky enemy retention at AIM BOT! strength

At 100, the aim cone is used to acquire an enemy. Once acquired, the same enemy
keeps priority even if movement takes it outside that cone or another entity
moves closer to the crosshair. Range, visibility, target-type and dimension
checks still apply; a removed/invalid target is released, not dereferenced later.
Strengths below 100 retain the previous selection behaviour.

The target point and player's eye are recomputed each game tick, including during
physical look input. This is not a promise of zero rendered error: camera latency,
teleports, fast motion, blocked/out-of-range targets and invalid feedback still
limit tracking. No extra HUD text or attack behaviour is added. Regression tests
exercise narrow-cone retention against a competing mob, continuous strafing and
jumping, player translation, physical-look disturbances and target removal.

## 0.9.2 — full correction when passing through an enemy

The user clarified that sticky selection was not the issue: passing through an
enemy causes a near-180-degree reversal, and the camera took too long to catch up.
Two old limits caused that behaviour at maximum strength: the 720-degree/second
angular ceiling, and the default 10-native-input-unit cap after calibration.

At **100 = AIM BOT!**, the controller now requests the complete shortest-angle
correction toward the current torso/head point on each usable feedback update.
It bypasses the gradual angular ceiling and allows a larger finite calibrated
native-input limit (4096 units), so ordinary 180-degree reversals are not reduced
to a series of tiny turns. Below 100, the previous smooth curve and 10-unit native
cap remain unchanged. Calibration, pitch bounds, range/visibility/target filters,
menu pausing, and the guard against piling corrections onto stale feedback stay.

Regression tests require head and torso alignment after one deferred synthetic
camera update while the player crosses through the enemy's X/Z position, with
swapped, inverted and scaled native axes. This is not an Android latency guarantee:
game/render interpolation, internal turn clamps, near-vertical pitch limits and
the tick rate can still cause transient error. No zero-frame/perfect-lock claim.
