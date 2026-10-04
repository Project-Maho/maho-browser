#!/usr/bin/env python3
"""Thin portable wrapper around the common runtime smoke harness.

Paths are caller-supplied (CI supplies the built artifacts), e.g.:

  python3 maho/scripts/verify_win_cli_e2e.py --browser out/Default/chrome.exe --cli out/Default/maho.exe
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import verify_desktop_runtime_smoke as smoke  # noqa: E402

if __name__ == "__main__":
    argv = sys.argv[1:]
    if not any(a == "--platform" or a.startswith("--platform=") for a in argv):
        argv += ["--platform", "windows"]
    sys.exit(smoke.main(argv))
