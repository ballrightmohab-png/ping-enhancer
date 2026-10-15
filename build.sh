#!/usr/bin/env bash
#
# LeviFSR AI - build script.
#
# Produces dist/arm64-v8a/levifsr_ai.levipack, an installable LeviLaunchroid
# native module.
#
# Requirements:
#   * Android NDK r25 or newer (arm64-v8a). Resolved from $ANDROID_NDK_HOME,
#     $ANDROID_NDK_ROOT, $ANDROID_HOME/ndk/<version> or $ANDROID_SDK_ROOT, or
#     pass -n <path>.
#   * CMake 3.22+ and Ninja (for the Android build).
#   * A host C++ compiler for the unit tests (optional, -t to skip).
#   * Node.js for the GLSL validation (optional, -s to skip).
#
# Usage:
#   ./build.sh              # build + test + package
#   ./build.sh -c           # clean rebuild
#   ./build.sh -t           # skip the host unit tests
#   ./build.sh -s           # skip the GLSL validation
#   ./build.sh -n <ndk>     # explicit NDK path
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CLEAN=0
RUN_TESTS=1
RUN_SHADERS=1
NDK_PATH=""
BUILD_TYPE="Release"
ABI="arm64-v8a"
VERSION="${LEVIFSR_VERSION:-1.0.0}"

while getopts ":ctsn:v:" opt; do
  case "$opt" in
    c) CLEAN=1 ;;
    t) RUN_TESTS=0 ;;
    s) RUN_SHADERS=0 ;;
    n) NDK_PATH="$OPTARG" ;;
    v) VERSION="$OPTARG" ;;
    *) echo "usage: $0 [-c] [-t] [-s] [-n <ndk>] [-v <version>]" >&2; exit 2 ;;
  esac
done

say() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
info() { printf '    %s\n' "$1"; }
die() { printf '\n\033[31merror:\033[0m %s\n' "$1" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 0. Third party headers used by the preloader SDK
# ---------------------------------------------------------------------------
# pl/Config.hpp needs nlohmann/json, boost.pfr, magic_enum and fmt. CMake
# fetches them with FetchContent for the Android build; the host tools here
# need them too.
DEPS_DIR="build/deps"
DEPS_INCLUDES=""
fetch_dep() {
  local name="$1" url="$2" tag="$3" subdir="${4:-}"
  if [[ -d "$DEPS_DIR/$name" ]]; then
    return 0
  fi
  info "fetching $name $tag"
  git clone --depth 1 --branch "$tag" "$url" "$DEPS_DIR/$name" >/dev/null 2>&1 \
    || die "could not clone $name ($url @ $tag)"
  [[ -z "$subdir" ]] || DEPS_INCLUDES="$DEPS_INCLUDES -I $DEPS_DIR/$name/$subdir"
}

if [[ "$RUN_TESTS" -eq 1 ]]; then
  say "Third party headers"
  mkdir -p "$DEPS_DIR"
  fetch_dep json     https://github.com/nlohmann/json.git     v3.11.3      include
  fetch_dep pfr      https://github.com/boostorg/pfr.git      boost-1.88.0 include
  fetch_dep magic_enum https://github.com/Neargye/magic_enum.git v0.9.7    include
  fetch_dep fmt      https://github.com/fmtlib/fmt.git        11.2.0       include
  info "include paths:$DEPS_INCLUDES"
fi

# ---------------------------------------------------------------------------
# 1. Host unit tests
# ---------------------------------------------------------------------------
if [[ "$RUN_TESTS" -eq 1 ]]; then
  say "Host unit tests"
  HOST_CXX="${CXX:-g++}"
  mkdir -p build/host
  # shellcheck disable=SC2086
  HOST_CXX="${CXX:-g++}"
  mkdir -p build/host
  # shellcheck disable=SC2086
  "$HOST_CXX" -std=c++20 -Wall -Wextra -Wpedantic \
    -DLEVIFSR_HOST_BUILD \
    -I src -I android_stubs \
    tests/test_main.cpp src/DynamicResolution.cpp src/AiBackend.cpp \
    -o build/host/levifsr_tests
  ./build/host/levifsr_tests

  # Packaging pipeline tests (pure Python, no NDK required).
  python3 tests/test_packaging.py
fi

# ---------------------------------------------------------------------------
# 2. GLSL shader validation
# ---------------------------------------------------------------------------
if [[ "$RUN_SHADERS" -eq 1 ]]; then
  say "GLSL shader validation"
  if command -v node >/dev/null 2>&1; then
    (
      cd tools/shader-validate
      if [[ ! -d node_modules ]]; then
        info "installing glslang"
        npm install --silent --no-audit --no-fund
      fi
      python3 - <<'PY'
import re, pathlib
src = pathlib.Path('../../src/Shaders.hpp').read_text()
for var in ('kFullscreenVertexShader', 'kReconstructionFragmentShader'):
    m = re.search(var + r'\s*=\s*R"\(([\s\S]*?)\)";', src)
    pathlib.Path(f'{var}.{"vert" if "Vertex" in var else "frag"}').write_text(m.group(1))
PY
      node validate.mjs kFullscreenVertexShader.vert \
           kReconstructionFragmentShader.frag
    )
  else
    info "node not found; skipping (the shaders are not machine verified)"
  fi
fi

# ---------------------------------------------------------------------------
# 3. Locate the Android NDK
# ---------------------------------------------------------------------------
say "Locating the Android NDK"
resolve_ndk() {
  local candidates=("$NDK_PATH" "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}")
  local sdk_root
  for sdk_root in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}"; do
    [[ -n "$sdk_root" ]] || continue
    if [[ -d "$sdk_root/ndk" ]]; then
      # Newest first.
      while IFS= read -r dir; do candidates+=("$dir"); done < <(
        ls -1d "$sdk_root"/ndk/* 2>/dev/null | sort -r)
    fi
    [[ -d "$sdk_root/ndk-bundle" ]] && candidates+=("$sdk_root/ndk-bundle")
  done
  local candidate
  for candidate in "${candidates[@]}"; do
    [[ -n "$candidate" ]] || continue
    if [[ -f "$candidate/build/cmake/android.toolchain.cmake" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done
  return 1
}

NDK="$(resolve_ndk || true)"
if [[ -z "$NDK" ]]; then
  echo
  echo "The Android NDK was not found, so the ARM64 native library cannot be"
  echo "built here."
  echo
  echo "To produce the real liblevifsr_ai.so, install the NDK and re-run:"
  echo "    ./build.sh -n /path/to/ndk"
  echo
  echo "Install the NDK with Android Studio (SDK Manager > SDK Tools > NDK)"
  echo "or download it from https://developer.android.com/ndk/downloads"
  exit 3
fi
info "NDK: $NDK"

TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
BUILD_DIR="build/android-$ABI-$BUILD_TYPE"

if [[ "$CLEAN" -eq 1 ]]; then
  info "removing $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

# ---------------------------------------------------------------------------
# 4. Configure and build the ARM64 shared object
# ---------------------------------------------------------------------------
say "Building $ABI shared object"
CMAKE_BIN="${CMAKE:-cmake}"
"$CMAKE_BIN" -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM=android-24 \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DLEVIFSR_MOD_VERSION="$VERSION" \
  -DLEVIFSR_BUILD_TESTS=OFF
"$CMAKE_BIN" --build "$BUILD_DIR" --target levifsr_ai

LIBRARY="$BUILD_DIR/out/$ABI/liblevifsr_ai.so"
[[ -f "$LIBRARY" ]] || die "the build did not produce $LIBRARY"
info "built $(stat -c%s "$LIBRARY") bytes: $LIBRARY"

# ---------------------------------------------------------------------------
# 5. Generate the shipped default configuration
# ---------------------------------------------------------------------------
say "Generating the default configuration"
CONFIG_OUT="build/generated-config"
mkdir -p "$CONFIG_OUT"
HOST_CXX="${CXX:-g++}"
# shellcheck disable=SC2086
"$HOST_CXX" -std=c++20 -Wall -Wextra \
  -DPL_CONFIG_NO_RUNTIME \
  -I src -I android_stubs -I third_party/preloader_headers "$DEPS_INCLUDES" \
  src/config_generator.cpp -o build/host/levifsr_config_generator
./build/host/levifsr_config_generator "$CONFIG_OUT"

# ---------------------------------------------------------------------------
# 6. Package the .levipack
# ---------------------------------------------------------------------------
say "Packaging the .levipack"
python3 scripts/package_levipack.py \
  --library "$LIBRARY" \
  --manifest manifest.json.in \
  --config-dir "$CONFIG_OUT" \
  --version "$VERSION" \
  --output "dist/$ABI/levifsr_ai.levipack"

say "Verifying the .levipack"
python3 scripts/verify_levipack.py "dist/$ABI/levifsr_ai.levipack"

say "Done"
info "dist/$ABI/levifsr_ai.levipack"
