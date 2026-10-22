# Adaptive Aim 0.5.0 — standalone LeviLauncher mod

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

Open the launcher mod menu → **Adaptive Aim**:

- Master switch: disabled by default.
- **Target players**: on by default.
- **Target mobs**: off by default. Enable for chickens/other mobs.
- **Aim strength (%)**: 0–100, default 50. Zero means no assist.
- **Aim at head**: off = torso, on = head (bounding-box fractions, not skeletons).
- **Adaptive slowdown**: slows near the aim point.
- **Aim cone**: 1–60°, default 20, measured in both pitch and yaw.
- **Range**: 1–6 blocks, default 4.
- **Maximum turn**: 5–90°/second, default 70; strength scales the resulting turn.

No auto-clicking, reach, packets, ESP or extra gameplay modules. The Mob category
bit distinguishes mobs from items/projectiles. Players follow only the players
switch, even when they also carry the Mob category.
Settings reset on native mod disable/reload; custom disk persistence is not implemented.

## Standalone implementation

`src/game/Runtime.hpp` scans `libminecraftpe.so` using the launcher scanner.
It owns **two hooks**: local-player normal tick and `ClientInstance::isShowingMenu`.
The latter is resolved through RTTI slot **151**, using the same rule shipped in
LeviLauncher **v1.5.27** for Minecraft **1.26.50+**. There is **no container-screen
destructor signature or container/chat lifecycle hook requirement** anymore.

Menu-query return values are preserved. Aim starts paused until the game reports
that no menu is showing; unknown menu state is not treated as gameplay. Hook
installation rolls back on failure and disable removes both hooks. There is no
BedrockTools event bus, API lookup or dependency.

The monitor retries incomplete scans/installation every two seconds while the
module is enabled at the native lifecycle level. The live HUD distinguishes
missing signatures, absent world ticks, disabled filters, range/cone rejection,
solid obstructions and a commanded turn with no immediate camera change.
Camera changes may be deferred by the game or other mods, so that last message
is a diagnostic, not proof of a broken ABI.

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
