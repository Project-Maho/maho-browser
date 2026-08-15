from __future__ import annotations

import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
from unittest import mock

import pytest

MODULE_PATH = Path(__file__).with_name("macos_vm_live_matrix.py")
SPEC = importlib.util.spec_from_file_location("macos_vm_live_matrix", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"unable to load {MODULE_PATH}")
matrix = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = matrix
SPEC.loader.exec_module(matrix)


def _config(tmp_path: Path):
    return matrix.Config(
        workspace=tmp_path,
        state_dir=tmp_path / "state",
        identity=tmp_path / "identity",
        golden="maho-e2e-golden",
        tart_home=tmp_path / "tart",
        prefix="proof",
    )


FIXTURE_PATH = Path(__file__).with_name("macos_vm_matrix_test_fixture.py")
FIXTURE_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_matrix_test_fixture",
    FIXTURE_PATH,
)
if FIXTURE_SPEC is None or FIXTURE_SPEC.loader is None:
    raise RuntimeError(f"unable to load {FIXTURE_PATH}")
fixture = importlib.util.module_from_spec(FIXTURE_SPEC)
sys.modules[FIXTURE_SPEC.name] = fixture
FIXTURE_SPEC.loader.exec_module(fixture)
FakeRunner = fixture.FakeRunner

def test_dry_run_has_exact_order_and_no_side_effects(tmp_path: Path) -> None:
    stdout = io.StringIO()
    with mock.patch("sys.stdout", stdout):
        assert matrix.main(
            [
                "--workspace",
                str(tmp_path),
                "--state-dir",
                str(tmp_path / "state"),
                "--identity",
                str(tmp_path / "id"),
                "--tart-home",
                str(tmp_path / "tart"),
                "--prefix",
                "proof",
                "--dry-run",
            ]
        ) == 0
    payload = json.loads(stdout.getvalue())
    assert [step["name"] for step in payload["steps"]] == [
        "doctor",
        "list",
        "primary",
        "primary-fallback",
        "interactive",
        "ai",
        "all-a",
        "all-b",
        "failure",
        "cancel",
        "inventory",
    ]
    assert "<identity>" in json.dumps(payload)
    assert str(tmp_path / "id") not in json.dumps(payload)
    assert not (tmp_path / "test-results").exists()


def test_invalid_prefix_rejected_without_side_effects(tmp_path: Path) -> None:
    assert matrix.main(
        [
            "--workspace",
            str(tmp_path),
            "--state-dir",
            str(tmp_path / "state"),
            "--identity",
            str(tmp_path / "id"),
            "--tart-home",
            str(tmp_path / "tart"),
            "--prefix",
            "../escape",
            "--dry-run",
        ]
    ) == 2
    assert not (tmp_path / "test-results").exists()


def test_success_path_skips_fallback_and_verifies_last(tmp_path: Path) -> None:
    config = _config(tmp_path)
    runner = FakeRunner(config)
    result = matrix.execute(config, runner=runner)
    names = [
        "verify" if str(matrix.matrix_plan.VERIFIER) in argv else (
            "inventory" if argv[0] == "tart" else (
                argv[argv.index("--run-id") + 1].removeprefix("proof-")
                if "--run-id" in argv else argv[2]
            )
        )
        for argv in runner.calls
    ]
    assert result["fallback"] is False
    assert "primary-fallback" not in names
    assert names[-2:] == ["inventory", "verify"]
    assert json.loads(
        (config.matrix_dir / "matrix-receipt.json").read_text()
    )["valid"] is True


def test_transport_failure_runs_fallback_exactly_once(tmp_path: Path) -> None:
    config = _config(tmp_path)
    runner = FakeRunner(config, primary_exit=3)
    result = matrix.execute(config, runner=runner)
    fallback_calls = [
        argv
        for argv in runner.calls
        if "proof-primary-fallback" in argv
        and str(matrix.matrix_plan.LIFECYCLE) in argv
    ]
    verifier = runner.calls[-1]
    assert result["fallback"] is True
    assert len(fallback_calls) == 1
    assert "--require-transport" not in fallback_calls[0]
    assert verifier.count("--primary-fallback") == 1


@pytest.mark.parametrize(
    ("primary_exit", "primary_error", "failure_exit", "cancel_exit", "verifier_exit", "expected"),
    (
        (
            2, None, 1, 130, 0, "primary: expected exit 0, got 2",
        ),
        (
            3,
            "different failure",
            1,
            130,
            0,
            "primary: expected exit 0, got 3",
        ),
        (0, None, 2, 130, 0, "failure: expected exit 1, got 2"),
        (0, None, 1, 3, 0, "cancel: expected exit 130, got 3"),
        (0, None, 1, 130, 1, "evidence verifier failed"),
    ),
)
def test_unexpected_result_fails_without_retry(
    tmp_path: Path,
    primary_exit: int,
    primary_error: str | None,
    failure_exit: int,
    cancel_exit: int,
    verifier_exit: int,
    expected: str,
) -> None:
    config = _config(tmp_path)
    runner = FakeRunner(
        config,
        primary_exit=primary_exit,
        primary_error=primary_error,
        failure_exit=failure_exit,
        cancel_exit=cancel_exit,
        verifier_exit=verifier_exit,
    )
    with pytest.raises(RuntimeError, match=expected):
        matrix.execute(config, runner=runner)
    assert len(runner.calls) == len(set(runner.calls))
    assert json.loads(
        (config.matrix_dir / "matrix-receipt.json").read_text()
    )["valid"] is False


def test_preexisting_run_rejected_before_doctor(tmp_path: Path) -> None:
    config = _config(tmp_path)
    (config.root / "proof-ai").mkdir(parents=True)
    runner = FakeRunner(config)
    with pytest.raises(RuntimeError, match="refusing to reuse"):
        matrix.execute(config, runner=runner)
    assert runner.calls == []


def test_source_has_no_polling_sleep_or_shell_execution() -> None:
    source = MODULE_PATH.read_text(encoding="utf-8")
    assert "sleep(" not in source
    assert "shell=True" not in source
