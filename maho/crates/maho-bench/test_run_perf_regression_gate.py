from pathlib import Path
import json
import subprocess
import sys

import pytest

from run_perf_regression_gate import parse_criterion_directory


def write_estimate(root: Path, generation: str, mean_ms: float) -> None:
    destination = root / "tab_switch_active_to_active" / generation
    destination.mkdir(parents=True)
    (destination / "estimates.json").write_text(
        json.dumps({"mean": {"point_estimate": mean_ms * 1_000_000}}),
        encoding="utf-8",
    )


@pytest.mark.parametrize("base_ms,new_ms", [(1.0, 50.0), (50.0, 1.0)])
def test_current_measurement_takes_precedence(
    tmp_path: Path, base_ms: float, new_ms: float,
) -> None:
    write_estimate(tmp_path, "base", base_ms)
    write_estimate(tmp_path, "new", new_ms)
    estimates = parse_criterion_directory(tmp_path)
    assert len(estimates) == 1
    assert estimates[0].mean_ms == new_ms


def test_target_directory_reaches_cargo(tmp_path: Path, monkeypatch) -> None:
    import run_perf_regression_gate as gate
    commands = []
    monkeypatch.setattr(sys, "argv", ["gate", "--target-dir", str(tmp_path), "--bench", "tab_switch"])
    monkeypatch.setattr(gate.shutil, "which", lambda _: "cargo")
    def run(command):
        commands.append(command)
        return subprocess.CompletedProcess(command, 0)
    monkeypatch.setattr(gate.subprocess, "run", run)
    write_estimate(tmp_path / "criterion", "new", 1.0)
    assert gate.main() == 0
    assert commands
    command = commands[0]
    assert "--target-dir" in command
    assert Path(command[command.index("--target-dir") + 1]) == tmp_path.resolve()


def test_baseline_only_report_remains_readable(tmp_path: Path) -> None:
    write_estimate(tmp_path, "base", 2.0)
    estimates = parse_criterion_directory(tmp_path)
    assert len(estimates) == 1
    assert estimates[0].mean_ms == 2.0


def test_cli_rejects_current_regression_despite_passing_baseline(tmp_path: Path) -> None:
    write_estimate(tmp_path, "base", 1.0)
    write_estimate(tmp_path, "new", 50.0)
    result = subprocess.run(
        [
            sys.executable, str(Path(__file__).with_name("run_perf_regression_gate.py")),
            "--parse-only", "--criterion-dir", str(tmp_path), "--no-color",
        ],
        capture_output=True, text=True, check=False, timeout=10,
    )
    assert result.returncode == 1, result.stdout + result.stderr
