# Source attribution

The version-specific Minecraft byte patterns and layout constants in
`src/game/Signatures.hpp` and `src/game/Game.hpp` are adapted from
[QYCottage/BedrockTools](https://github.com/QYCottage/BedrockTools), revision
`b0ed3e9a0e5cb598dc5c93f22e5f159de80d5d95`.
The MIT notice in LICENSE is preserved from that revision (copyright RadiantByte).
Its README says GPL-3.0, inconsistent with its LICENSE file at that revision.

BedrockTools is **not** a build or runtime dependency. No BedrockTools .so is
loaded, no exported API is queried, and no BedrockTools events are subscribed.

LeviLauncher's preloader API is the host plugin interface, built from
LiteLDev/preloader-android revision `5d108da350ce3e7f13f51415f81e65344ecd5d14`.
It provides the native mod lifecycle, menu UI, signature scanner and hook chain.
See that project's LICENSE for its terms. It is supplied by the launcher at runtime.

## User-requested frame-gen adaptation

The automatic-correction smoother in `src/CameraSmoother.hpp` and the original
turn-callback bridge were adapted at the user's explicit request from their
repository `ballrightmohab-png/frame-gen`, branch `arena/01a0f331-frame-gen`,
commit `a223a14b0fe678b3d992b2248ecb3d31a42fce0a`, specifically
`src/CameraSmoother.cpp` / `.hpp`. The reference branch had no top-level license
file; no new license grant for that repository is asserted here. No frame-gen
library or renderer assets are included or required.

The HitResult ray-direction field ordering was cross-checked against the public
LeviLamina HitResult declaration (not a new runtime dependency):
https://github.com/LiteLDev/LeviLamina/blob/4ca13569fc0d1cdeb873cf6e7c48ddcc7a74e59c/src/mc/world/phys/HitResult.h
No generated LeviLamina implementation is vendored here; Android layout validation
is still required.
