#!/usr/bin/env python3
"""Packages LeviFSR AI as an installable LeviLaunchroid .levipack.

A .levipack is a zip archive that contains at least manifest.json and the
native library named in manifest.json's `entry` field. LeviLaunchroid's
ModManager reads `name`, `author`, `version`, `description`, `type`, `entry`
and `minecraft_versions` from the top level of manifest.json, so they must
stay top level.

Usage:
    python3 scripts/package_levipack.py \
        --library build/android-arm64-v8a-Release/out/arm64-v8a/liblevifsr_ai.so \
        --output dist/levifsr_ai.levipack
"""
import argparse
import struct
import json
import sys
import zipfile
from pathlib import Path

MOD_ID = "levifsr_ai"
ENTRY = "liblevifsr_ai.so"

# Files that must exist inside the archive for the launcher to accept it.
REQUIRED = {"manifest.json", ENTRY, "config/config.json"}


def read_dt_needed(path: Path) -> list:
    """Returns the DT_NEEDED entries of an ELF file, without external tools."""
    data = path.read_bytes()
    is64 = data[4] == 2
    end = "<"  # Android and this checker both target little-endian ARM64.

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        return struct.unpack_from(end + fmt, data, offset)[0], offset + size

    if is64:
        e_shoff, off = unpack("Q", 0x28)
        e_shentsize, _ = unpack("H", 0x3A)
        e_shnum, _ = unpack("H", 0x3C)
        e_shstrndx, _ = unpack("H", 0x3E)
        # section header: name, type, flags, addr, offset, size, link, ...
        sh_offset, _ = unpack("Q", e_shoff + 0x18)
        sh_size, _ = unpack("Q", e_shoff + 0x20)
    else:
        raise RuntimeError("32-bit ELF is not supported")

    strtab_offset = None
    strtab_size = 0
    dynamic = None
    for index in range(e_shnum):
        base = e_shoff + index * e_shentsize
        sh_type, _ = unpack("I", base + 4)
        sh_offset, _ = unpack("Q", base + 0x18)
        sh_size, _ = unpack("Q", base + 0x20)
        if sh_type == 3:  # SHT_STRTAB
            if strtab_offset is None:
                strtab_offset = sh_offset
                strtab_size = sh_size
        elif sh_type == 6:  # SHT_DYNAMIC
            dynamic = (sh_offset, sh_size)

    if dynamic is None or strtab_offset is None:
        return []

    # Fall back to .dynstr (the section the dynamic table indexes into).
    dynstr = data[strtab_offset:strtab_offset + strtab_size]
    offset, size = dynamic
    entry_size = 16
    result = []
    for position in range(0, size, entry_size):
        if is64:
            d_tag, _ = unpack("q", offset + position)
            d_val, _ = unpack("Q", offset + position + 8)
        if d_tag == 1:  # DT_NEEDED
            name = dynstr[d_val:dynstr.index(b"\x00", d_val)]
            result.append(name.decode("utf-8", "replace"))
        elif d_tag == 0:  # DT_NULL
            break
    return result


def load_manifest(source: Path, version: str) -> dict:
    manifest = json.loads(source.read_text(encoding="utf-8"))
    # Keep the manifest and the CMake package version in sync.
    manifest["version"] = version
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", type=Path,
                        default=Path("manifest.json.in"))
    parser.add_argument("--config-dir", type=Path, default=None,
                        help="Directory containing config.json / "
                             "config.schema.json to ship")
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--allow-placeholder", action="store_true",
                        help="Permit packaging a non-ARM64 placeholder "
                             "library (development only)")
    args = parser.parse_args()

    try:
        if not args.library.is_file():
            raise FileNotFoundError(f"Library not found: {args.library}")

        manifest = load_manifest(args.manifest, args.version)

        # Refuse to ship a host-architecture library silently: that would look
        # like a working package while the launcher rejects it.
        if not args.allow_placeholder:
            with open(args.library, "rb") as handle:
                header = handle.read(20)
            if len(header) < 20 or header[:4] != b"\x7fELF":
                raise RuntimeError(
                    f"{args.library} is not an ELF shared object. Build with "
                    "the Android NDK first (see build.sh).")
            ei_class = header[4]   # 1 = 32 bit, 2 = 64 bit
            e_machine = int.from_bytes(header[18:20], "little")
            if ei_class != 2 or e_machine != 0xB7:  # EM_AARCH64
                raise RuntimeError(
                    f"{args.library} is not an ARM64 shared object "
                    f"(EI_CLASS={ei_class}, e_machine=0x{e_machine:x}). "
                    "LeviLaunchroid requires arm64-v8a.")

            # An ARM64 ELF alone is not enough: a desktop or musl ARM64 build
            # has the right header but cannot run on Android. The module links
            # liblog, so a real NDK build always names it in DT_NEEDED.
            needed = read_dt_needed(args.library)
            if "liblog.so" not in needed:
                raise RuntimeError(
                    f"{args.library} does not link liblog.so "
                    f"(DT_NEEDED={needed or 'none'}). It does not look like "
                    "an Android NDK build. Build with the Android NDK first "
                    "(see build.sh).")

        output = args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            output.unlink()

        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as archive:
            archive.writestr(
                "manifest.json",
                json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
            archive.write(args.library, ENTRY)

            if args.config_dir and args.config_dir.is_dir():
                for name in ("config.json", "config.schema.json"):
                    candidate = args.config_dir / name
                    if candidate.is_file():
                        archive.write(candidate, f"config/{name}")
    except Exception as error:  # noqa: BLE001 - report and exit non-zero
        print(f"error: {error}", file=sys.stderr)
        return 1

    # Re-open and verify exactly what will be shipped.
    try:
        with zipfile.ZipFile(output, "r") as archive:
            names = set(archive.namelist())
            missing = sorted(REQUIRED - names)
            if missing:
                raise RuntimeError(f"package is missing {missing}")
            stored = json.loads(archive.read("manifest.json"))
            if stored != manifest:
                raise RuntimeError("manifest verification failed")
            if archive.getinfo(ENTRY).file_size != args.library.stat().st_size:
                raise RuntimeError("library verification failed")
            print(f"OK: {output}")
            for name in sorted(names):
                print(f"  {name:28} {archive.getinfo(name).file_size:>9} bytes")
    except Exception as error:  # noqa: BLE001
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
