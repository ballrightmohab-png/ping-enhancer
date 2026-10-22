"""Guard against reintroducing the removed external mod dependency."""
from pathlib import Path
import unittest

ROOT = Path(__file__).parents[1]

class StandaloneTests(unittest.TestCase):
    def test_no_external_sdk_headers_or_api(self):
        for source in (ROOT / 'src').rglob('*'):
            if source.suffix in ('.hpp', '.cpp'):
                text = source.read_text()
                self.assertNotIn('#include <bedrocktools/', text, str(source))
                self.assertNotIn('BedrockTools_GetApi', text, str(source))
                self.assertNotIn('libBedrockTools.so', text, str(source))

    def test_no_external_sdk_build_fetch(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text().lower()
        self.assertNotIn('fetchcontent_declare(bedrocktools', cmake)
        self.assertNotIn('bedrocktools_source_dir', cmake)
        self.assertNotIn('.external/BedrockTools', (ROOT / 'xmake.lua').read_text())
