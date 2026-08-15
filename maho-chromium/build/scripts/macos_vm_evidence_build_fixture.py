from __future__ import annotations

import sys
from pathlib import Path


def build_proof(
    run_dir: Path,
    output_root: Path,
    required_tests: set[str],
    write_json,
) -> None:
    output_root.mkdir(parents=True, exist_ok=True)
    artifacts = {
        "app_bundle": output_root / "Maho.app",
        "interactive_binary": output_root / "interactive_ui_tests",
        "runtime_deps": output_root / "interactive_ui_tests.runtime_deps",
    }
    artifacts["app_bundle"].mkdir(exist_ok=True)
    artifacts["interactive_binary"].write_text("binary", encoding="utf-8")
    artifacts["runtime_deps"].write_text("deps", encoding="utf-8")
    write_json(
        run_dir / "build-preflight.json",
        {
            "schema_version": 1,
            "canonical_build_argv": [
                sys.executable,
                "maho-chromium/build/scripts/build_maho.py",
                "--out-dir",
                "out/Default",
                "--no-launch",
                "--ninja-target",
                "chrome",
                "--ninja-target",
                "interactive_ui_tests",
            ],
            "build_returncode": 0,
            "output_root": str(output_root),
            "artifacts": {
                name: str(path) for name, path in artifacts.items()
            },
            "architecture": "arm64",
            "app_signature_valid": True,
            "binary_signature_valid": True,
            "list_tests_returncode": 0,
            "required_tests": sorted(required_tests),
            "listed_tests": sorted(required_tests),
        },
    )
