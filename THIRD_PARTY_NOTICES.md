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
