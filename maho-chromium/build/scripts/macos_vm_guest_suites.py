#!/usr/bin/env python3
"""Strict guest-local command contracts for the macOS VM E2E suites."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any
from urllib.parse import urlsplit


LOOPBACK_CDP_HOST = "127.0.0.1"
NODE_MAJOR_VERSION = 24
INTENTIONAL_FAILURE_EXIT_CODE = 1
INTENTIONAL_FAILURE_MARKER = "INTENTIONAL_FAILURE_FIXTURE"

AI_MOCK_SCENARIOS = (
    "ai_panel_renders",
    "ai_real_turn",
    "ai_mock_general_turn",
    "ai_mock_quick_action_context",
    "ai_mock_server_error",
    "ai_mock_key_cleared_no_request",
)

# e2e_chromium_ai.js reads the API key once, before launching Maho. The
# key-cleared scenario therefore needs its own no-key process; combining it
# with the request-producing scenarios would make one side of the suite fail.
_NO_KEY_AI_SCENARIOS = frozenset(("ai_mock_key_cleared_no_request",))
_GTEST_FILTER_OPERATORS = frozenset("*?:-")
_GTEST_SUCCESS_RE = re.compile(r"^\[\s+OK\s+\]\s+([^\s]+)\s+\(\d+ ms\)$")
_AI_RESULT_RE = re.compile(r"^(PASS|FAIL|SKIP)\s+([A-Za-z0-9_]+)(?:\s|$)")
_MOCK_URL_PREFIX = "MOCK_LLM base_url="
_PROFILE_PREFIX = "PROFILE="
_AI_PROFILE_BASENAME_PREFIX = "maho-ai-e2e-"


class SuiteContractError(ValueError):
    """Raised before execution when a suite command would be unsafe or broad."""


class SuiteValidationError(RuntimeError):
    """Raised when a completed guest suite does not satisfy its strict contract."""

    def __init__(self, suite: str, issues: Sequence[str]) -> None:
        self.suite = suite
        self.issues = tuple(issues)
        super().__init__(f"{suite} validation failed: {'; '.join(self.issues)}")


@dataclass(frozen=True)
class GuestCommand:
    """An argv-only guest command plus the artifacts its runner must capture."""

    suite: str
    argv: tuple[str, ...]
    environment: tuple[tuple[str, str], ...]
    stdout_path: str
    stderr_path: str
    expected_results: tuple[str, ...]
    summary_path: str | None = None
    cdp_host: str | None = None
    cdp_port: int | None = None
    required_node_major: int | None = None

    def environment_dict(self) -> dict[str, str]:
        return dict(self.environment)


@dataclass(frozen=True)
class CapturedCommandResult:
    """Captured process output associated with the command that produced it."""

    command: GuestCommand
    returncode: int
    stdout: str
    stderr: str


@dataclass(frozen=True)
class SuiteValidation:
    """Machine-consumable success result returned only after strict validation."""

    suite: str
    expected: tuple[str, ...]
    passed: tuple[str, ...]
    returncodes: tuple[int, ...]
    profile_paths: tuple[str, ...] = ()

    def to_dict(self) -> dict[str, object]:
        receipt: dict[str, object] = {
            "suite": self.suite,
            "expected": list(self.expected),
            "passed": list(self.passed),
            "returncodes": list(self.returncodes),
            "status": "SUCCESS",
        }
        if self.profile_paths:
            receipt["profile_paths"] = list(self.profile_paths)
        return receipt


def _absolute_guest_path(raw_path: str, label: str) -> PurePosixPath:
    if not isinstance(raw_path, str) or not raw_path:
        raise SuiteContractError(f"{label} must be a non-empty absolute guest path")
    path = PurePosixPath(raw_path)
    if not path.is_absolute() or ".." in path.parts:
        raise SuiteContractError(f"{label} must be an absolute guest path without traversal")
    return path


def _validated_gtest_names(test_names: Sequence[str]) -> tuple[str, ...]:
    names = tuple(test_names)
    if not names:
        raise SuiteContractError("interactive suite requires at least one exact gtest name")
    if len(names) != len(set(names)):
        raise SuiteContractError("interactive suite contains duplicate gtest names")
    for name in names:
        if not isinstance(name, str) or not name or "." not in name:
            raise SuiteContractError(f"invalid exact gtest name: {name!r}")
        operator = next((character for character in name if character in _GTEST_FILTER_OPERATORS), None)
        if operator is not None:
            raise SuiteContractError(
                f"gtest name {name!r} contains filter operator {operator!r}"
            )
    return names


def _validated_ai_scenarios(scenarios: Sequence[str]) -> tuple[str, ...]:
    names = tuple(scenarios)
    if len(names) != len(set(names)):
        raise SuiteContractError("AI mock suite contains duplicate scenario names")
    expected = set(AI_MOCK_SCENARIOS)
    actual = set(names)
    if actual != expected or len(names) != len(AI_MOCK_SCENARIOS):
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise SuiteContractError(
            "AI mock suite must contain exactly all six scenarios "
            f"(missing={missing}, extra={extra})"
        )
    return names


def _validated_loopback(host: str, port: int) -> tuple[str, int]:
    if host != LOOPBACK_CDP_HOST:
        raise SuiteContractError(
            f"guest CDP host must be exactly {LOOPBACK_CDP_HOST}, got {host!r}"
        )
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise SuiteContractError(f"guest CDP port must be in 1..65535, got {port!r}")
    return host, port


def _environment(**values: str) -> tuple[tuple[str, str], ...]:
    return tuple(sorted(values.items()))


def build_interactive_command(
    guest_workspace: str,
    guest_run_dir: str,
    test_names: Sequence[str],
    *,
    suite_name: str = "interactive-smoke",
    binary_name: str = "interactive_ui_tests",
) -> GuestCommand:
    """Build the exact zero-retry Chromium interactive test command."""

    if (
        not suite_name
        or "/" in suite_name
        or not binary_name
        or "/" in binary_name
        or binary_name in (".", "..")
        or binary_name.endswith(".runtime_deps")
    ):
        raise SuiteContractError(
            "interactive suite and binary names must be single path segments"
        )
    workspace = _absolute_guest_path(guest_workspace, "guest workspace")
    run_dir = _absolute_guest_path(guest_run_dir, "guest run directory")
    tests = _validated_gtest_names(test_names)
    binary = workspace / "chromium/src/out/Default" / binary_name
    summary = run_dir / "interactive-test-summary.json"
    bin_str = str(binary)
    summary_str = str(summary)
    filter_arg = f"--gtest_filter={':'.join(tests)}"
    summary_arg = f"--test-launcher-summary-output={summary_str}"

    return GuestCommand(
        suite=suite_name,
        argv=(
            bin_str,
            filter_arg,
            "--gtest_fail_if_no_test_selected",
            "--test-launcher-jobs=1",
            "--test-launcher-retry-limit=0",
            summary_arg,
        ),
        environment=(),
        stdout_path=str(run_dir / f"{suite_name}.stdout.log"),
        stderr_path=str(run_dir / f"{suite_name}.stderr.log"),
        summary_path=str(summary),
        expected_results=tests,
    )


def _gtest_summary_issues(
    summary: Mapping[str, Any], expected_tests: tuple[str, ...]
) -> list[str]:
    issues: list[str] = []
    per_iteration_data = summary.get("per_iteration_data")
    if not isinstance(per_iteration_data, list) or len(per_iteration_data) != 1:
        return [
            "per_iteration_data must contain exactly one iteration; "
            "zero iterations and retries are invalid"
        ]

    iteration = per_iteration_data[0]
    if not isinstance(iteration, Mapping):
        return ["the sole per_iteration_data entry must be an object"]

    actual_tests = set(iteration.keys())
    expected_set = set(expected_tests)
    if actual_tests != expected_set:
        issues.append(
            "selected test set mismatch "
            f"(missing={sorted(expected_set - actual_tests)}, "
            f"extra={sorted(actual_tests - expected_set)})"
        )

    for test_name in expected_tests:
        runs = iteration.get(test_name)
        if not isinstance(runs, list) or len(runs) != 1:
            run_count = len(runs) if isinstance(runs, list) else 0
            issues.append(
                f"{test_name} ran {run_count} times; expected exactly one run and no retries"
            )
            continue
        run = runs[0]
        if not isinstance(run, Mapping):
            issues.append(f"{test_name} result must be an object")
            continue
        status = run.get("status")
        if status != "SUCCESS":
            issues.append(f"{test_name} status is {status!r}, expected 'SUCCESS'")
    return issues


def merge_gtest_summary_with_stdout(
    summary: Mapping[str, Any],
    expected_tests: Sequence[str],
    stdout: str,
) -> dict[str, Any]:
    tests = _validated_gtest_names(expected_tests)
    merged = dict(summary)
    per_iteration_data = summary.get("per_iteration_data")
    if not isinstance(per_iteration_data, list) or len(per_iteration_data) != 1:
        return merged
    iteration = per_iteration_data[0]
    if not isinstance(iteration, Mapping):
        return merged

    merged_iteration = dict(iteration)
    missing = set(tests) - set(merged_iteration)
    for line in stdout.splitlines():
        match = _GTEST_SUCCESS_RE.match(line)
        if match and match.group(1) in missing:
            merged_iteration[match.group(1)] = [{"status": "SUCCESS"}]
    merged["per_iteration_data"] = [merged_iteration]
    return merged


def validate_gtest_summary(
    summary: Mapping[str, Any], expected_tests: Sequence[str]
) -> SuiteValidation:
    """Validate an exact one-iteration, one-run-per-test Chromium summary."""

    tests = _validated_gtest_names(expected_tests)
    if not isinstance(summary, Mapping):
        raise SuiteValidationError("interactive-smoke", ("test summary must be an object",))
    issues = _gtest_summary_issues(summary, tests)
    if issues:
        raise SuiteValidationError("interactive-smoke", issues)
    return SuiteValidation("interactive-smoke", tests, tests, ())


def validate_interactive_result(
    result: CapturedCommandResult,
    summary: Mapping[str, Any],
) -> SuiteValidation:
    """Validate both the interactive process result and its launcher summary."""

    command = result.command
    issues: list[str] = []
    if not command.suite or command.summary_path is None:
        issues.append("result is not associated with an interactive test command")
    if result.returncode != 0:
        issues.append(f"interactive process exited with {result.returncode}")
    try:
        tests = _validated_gtest_names(command.expected_results)
    except SuiteContractError as error:
        issues.append(str(error))
        tests = ()
    if not isinstance(summary, Mapping):
        issues.append("test summary must be an object")
    elif tests:
        issues.extend(_gtest_summary_issues(summary, tests))
    if issues:
        raise SuiteValidationError("interactive-smoke", issues)
    return SuiteValidation(
        suite="interactive-smoke",
        expected=tests,
        passed=tests,
        returncodes=(result.returncode,),
    )


def _build_ai_process_command(
    *,
    workspace: PurePosixPath,
    run_dir: PurePosixPath,
    node_binary: str,
    scenarios: tuple[str, ...],
    cdp_host: str,
    cdp_port: int,
    api_key: str,
    log_stem: str,
) -> GuestCommand:
    script = workspace / "maho-chromium/build/scripts/e2e_chromium_ai.js"
    app = workspace / "chromium/src/out/Default/Maho.app"
    scenario_argv = tuple(
        argument
        for scenario in scenarios
        for argument in ("--scenario", scenario)
    )
    return GuestCommand(
        suite="ai-mock",
        argv=(node_binary, str(script), "--mock", *scenario_argv),
        environment=_environment(
            MAHO_APP_PATH=str(app),
            MAHO_E2E_AI_API_KEY=api_key,
            MAHO_E2E_AI_BASE_URL="",
            MAHO_E2E_AI_MODEL="gpt-4o-mini",
            MAHO_E2E_AI_PROVIDER="openai",
            MAHO_E2E_CDP_PORT=str(cdp_port),
            MAHO_E2E_SOURCE_PROFILE=str(run_dir / "empty-source-profile"),
            TMPDIR=str(run_dir / f"{log_stem}-tmp"),
        ),
        stdout_path=str(run_dir / f"{log_stem}.stdout.log"),
        stderr_path=str(run_dir / f"{log_stem}.stderr.log"),
        expected_results=scenarios,
        cdp_host=cdp_host,
        cdp_port=cdp_port,
        required_node_major=NODE_MAJOR_VERSION,
    )


def build_ai_mock_commands(
    guest_workspace: str,
    guest_run_dir: str,
    scenarios: Sequence[str],
    *,
    node_binary: str = "node",
    cdp_host: str = LOOPBACK_CDP_HOST,
    cdp_port: int = 9422,
) -> tuple[GuestCommand, ...]:
    """Build explicit loopback-only commands covering all six mock scenarios.

    The returned commands must be run sequentially. Request-producing scenarios
    run with a fixed mock credential, while the key-cleared scenario runs in a
    separate process with an explicitly empty credential.
    """

    workspace = _absolute_guest_path(guest_workspace, "guest workspace")
    run_dir = _absolute_guest_path(guest_run_dir, "guest run directory")
    names = _validated_ai_scenarios(scenarios)
    host, port = _validated_loopback(cdp_host, cdp_port)
    if not isinstance(node_binary, str) or not node_binary:
        raise SuiteContractError("Node 24 binary must be a non-empty argv element")

    keyed = tuple(name for name in names if name not in _NO_KEY_AI_SCENARIOS)
    no_key = tuple(name for name in names if name in _NO_KEY_AI_SCENARIOS)
    return (
        _build_ai_process_command(
            workspace=workspace,
            run_dir=run_dir,
            node_binary=node_binary,
            scenarios=keyed,
            cdp_host=host,
            cdp_port=port,
            api_key="test-key-mock",
            log_stem="ai-mock",
        ),
        _build_ai_process_command(
            workspace=workspace,
            run_dir=run_dir,
            node_binary=node_binary,
            scenarios=no_key,
            cdp_host=host,
            cdp_port=port,
            api_key="",
            log_stem="ai-mock-no-key",
        ),
    )


def _explicit_scenarios_from_argv(argv: tuple[str, ...]) -> tuple[str, ...] | None:
    if len(argv) < 5 or argv[2] != "--mock":
        return None
    tail = argv[3:]
    if len(tail) % 2 != 0:
        return None
    scenarios: list[str] = []
    for index in range(0, len(tail), 2):
        if tail[index] != "--scenario":
            return None
        scenarios.append(tail[index + 1])
    return tuple(scenarios)


def _validate_mock_url(line: str) -> str | None:
    parsed = urlsplit(line[len(_MOCK_URL_PREFIX) :].strip())
    if parsed.scheme != "http" or parsed.hostname != LOOPBACK_CDP_HOST or parsed.port is None:
        return f"mock LLM endpoint is not loopback-only: {parsed.geturl()!r}"
    return None


def _validated_ai_profile_path(
    raw_path: str, environment: Mapping[str, str], result_index: int
) -> tuple[str | None, str | None]:
    tmpdir_raw = environment.get("TMPDIR", "")
    tmpdir = PurePosixPath(tmpdir_raw)
    profile = PurePosixPath(raw_path)
    if not tmpdir_raw or not tmpdir.is_absolute() or ".." in tmpdir.parts:
        return None, f"AI mock process {result_index} declares an invalid TMPDIR"
    if not raw_path or not profile.is_absolute() or ".." in profile.parts:
        return None, f"AI mock process {result_index} reported a non-absolute profile path"
    if profile.parent != tmpdir:
        return None, (
            f"AI mock process {result_index} profile is not directly underneath its TMPDIR"
        )
    if not profile.name.startswith(_AI_PROFILE_BASENAME_PREFIX):
        return None, (
            f"AI mock process {result_index} profile basename does not start with "
            f"{_AI_PROFILE_BASENAME_PREFIX!r}"
        )
    return str(profile), None


def validate_ai_mock_results(
    results: Sequence[CapturedCommandResult],
    expected_scenarios: Sequence[str] = AI_MOCK_SCENARIOS,
) -> SuiteValidation:
    """Validate exact argv contracts and one PASS result for every AI scenario."""

    expected = _validated_ai_scenarios(expected_scenarios)
    issues: list[str] = []
    commands = tuple(result.command for result in results)
    if not commands:
        issues.append("AI mock suite produced no command results")

    commanded_scenarios: list[str] = []
    passed_scenarios: list[str] = []
    returncodes: list[int] = []
    profile_paths: list[str] = []
    for result_index, result in enumerate(results):
        command = result.command
        returncodes.append(result.returncode)
        if command.suite != "ai-mock":
            issues.append(f"result {result_index} is not associated with an ai-mock command")
        if command.cdp_host != LOOPBACK_CDP_HOST:
            issues.append(
                f"result {result_index} command exposes CDP at {command.cdp_host!r}"
            )
        if command.required_node_major != NODE_MAJOR_VERSION:
            issues.append(
                f"result {result_index} does not require Node {NODE_MAJOR_VERSION}"
            )
        environment = command.environment_dict()
        if environment.get("MAHO_E2E_CDP_PORT") != str(command.cdp_port):
            issues.append(f"result {result_index} has an inconsistent CDP port environment")
        if not environment.get("MAHO_E2E_SOURCE_PROFILE", "").endswith(
            "/empty-source-profile"
        ):
            issues.append(f"result {result_index} could read a real source profile")
        explicit = _explicit_scenarios_from_argv(command.argv)
        if explicit is None or explicit != command.expected_results:
            issues.append(f"result {result_index} does not use exact explicit scenario argv")
            explicit = ()
        commanded_scenarios.extend(explicit)
        if result.returncode != 0:
            issues.append(f"AI mock process {result_index} exited with {result.returncode}")

        mock_url_count = 0
        profile_lines: list[str] = []
        for line in (result.stdout + "\n" + result.stderr).splitlines():
            if line.startswith(_MOCK_URL_PREFIX):
                mock_url_count += 1
                url_issue = _validate_mock_url(line)
                if url_issue:
                    issues.append(url_issue)
            if line.startswith("FATAL"):
                issues.append(f"AI mock process {result_index} reported {line!r}")
            match = _AI_RESULT_RE.match(line)
            if not match:
                continue
            disposition, scenario = match.groups()
            if disposition != "PASS":
                issues.append(f"AI scenario {scenario} reported {disposition}")
            else:
                passed_scenarios.append(scenario)
        for line in result.stdout.splitlines():
            if line.startswith(_PROFILE_PREFIX):
                profile_lines.append(line[len(_PROFILE_PREFIX) :].strip())
        if mock_url_count != 1:
            issues.append(
                f"AI mock process {result_index} reported {mock_url_count} mock endpoints; "
                "expected exactly one"
            )
        if len(profile_lines) != 1:
            issues.append(
                f"AI mock process {result_index} reported {len(profile_lines)} profiles; "
                "expected exactly one"
            )
        else:
            profile_path, profile_issue = _validated_ai_profile_path(
                profile_lines[0], environment, result_index
            )
            if profile_issue:
                issues.append(profile_issue)
            elif profile_path is not None:
                profile_paths.append(profile_path)

    if tuple(commanded_scenarios) != expected:
        issues.append(
            "commanded AI scenario order/set mismatch "
            f"(expected={list(expected)}, actual={commanded_scenarios})"
        )
    if tuple(passed_scenarios) != expected:
        missing = sorted(set(expected) - set(passed_scenarios))
        extra = sorted(set(passed_scenarios) - set(expected))
        duplicates = sorted(
            scenario for scenario in set(passed_scenarios) if passed_scenarios.count(scenario) > 1
        )
        issues.append(
            "AI PASS result mismatch "
            f"(missing={missing}, extra={extra}, duplicates={duplicates}, "
            f"actual={passed_scenarios})"
        )
    if len(profile_paths) != len(set(profile_paths)):
        issues.append("keyed and no-key AI subprocess profiles must differ")
    if issues:
        raise SuiteValidationError("ai-mock", issues)
    return SuiteValidation(
        suite="ai-mock",
        expected=expected,
        passed=tuple(passed_scenarios),
        returncodes=tuple(returncodes),
        profile_paths=tuple(profile_paths),
    )


def build_intentional_failure_command(
    guest_workspace: str,
    guest_run_dir: str,
    *,
    python_binary: str = "python3",
) -> GuestCommand:
    """Build the non-retrying fixture command used by lifecycle cleanup tests."""

    workspace = _absolute_guest_path(guest_workspace, "guest workspace")
    run_dir = _absolute_guest_path(guest_run_dir, "guest run directory")
    if not isinstance(python_binary, str) or not python_binary:
        raise SuiteContractError("Python binary must be a non-empty argv element")
    script = workspace / "maho-chromium/build/scripts/macos_vm_guest_suites.py"
    fixture_dir = run_dir / "intentional-failure-fixture"
    return GuestCommand(
        suite="intentional-failure-fixture",
        argv=(
            python_binary,
            str(script),
            "intentional-failure-fixture",
            "--artifact-dir",
            str(fixture_dir),
        ),
        environment=(),
        stdout_path=str(run_dir / "intentional-failure-fixture.stdout.log"),
        stderr_path=str(run_dir / "intentional-failure-fixture.stderr.log"),
        expected_results=(),
    )


def run_intentional_failure_fixture(artifact_dir: str) -> int:
    """Write deterministic diagnostics, then fail for orchestrator cleanup QA."""

    output_dir = Path(artifact_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    diagnostic = {
        "fixture": "intentional-failure-fixture",
        "intentional": True,
        "exit_code": INTENTIONAL_FAILURE_EXIT_CODE,
    }
    (output_dir / "intentional-failure.json").write_text(
        json.dumps(diagnostic, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"{INTENTIONAL_FAILURE_MARKER}: preserved diagnostic at {output_dir}")
    print(
        f"{INTENTIONAL_FAILURE_MARKER}: exiting nonzero by design",
        file=sys.stderr,
    )
    return INTENTIONAL_FAILURE_EXIT_CODE


def _argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Guest-local suite helpers for the isolated macOS VM runner."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    fixture = subparsers.add_parser(
        "intentional-failure-fixture",
        help="write diagnostics and exit nonzero to exercise orchestrator cleanup",
    )
    fixture.add_argument("--artifact-dir", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _argument_parser().parse_args(argv)
    if args.command == "intentional-failure-fixture":
        return run_intentional_failure_fixture(args.artifact_dir)
    raise AssertionError(f"unhandled command: {args.command}")


if __name__ == "__main__":
    sys.exit(main())
