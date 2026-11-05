"""Guard the pinned upstream 1.26.60 profile; not an Android binary validation."""
import json
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).parents[1]

class CompatibilityTests(unittest.TestCase):
    def test_upstream_offsets(self):
        profile = json.loads((ROOT / 'docs/MC_1.26.60_PROFILE.json').read_text())
        text = (ROOT / 'src/game/Game.hpp').read_text()
        for namespace, offsets in profile['offsets'].items():
            block = re.search(r'namespace ' + namespace + r' \{([^}]+)\}', text)[1]
            actual = {name: int(value, 0) for name, value in re.findall(r'std::size_t (\w+) = ([\w]+);', block)}
            for name, expected in offsets.items():
                self.assertEqual(actual[name], expected, f'{namespace}::{name}')

    def test_upstream_signatures_and_menu(self):
        profile = json.loads((ROOT / 'docs/MC_1.26.60_PROFILE.json').read_text())
        text = (ROOT / 'src/game/Signatures.hpp').read_text()
        self.assertEqual(dict(re.findall(r'\{"(\w+)", "([^"]+)"\}', text)), profile['signatures'])
        self.assertEqual(re.search(r'menuPattern="([^"]+)"', text)[1], profile['menu_signature'])
        runtime = (ROOT / 'src/game/Runtime.hpp').read_text()
        self.assertNotIn('resolveVtableFunction', runtime)
        self.assertNotIn('menuSlot=151', runtime)
