#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from typing import Callable, Mapping, Sequence

PLAN_PATH = Path(__file__).with_name("macos_vm_matrix_plan.py")
PLAN_SPEC = importlib.util.spec_from_file_location("macos_vm_matrix_plan", PLAN_PATH)
if PLAN_SPEC is None or PLAN_SPEC.loader is None:
    raise RuntimeError(f"unable to load {PLAN_PATH}")
matrix_plan = importlib.util.module_from_spec(PLAN_SPEC)
sys.modules[PLAN_SPEC.name] = matrix_plan
PLAN_SPEC.loader.exec_module(matrix_plan)
VALIDATION_PATH = Path(__file__).with_name("macos_vm_matrix_validation.py")
VALIDATION_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_matrix_validation",
    VALIDATION_PATH,
)
if VALIDATION_SPEC is None or VALIDATION_SPEC.loader is None:
    raise RuntimeError(f"unable to load {VALIDATION_PATH}")
validation = importlib.util.module_from_spec(VALIDATION_SPEC)
sys.modules[VALIDATION_SPEC.name] = validation
VALIDATION_SPEC.loader.exec_module(validation)
Config = matrix_plan.Config
Step = matrix_plan.Step
plan = matrix_plan.plan
verifier_argv = matrix_plan.verifier_argv
PREFIX = re.compile(r"^[a-z0-9][a-z0-9-]*$")


def _safe_before_start(config: Config, steps: Sequence[Step]) -> None:
    existing = [
        step.run_id
        for step in steps
        if step.run_id is not None and (config.root / step.run_id).exists()
    ]
    if config.matrix_dir.exists():
        existing.append(config.matrix_dir.name)
    if existing:
        raise RuntimeError(f"refusing to reuse evidence paths: {', '.join(existing)}")


def _write_atomic(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=path.parent,
        prefix=f".{path.name}.",
        delete=False,
    ) as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")
        temporary = Path(handle.name)
    temporary.replace(path)


def _record(
    config: Config,
    step: Step,
    result: subprocess.CompletedProcess[str],
) -> None:
    config.matrix_dir.mkdir(parents=True, exist_ok=True)
    stem = config.matrix_dir / step.name
    stem.with_suffix(".stdout.log").write_text(result.stdout, encoding="utf-8")
    stem.with_suffix(".stderr.log").write_text(result.stderr, encoding="utf-8")


def _validate_setup_output(
    step: Step,
    completed: subprocess.CompletedProcess[str],
) -> None:
    if step.name not in {"doctor", "list"}:
        return
    try:
        value = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{step.name}: output is not JSON") from error
    if step.name == "doctor":
        if not isinstance(value, dict) or value.get("ok") is not True:
            raise RuntimeError("doctor: output does not report ok")
        return
    if (
        not isinstance(value, dict)
        or set(value.get("suites", ())) != matrix_plan.SUITES
        or set(value.get("ai_scenarios", ())) != matrix_plan.SCENARIOS
    ):
        raise RuntimeError("list: manifest inventory mismatch")


def execute(
    config: Config,
    *,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    environment: Mapping[str, str] | None = None,
) -> dict[str, object]:
    steps = plan(config)
    _safe_before_start(config, steps)
    config.matrix_dir.mkdir(parents=True)
    env = dict(environment or os.environ)
    env["TART_HOME"] = str(config.tart_home)
    outcomes: list[dict[str, object]] = []
    fallback = False
    try:
        for step in steps:
            if step.name == "primary-fallback" and not fallback:
                continue
            completed = runner(
                step.argv,
                cwd=config.workspace,
                env=env,
                text=True,
                capture_output=True,
                check=False,
            )
            _record(config, step, completed)
            actual = completed.returncode
            if step.name == "inventory" and actual == 0:
                config.inventory.write_text(completed.stdout, encoding="utf-8")
            if step.name == "primary" and actual == 3:
                if validation.eligible_primary_failure(
                    config.root,
                    step.run_id or "",
                ):
                    fallback = True
                    outcomes.append({"name": step.name, "exit": actual})
                    continue
            if actual != step.expected_exit:
                raise RuntimeError(
                    f"{step.name}: expected exit {step.expected_exit}, got {actual}"
                )
            _validate_setup_output(step, completed)
            if step.run_id is not None:
                receipt = validation.terminal_receipt(
                    config.root,
                    step.name,
                    step.run_id,
                    actual,
                )
                validation.step_contract(
                    config.root,
                    step.name,
                    step.run_id,
                    receipt,
                )
            outcomes.append({"name": step.name, "exit": actual})
        verifier = runner(
            verifier_argv(config, fallback),
            cwd=config.workspace,
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        verify_step = Step("verify", verifier_argv(config, fallback), 0)
        _record(config, verify_step, verifier)
        if verifier.returncode != 0:
            raise RuntimeError(f"evidence verifier failed: {verifier.stdout.strip()}")
        summary: dict[str, object] = {
            "valid": True,
            "fallback": fallback,
            "outcomes": outcomes,
        }
        _write_atomic(config.matrix_dir / "matrix-receipt.json", summary)
        return summary
    except BaseException as error:
        _write_atomic(
            config.matrix_dir / "matrix-receipt.json",
            {
                "valid": False,
                "fallback": fallback,
                "outcomes": outcomes,
                "error": str(error),
            },
        )
        raise


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--identity", type=Path, required=True)
    parser.add_argument("--golden", default="maho-e2e-golden")
    parser.add_argument("--tart-home", type=Path, required=True)
    parser.add_argument(
        "--prefix",
        default=time.strftime("live-%Y%m%dT%H%M%SZ", time.gmtime()),
    )
    parser.add_argument("--dry-run", action="store_true")
    return parser


def _public_argv(argv: Sequence[str]) -> list[str]:
    values = list(argv)
    if "--identity" in values:
        values[values.index("--identity") + 1] = "<identity>"
    return values


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if not PREFIX.fullmatch(args.prefix):
        print("invalid --prefix: use lowercase letters, digits, and hyphens", file=sys.stderr)
        return 2
    config = Config(
        workspace=args.workspace.resolve(),
        state_dir=args.state_dir.expanduser().resolve(),
        identity=args.identity.expanduser().resolve(),
        golden=args.golden,
        tart_home=args.tart_home.expanduser().resolve(),
        prefix=args.prefix,
    )
    if args.dry_run:
        print(
            json.dumps(
                {
                    "steps": [
                        {
                            "name": step.name,
                            "argv": _public_argv(step.argv),
                            "expected_exit": step.expected_exit,
                            "run_id": step.run_id,
                        }
                        for step in plan(config)
                    ],
                    "verifier": list(verifier_argv(config, False)),
                },
                indent=2,
            )
        )
        return 0
    try:
        result = execute(config)
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
