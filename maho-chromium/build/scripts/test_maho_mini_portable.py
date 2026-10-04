"""Build-graph contract: Maho Mini is the real portable Views implementation
on every desktop OS, not the no-op stub.

Machine-consumed inputs pinned here: the `maho_mini` source_set source lists
in maho-chromium/browser/BUILD.gn and DEFERRED/no-op markers in the Mini
sources. Exact narrow build: see .omo/evidence/cross-platform-parity/build-commands.txt
"""

import re
import unittest
from pathlib import Path

MAHO_CHROMIUM = Path(__file__).resolve().parents[2]
GN = (MAHO_CHROMIUM / "browser" / "BUILD.gn").read_text(encoding="utf-8")
MINI_DIR = MAHO_CHROMIUM / "browser" / "ui" / "views" / "maho_mini"


def mini_block(gn_condition: str) -> str:
    match = re.search(
        r'if \(%s\) \{\n  source_set\("maho_mini"\) \{.*?\n  \}\n\}' % gn_condition,
        GN,
        re.S,
    )
    return match.group(0) if match else ""


class MiniPortableSources(unittest.TestCase):
    def test_windows_block_uses_real_sources(self):
        block = mini_block("is_win")
        self.assertIn("ui/views/maho_mini/maho_mini_window.cc", block)
        self.assertIn("ui/views/maho_mini/maho_mini_top_bar_view.cc", block)
        self.assertNotIn("maho_mini_window_stub.cc", block)

    def test_linux_block_uses_real_sources(self):
        block = mini_block("is_linux")
        self.assertIn("ui/views/maho_mini/maho_mini_window.cc", block)
        self.assertIn("ui/views/maho_mini/maho_mini_top_bar_view.cc", block)
        self.assertNotIn("maho_mini_window_stub.cc", block)

    def test_windows_linux_blocks_link_global_shortcut_listener(self):
        for condition in ("is_win", "is_linux"):
            self.assertIn(
                "//ui/base/accelerators/global_accelerator_listener",
                mini_block(condition),
            )

    def test_no_deferred_noop_markers_in_mini_sources(self):
        for source in MINI_DIR.glob("*.cc"):
            self.assertNotIn("DEFERRED", source.read_text(encoding="utf-8"), source.name)

    def test_override_script_nonmac_list_uses_real_window(self):
        script = (MAHO_CHROMIUM / "build" / "scripts" / "apply_chromium_src_overrides.py").read_text(
            encoding="utf-8"
        )
        non_mac = re.search(
            r"_CHROME_BROWSER_UI_NON_MAC_SOURCES: Final\[tuple\[str, \.\.\.\]\] = \(.*?\)",
            script,
            re.S,
        ).group(0)
        self.assertNotIn("maho_mini_window_stub.cc", non_mac)
        self.assertIn("maho_mini_window.cc", non_mac)
        self.assertIn(
            '"//ui/base/accelerators/global_accelerator_listener"', script
        )


if __name__ == "__main__":
    unittest.main()
