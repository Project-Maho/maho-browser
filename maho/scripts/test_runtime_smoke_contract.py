"""Build-input contract for the portable runtime smoke harness.

Machine-consumed values only: the shipped scripts must not contain personal
absolute paths (build machines differ) and must not use time.sleep polling
(readiness must be state-driven). Red-line for the parity work; see
.omo/evidence/cross-platform-parity/build-commands.txt for exact commands.
"""

from pathlib import Path
import re
import unittest

SCRIPTS = Path(__file__).resolve().parent
LEGACY = [
    SCRIPTS / "verify_win_cli_e2e.py",
    SCRIPTS / "verify_wsl_cli_e2e.py",
]
HARNESS = SCRIPTS / "verify_desktop_runtime_smoke.py"

PERSONAL_PATH = re.compile(r"C:\\Users\\|/home/sook")
SLEEP_CALL = re.compile(r"\btime\.sleep\s*\(")


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8") if p.is_file() else ""


class LegacyScriptPortability(unittest.TestCase):
    def test_no_personal_absolute_paths(self):
        for p in LEGACY:
            self.assertNotRegex(_read(p), PERSONAL_PATH, f"{p.name} hardcodes a personal path")

    def test_no_fixed_sleep_polling(self):
        for p in LEGACY:
            self.assertNotRegex(_read(p), SLEEP_CALL, f"{p.name} uses time.sleep polling")

    def test_delegate_to_common_harness(self):
        for p in LEGACY:
            text = _read(p)
            self.assertIn("verify_desktop_runtime_smoke", text, f"{p.name} must delegate to the common harness")


class CommonHarnessContract(unittest.TestCase):
    def test_harness_exists(self):
        self.assertTrue(HARNESS.is_file(), "verify_desktop_runtime_smoke.py is missing")

    def test_harness_is_sleep_free_and_configurable(self):
        text = _read(HARNESS)
        self.assertNotRegex(text, SLEEP_CALL)
        for needle in ("--browser", "--cli", "readiness", "teardown"):
            self.assertIn(needle, text)


if __name__ == "__main__":
    unittest.main()
