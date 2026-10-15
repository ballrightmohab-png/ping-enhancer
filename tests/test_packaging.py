#!/usr/bin/env python3
"""Tests the LeviFSR AI packaging pipeline end to end.

These tests build synthetic ARM64 shared objects in memory so the packager and
the verifier can be exercised without an Android NDK:

  * a library that links liblog.so (an Android NDK build) is accepted
  * a library that does not (a desktop or musl ARM64 build) is rejected
  * the produced archive contains everything LeviLaunchroid's loader needs
  * the verifier agrees with the packager

Run with:  python3 tests/test_packaging.py
"""
import json
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import package_levipack as pkg  # noqa: E402

EM_AARCH64 = 0xB7
ET_DYN = 3
SHT_STRTAB = 3
SHT_DYNAMIC = 6
DT_NULL = 0
DT_NEEDED = 1

failures = []
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        failures.append(message)
        print(f"  FAIL {message}")


def build_elf(needed_names):
    """Builds a minimal, structurally valid ARM64 shared object."""
    dynstr = b"\x00"
    offsets = []
    for name in needed_names:
        offsets.append(len(dynstr))
        dynstr += name.encode() + b"\x00"

    dynamic = b""
    for offset in offsets:
        dynamic += struct.pack("<qQ", DT_NEEDED, offset)
    dynamic += struct.pack("<qQ", DT_NULL, 0)

    shstr = b"\x00.dynstr\x00.dynamic\x00.shstrtab\x00"
    name_dynstr = 1
    name_dynamic = 9
    name_shstrtab = 18

    ehsize = 64
    phentsize = 56
    shentsize = 64

    phoff = ehsize
    dynstr_off = phoff + phentsize * 2
    dynamic_off = dynstr_off + len(dynstr)
    shstr_off = dynamic_off + len(dynamic)
    shoff = (shstr_off + len(shstr) + 15) & ~15

    def phdr(ptype, offset, size, flags=5):
        return struct.pack("<IIQQQQQQ", ptype, flags, offset,
                           0x10000 + offset, 0x10000 + offset, size, size, 8)

    ident = b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8
    header = ident + struct.pack(
        "<HHIQQQIHHHHHH", ET_DYN, EM_AARCH64, 1, 0, phoff, shoff, 0,
        ehsize, phentsize, 2, shentsize, 3, 2)

    def shdr(name, stype, offset, size):
        return struct.pack("<IIQQQQIIQQ", name, stype, 0, 0, offset, size,
                           0, 0, 1, 0)

    sections = [
        shdr(0, 0, 0, 0),
        shdr(name_dynstr, SHT_STRTAB, dynstr_off, len(dynstr)),
        shdr(name_dynamic, SHT_DYNAMIC, dynamic_off, len(dynamic)),
        shdr(name_shstrtab, SHT_STRTAB, shstr_off, len(shstr)),
    ]

    body = bytearray()
    body += header
    body += phdr(1, 0, shoff)              # PT_LOAD
    body += phdr(2, dynamic_off, len(dynamic))  # PT_DYNAMIC
    body += dynstr
    body += dynamic
    body += shstr
    body += b"\x00" * (shoff - len(body))
    body += b"".join(sections)
    return bytes(body)


def main():
    print("LeviFSR AI packaging tests\n")
    manifest = ROOT / "manifest.json.in"

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        android_lib = tmp / "liblevifsr_ai.so"
        android_lib.write_bytes(build_elf(["liblog.so", "libc.so", "libm.so"]))

        musl_lib = tmp / "liblevifsr_desktop.so"
        musl_lib.write_bytes(build_elf(["libc.so"]))

        config_dir = tmp / "generated-config"
        config_dir.mkdir()
        (config_dir / "config.json").write_text('{"version": 1}\n')
        (config_dir / "config.schema.json").write_text('{"type": "object"}\n')

        # --- DT_NEEDED parsing -------------------------------------------
        print("  dt_needed parsing")
        needed = pkg.read_dt_needed(android_lib)
        check("liblog.so" in needed, f"liblog.so missing from {needed}")
        check(needed == ["liblog.so", "libc.so", "libm.so"],
              f"unexpected DT_NEEDED order: {needed}")
        check(pkg.read_dt_needed(musl_lib) == ["libc.so"],
              "musl library should only need libc.so")

        # --- A non-Android library is rejected ---------------------------
        print("  non-Android library rejection")
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/package_levipack.py"),
             "--library", str(musl_lib),
             "--manifest", str(manifest),
             "--output", str(tmp / "rejected.levipack")],
            capture_output=True, text=True)
        check(result.returncode != 0,
              "a non-Android library must not be packaged")
        check("liblog.so" in result.stderr,
              f"rejection should mention liblog.so, got: {result.stderr!r}")
        check(not (tmp / "rejected.levipack").exists(),
              "no archive should be produced on rejection")

        # --- A well formed package is produced ---------------------------
        print("  android library packaging")
        out = tmp / "levifsr_ai.levipack"
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/package_levipack.py"),
             "--library", str(android_lib),
             "--manifest", str(manifest),
             "--config-dir", str(config_dir),
             "--version", "9.9.9",
             "--output", str(out)],
            capture_output=True, text=True)
        check(result.returncode == 0,
              f"packaging failed: {result.stdout} {result.stderr}")
        check(out.exists(), "the .levipack was not produced")

        with zipfile.ZipFile(out) as archive:
            names = set(archive.namelist())
            stored = json.loads(archive.read("manifest.json"))

        check("manifest.json" in names, "manifest.json missing")
        check("liblevifsr_ai.so" in names, "the entry library is missing")
        check("config/config.json" in names, "config/config.json missing")
        check("config/config.schema.json" in names,
              "config/config.schema.json missing")
        check(stored["type"] == "preload-native",
              f"type is {stored.get('type')!r}")
        check(stored["entry"] == "liblevifsr_ai.so",
              f"entry is {stored.get('entry')!r}")
        check(stored["version"] == "9.9.9",
              f"version override ignored: {stored.get('version')!r}")
        check("minecraft_versions" in stored,
              "minecraft_versions missing from the manifest")
        check(stored["minecraft_versions"] == [],
              "minecraft_versions should be empty for all versions")

        # --- The verifier accepts what the packager produced --------------
        print("  verifier agreement")
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/verify_levipack.py"),
             str(out)],
            capture_output=True, text=True)
        check(result.returncode == 0,
              f"verifier rejected the package: {result.stdout}{result.stderr}")
        check("OK" in result.stdout, "verifier did not report OK")

        # --- The verifier rejects a broken package ------------------------
        print("  verifier rejection")
        bad = tmp / "bad.levipack"
        with zipfile.ZipFile(bad, "w") as archive:
            archive.writestr("manifest.json", json.dumps({"type": "wrong"}))
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/verify_levipack.py"),
             str(bad)], capture_output=True, text=True)
        check(result.returncode != 0, "verifier accepted a broken package")

    print(f"\n{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
