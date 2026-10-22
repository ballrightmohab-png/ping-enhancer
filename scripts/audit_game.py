"""Offline signature audit of a user-supplied ARM64 Minecraft ELF or APK.

Does not download the game or execute its code. A unique match validates only
pattern presence, not function identity, layouts, or gameplay correctness.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

PATTERNS = Path(__file__).resolve().parents[1] / 'src/game/Signatures.hpp'
APK_ENTRY = 'lib/arm64-v8a/libminecraftpe.so'


def load_patterns(path=PATTERNS):
    entries = re.findall(r'\{"([^"\n]+)", "([^"\n]+)"\}', Path(path).read_text())
    if not entries:
        raise ValueError('No signature definitions found')
    result = {}
    for name, text in entries:
        tokens = text.split()
        if not tokens or any(t not in ('?', '??') and not re.fullmatch(r'[0-9a-fA-F]{2}', t) for t in tokens):
            raise ValueError(f'Invalid signature: {name}')
        if name in result:
            raise ValueError(f'Duplicate signature: {name}')
        result[name] = [None if t in ('?', '??') else int(t, 16) for t in tokens]
    return result


def read_game(path):
    path = Path(path)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            if APK_ENTRY not in archive.namelist():
                raise ValueError(f'APK does not contain {APK_ENTRY}; supply the ARM64 split APK or .so')
            return archive.read(APK_ENTRY)
    return path.read_bytes()


def executable_segments(data):
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 game library')
    if struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected ARM64 (AArch64), not another game architecture')
    offset = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size < 56 or not count or count > 4096 or offset + size * count > len(data):
        raise ValueError('Invalid ELF program-header table')
    segments = []
    for i in range(count):
        kind, flags, start, address, _, length, _, _ = struct.unpack_from('<IIQQQQQQ', data, offset + i * size)
        if kind == 1 and flags & 1:  # PT_LOAD + PF_X
            if start + length > len(data):
                raise ValueError('Executable segment extends past end of file')
            segments.append((start, address, length))
    if not segments:
        raise ValueError('ELF contains no executable load segments')
    return segments


def scan(data, segments, tokens):
    # Anchor on the longest exact run, then verify all wildcard-masked bytes.
    runs = []
    start = None
    for i, value in enumerate(tokens + [None]):
        if value is not None and start is None:
            start = i
        elif value is None and start is not None:
            runs.append((start, bytes(tokens[start:i])))
            start = None
    if not runs:
        raise ValueError('A signature cannot be entirely wildcards')
    anchor_offset, anchor = max(runs, key=lambda run: len(run[1]))
    count, examples, seen = 0, [], set()
    for segment_start, segment_address, length in segments:
        end = segment_start + length
        cursor = segment_start + anchor_offset
        while True:
            match = data.find(anchor, cursor, end)
            if match < 0:
                break
            cursor = match + 1
            candidate = match - anchor_offset
            if candidate < segment_start or candidate + len(tokens) > end:
                continue
            if all(value is None or data[candidate + i] == value for i, value in enumerate(tokens)):
                address = segment_address + candidate - segment_start
                # ARM64 functions are 4-byte aligned; overlapping ELF segments
                # must not inflate match counts.
                if address % 4 or address in seen:
                    continue
                seen.add(address)
                count += 1
                if len(examples) < 5:
                    examples.append({'file_offset': hex(candidate), 'elf_virtual_address': hex(address)})
    return {'matches': count, 'status': 'unique' if count == 1 else ('missing' if not count else 'ambiguous'),
            'examples': examples}


def audit(data, patterns=None):
    segments = executable_segments(data)
    patterns = load_patterns() if patterns is None else patterns
    results = {name: scan(data, segments, tokens) for name, tokens in patterns.items()}
    return {'sha256': hashlib.sha256(data).hexdigest(), 'architecture': 'arm64-v8a',
            'not_audited': ['RTTI ClientInstance::isShowingMenu slot 151', 'function identities', 'game layouts'],
            'all_patterns_unique': all(result['matches'] == 1 for result in results.values()),
            'warning': 'Pattern matches alone do not validate function identity, data layouts, or gameplay.',
            'signatures': results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, help='Your installed Minecraft ARM64 .so or APK')
    parser.add_argument('--output', type=Path, help='Write the JSON report here')
    args = parser.parse_args()
    try:
        result = audit(read_game(args.game))
    except (OSError, ValueError, zipfile.BadZipFile, struct.error) as error:
        parser.exit(2, f'Audit failed: {error}\n')
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')
    return 0 if result['all_patterns_unique'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
