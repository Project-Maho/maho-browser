from __future__ import annotations

from pathlib import Path
import sys
from typing import TypeAlias

Json: TypeAlias = None | bool | int | float | str | list["Json"] | dict[str, "Json"]
APP_BUNDLE = "Maho.app"
INTERACTIVE_BINARY = "interactive_ui_tests"
RUNTIME_DEPS = "interactive_ui_tests.runtime_deps"


def _object(value: Json) -> dict[str, Json]:
    return value if isinstance(value, dict) else {}


def _strings(value: Json) -> list[str]:
    if not isinstance(value, list) or not all(
        isinstance(item, str) for item in value
    ):
        return []
    return [item for item in value if isinstance(item, str)]


def check_build_evidence(
    root: Path,
    ids: dict[str, str],
    receipts: dict[str, dict[str, Json]],
    required_tests: set[str],
    load_object,
    issues: list[str],
) -> None:
    expected_argv = [
        sys.executable,
        "maho-chromium/build/scripts/build_maho.py",
        "--out-dir",
        "out/Default",
        "--no-launch",
        "--ninja-target",
        "chrome",
        "--ninja-target",
        INTERACTIVE_BINARY,
    ]
    for role, run_id in ids.items():
        proof = load_object(root / run_id / "build-preflight.json", issues)
        receipt = receipts[role]
        host = _object(receipt.get("host_facts"))
        expected_output_root = (
            Path(str(host.get("workspace", "")))
            / "chromium/src/out/Default"
        )
        output_root = Path(str(proof.get("output_root", "")))
        artifacts = _object(proof.get("artifacts"))
        artifact_paths = {
            "app_bundle": expected_output_root / APP_BUNDLE,
            "interactive_binary": expected_output_root / INTERACTIVE_BINARY,
            "runtime_deps": expected_output_root / RUNTIME_DEPS,
        }
        valid = (
            proof.get("schema_version") == 1
            and proof.get("canonical_build_argv") == expected_argv
            and proof.get("build_returncode") == 0
            and output_root == expected_output_root
            and receipt.get("build_path") == str(expected_output_root)
            and not expected_output_root.is_symlink()
            and proof.get("architecture") == "arm64"
            and proof.get("app_signature_valid") is True
            and proof.get("binary_signature_valid") is True
            and proof.get("list_tests_returncode") == 0
            and set(_strings(proof.get("required_tests"))) == required_tests
            and set(_strings(proof.get("listed_tests"))) >= required_tests
            and all(
                artifacts.get(name) == str(path) and path.exists()
                for name, path in artifact_paths.items()
            )
        )
        if not valid:
            issues.append(f"{run_id}: build preflight evidence mismatch")
