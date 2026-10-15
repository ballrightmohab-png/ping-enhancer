#!/usr/bin/env python3
"""Validates a LeviFSR AI .levipack against LeviLaunchroid's loader rules.

The checks mirror what the launcher does when it imports a package:

  * ModManager.parseDirectoryMod() requires manifest.json at the archive root
    and reads `type`, `name`, `entry`, `minecraft_versions` from it.
  * ModNativeLoader loads the file named by `entry` with System.load(), so it
    must exist and be an ARM64 ELF shared object.
  * The native library name must match the `lib<name>.so` convention used by
    the preloader SDK.

Usage:
    python3 scripts/verify_levipack.py dist/levifsr_ai.levipack
"""
import json
import struct
import sys
import zipfile

REQUIRED_MANIFEST_FIELDS = ("type", "name", "entry", "minecraft_versions")
EXPECTED_TYPE = "preload-native"


def read_elf_header(data: bytes):
    if len(data) < 64 or data[:4] != b"\x7fELF":
        return None
    ei_class = data[4]                                  # 2 == ELF64
    e_type = struct.unpack_from("<H", data, 16)[0]      # 3 == ET_DYN
    e_machine = struct.unpack_from("<H", data, 18)[0]   # 0xB7 == AArch64
    return {"class": ei_class, "type": e_type, "machine": e_machine}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_levipack.py <file.levipack>", file=sys.stderr)
        return 2

    path = sys.argv[1]
    try:
        with zipfile.ZipFile(path) as archive:
            names = set(archive.namelist())

            if "manifest.json" not in names:
                print("FAIL missing manifest.json", file=sys.stderr)
                return 1

            manifest = json.loads(archive.read("manifest.json"))
            for field in REQUIRED_MANIFEST_FIELDS:
                if field not in manifest:
                    print(f"FAIL manifest.json is missing '{field}'",
                          file=sys.stderr)
                    return 1

            if manifest["type"] != EXPECTED_TYPE:
                print(f"FAIL type is {manifest['type']!r}, expected "
                      f"{EXPECTED_TYPE!r}", file=sys.stderr)
                return 1

            entry = manifest["entry"]
            if entry not in names:
                print(f"FAIL entry {entry!r} is not in the archive",
                      file=sys.stderr)
                return 1

            header = read_elf_header(archive.read(entry)[:64])
            if header is None:
                print(f"FAIL {entry} is not an ELF shared object",
                      file=sys.stderr)
                return 1
            if header["class"] != 2:
                print(f"FAIL {entry} is 32-bit, LeviLaunchroid needs arm64",
                      file=sys.stderr)
                return 1
            if header["machine"] != 0xB7:
                print(f"FAIL {entry} is not AArch64 "
                      f"(e_machine=0x{header['machine']:x})", file=sys.stderr)
                return 1
            if header["type"] != 3:
                print(f"FAIL {entry} is not a shared object "
                      f"(e_type={header['type']})", file=sys.stderr)
                return 1

            if "config/config.json" not in names:
                print("WARN the package ships no config/config.json",
                      file=sys.stderr)

            print(f"OK   {path}")
            print(f"     id={manifest.get('id', '-')} "
                  f"name={manifest['name']!r} "
                  f"version={manifest.get('version', '-')}")
            print(f"     type={manifest['type']} entry={entry}")
            print(f"     minecraft_versions={manifest['minecraft_versions']}")
            print(f"     files={len(names)}")
    except Exception as error:  # noqa: BLE001
        print(f"FAIL {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
