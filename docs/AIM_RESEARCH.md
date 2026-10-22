# Making aim assist produce real camera movement: source research

This is an evidence review, not a new verified mod release. The user targets
Android ARM64, Minecraft 1.26.51, LeviLauncher 1.5.27. Research used GitHub source
and repository documentation; no game binary or device was available.

## Findings

### 1. Work in the game's actual look-input path

Android BedrockTools Zoom hooks `LocalPlayerApplyTurnDelta`, modifies the supplied
two-component delta, and forwards it to the original routine. Cinematic Camera
uses the same boundary. LeviLamroid's Android camera lock hooks this routine too.
These support using a native look-input hook rather than inventing a camera-write
offset or relying on an actor-field write to move the view.

- [BedrockTools zoom.cpp, pinned source](https://github.com/QYCottage/BedrockTools/blob/b0ed3e9a0e5cb598dc5c93f22e5f159de80d5d95/src/modules/visual/zoom.cpp)
- [BedrockTools cinematiccamera.cpp](https://github.com/QYCottage/BedrockTools/blob/b0ed3e9a0e5cb598dc5c93f22e5f159de80d5d95/src/modules/visual/cinematiccamera.cpp)
- [LeviLamroid CoreHooks.cpp](https://github.com/spygamingog/LeviLamroid/blob/f988a23c61889fda3ece324db1994077c5b4ed09/src/hooks/CoreHooks.cpp)

Limit: modifying an existing incoming look delta is not equivalent to successfully
creating a new look action by calling the routine from a world tick. These sources
do not establish that our current tick-call architecture is either working or
broken on the user's binary.

### 2. Immediate actor rotation is not a reliable camera-success test

Lamium's Minecraft 1.26.51.01 camera notes describe the active camera entity being
turned first and the orientation copied back to the player later. Its author
explicitly states `_applyTurnDelta` does not synchronously change player rotation.
This is Windows/LeviLamina evidence, NOT an Android runtime verification.

- [Lamium CAMERA.md](https://github.com/amatouhake/Lamium/blob/e168c739cb3d1b736a3044900600926fecf8ba50/docs/CAMERA.md)

Our current `Mod.cpp` reads `player->rotation()` immediately after calling `turn`.
Its "no immediate rotation change" message must not be interpreted as proof that
look input failed. Observe a later frame/tick and rendered camera movement instead.

### 3. Actor angles and camera-input axes must be distinguished

Our code assumes actor rotation is `(pitch, yaw)` and sends `(pitchDelta, yawDelta)`
to the turn routine without verifying the input axis convention on this Android
build. LaminaView's Windows 1.26.51.x documentation labels the incoming Vec2 fields
as yaw first and pitch second (its SDK names the second field `z`). This conflicts
with some unverified aim-assist code found during the search.

- [LaminaView README and platform target](https://github.com/amatouhake/LaminaView/blob/7a4f0f45457c72f66472946abc42e01fe35c0ba8/README.md)

Do not blindly swap Android axes based on Windows code. First capture an ordinary
horizontal camera swipe and an ordinary vertical swipe, with subsequent observed
orientation changes. Confirm order, sign and scale independently. Use explicit
`LookDelta` and `ActorAngles` types after calibration rather than an ambiguous Vec2.

### 4. Another aim-assist PR is not a verified solution

BedrockTools PR #22 targets 26.51 and proposes using the turn-input path. Its
`AIM_ASSIST_DEV.md` explicitly calls it a development source build with unverified
runtime behavior/signatures/camera integration. It cannot substantiate a claim
that an equivalent implementation will work on the user's Android device.

- [PR #22](https://github.com/QYCottage/BedrockTools/pull/22)
- [Development notes at the reviewed revision](https://github.com/wendellestumano/Bedaim/blob/4a184ea5170284cd2ec9f67222c0e55d31009fa2/AIM_ASSIST_DEV.md)

### 5. Do not import other versions' numeric addresses

LeviLamroid contains per-version Android offset tables and explicitly labels its
newer table 1.26.52.3. CameraOverhaul publishes offsets from an inspected ARM64
binary, but warns these are not universal runtime addresses. Neither justifies
hardcoding a matching-looking number into our 1.26.51 build.

- [LeviLamroid version tables](https://github.com/spygamingog/LeviLamroid/blob/f988a23c61889fda3ece324db1994077c5b4ed09/src/mcpe/Offsets.cpp)
- [CameraOverhaul porting evidence](https://github.com/tuffxiomi/CameraOverhaul/blob/5c05d21791a821a8daee5ea0dccb2ff266b92797/PORTING_NOTES.md)

## Implementation/validation order

1. Read the user's current v0.5 status first: hooks not installed, no world ticks,
   UI blocked, no targets, target outside cone, or turn submitted are different
   failure stages. Do not refactor aiming to hide a hook-initialization failure.
2. Instrument the live turn-input function as a pass-through, not another turn
   generator. Preserve the original call exactly once. Count ordinary look calls
   and sample both input components for axis calibration, without moving aim.
3. Confirm chicken discovery and filter counts in a flat private world. Verify the
   actor list actually contains mobs before replacing it with a different query.
4. Once input axes/sign/scale are established, compute a bounded angular correction
   from a fresh eligible target and add it in the natural look-input hook. Target
   selection can remain on ticks; expire stale selections, cancel on menus/world
   exit, and do not retain raw actor pointers across ticks.
5. Initially assist only during an actual camera swipe. Do not promise idle lock-on;
   supporting it requires a separately verified camera-update path, not a fabricated
   input call or a guessed rotation write.
6. Test horizontal and vertical targets separately, then moving chicken tracking,
   head/torso, player/mob switches, zero strength, menus, reload and world changes.
   Disable other camera/zoom mods for the first test to avoid shared-hook effects.
7. Only label the mod working after observing actual rendered camera behavior on
   Android 1.26.51. Mock event-to-turn tests establish software wiring, not this.

## What this research does NOT establish

It does not identify the user's current runtime failure stage, confirm every
signature/layout, establish Android look-axis mapping, prove camera movement, or
provide undetectability. No new binary is presented as a verified fix here.

## Follow-up: user's explicit idle requirement

The user requires tracking with no mouse/touch movement, so the swipe-only
proposal above was **not adopted**. Version 0.6.0 retains automatic tick-driven
requests, adds configurable camera-axis mapping and tests a deferred-camera
closed loop without look events. This does not establish the real native idle
camera path. The actual reproduced voxel traversal bug was fixed independently.
