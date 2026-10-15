# LeviFSR AI

**AI-Powered Upscaling & Low-Latency Rendering** — a native rendering module
for **LeviLaunchroid** (Minecraft Bedrock Edition on Android), targeting
**arm64-v8a** devices with **ARM Mali** and **Qualcomm Adreno** GPUs.

It renders the 3D scene at a reduced internal resolution, reconstructs the
image with an edge-adaptive spatial upscaler, and adapts the render scale at
runtime to hold a chosen frame rate — while keeping the interface at native
resolution and adding as little latency as possible.

---

## Status: what is real and what is not

This module is honest about its own limits. Read this before installing.

| Area | Status |
| --- | --- |
| Mod Menu integration, settings, persistence | **Implemented and verified** |
| Edge-adaptive spatial reconstruction + CAS | **Implemented**, shaders validated with glslang |
| Reduced internal render resolution | **Implemented**, detected at runtime with automatic revert |
| Adaptive dynamic resolution | **Implemented**, 59 host unit tests |
| GPU vendor detection (Mali / Adreno / …) | **Implemented** |
| Diagnostic overlay | **Implemented** |
| Low-latency frame path | **Implemented** (no copies to CPU, no blocking queries, no per-frame allocation) |
| Temporal upscaling | **Not implemented** — no motion vectors available |
| AI / neural upscaling | **Not active** — probe only, see below |

### About the name and "AI"

The name is a product name. **This is not AMD FSR, FSR 2, FSR 3 or FSR 4**, and
it does not use any AMD code, shader, or SDK. The reconstruction filter is an
independent implementation inspired by the *published ideas* behind spatial
upscalers: directional edge-adaptive sampling plus contrast-adaptive
sharpening with local min/max clamping.

**The neural backend ships as a probe, not as a working model.** The module
does not contain pretrained weights, does not download anything, and does not
pretend a shader is machine learning. At load time it looks for an Android
Neural Networks runtime and for a `.tflite` model **that you supply** in
`<mod dir>/models/`. Until you supply one, the status is reported as
`MissingModel` / `Unavailable` and the GPU spatial upscaler handles all
rendering. Nothing in the UI ever reports the AI backend as active unless a
model was really parsed and an inference really ran.

### Why temporal upscaling is not implemented

A temporal upscaler needs, per frame: motion vectors, the previous frame's
colour and depth, camera jitter, and a history buffer with disocclusion
rejection. None of these is exposed by any API available to a LeviLaunchroid
native module, and none can be reconstructed reliably. Fabricating motion
vectors would produce ghosting, which is explicitly worse than a good spatial
result for competitive play. The module therefore ships the best valid spatial
path and reports temporal upscaling as unsupported.

---

## How the rendering pipeline works

The module works at two levels and degrades between them automatically.

```
      ┌─────────────────────────────────────────────────────────┐
      │  Minecraft renders the 3D scene                          │
      │      into an offscreen framebuffer (FBO)                 │
      │                                                          │
      │   ┌──────────────────────────────────────────┐          │
      │   │ SceneTargetScaler                        │          │
      │   │  detects the FBO at runtime and          │          │
      │   │  reallocates it at renderScale %         │  Level 2 │
      │   │  (verifies completeness, reverts on      │          │
      │   │   any error)                             │          │
      │   └──────────────────────────────────────────┘          │
      │                     │                                    │
      │                     ▼                                    │
      │  Minecraft draws the HUD / inventory / hotbar            │
      │      on the native-resolution default framebuffer        │
      └─────────────────────────────────────────────────────────┘
                            │
                            ▼  eglSwapBuffers intercepted
      ┌─────────────────────────────────────────────────────────┐
      │  RenderPipeline::onPresent()                             │
      │   1. glBlitFramebuffer → module texture (GPU only,       │
      │      no CPU readback)                             Level 1 │
      │   2. fullscreen triangle:                                │
      │      edge-adaptive reconstruction + detail recovery      │
      │      + contrast-adaptive sharpening                      │
      │   3. restore every GL state the game had                 │
      └─────────────────────────────────────────────────────────┘
                            │
                            ▼  original eglSwapBuffers
```

**Level 1 (presentation reconstruction)** works whenever the game presents
through EGL/OpenGL ES. It is a genuine GPU post-process, but on its own it
does **not** reduce the renderer's workload — it only reconstructs and sharpens.

**Level 2 (internal resolution)** is what actually reduces GPU load. The module
hooks `glBindFramebuffer`, `glViewport`, `glFramebufferTexture2D`,
`glFramebufferRenderbuffer`, `glTexImage2D`, `glTexStorage2D`,
`glBindTexture`, `glDeleteTextures`, `glBindRenderbuffer` and
`glRenderbufferStorage`, and from those calls it identifies the framebuffer
whose viewport matches the display size — the 3D scene target — and reallocates
it smaller.

Because OpenGL ES 3 has **no** API to query a texture's size or internal
format (`glGetTexLevelParameteriv` does not exist in ES), the module tracks
those parameters from the intercepted allocation calls instead.

Level 2 refuses to touch a target that is:

- immutable (allocated with `glTexStorage2D`),
- mipmapped,
- multisampled,
- or whose depth attachment does not match the colour size.

In every one of those cases it reports the reason and falls back to Level 1.
After any resize it checks `glCheckFramebufferStatus` and reverts immediately
if the target is incomplete.

### What the module never does

- It never blurs or replaces texture assets. Minecraft's textures, mipmaps and
  filtering are untouched; only the size of the render target changes.
- It never resamples the HUD: the interface is drawn by the game on the
  default framebuffer at native resolution.
- It never reads a frame back to the CPU.
- It never blocks on a GPU query (the timer query result is polled).
- It never compiles a shader during gameplay: the program is built once, on
  the first frame after a context change.
- It never touches input. No key, mouse, touch or cursor handling is modified.

---

## Settings

Every control below is wired to real behaviour. Controls that cannot work on
the current device report why instead of pretending to work.

### Main

| Setting | Default | Effect |
| --- | --- | --- |
| Master Enable | ON | Turns the whole pipeline on/off. Off = the game renders untouched. |
| Upscaling | ON | Enables the reconstruction pass. |
| Upscaling Mode | Performance | Preset; sets render scale and sharpening. |
| Render Scale | 70% | Internal 3D resolution (100/90/80/70/60/50). |
| Sharpening | 35% | Contrast-adaptive sharpening strength. |
| Dynamic Resolution | ON | Adapts the scale to hold the FPS target. |
| Target FPS | 60 | Target for the adaptive controller (30/45/60/90/120). |
| GPU Optimization | ON | Vendor-aware tuning for Mali and Adreno. |
| Low-Latency Mode | ON | Keeps the frame path free of added latency. |

### Advanced

Internal Resolution Control, Spatial Upscaling, Detail Recovery, Frame Pacing
Optimization, Render Resolution Stabilization, Resource and Buffer Management,
Damage Based Swap, Minimum/Maximum Render Scale, Safe Mode, Diagnostic
Overlay, GPU Timings, Verbose Logging and Restore Defaults.

**Safe Mode** disables every rendering change while keeping the measurements
running — use it if the image ever looks wrong.

The module publishes a **Mod Menu V2 schema** (categories, sections,
conditional visibility, live values, sliders with units) when the launcher's
preloader supports `pl::modmenu::setConfigSchemaJson`, and falls back to the
classic configuration list otherwise.

### Adaptive resolution behaviour

- Decides from the **75th percentile** of the last 32 frame times, so one
  hitch cannot change the resolution.
- Evaluates every 16 frames.
- Needs **2** agreeing evaluations to drop the scale, **3** to raise it.
- Cooldown of 1 evaluation after a drop, 3 after a raise — recovery is
  deliberately slower than reduction.
- Moves in 5% steps inside the user's min/max bounds.
- Only runs while Level 2 is active.

---

## Building

### Requirements

- **Android NDK** r25 or newer (arm64-v8a)
- **CMake** 3.22+ and **Ninja**
- A host C++20 compiler (`g++`/`clang++`) for the unit tests and the config
  generator
- **Node.js 18+** for the optional GLSL validation

### Build

```bash
./build.sh                 # tests + shader validation + build + package
./build.sh -n /path/to/ndk # explicit NDK location
./build.sh -c              # clean rebuild
./build.sh -t              # skip the host unit tests
./build.sh -s              # skip the GLSL validation
```

Output:

```
dist/arm64-v8a/levifsr_ai.levipack
```

### Install

1. Open LeviLaunchroid → Mods (Mod Manager).
2. Import `levifsr_ai.levipack`.
3. Enable the mod for your Minecraft version.
4. Launch Minecraft and open the Mod Menu to configure LeviFSR AI.

### CI

`.github/workflows/build-levipack.yml` installs the NDK on GitHub Actions,
runs the full build, and uploads `levifsr_ai-arm64-v8a.levipack` as an
artifact (and attaches it to `v*` releases). This is the easiest way to obtain
a real ARM64 library if you do not have an NDK locally.

---

## Package layout

```
levifsr_ai.levipack
├── manifest.json                 # type/name/entry/minecraft_versions
├── liblevifsr_ai.so              # arm64-v8a native module
└── config/
    ├── config.json               # defaults generated from the C++ struct
    └── config.schema.json
```

`manifest.json` follows the `preload-native` format that
`ModManager.parseDirectoryMod()` reads. The `entry` library exports
`PLGetModRegistration`, which is how the preloader finds the module.

---

## Project layout

```
src/
  Config.hpp             Configuration model, clamping, presets
  DynamicResolution.{hpp,cpp}  Adaptive resolution controller (GL free)
  GlApi.{hpp,cpp}        Runtime-resolved EGL/GLES3 table + GPU detection
  Shaders.hpp            GLSL ES 3.00 reconstruction shaders
  RenderPipeline.{hpp,cpp}  Presentation hook, scene scaling, metrics
  AiBackend.{hpp,cpp}    Experimental NNAPI probe and model validation
  Mod.cpp                Lifecycle, Mod Menu integration, overlay
  config_generator.cpp   Host tool: generates the shipped config JSON
tests/
  test_main.cpp          Host unit tests (59 checks)
  test_packaging.py      Packaging pipeline tests (20 checks)
scripts/
  package_levipack.py    Builds and self-verifies the .levipack
  verify_levipack.py     Validates a .levipack against the loader rules
tools/shader-validate/   glslang based GLSL validation
third_party/
  preloader_headers/     pl:: SDK headers (vendored from preloader-android)
  khronos/               Official Khronos EGL and GLES3 headers
android_stubs/           Host-only stubs for the host build
preloader_stub/          Build-time stub that emits DT_NEEDED libpreloader.so
```

---

## Verification performed

Everything below was actually executed in the development environment.

| Check | Result |
| --- | --- |
| All six translation units compile with `-Wall -Wextra -Wpedantic` | 0 errors, 0 warnings |
| Host unit tests (config, adaptive controller, AI model validation) | **59 checks, 0 failures** (ASan + UBSan clean) |
| Packaging pipeline tests | **20 checks, 0 failures** |
| GLSL ES 3.00 shaders compiled with glslang | Both compile to valid SPIR-V |
| Full ARM64 compile **and link** of the module | Links; exports `PLGetModRegistration`; exactly 6 undefined `pl::` symbols |
| CMake configure + config generation | Works |

### Not verified — no Android device available

**There is no Android device and no Android NDK in the development
environment.** The following therefore could not be tested and are *not*
claimed to work:

- Loading the module inside LeviLaunchroid on a real device.
- Appearance of the settings in the real Mod Menu.
- The RuntimeConfig V2 schema rendering.
- Detection of the scene framebuffer on a real Bedrock build.
- Any FPS, latency or image-quality measurement. **No benchmark numbers are
  reported anywhere in this project, because none was measured.**
- Thermal behaviour over a long session.

The ARM64 link check used a musl target because no bionic sysroot was
available; it proves the code compiles and links for AArch64, **not** that the
resulting library runs on Android. `scripts/package_levipack.py` refuses to
package a library that does not link `liblog.so`, so a non-Android build can
never be shipped by mistake.

---

## Known limitations

1. **Level 2 depends on the game's render structure.** If Bedrock does not
   render the 3D scene into a plain, mutable, non-multisampled texture-backed
   framebuffer, the module falls back to Level 1 and says so in the overlay.
2. **No temporal reconstruction** (no motion vectors available).
3. **No neural inference** unless you supply a model.
4. **The reconstruction pass costs GPU time.** At 100% render scale it is pure
   overhead; use Native mode if you do not want it.
5. **Level 1 alone reduces no GPU load.** Only Level 2 does.
6. **Dynamic resolution cannot fix CPU-bound stutter** (chunk loading, entity
   logic, garbage collection). It only helps when the GPU is the bottleneck.
7. **The GPU timer query is optional** and absent on some drivers; the overlay
   then shows `n/a` rather than an estimate.

---

## License and attribution

This project is not affiliated with Mojang, Microsoft, AMD, ARM, Qualcomm or
the LeviLaunchroid authors. Minecraft is a trademark of Mojang Studios. ARM
Mali is a trademark of ARM Limited; Adreno and Qualcomm are trademarks of
Qualcomm Incorporated; FSR and FidelityFX are trademarks of Advanced Micro
Devices, Inc.

The `pl::` SDK headers in `third_party/preloader_headers` come from
[LiteLDev/preloader-android](https://github.com/LiteLDev/preloader-android).
The EGL and GLES3 headers in `third_party/khronos` come from the Khronos
registries.
