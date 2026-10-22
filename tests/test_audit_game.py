import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).parents[1]
spec = importlib.util.spec_from_file_location('audit_game', ROOT / 'scripts/audit_game.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def elf(payload=b'\x00' * 64):
    data = bytearray(256 + len(payload))
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', data, 18, 183)
    struct.pack_into('<Q', data, 32, 64)
    struct.pack_into('<HH', data, 54, 56, 1)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 256, 0x1000, 0, len(payload), len(payload), 4096)
    data[256:] = payload
    return bytes(data)


class AuditTests(unittest.TestCase):
    def test_unique_masked_match(self):
        report = module.audit(elf(b'\xAA\x01\xCC\xDD' + bytes(12)), {'test': [0xAA, None, 0xCC, 0xDD]})
        self.assertTrue(report['all_patterns_unique'])
        self.assertEqual(report['signatures']['test']['examples'][0]['elf_virtual_address'], '0x1000')

    def test_missing_and_ambiguous(self):
        report = module.audit(elf(b'\xAA\x01\xCC\xDD' * 2), {
            'duplicate': [0xAA, None, 0xCC, 0xDD], 'absent': [0x11, 0x22]})
        self.assertFalse(report['all_patterns_unique'])
        self.assertEqual(report['signatures']['duplicate']['status'], 'ambiguous')
        self.assertEqual(report['signatures']['duplicate']['matches'], 2)
        self.assertEqual(report['signatures']['absent']['status'], 'missing')

    def test_excludes_data_and_unaligned_matches(self):
        data = bytearray(elf(b'\x00\xAA\xBB\xCC\x00\x00\x00\x00'))
        data[128:131] = b'\xAA\xBB\xCC'  # Outside executable segment.
        report = module.audit(data, {'pattern': [0xAA, 0xBB, 0xCC]})
        self.assertEqual(report['signatures']['pattern']['matches'], 0)

    def test_reject_wrong_architecture_and_truncation(self):
        data = bytearray(elf())
        struct.pack_into('<H', data, 18, 62)
        with self.assertRaisesRegex(ValueError, 'ARM64'):
            module.audit(data)
        with self.assertRaises(ValueError):
            module.audit(elf()[:100])
        with self.assertRaises(ValueError):
            module.audit(b'not an ELF')

    def test_apk_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'game.apk'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr(module.APK_ENTRY, elf())
            self.assertEqual(module.read_game(path), elf())
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('lib/x86/libminecraftpe.so', b'wrong architecture')
            with self.assertRaisesRegex(ValueError, 'ARM64 split'):
                module.read_game(path)

    def test_uses_all_current_source_patterns(self):
        patterns = module.load_patterns()
        self.assertEqual(len(patterns), 6)
        self.assertNotIn('ContainerScreenControllerDtor', patterns)
        self.assertIn('NormalTick', patterns)
        self.assertTrue(all(any(value is not None for value in values) for values in patterns.values()))


if __name__ == '__main__':
    unittest.main()
