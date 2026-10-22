"""Package an Android library using LeviLauncher's preload-native manifest."""
import argparse
import json
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED


def package(library, output):
    library = Path(library)
    if not library.is_file():
        raise FileNotFoundError(library)
    # Reject host binaries rather than shipping a non-Android artifact accidentally.
    header = library.read_bytes()[:20]
    if len(header) < 20 or header[:5] != b'\x7fELF\x02' or header[5] != 1 or int.from_bytes(header[18:20], 'little') != 183:
        raise ValueError('Expected a little-endian ARM64 ELF shared library')
    manifest = dict(type='preload-native', name='Adaptive Aim', author='ping-enhancer contributors',
                    description='Standalone aim-assist mod. No other gameplay mod required.',
                    version='0.8.0', entry='libAdaptiveAim.so')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, 'w', ZIP_DEFLATED) as archive:
        archive.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
        archive.write(library, 'libAdaptiveAim.so')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('library')
    parser.add_argument('output')
    args = parser.parse_args()
    package(args.library, args.output)
