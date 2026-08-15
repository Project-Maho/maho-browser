from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest

MODULE_PATH = Path(__file__).with_name("macos_vm_live_matrix.py")
SPEC = importlib.util.spec_from_file_location("macos_vm_live_matrix_setup", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"unable to load {MODULE_PATH}")
matrix = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = matrix
SPEC.loader.exec_module(matrix)


@pytest.mark.parametrize(
    ("step_name", "stdout", "message"),
    (
        ("doctor", '{"ok":false}', "doctor: output does not report ok"),
        (
            "list",
            '{"suites":[],"ai_scenarios":[]}',
            "list: manifest inventory mismatch",
        ),
        ("doctor", "not-json", "doctor: output is not JSON"),
    ),
)
def test_setup_proxy_output_is_rejected(
    step_name: str,
    stdout: str,
    message: str,
) -> None:
    step = matrix.Step(step_name, ("command",), 0)
    completed = subprocess.CompletedProcess(("command",), 0, stdout, "")
    with pytest.raises(RuntimeError, match=message):
        matrix._validate_setup_output(step, completed)


@pytest.mark.parametrize(
    ("receipt", "isolation", "eligible"),
    (
        (
            {
                "schema_version": 1,
                "run_id": "primary",
                "finalized": True,
                "terminal_status": "transport_failed",
                "guest_facts": {
                    "first_error": (
                        "required transport 'virtiofs-read-only' "
                        "did not pass guest preflight"
                    )
                },
                "teardown_result": {"succeeded": False},
            },
            {
                "evidence_kind": "early_exit",
                "is_isolated": True,
                "violation_codes": [],
            },
            False,
        ),
        (
            {
                "schema_version": 1,
                "run_id": "primary",
                "finalized": True,
                "terminal_status": "transport_failed",
                "guest_facts": {
                    "first_error": (
                        "required transport 'virtiofs-read-only' "
                        "did not pass guest preflight"
                    )
                },
                "teardown_result": {"succeeded": True},
            },
            {
                "evidence_kind": "early_exit",
                "is_isolated": False,
                "violation_codes": ["process"],
            },
            False,
        ),
    ),
)
def test_fallback_requires_clean_primary_teardown_and_isolation(
    tmp_path: Path,
    receipt: dict[str, object],
    isolation: dict[str, object],
    eligible: bool,
) -> None:
    run_dir = tmp_path / "primary"
    run_dir.mkdir()
    (run_dir / "receipt.json").write_text(json.dumps(receipt), encoding="utf-8")
    (run_dir / "host-isolation.json").write_text(
        json.dumps(isolation),
        encoding="utf-8",
    )
    assert (
        matrix.validation.eligible_primary_failure(tmp_path, "primary")
        is eligible
    )
