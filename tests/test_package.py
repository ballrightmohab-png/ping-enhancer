import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location('pack', Path(__file__).parents[1] / 'scripts/package.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

class PackageTests(unittest.TestCase):
    def test_missing_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(FileNotFoundError):
                pack.package(Path(tmp)/'missing', Path(tmp)/'out.levipack')

    def test_reject_host_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'lib.so'
            source.write_bytes(b'not an ARM64 library')
            with self.assertRaises(ValueError):
                pack.package(source, Path(tmp)/'out.levipack')

    def test_manifest_and_entry(self):
        # Synthetic ELF header: tests archive contract, NOT binary validity.
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'lib.so'
            header = bytearray(64)
            header[:6] = b'\x7fELF\x02\x01'
            header[18:20] = (183).to_bytes(2, 'little')
            source.write_bytes(header)
            output = Path(tmp)/'out.levipack'
            pack.package(source, output)
            with ZipFile(output) as archive:
                manifest = json.loads(archive.read('manifest.json'))
                self.assertEqual(manifest['type'], 'preload-native')
                self.assertIn(manifest['entry'], archive.namelist())
                self.assertEqual(archive.read(manifest['entry']), header)
