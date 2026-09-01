#!/usr/bin/env python3
"""Manifest boundary and inventory CLI for isolated macOS VM E2E tests."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import ipaddress
import json
from pathlib import Path, PurePosixPath
import re
import socket
import subprocess
import sys
from typing import Any, Callable, ClassVar, Mapping, Sequence


_SCRIPT_DIR = Path(__file__).resolve().parent
_DEFAULT_MANIFEST_PATH = _SCRIPT_DIR / "macos_vm_e2e_manifest.json"
_APPROVED_BASE_IMAGE = "ghcr.io/cirruslabs/macos-tahoe-base:latest"
_APPROVED_GOLDEN_IMAGE = "maho-e2e-golden"
_APPROVED_GUEST_WORKSPACE = "/Users/Shared/maho-workspace"
_APPROVED_OUTPUT_DIR = "out/Default"
_REQUIRED_INTERACTIVE_TEST = (
    "MahoAiSidePanelLifecycleTest.ShowPanel_EntryIsShowing"
)
_AI_SCENARIO_COUNT = 6
_NAME_PATTERN = re.compile(r"^[a-z][a-z0-9-]*$")


class MacosVmE2eError(Exception):
    code: ClassVar[str]

    def as_dict(self) -> dict[str, dict[str, str]]:
        return {"error": {"code": self.code, "message": str(self)}}


class InvalidManifestError(MacosVmE2eError):
    code = "invalid_manifest"


class UnknownSuiteError(MacosVmE2eError):
    code = "unknown_suite"

    def __init__(self, name: str, available: Sequence[str]) -> None:
        super().__init__(
            f"unknown suite {name!r}; available suites: {', '.join(available)}"
        )


@dataclass(frozen=True)
class GtestSuite:
    name: str
    gtest_filter: tuple[str, ...]
    binary: str = "interactive_ui_tests"
    host_relay: bool = False
    kind: ClassVar[str] = "gtest"


@dataclass(frozen=True)
class AiMockSuite:
    name: str
    scenarios: tuple[str, ...]
    kind: ClassVar[str] = "ai_mock"


@dataclass(frozen=True)
class AggregateSuite:
    name: str
    members: tuple[str, ...]
    kind: ClassVar[str] = "aggregate"


Suite = GtestSuite | AiMockSuite | AggregateSuite


@dataclass(frozen=True)
class Manifest:
    base_image: str
    golden_image: str
    guest_workspace: Path
    output_dir: Path
    cdp_address: str
    suites: tuple[Suite, ...]

    @property
    def suite_names(self) -> tuple[str, ...]:
        return tuple(suite.name for suite in self.suites)

    @property
    def ai_scenarios(self) -> tuple[str, ...]:
        return tuple(
            scenario
            for suite in self.suites
            if isinstance(suite, AiMockSuite)
            for scenario in suite.scenarios
        )

    def suite(self, name: str) -> Suite:
        for suite in self.suites:
            if suite.name == name:
                return suite
        raise UnknownSuiteError(name, self.suite_names)

    def inventory(self) -> dict[str, list[str]]:
        return {
            "suites": list(self.suite_names),
            "ai_scenarios": list(self.ai_scenarios),
        }


def _invalid(location: str, message: str) -> InvalidManifestError:
    return InvalidManifestError(f"{location}: {message}")


def _reject_duplicate_json_keys(
    pairs: list[tuple[str, Any]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _invalid("manifest", f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _expect_object(value: object, location: str) -> Mapping[str, object]:
    if not isinstance(value, dict):
        raise _invalid(location, "must be an object")
    return value


def _expect_exact_keys(
    value: Mapping[str, object],
    expected: set[str],
    location: str,
) -> None:
    actual = set(value)
    missing = sorted(expected - actual)
    unknown = sorted(actual - expected)
    if missing:
        raise _invalid(location, f"missing keys: {', '.join(missing)}")
    if unknown:
        raise _invalid(location, f"unknown keys: {', '.join(unknown)}")


def _expect_keys(
    value: Mapping[str, object],
    *,
    required: set[str],
    optional: set[str],
    location: str,
) -> None:
    actual = set(value)
    missing = sorted(required - actual)
    unknown = sorted(actual - required - optional)
    if missing:
        raise _invalid(location, f"missing keys: {', '.join(missing)}")
    if unknown:
        raise _invalid(location, f"unknown keys: {', '.join(unknown)}")


def _parse_binary_name(value: object, location: str) -> str:
    binary = _expect_string(value, location)
    if (
        "/" in binary
        or binary in (".", "..")
        or binary.endswith(".runtime_deps")
        or not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.+-]*", binary)
    ):
        raise _invalid(location, f"invalid gtest binary name {binary!r}")
    return binary


def _expect_string(value: object, location: str) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise _invalid(location, "must be a non-empty string without outer whitespace")
    return value


def _expect_string_list(value: object, location: str) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise _invalid(location, "must be an array")
    return tuple(
        _expect_string(item, f"{location}[{index}]")
        for index, item in enumerate(value)
    )


def _reject_duplicates(values: Sequence[str], location: str) -> None:
    seen: set[str] = set()
    for value in values:
        if value in seen:
            raise _invalid(location, f"duplicate value {value!r}")
        seen.add(value)


def _validate_path_text(value: str, location: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if "\\" in value or ".." in path.parts:
        raise _invalid(location, "path traversal is not allowed")
    return path


def _parse_guest_workspace(value: object) -> Path:
    raw = _expect_string(value, "guest_workspace")
    path = _validate_path_text(raw, "guest_workspace")
    if not path.is_absolute():
        raise _invalid("guest_workspace", "must be an absolute guest path")
    if raw != _APPROVED_GUEST_WORKSPACE:
        raise _invalid(
            "guest_workspace",
            f"must be exactly {_APPROVED_GUEST_WORKSPACE!r}",
        )
    return Path(raw)


def _parse_output_dir(value: object) -> Path:
    raw = _expect_string(value, "output_dir")
    path = _validate_path_text(raw, "output_dir")
    if path.is_absolute():
        raise _invalid("output_dir", "must be relative to chromium/src")
    if raw != _APPROVED_OUTPUT_DIR:
        raise _invalid("output_dir", f"must be exactly {_APPROVED_OUTPUT_DIR!r}")
    return Path(raw)


def _parse_cdp_address(value: object) -> str:
    raw = _expect_string(value, "cdp_address")
    try:
        address = ipaddress.ip_address(raw)
    except ValueError as error:
        raise _invalid("cdp_address", "must be a loopback IP address") from error
    if not address.is_loopback:
        raise _invalid("cdp_address", "must be a loopback IP address")
    return raw


def _parse_suite_name(value: object, location: str) -> str:
    name = _expect_string(value, location)
    if not _NAME_PATTERN.fullmatch(name):
        raise _invalid(location, "must use lowercase letters, digits, and hyphens")
    return name


def _parse_gtest_filter(value: object, location: str) -> tuple[str, ...]:
    tests = _expect_string_list(value, location)
    if not tests:
        raise _invalid(location, "must contain at least one exact test name")
    _reject_duplicates(tests, location)
    for test in tests:
        if any(character in test for character in "*?[]:") or "." not in test:
            raise _invalid(
                location,
                f"{test!r} is not an exact gtest name; wildcards are forbidden",
            )
        if any(character.isspace() for character in test):
            raise _invalid(location, f"{test!r} must not contain whitespace")
    return tests


def _parse_suites(value: object) -> tuple[Suite, ...]:
    if not isinstance(value, list) or not value:
        raise _invalid("suites", "must be a non-empty array")

    suites: list[Suite] = []
    names: list[str] = []
    all_ai_scenarios: list[str] = []
    all_gtests: list[str] = []
    for index, raw_suite in enumerate(value):
        location = f"suites[{index}]"
        suite_data = _expect_object(raw_suite, location)
        if "kind" not in suite_data:
            raise _invalid(location, "missing keys: kind")
        kind = _expect_string(suite_data["kind"], f"{location}.kind")
        if kind == GtestSuite.kind:
            _expect_keys(
                suite_data,
                required={"name", "kind", "gtest_filter"},
                optional={"binary", "host_relay"},
                location=location,
            )
            name = _parse_suite_name(suite_data["name"], f"{location}.name")
            tests = _parse_gtest_filter(
                suite_data["gtest_filter"],
                f"{location}.gtest_filter",
            )
            binary = (
                _parse_binary_name(suite_data["binary"], f"{location}.binary")
                if "binary" in suite_data
                else GtestSuite.binary
            )
            host_relay = suite_data.get("host_relay", False)
            if not isinstance(host_relay, bool):
                raise _invalid(location, "host_relay must be a boolean")
            suites.append(
                GtestSuite(
                    name=name,
                    gtest_filter=tests,
                    binary=binary,
                    host_relay=host_relay,
                )
            )
            all_gtests.extend(tests)
        elif kind == AiMockSuite.kind:
            _expect_exact_keys(
                suite_data,
                {"name", "kind", "scenarios"},
                location,
            )
            name = _parse_suite_name(suite_data["name"], f"{location}.name")
            scenarios = _expect_string_list(
                suite_data["scenarios"],
                f"{location}.scenarios",
            )
            if not scenarios:
                raise _invalid(f"{location}.scenarios", "must not be empty")
            _reject_duplicates(scenarios, f"{location}.scenarios")
            for scenario in scenarios:
                if not re.fullmatch(r"[a-z][a-z0-9_]*", scenario):
                    raise _invalid(
                        f"{location}.scenarios",
                        f"invalid scenario name {scenario!r}",
                    )
            suites.append(AiMockSuite(name=name, scenarios=scenarios))
            all_ai_scenarios.extend(scenarios)
        elif kind == AggregateSuite.kind:
            _expect_exact_keys(
                suite_data,
                {"name", "kind", "members"},
                location,
            )
            name = _parse_suite_name(suite_data["name"], f"{location}.name")
            members = _expect_string_list(
                suite_data["members"],
                f"{location}.members",
            )
            if not members:
                raise _invalid(f"{location}.members", "must not be empty")
            _reject_duplicates(members, f"{location}.members")
            suites.append(AggregateSuite(name=name, members=members))
        else:
            raise _invalid(f"{location}.kind", f"unknown suite kind {kind!r}")
        names.append(name)

    _reject_duplicates(names, "suites")
    _reject_duplicates(all_ai_scenarios, "AI scenarios")
    if len(all_ai_scenarios) != _AI_SCENARIO_COUNT:
        raise _invalid(
            "AI scenarios",
            f"must contain exactly {_AI_SCENARIO_COUNT} scenarios",
        )
    if _REQUIRED_INTERACTIVE_TEST not in all_gtests:
        raise _invalid(
            "gtest_filter",
            f"must include {_REQUIRED_INTERACTIVE_TEST!r}",
        )
    _validate_aggregate_members(suites)
    return tuple(suites)


def _validate_aggregate_members(suites: Sequence[Suite]) -> None:
    by_name = {suite.name: suite for suite in suites}
    for suite in suites:
        if not isinstance(suite, AggregateSuite):
            continue
        for member in suite.members:
            if member not in by_name:
                raise _invalid(
                    f"suite {suite.name!r}",
                    f"unknown member suite {member!r}",
                )

    def visit(name: str, active: set[str], complete: set[str]) -> None:
        if name in complete:
            return
        if name in active:
            raise _invalid("suites", f"aggregate cycle includes {name!r}")
        active.add(name)
        suite = by_name[name]
        if isinstance(suite, AggregateSuite):
            for member in suite.members:
                visit(member, active, complete)
        active.remove(name)
        complete.add(name)

    complete: set[str] = set()
    for suite_name in by_name:
        visit(suite_name, set(), complete)


def parse_manifest(data: object) -> Manifest:
    root = _expect_object(data, "manifest")
    _expect_exact_keys(
        root,
        {
            "version",
            "base_image",
            "golden_image",
            "guest_workspace",
            "output_dir",
            "cdp_address",
            "suites",
        },
        "manifest",
    )
    if type(root["version"]) is not int or root["version"] != 1:
        raise _invalid("version", "must be integer 1")

    base_image = _expect_string(root["base_image"], "base_image")
    if base_image != _APPROVED_BASE_IMAGE:
        raise _invalid("base_image", f"must be exactly {_APPROVED_BASE_IMAGE!r}")
    golden_image = _expect_string(root["golden_image"], "golden_image")
    if golden_image != _APPROVED_GOLDEN_IMAGE:
        raise _invalid(
            "golden_image",
            f"must be exactly {_APPROVED_GOLDEN_IMAGE!r}",
        )

    return Manifest(
        base_image=base_image,
        golden_image=golden_image,
        guest_workspace=_parse_guest_workspace(root["guest_workspace"]),
        output_dir=_parse_output_dir(root["output_dir"]),
        cdp_address=_parse_cdp_address(root["cdp_address"]),
        suites=_parse_suites(root["suites"]),
    )


def load_manifest(path: Path | str = _DEFAULT_MANIFEST_PATH) -> Manifest:
    manifest_path = Path(path)
    try:
        text = manifest_path.read_text(encoding="utf-8")
        data = json.loads(text, object_pairs_hook=_reject_duplicate_json_keys)
    except InvalidManifestError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise _invalid(str(manifest_path), str(error)) from error
    return parse_manifest(data)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Manage isolated macOS VM E2E suite inventory."
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=_DEFAULT_MANIFEST_PATH,
        help=argparse.SUPPRESS,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    list_parser = subparsers.add_parser("list", help="List manifest suites and AI scenarios.")
    list_parser.add_argument(
        "--manifest",
        dest="command_manifest",
        type=Path,
        help=argparse.SUPPRESS,
    )
    list_parser.add_argument("--json", action="store_true", help="Print JSON.")
    return parser.parse_args(argv)


def _print_error(error: MacosVmE2eError, json_output: bool) -> None:
    if json_output:
        print(json.dumps(error.as_dict(), sort_keys=True), file=sys.stderr)
    else:
        print(f"{error.code}: {error}", file=sys.stderr)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    manifest_path = args.command_manifest or args.manifest
    try:
        manifest = load_manifest(manifest_path)
    except MacosVmE2eError as error:
        _print_error(error, args.json)
        return 2

    inventory = manifest.inventory()
    if args.json:
        print(json.dumps(inventory, indent=2))
    else:
        for name in inventory["suites"]:
            print(name)
        for name in inventory["ai_scenarios"]:
            print(name)
    return 0

import contextlib as _contextlib
import dataclasses as _dataclasses
import os as _os
import platform as _platform
import shlex as _shlex
import shutil as _shutil
import signal as _signal
import subprocess as _subprocess
import tempfile as _tempfile
import threading as _threading
import time as _time
from typing import IO as _IO
import uuid as _uuid
from collections.abc import Callable as _Callable

from macos_vm_build_transport import (
    APP_BUNDLE as _APP_BUNDLE,
    INTERACTIVE_BINARY as _INTERACTIVE_BINARY,
    RUNTIME_DEPS_FILE as _RUNTIME_DEPS_FILE,
    canonical_build_argv as _canonical_build_argv,
    inspect_preflight as _inspect_preflight,
    plan_rsync_fallback as _plan_rsync_fallback,
    preflight_command_plan as _preflight_command_plan,
    read_only_mount_contract as _read_only_mount_contract,
)
from macos_vm_guest_suites import (
    CapturedCommandResult as _CapturedCommandResult,
    GuestCommand as _GuestCommand,
    SuiteValidationError as _SuiteValidationError,
    build_ai_mock_commands as _build_ai_mock_commands,
    build_intentional_failure_command as _build_intentional_failure_command,
    build_interactive_command as _build_interactive_command,
    merge_gtest_summary_with_stdout as _merge_gtest_summary_with_stdout,
    validate_ai_mock_results as _validate_ai_mock_results,
    validate_interactive_result as _validate_interactive_result,
)
from macos_vm_isolation import (
    IsolationSnapshot as _HostIsolationSnapshot,
    capture_snapshot as _capture_snapshot,
    compare_early_exit_snapshots as _compare_early_exit_snapshots,
    compare_run_snapshots as _compare_run_snapshots,
)
from macos_vm_runtime import (
    AtomicReceiptWriter as _AtomicReceiptWriter,
    CancellationToken as _CancellationToken,
    CommandResult as _RuntimeCommandResult,
    CommandStatus as _CommandStatus,
    IsolationSnapshot as _ReceiptIsolationSnapshot,
    RunReceipt as _RunReceipt,
    SubprocessArgvRunner as _SubprocessArgvRunner,
    SuiteResult as _ReceiptSuiteResult,
    TeardownResult as _TeardownResult,
    TerminalStatus as _TerminalStatus,
    utc_timestamp as _utc_timestamp,
)
from macos_vm_tart import (
    PreparationState as _PreparationState,
    SubprocessRunner as _TartSubprocessRunner,
    TartAdapterError as _TartAdapterError,
    TartConfig as _TartConfig,
    doctor as _tart_doctor,
    key_only_ssh_command as _key_only_ssh_command,
    load_state as _load_preparation_state,
    prepare as _tart_prepare,
)


_OWNERSHIP_FILE = "ownership.json"
_INTENTIONAL_FAILURE_SUITE = "intentional-failure-fixture"
_DEFAULT_CDP_PORT = 9422
_DEFAULT_BUILD_OUTPUT = Path(_APPROVED_OUTPUT_DIR)
_GUEST_RUN_ROOT = PurePosixPath("/Users/admin/maho-e2e-runs")
_WINDOW_PROBE = _SCRIPT_DIR / "macos_vm_window_probe.swift"


class LifecycleError(RuntimeError):
    """An operator-facing lifecycle error."""


class _StageFailure(LifecycleError):
    def __init__(self, status: _TerminalStatus, message: str) -> None:
        super().__init__(message)
        self.status = status


@_dataclasses.dataclass(frozen=True)
class OwnedClone:
    name: str
    directory: Path
    golden: str
    token: str

    @property
    def receipt(self) -> Path:
        return self.directory / _OWNERSHIP_FILE


@_dataclasses.dataclass
class _RunningVm:
    process: _subprocess.Popen[str]
    log_handle: _IO[str]


@_dataclasses.dataclass
class LifecycleServices:
    runner: _SubprocessArgvRunner = _dataclasses.field(
        default_factory=lambda: _SubprocessArgvRunner()
    )
    snapshot: _Callable[..., _HostIsolationSnapshot] = _capture_snapshot
    popen_factory: _Callable[..., _subprocess.Popen[str]] = _subprocess.Popen
    tart_runner: _TartSubprocessRunner = _dataclasses.field(
        default_factory=_TartSubprocessRunner
    )
    command_results: list[_RuntimeCommandResult] = _dataclasses.field(
        default_factory=list
    )


def _workspace_root(args: argparse.Namespace | None = None) -> Path:
    configured = getattr(args, "workspace", None) if args is not None else None
    return Path(configured or _SCRIPT_DIR.parents[2]).resolve()


def _state_root(value: Path | str | None = None) -> Path:
    configured = value or _os.environ.get("MAHO_MACOS_VM_STATE")
    return Path(
        configured or (Path.home() / ".cache" / "maho-macos-vm")
    ).expanduser()


def _atomic_json(path: Path, value: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = _tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        _os.fchmod(descriptor, 0o600)
        with _os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, sort_keys=True)
            output.write("\n")
            output.flush()
            _os.fsync(output.fileno())
        _os.replace(temporary, path)
    finally:
        with _contextlib.suppress(FileNotFoundError):
            temporary.unlink()


def _deadline(runner: _SubprocessArgvRunner, seconds: float) -> float:
    return runner.deadline_after(seconds)


def _run_command(
    services: LifecycleServices,
    argv: Sequence[str],
    *,
    status: _TerminalStatus,
    description: str,
    timeout: float,
    cwd: Path | None = None,
    environment: Mapping[str, str] | None = None,
    cancellation: _CancellationToken | None = None,
    on_process_started: _Callable[[int], None] | None = None,
    required: bool = True,
) -> _RuntimeCommandResult:
    result = services.runner.run(
        argv,
        deadline=_deadline(services.runner, timeout),
        cwd=cwd,
        environment=environment,
        cancellation=cancellation,
        on_process_started=on_process_started,
    )
    services.command_results.append(result)
    if result.status is _CommandStatus.CANCELLED:
        raise _StageFailure(_TerminalStatus.CANCELLED, result.error or "cancelled")
    if required and not result.succeeded:
        detail = result.stderr.strip() or result.stdout.strip() or result.error or "failed"
        raise _StageFailure(status, f"{description}: {detail}")
    return result


def _tart_config(
    manifest: Manifest,
    args: argparse.Namespace | None,
    *,
    create_key: bool = False,
) -> _TartConfig:
    state = _state_root(getattr(args, "state_dir", None) if args else None)
    state.mkdir(parents=True, exist_ok=True)
    identity_value = getattr(args, "identity", None) if args else None
    private_key = (
        Path(identity_value).expanduser() if identity_value else state / "id_ed25519"
    )
    public_key = Path(f"{private_key}.pub")
    if create_key and (not private_key.exists() or not public_key.exists()):
        completed = _subprocess.run(
            ("ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(private_key)),
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise LifecycleError(completed.stderr.strip() or "ssh-keygen failed")
        private_key.chmod(0o600)
    return _TartConfig(
        base_image=manifest.base_image,
        golden_image=getattr(args, "golden", None) or manifest.golden_image,
        preparation_image=f"{getattr(args, 'golden', None) or manifest.golden_image}-preparing",
        state_path=state / "preparation-state.json",
        private_key=private_key,
        public_key=public_key,
    )


def _source_image_identity(config: _TartConfig) -> Mapping[str, object]:
    if not config.state_path.exists():
        return {"base_image": config.base_image, "golden_image": config.golden_image}
    try:
        state: _PreparationState = _load_preparation_state(config.state_path)
    except Exception:
        return {"base_image": config.base_image, "golden_image": config.golden_image}
    return {
        "base_image": state.base_image,
        "golden_image": state.golden_image,
        "source_identity": state.source_identity,
    }


def _parse_gtest_list(output: str) -> tuple[str, ...]:
    suite = ""
    tests: list[str] = []
    for raw_line in output.splitlines():
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        if not raw_line.startswith((" ", "\t")):
            suite = raw_line.split("#", 1)[0].strip()
            continue
        test = raw_line.split("#", 1)[0].strip()
        if suite and test:
            tests.append(f"{suite}{test}")
    return tuple(tests)


def _required_gtests(
    manifest: Manifest, *, selected_gtest: str | None = None
) -> tuple[str, ...]:
    if selected_gtest is not None:
        return (selected_gtest,)
    return tuple(
        test
        for suite in manifest.suites
        if isinstance(suite, GtestSuite)
        for test in suite.gtest_filter
    )


def _required_gtests_by_binary(
    manifest: Manifest, *, selected_gtest: str | None = None
) -> dict[str, tuple[str, ...]]:
    grouped: dict[str, list[str]] = {}
    for suite in manifest.suites:
        if not isinstance(suite, GtestSuite):
            continue
        if selected_gtest is not None and selected_gtest not in suite.gtest_filter:
            continue
        tests = (
            (selected_gtest,)
            if selected_gtest is not None
            else suite.gtest_filter
        )
        grouped.setdefault(suite.binary, []).extend(tests)
    return {binary: tuple(tests) for binary, tests in grouped.items()}


@_dataclasses.dataclass(frozen=True)
class _BinaryPreflightObservation:
    binary: str
    architecture: str
    binary_signature_valid: bool
    list_tests_returncode: int | None
    listed_tests: tuple[str, ...]
    required_tests: tuple[str, ...]
    ready: bool
    issues: tuple[str, ...]
    missing_paths: tuple[str, ...]
    stale_paths: tuple[str, ...]

    def to_json(self) -> dict[str, object]:
        return {
            "binary": self.binary,
            "architecture": self.architecture,
            "binary_signature_valid": self.binary_signature_valid,
            "list_tests_returncode": self.list_tests_returncode,
            "listed_tests": list(self.listed_tests),
            "required_tests": list(self.required_tests),
            "ready": self.ready,
            "issues": list(self.issues),
            "missing_paths": list(self.missing_paths),
            "stale_paths": list(self.stale_paths),
        }


def _codesign_host_artifacts(
    output_root: Path,
    binaries: dict[str, tuple[str, ...]],
) -> None:
    """Ad-hoc re-sign the app bundle and every suite binary that exists."""
    app_bundle = output_root / "Maho.app"
    if app_bundle.exists():
        _subprocess.run(
            ("/usr/bin/codesign", "--force", "--deep", "-s", "-", app_bundle.as_posix()),
            timeout=120,
            check=False,
            capture_output=True,
        )
    for binary_name in binaries:
        binary = output_root / binary_name
        if binary.exists():
            _subprocess.run(
                ("/usr/bin/codesign", "--force", "-s", "-", binary.as_posix()),
                timeout=120,
                check=False,
                capture_output=True,
            )


def _run_per_binary_preflight(
    workspace: Path,
    binaries: dict[str, tuple[str, ...]],
    services: LifecycleServices,
    cancellation: _CancellationToken,
) -> tuple[bool, list[_BinaryPreflightObservation], tuple[Path, ...]]:
    """Run architecture/signature/inventory checks for each suite binary."""
    chromium_src = workspace / "chromium/src"
    app_plan = _preflight_command_plan(chromium_src, _DEFAULT_BUILD_OUTPUT)
    app_signature = _run_command(
        services,
        app_plan.app_signature_argv,
        status=_TerminalStatus.BUILD_FAILED,
        description="Maho.app signature check failed",
        timeout=120,
        cancellation=cancellation,
    )
    observations: list[_BinaryPreflightObservation] = []
    dependencies: list[Path] = []
    seen: set[Path] = set()
    for binary_name, required in binaries.items():
        plan = _preflight_command_plan(
            chromium_src, _DEFAULT_BUILD_OUTPUT, binary_name=binary_name
        )
        architecture = _run_command(
            services,
            plan.architecture_argv,
            status=_TerminalStatus.BUILD_FAILED,
            description="interactive binary architecture check failed",
            timeout=60,
            cancellation=cancellation,
        )
        binary_signature = _run_command(
            services,
            plan.binary_signature_argv,
            status=_TerminalStatus.BUILD_FAILED,
            description="interactive binary signature check failed",
            timeout=120,
            cancellation=cancellation,
        )
        listed = _run_command(
            services,
            plan.list_tests_argv,
            status=_TerminalStatus.BUILD_FAILED,
            description="gtest inventory failed",
            timeout=300,
            cancellation=cancellation,
        )
        preflight = _inspect_preflight(
            workspace_root=workspace,
            output_dir=_DEFAULT_BUILD_OUTPUT,
            binary_name=binary_name,
            required_tests=required,
            listed_tests=_parse_gtest_list(listed.stdout),
            observed_architecture=architecture.stdout,
            app_signature_valid=app_signature.succeeded,
            binary_signature_valid=binary_signature.succeeded,
            list_tests_exit_code=listed.returncode,
        )
        observations.append(
            _BinaryPreflightObservation(
                binary=binary_name,
                architecture=architecture.stdout.strip(),
                binary_signature_valid=binary_signature.succeeded,
                list_tests_returncode=listed.returncode,
                listed_tests=_parse_gtest_list(listed.stdout),
                required_tests=required,
                ready=preflight.ready,
                issues=preflight.issues,
                missing_paths=tuple(
                    str(path) for path in preflight.missing_paths
                ),
                stale_paths=tuple(
                    str(path) for path in preflight.stale_paths
                ),
            )
        )
        for dependency in preflight.dependencies:
            if dependency not in seen:
                seen.add(dependency)
                dependencies.append(dependency)
    return app_signature.succeeded, observations, tuple(dependencies)


def _preflight_blockers(
    observations: list[_BinaryPreflightObservation],
) -> list[str]:
    issues: list[str] = []
    for observation in observations:
        issues.extend(observation.issues)
        issues.extend(
            f"missing: {path}" for path in observation.missing_paths
        )
        issues.extend(f"stale: {path}" for path in observation.stale_paths)
    return issues


def _run_build_and_preflight(
    workspace: Path,
    manifest: Manifest,
    services: LifecycleServices,
    cancellation: _CancellationToken,
    run_directory: Path,
    selected_gtest: str | None = None,
) -> tuple[Path, ...]:
    binaries = _required_gtests_by_binary(
        manifest, selected_gtest=selected_gtest
    )
    build_argv = _canonical_build_argv(workspace, ninja_targets=tuple(binaries))
    build_result = _run_command(
        services,
        build_argv,
        status=_TerminalStatus.BUILD_FAILED,
        description="canonical Default build failed",
        timeout=4 * 60 * 60,
        cwd=workspace,
        cancellation=cancellation,
        required=False,
    )
    (run_directory / "build.stdout.log").write_text(
        build_result.stdout, encoding="utf-8"
    )
    (run_directory / "build.stderr.log").write_text(
        build_result.stderr, encoding="utf-8"
    )
    if not build_result.succeeded:
        detail = (
            build_result.stderr.strip()
            or build_result.stdout.strip()
            or build_result.error
            or "failed"
        )
        raise _StageFailure(
            _TerminalStatus.BUILD_FAILED,
            f"canonical Default build failed: {detail}",
        )
    output_root = workspace / "chromium/src" / _DEFAULT_BUILD_OUTPUT
    if cancellation.is_cancelled:
        raise _StageFailure(_TerminalStatus.CANCELLED, cancellation.reason)
    _codesign_host_artifacts(output_root, binaries)
    app_signature_valid, observations, dependencies = _run_per_binary_preflight(
        workspace, binaries, services, cancellation
    )
    blockers = _preflight_blockers(observations)
    if blockers:
        raise _StageFailure(_TerminalStatus.BUILD_FAILED, "; ".join(blockers))
    _atomic_json(
        run_directory / "build-preflight.json",
        {
            "schema_version": 1,
            "canonical_build_argv": list(build_argv),
            "build_returncode": build_result.returncode,
            "output_root": str(output_root),
            "artifacts": {
                "app_bundle": str(output_root / _APP_BUNDLE),
                "interactive_binary": str(
                    output_root / _INTERACTIVE_BINARY
                ),
                "runtime_deps": str(
                    output_root / _RUNTIME_DEPS_FILE
                ),
                "suite_binaries": {
                    binary: {
                        "binary": str(output_root / binary),
                        "runtime_deps": str(
                            output_root / f"{binary}.runtime_deps"
                        ),
                    }
                    for binary in binaries
                },
            },
            "app_signature_valid": app_signature_valid,
            "preflight_binaries": [
                observation.to_json() for observation in observations
            ],
            "required_tests": list(_required_gtests(manifest)),
        },
    )
    return dependencies


def _preflight_existing_build(
    workspace: Path,
    manifest: Manifest,
    services: LifecycleServices,
    cancellation: _CancellationToken,
    run_directory: Path,
    selected_gtest: str | None = None,
) -> tuple[Path, ...]:
    binaries = _required_gtests_by_binary(
        manifest, selected_gtest=selected_gtest
    )
    output_root = workspace / "chromium/src" / _DEFAULT_BUILD_OUTPUT
    if cancellation.is_cancelled:
        raise _StageFailure(_TerminalStatus.CANCELLED, cancellation.reason)
    _codesign_host_artifacts(output_root, binaries)
    app_signature_valid, observations, dependencies = _run_per_binary_preflight(
        workspace, binaries, services, cancellation
    )
    blockers = _preflight_blockers(observations)
    if blockers:
        raise _StageFailure(_TerminalStatus.BUILD_FAILED, "; ".join(blockers))
    _atomic_json(
        run_directory / "build-preflight.json",
        {
            "schema_version": 1,
            "build_mode": "prebuilt",
            "output_root": str(output_root),
            "artifacts": {
                "app_bundle": str(output_root / _APP_BUNDLE),
                "interactive_binary": str(
                    output_root / _INTERACTIVE_BINARY
                ),
                "runtime_deps": str(
                    output_root / _RUNTIME_DEPS_FILE
                ),
                "suite_binaries": {
                    binary: {
                        "binary": str(output_root / binary),
                        "runtime_deps": str(
                            output_root / f"{binary}.runtime_deps"
                        ),
                    }
                    for binary in binaries
                },
            },
            "app_signature_valid": app_signature_valid,
            "preflight_binaries": [
                observation.to_json() for observation in observations
            ],
            "required_tests": list(_required_gtests(manifest)),
        },
    )
    return dependencies


def _new_clone(
    golden: str,
    state_root: Path,
    services: LifecycleServices,
    cancellation: _CancellationToken,
) -> OwnedClone:
    token = _uuid.uuid4().hex
    name = f"maho-e2e-{token[:16]}"
    clone = OwnedClone(name, state_root / "clones" / name, golden, token)
    clone.directory.mkdir(parents=True, exist_ok=False)
    _atomic_json(
        clone.receipt,
        {
            "version": 1,
            "name": name,
            "golden": golden,
            "token": token,
            "owner_pid": _os.getpid(),
            "created_at": _utc_timestamp(),
        },
    )
    try:
        _run_command(
            services,
            ("tart", "clone", golden, name),
            status=_TerminalStatus.VM_START_FAILED,
            description="ephemeral clone failed",
            timeout=1800,
            cancellation=cancellation,
        )
    except BaseException:
        _shutil.rmtree(clone.directory, ignore_errors=True)
        raise
    return clone


def _is_owned(clone: OwnedClone) -> bool:
    try:
        receipt = json.loads(clone.receipt.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    return receipt.get("name") == clone.name and receipt.get("token") == clone.token


def _external_chromium_target(workspace: Path) -> Path | None:
    chrom_link = workspace / "chromium"
    if not chrom_link.is_symlink():
        return None
    try:
        target = Path(_os.readlink(chrom_link))
    except OSError:
        return None
    volumes = Path("/Volumes")
    if (
        not target.is_absolute()
        or not target.is_relative_to(volumes)
        or target.name != "chromium"
        or any(character in str(target) for character in (":", ",", "\n", "\r"))
        or not (target / "src").is_dir()
    ):
        return None
    return target


_HOST_RELAY_PORT = 8899


def _selected_suite(manifest: Manifest, suite_name: str) -> Suite | None:
    for suite in manifest.suites:
        if suite.name == suite_name:
            return suite
    return None


def _maybe_start_host_relay(
    manifest: Manifest, suite_name: str
) -> _subprocess.Popen[bytes] | None:
    suite = _selected_suite(manifest, suite_name)
    if not isinstance(suite, GtestSuite) or not suite.host_relay:
        return None
    relay_script = Path(__file__).resolve().parent / "macos_vm_connect_relay.py"
    relay_argv = (
        sys.executable,
        str(relay_script),
        "--port",
        str(_HOST_RELAY_PORT),
    )
    process = _subprocess.Popen(
        relay_argv,
        stdin=_subprocess.DEVNULL,
        stdout=_subprocess.DEVNULL,
        stderr=_subprocess.DEVNULL,
        start_new_session=True,
    )
    deadline = _time.monotonic() + 5
    last_error: OSError | None = None
    while _time.monotonic() < deadline:
        if process.poll() is not None:
            break
        try:
            probe = socket.create_connection(
                ("127.0.0.1", _HOST_RELAY_PORT), timeout=2
            )
            probe.close()
            return process
        except OSError as error:
            last_error = error
            _time.sleep(0.1)
    process.terminate()
    raise _StageFailure(
        _TerminalStatus.VM_START_FAILED,
        "host connect relay failed to start on port "
        f"{_HOST_RELAY_PORT}: exit={process.poll()} last_error={last_error}",
    )


def _stop_host_relay(relay: _subprocess.Popen[bytes] | None) -> None:
    if relay is None:
        return
    if relay.poll() is None:
        relay.terminate()
        try:
            relay.wait(timeout=10)
        except _subprocess.TimeoutExpired:
            relay.kill()
            relay.wait(timeout=10)


def _start_clone(
    clone: OwnedClone,
    mount_argument: str,
    run_directory: Path,
    services: LifecycleServices,
    workspace: Path | None = None,
) -> _RunningVm:
    log_path = run_directory / "tart-run.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_handle = log_path.open("w", encoding="utf-8")
    dir_args = [f"--dir={mount_argument}"]
    if workspace is not None:
        target = _external_chromium_target(Path(workspace))
        if target is not None:
            dir_args.append(f"--dir=chromium:{target}:ro,tag=chromium")
    tart_argv = [
        "tart",
        "run",
        "--no-graphics",
        *dir_args,
        clone.name,
    ]
    try:
        process = services.popen_factory(
            tuple(tart_argv),
            stdin=_subprocess.DEVNULL,
            stdout=log_handle,
            stderr=_subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
    except BaseException:
        log_handle.close()
        raise
    return _RunningVm(process=process, log_handle=log_handle)


def _stop_vm_process(running: _RunningVm | None) -> str | None:
    if running is None:
        return None
    error: str | None = None
    process = running.process
    try:
        if process.poll() is None:
            with _contextlib.suppress(ProcessLookupError):
                _os.killpg(process.pid, _signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except _subprocess.TimeoutExpired:
                with _contextlib.suppress(ProcessLookupError):
                    _os.killpg(process.pid, _signal.SIGKILL)
                try:
                    process.wait(timeout=10)
                except _subprocess.TimeoutExpired:
                    error = "Tart process did not exit after SIGKILL"
    finally:
        running.log_handle.close()
    return error


def _teardown_clone(
    clone: OwnedClone | None,
    running: _RunningVm | None,
    services: LifecycleServices,
) -> _TeardownResult:
    started = _utc_timestamp()
    errors: list[str] = []
    process_error = _stop_vm_process(running)
    if process_error:
        errors.append(process_error)
    if clone is None:
        return _TeardownResult(True, True, started, _utc_timestamp())
    if not _is_owned(clone):
        return _TeardownResult(
            True,
            False,
            started,
            _utc_timestamp(),
            error="refusing to delete an unowned clone",
        )
    for argv in (("tart", "stop", clone.name), ("tart", "delete", clone.name)):
        result = _run_command(
            services,
            argv,
            status=_TerminalStatus.TEARDOWN_FAILED,
            description="clone teardown failed",
            timeout=300,
            required=False,
        )
        if not result.succeeded:
            combined = " ".join(
                part.strip()
                for part in (result.stderr, result.stdout, result.error)
                if part
            )
            # Stopping an already-exited clone is the desired end state; tart
            # reports it as an error but the delete below still succeeds.
            if argv[1] == "stop" and "is not running" in combined:
                continue
            errors.append(
                result.stderr.strip() or result.error or repr(argv)
            )
    if not errors:
        _shutil.rmtree(clone.directory, ignore_errors=True)
    return _TeardownResult(
        attempted=True,
        succeeded=not errors,
        started_at=started,
        finished_at=_utc_timestamp(),
        error="; ".join(errors) if errors else None,
        details={"clone": clone.name},
    )


def _strict_ssh_base(config: _TartConfig, ip_address: str) -> tuple[str, ...]:
    command = _key_only_ssh_command(config, ip_address, ("true",)).argv
    target_index = command.index(f"{config.guest_user}@{ip_address}")
    return (
        *command[:target_index],
        "-o",
        "ConnectTimeout=5",
        "-o",
        "ConnectionAttempts=24",
        command[target_index],
    )


def _remote_argv(
    ssh_base: Sequence[str],
    argv: Sequence[str],
    environment: Mapping[str, str] | None = None,
) -> tuple[str, ...]:
    remote: list[str] = []
    if environment:
        remote.extend(("/usr/bin/env", *(f"{key}={value}" for key, value in sorted(environment.items()))))
    if argv and argv[0] == "sudo":
        remote_cmd = f"echo admin | sudo -S {_shlex.join(argv[1:])}"
        return (*ssh_base, remote_cmd)
    remote.extend(argv)
    return (*ssh_base, _shlex.join(remote))


def _scp_argv(
    config: _TartConfig,
    ip_address: str,
    source: str,
    destination: Path,
    *,
    recursive: bool = False,
) -> tuple[str, ...]:
    # scp rejects ssh's "-b <address>" bind flag; translate it into the
    # scp-compatible "-o BindAddress=<address>" form.
    options: list[str] = []
    skip_next = False
    for item in _strict_ssh_base(config, ip_address)[1:-1]:
        if skip_next:
            options.extend(("-o", f"BindAddress={item}"))
            skip_next = False
            continue
        if item == "-b":
            skip_next = True
            continue
        options.append(item)
    return (
        "scp",
        *( ("-r",) if recursive else () ),
        *options,
        f"{config.guest_user}@{ip_address}:{source}",
        str(destination),
    )


def _wait_for_guest(
    clone: OwnedClone,
    config: _TartConfig,
    services: LifecycleServices,
    cancellation: _CancellationToken,
) -> tuple[str, tuple[str, ...]]:
    ip_result = _run_command(
        services,
        ("tart", "ip", clone.name, "--wait", "600"),
        status=_TerminalStatus.GUEST_NOT_READY,
        description="guest did not acquire an IP address",
        timeout=600,
        cancellation=cancellation,
    )
    ip_address = ip_result.stdout.strip()
    if not ip_address:
        raise _StageFailure(_TerminalStatus.GUEST_NOT_READY, "empty guest IP address")
    ssh_base = _strict_ssh_base(config, ip_address)
    start_time = _time.monotonic()
    last_error = "key-only SSH did not become ready"
    cancelled = _threading.Event()
    registration = cancellation.register(cancelled.set)
    try:
        while _time.monotonic() - start_time < 180:
            result = services.runner.run(
                (*ssh_base, "true"),
                deadline=_deadline(services.runner, 30),
                cancellation=cancellation,
            )
            if result.succeeded:
                services.command_results.append(result)
                return ip_address, ssh_base
            last_error = result.stderr.strip() or result.stdout.strip() or last_error
            if cancelled.wait(2):
                raise _StageFailure(_TerminalStatus.CANCELLED, cancellation.reason)
    finally:
        registration.unregister()
    raise _StageFailure(_TerminalStatus.GUEST_NOT_READY, f"key-only SSH did not become ready: {last_error}")


def _guest_suite_preflight_argv(
    manifest: Manifest,
    ssh_base: Sequence[str],
) -> tuple[str, ...]:
    guest_output = (
        PurePosixPath(str(manifest.guest_workspace))
        / "chromium/src"
        / _DEFAULT_BUILD_OUTPUT.as_posix()
    )
    binaries = sorted(
        {
            suite.binary
            for suite in manifest.suites
            if isinstance(suite, GtestSuite)
        }
    )
    existence_checks = [
        f"test -e {_shlex.quote(str(guest_output / binary))}"
        for binary in binaries
    ]
    existence_checks.append(
        f"test -e {_shlex.quote(str(guest_output / _APP_BUNDLE))}"
    )
    return _remote_argv(
        ssh_base,
        (
            "sh",
            "-lc",
            " && ".join(existence_checks),
        ),
    )


def _configure_transport(
    workspace: Path,
    manifest: Manifest,
    dependencies: Sequence[Path],
    config: _TartConfig,
    ip_address: str,
    ssh_base: Sequence[str],
    services: LifecycleServices,
    cancellation: _CancellationToken,
    run_directory: Path,
    *,
    required_transport: str | None = None,
) -> str:
    mount = _read_only_mount_contract(workspace, manifest.guest_workspace)

    # macOS Virtualization.framework virtiofs drops symlinks from shared
    # directories entirely, so when the host workspace's `chromium` is an
    # external absolute symlink (e.g. chromium -> /Volumes/T9-Mac/chromium)
    # the guest can never see it through the workspace share. In that case,
    # mount the workspace share at a read-only sibling path and assemble the
    # guest workspace as a real directory holding a `chromium` symlink that
    # resolves through the separately mounted chromium share.
    chromium_target = _external_chromium_target(workspace)
    ws_mount_dir = (
        str(manifest.guest_workspace) + "-ro"
        if chromium_target
        else str(manifest.guest_workspace)
    )
    _run_command(
        services,
        _remote_argv(
            ssh_base,
            ("sudo", "mkdir", "-p", str(manifest.guest_workspace), ws_mount_dir),
        ),
        status=_TerminalStatus.TRANSPORT_FAILED,
        description="guest workspace creation failed",
        timeout=60,
        cancellation=cancellation,
    )
    remount_argv = mount.remount_argv[:-1] + (ws_mount_dir,)
    mount_result = _run_command(
        services,
        _remote_argv(ssh_base, remount_argv),
        status=_TerminalStatus.TRANSPORT_FAILED,
        description="read-only VirtioFS mount failed",
        timeout=120,
        cancellation=cancellation,
        required=False,
    )
    if mount_result.succeeded:
        write_probe = _run_command(
            services,
            _remote_argv(
                ssh_base,
                ("touch", str(PurePosixPath(ws_mount_dir) / ".maho-write-probe")),
            ),
            status=_TerminalStatus.TRANSPORT_FAILED,
            description="read-only write probe unexpectedly succeeded",
            timeout=30,
            cancellation=cancellation,
            required=False,
        )
        if write_probe.succeeded:
            raise _StageFailure(
                _TerminalStatus.TRANSPORT_FAILED,
                "host workspace mount is writable inside the guest",
            )
        if chromium_target is not None:
            target_path = chromium_target
            # Mount the chromium share tag at the symlink target's parent so
            # the wrapped share child (named after the target basename)
            # completes the exact guest-side target path.
            share_name = target_path.name or "chromium"
            mount_parent = str(target_path.parent)
            quoted_mount_parent = _shlex.quote(mount_parent)
            quoted_share_name = _shlex.quote(share_name)
            quoted_target = _shlex.quote(str(chromium_target))
            quoted_workspace = _shlex.quote(str(manifest.guest_workspace))
            _run_command(
                services,
                _remote_argv(
                    ssh_base,
                    (
                        "sh",
                        "-lc",
                        f"sudo mkdir -p {quoted_mount_parent} && "
                        f"(sudo mount_virtiofs {quoted_share_name} {quoted_mount_parent} 2>/dev/null || "
                        f"sudo mount -t virtiofs -o ro {quoted_share_name} {quoted_mount_parent}) && "
                        f"test -e {quoted_target} && ls -la {quoted_target}",
                    ),
                ),
                status=_TerminalStatus.TRANSPORT_FAILED,
                description="guest chromium virtiofs mount failed",
                timeout=120,
                cancellation=cancellation,
                required=False,
            )
            _run_command(
                services,
                _remote_argv(
                    ssh_base,
                    (
                        "sh",
                        "-lc",
                        f"sudo find {_shlex.quote(ws_mount_dir)} -mindepth 1 -maxdepth 1 "
                        "! -name chromium -exec ln -sfn {} "
                        f"{quoted_workspace}/ \\;",
                    ),
                ),
                status=_TerminalStatus.TRANSPORT_FAILED,
                description="guest workspace symlink assembly failed",
                timeout=60,
                cancellation=cancellation,
            )
            _run_command(
                services,
                _remote_argv(
                    ssh_base,
                    (
                        "sh",
                        "-lc",
                        f"sudo rm -rf {quoted_workspace}/chromium && "
                        f"sudo ln -sfn {quoted_target} {quoted_workspace}/chromium && "
                        f"test -e {quoted_workspace}/chromium/src",
                    ),
                ),
                status=_TerminalStatus.TRANSPORT_FAILED,
                description="guest chromium symlink shim failed",
                timeout=60,
                cancellation=cancellation,
                required=False,
            )
            shim_write_probe = _run_command(
                services,
                _remote_argv(
                    ssh_base,
                    (
                        "touch",
                        str(
                            PurePosixPath(str(manifest.guest_workspace))
                            / "chromium/.maho-write-probe"
                        ),
                    ),
                ),
                status=_TerminalStatus.TRANSPORT_FAILED,
                description="read-only write probe unexpectedly succeeded",
                timeout=30,
                cancellation=cancellation,
                required=False,
            )
            if shim_write_probe.succeeded:
                raise _StageFailure(
                    _TerminalStatus.TRANSPORT_FAILED,
                    "host chromium mount is writable inside the guest",
                )
        mounted_preflight = _run_command(
            services,
            _guest_suite_preflight_argv(manifest, ssh_base),
            status=_TerminalStatus.TRANSPORT_FAILED,
            description="read-only VirtioFS execution preflight failed",
            timeout=300,
            cancellation=cancellation,
            required=False,
        )
        if mounted_preflight.succeeded:
            return "virtiofs-read-only"
        _run_command(
            services,
            _remote_argv(
                ssh_base,
                ("sudo", "umount", ws_mount_dir),
            ),
            status=_TerminalStatus.TRANSPORT_FAILED,
            description="failed to unmount incompatible VirtioFS transport",
            timeout=120,
            cancellation=cancellation,
        )

    if required_transport == "virtiofs-read-only":
        raise _StageFailure(
            _TerminalStatus.TRANSPORT_FAILED,
            "required transport 'virtiofs-read-only' did not pass guest preflight",
        )

    _run_command(
        services,
        _remote_argv(
            ssh_base,
            (
                "sudo",
                "install",
                "-d",
                "-o",
                config.guest_user,
                "-g",
                "staff",
                str(manifest.guest_workspace),
            ),
        ),
        status=_TerminalStatus.TRANSPORT_FAILED,
        description="fallback workspace creation failed",
        timeout=60,
        cancellation=cancellation,
    )
    fallback = _plan_rsync_fallback(
        workspace_root=workspace,
        dependencies=dependencies,
        guest_workspace=manifest.guest_workspace,
        ssh_destination=f"{config.guest_user}@{ip_address}",
    )
    files_from = run_directory / "runtime-deps-files-from.txt"
    files_from.write_text(fallback.files_from, encoding="utf-8")
    (run_directory / "runtime-deps-hashes.json").write_text(
        json.dumps([_dataclasses.asdict(entry) for entry in fallback.manifest], indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    rsync_argv = list(fallback.rsync_argv)
    files_index = rsync_argv.index("--files-from=-")
    rsync_argv[files_index] = f"--files-from={files_from}"
    ssh_transport = _shlex.join(_strict_ssh_base(config, ip_address)[:-1])
    rsync_argv[1:1] = ["--rsh", ssh_transport]
    _run_command(
        services,
        tuple(rsync_argv),
        status=_TerminalStatus.TRANSPORT_FAILED,
        description="runtime-deps fallback transport failed",
        timeout=1800,
        cwd=workspace,
        cancellation=cancellation,
    )
    _run_command(
        services,
        _guest_suite_preflight_argv(manifest, ssh_base),
        status=_TerminalStatus.TRANSPORT_FAILED,
        description="runtime-deps fallback execution preflight failed",
        timeout=300,
        cancellation=cancellation,
    )
    return "runtime-deps-rsync"


def _expand_suite(manifest: Manifest, suite_name: str) -> tuple[Suite, ...]:
    if suite_name == _INTENTIONAL_FAILURE_SUITE:
        return ()
    suite = manifest.suite(suite_name)
    if not isinstance(suite, AggregateSuite):
        return (suite,)
    expanded: list[Suite] = []
    for member in suite.members:
        expanded.extend(_expand_suite(manifest, member))
    return tuple(expanded)


def _write_command_logs(
    run_directory: Path,
    command: _GuestCommand,
    result: _RuntimeCommandResult,
) -> tuple[str, str]:
    stdout_path = run_directory / Path(command.stdout_path).name
    stderr_path = run_directory / Path(command.stderr_path).name
    stdout_path.write_text(result.stdout, encoding="utf-8")
    stderr_path.write_text(result.stderr, encoding="utf-8")
    return str(stdout_path), str(stderr_path)


def _execute_guest_command(
    command: _GuestCommand,
    ssh_base: Sequence[str],
    services: LifecycleServices,
    cancellation: _CancellationToken,
    on_process_started: _Callable[[int], None] | None = None,
) -> tuple[_CapturedCommandResult, _RuntimeCommandResult]:
    def process_started(pid: int) -> None:
        if on_process_started is None:
            return
        try:
            on_process_started(pid)
        except BaseException:
            cancellation.cancel("host isolation snapshot failed")
            raise

    result = _run_command(
        services,
        _remote_argv(ssh_base, command.argv, command.environment_dict()),
        status=_TerminalStatus.SUITE_FAILED,
        description=f"{command.suite} command failed",
        timeout=45 * 60,
        cancellation=cancellation,
        on_process_started=process_started,
        required=False,
    )
    return (
        _CapturedCommandResult(command, result.returncode or 0, result.stdout, result.stderr),
        result,
    )


def _copy_guest_file(
    config: _TartConfig,
    ip_address: str,
    remote_path: str,
    local_path: Path,
    services: LifecycleServices,
    cancellation: _CancellationToken,
    *,
    failure_status: _TerminalStatus = _TerminalStatus.SUITE_FAILED,
) -> None:
    local_path.parent.mkdir(parents=True, exist_ok=True)
    _run_command(
        services,
        _scp_argv(config, ip_address, remote_path, local_path),
        status=failure_status,
        description=f"failed to copy guest result {remote_path}",
        timeout=300,
        cancellation=cancellation,
    )


def _run_selected_suites(
    manifest: Manifest,
    suite_name: str,
    guest_run_dir: PurePosixPath,
    config: _TartConfig,
    ip_address: str,
    ssh_base: Sequence[str],
    services: LifecycleServices,
    cancellation: _CancellationToken,
    run_directory: Path,
    record_results: _Callable[[tuple[_ReceiptSuiteResult, ...]], None] | None = None,
    on_process_started: _Callable[[int], None] | None = None,
    selected_gtest: str | None = None,
) -> tuple[_ReceiptSuiteResult, ...]:
    _run_command(
        services,
        _remote_argv(
            ssh_base,
            (
                "mkdir",
                "-p",
                str(guest_run_dir),
                str(guest_run_dir / "empty-source-profile"),
            ),
        ),
        status=_TerminalStatus.GUEST_NOT_READY,
        description="guest run directory creation failed",
        timeout=60,
        cancellation=cancellation,
    )
    _run_command(
        services,
        _guest_suite_preflight_argv(manifest, ssh_base),
        status=_TerminalStatus.GUEST_NOT_READY,
        description="guest suite preflight failed",
        timeout=300,
        cancellation=cancellation,
    )

    selected: tuple[Suite | str, ...] = (
        (_INTENTIONAL_FAILURE_SUITE,)
        if suite_name == _INTENTIONAL_FAILURE_SUITE
        else _expand_suite(manifest, suite_name)
    )
    ledger_timestamp = _utc_timestamp()
    results = [
        _ReceiptSuiteResult(
            name=item if isinstance(item, str) else item.name,
            status="not_run",
            started_at=ledger_timestamp,
            finished_at=ledger_timestamp,
            details={"selected": True},
        )
        for item in selected
    ]

    def persist() -> None:
        if record_results is not None:
            record_results(tuple(results))

    persist()
    for index, item in enumerate(selected):
        suite_started = _utc_timestamp()
        artifact_paths: list[str] = []
        returncode: int | None = None
        try:
            if isinstance(item, str):
                command = _build_intentional_failure_command(
                    str(manifest.guest_workspace), str(guest_run_dir)
                )
                captured, runtime_result = _execute_guest_command(
                    command,
                    ssh_base,
                    services,
                    cancellation,
                    on_process_started,
                )
                returncode = captured.returncode
                artifact_paths.extend(
                    _write_command_logs(run_directory, command, runtime_result)
                )
                message = (
                    "intentional failure fixture unexpectedly passed"
                    if captured.returncode == 0
                    else "intentional failure fixture failed as designed"
                )
                raise _StageFailure(_TerminalStatus.SUITE_FAILED, message)

            suite = item
            if isinstance(suite, GtestSuite):
                selected_tests = (
                    (selected_gtest,)
                    if selected_gtest is not None
                    else suite.gtest_filter
                )
                command = _build_interactive_command(
                    str(manifest.guest_workspace),
                    str(guest_run_dir),
                    selected_tests,
                    suite_name=suite.name,
                    binary_name=suite.binary,
                )
                captured, runtime_result = _execute_guest_command(
                    command,
                    ssh_base,
                    services,
                    cancellation,
                    on_process_started,
                )
                returncode = captured.returncode
                artifact_paths.extend(
                    _write_command_logs(run_directory, command, runtime_result)
                )
                summary_path = run_directory / "interactive-test-summary.json"
                _copy_guest_file(
                    config,
                    ip_address,
                    command.summary_path or "",
                    summary_path,
                    services,
                    cancellation,
                )
                artifact_paths.append(str(summary_path))
                summary = _merge_gtest_summary_with_stdout(
                    json.loads(summary_path.read_text(encoding="utf-8")),
                    command.expected_results,
                    captured.stdout,
                )
                summary_path.write_text(
                    json.dumps(summary, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                validation = _validate_interactive_result(captured, summary)
            elif isinstance(suite, AiMockSuite):
                commands = _build_ai_mock_commands(
                    str(manifest.guest_workspace),
                    str(guest_run_dir),
                    suite.scenarios,
                    node_binary="/opt/homebrew/bin/node",
                    cdp_host=manifest.cdp_address,
                    cdp_port=_DEFAULT_CDP_PORT,
                )
                captured_results: list[_CapturedCommandResult] = []
                for command in commands:
                    captured, runtime_result = _execute_guest_command(
                        command,
                        ssh_base,
                        services,
                        cancellation,
                        on_process_started,
                    )
                    returncode = captured.returncode
                    captured_results.append(captured)
                    artifact_paths.extend(
                        _write_command_logs(run_directory, command, runtime_result)
                    )
                validation = _validate_ai_mock_results(
                    captured_results, suite.scenarios
                )
            else:
                raise AssertionError(f"unexpanded aggregate suite: {suite.name}")
            results[index] = _ReceiptSuiteResult(
                name=suite.name,
                status="success",
                started_at=suite_started,
                finished_at=_utc_timestamp(),
                returncode=0,
                artifact_paths=tuple(artifact_paths),
                details=validation.to_dict(),
            )
            persist()
        except (_SuiteValidationError, json.JSONDecodeError) as error:
            results[index] = _ReceiptSuiteResult(
                name=item if isinstance(item, str) else item.name,
                status="failed",
                started_at=suite_started,
                finished_at=_utc_timestamp(),
                returncode=returncode,
                artifact_paths=tuple(artifact_paths),
                details={"error": str(error)},
            )
            persist()
            raise _StageFailure(_TerminalStatus.SUITE_FAILED, str(error)) from error
        except _StageFailure as error:
            results[index] = _ReceiptSuiteResult(
                name=item if isinstance(item, str) else item.name,
                status="failed",
                started_at=suite_started,
                finished_at=_utc_timestamp(),
                returncode=returncode,
                artifact_paths=tuple(artifact_paths),
                details={"error": str(error)},
            )
            persist()
            raise
    if not results:
        raise _StageFailure(_TerminalStatus.SUITE_FAILED, "no suite was executed")
    return tuple(results)


def _collect_guest_artifacts(
    config: _TartConfig,
    ip_address: str,
    guest_run_dir: PurePosixPath,
    run_directory: Path,
    services: LifecycleServices,
) -> Path:
    destination = run_directory / "guest-artifacts"
    destination.mkdir(parents=True, exist_ok=True)
    ssh_base = _strict_ssh_base(config, ip_address)
    listing = _run_command(
        services,
        _remote_argv(
            ssh_base,
            (
                "find",
                str(guest_run_dir),
                "-maxdepth",
                "3",
                "-type",
                "f",
                "(",
                "-name",
                "*.png",
                "-o",
                "-name",
                "*summary*.json",
                "-o",
                "-name",
                "intentional-failure.json",
                ")",
                "-print",
            ),
        ),
        status=_TerminalStatus.ARTIFACT_FAILED,
        description="guest artifact inventory failed",
        timeout=120,
    )
    root = str(guest_run_dir).rstrip("/") + "/"
    for remote_path in listing.stdout.splitlines():
        if not remote_path.startswith(root):
            raise _StageFailure(
                _TerminalStatus.ARTIFACT_FAILED,
                f"guest artifact escaped run directory: {remote_path}",
            )
        relative = PurePosixPath(remote_path[len(root) :])
        lowered = "/".join(relative.parts).casefold()
        if any(part in lowered for part in ("profile", "keychain", ".env", "cookie")):
            raise _StageFailure(
                _TerminalStatus.ARTIFACT_FAILED,
                f"forbidden guest artifact selected: {relative}",
            )
        local_path = destination / Path(*relative.parts)
        _copy_guest_file(
            config,
            ip_address,
            remote_path,
            local_path,
            services,
            _CancellationToken(),
            failure_status=_TerminalStatus.ARTIFACT_FAILED,
        )
    return destination


def doctor(args: argparse.Namespace | None = None) -> int:
    manifest = load_manifest(getattr(args, "manifest", _DEFAULT_MANIFEST_PATH))
    config = _tart_config(manifest, args)
    report = _tart_doctor(config, _TartSubprocessRunner())
    print(report.to_json())
    if not report.ok:
        failed = ", ".join(check.name for check in report.checks if not check.ok)
        raise LifecycleError(f"doctor checks failed: {failed}")
    return 0


def prepare(args: argparse.Namespace) -> int:
    manifest = load_manifest(args.manifest)
    config = _tart_config(manifest, args, create_key=True)
    result = _tart_prepare(config, _TartSubprocessRunner(), install_missing=True)
    print(json.dumps({
        "disposition": result.disposition.value,
        "golden_image": config.golden_image,
        "state_path": str(config.state_path),
        "source_identity": result.state.source_identity,
    }, sort_keys=True))
    return 0


def list_suites(args: argparse.Namespace | None = None) -> int:
    manifest = load_manifest(getattr(args, "manifest", _DEFAULT_MANIFEST_PATH))
    inventory = manifest.inventory()
    if getattr(args, "json", False):
        print(json.dumps(inventory, indent=2))
    else:
        for suite in inventory["suites"]:
            print(suite)
        for scenario in inventory["ai_scenarios"]:
            print(scenario)
    return 0


def _receipt_snapshot(
    stage: str,
    snapshot: _HostIsolationSnapshot,
    violations: Sequence[str] = (),
) -> _ReceiptIsolationSnapshot:
    return _ReceiptIsolationSnapshot(
        stage=stage,
        captured_at=_utc_timestamp(),
        facts=snapshot.to_json_dict(),
        violations=tuple(violations),
    )


def _source_revision(workspace: Path) -> str:
    result = _subprocess.run(
        ("git", "rev-parse", "HEAD"),
        cwd=workspace,
        check=False,
        capture_output=True,
        text=True,
    )
    revision = result.stdout.strip()
    if result.returncode != 0 or not revision:
        raise LifecycleError(
            result.stderr.strip() or "unable to resolve source revision"
        )
    return revision


def _required_snapshot(services: LifecycleServices) -> _HostIsolationSnapshot:
    try:
        return services.snapshot(window_probe_path=_WINDOW_PROBE)
    except Exception as error:
        raise _StageFailure(
            _TerminalStatus.ISOLATION_FAILED,
            f"host isolation snapshot failed: {error}",
        ) from error


def _checkpoint(
    writer: _AtomicReceiptWriter,
    receipt: _RunReceipt,
    **changes: object,
) -> _RunReceipt:
    updated = _dataclasses.replace(receipt, updated_at=_utc_timestamp(), **changes)
    writer.write_partial(updated)
    return updated


def run(
    args: argparse.Namespace,
    *,
    services: LifecycleServices | None = None,
) -> int:
    services = services or LifecycleServices()
    manifest = load_manifest(args.manifest)
    available = (*manifest.suite_names, _INTENTIONAL_FAILURE_SUITE)
    if args.suite not in available:
        raise LifecycleError(
            f"unknown suite {args.suite!r}; choose from: {', '.join(available)}"
        )
    selected_gtest = getattr(args, "gtest", None)
    required_transport = getattr(args, "require_transport", None)
    if selected_gtest is not None:
        if any(character in selected_gtest for character in "*?[]:") or "." not in selected_gtest:
            raise LifecycleError(
                f"--gtest requires one exact gtest name, got {selected_gtest!r}"
            )
        selected_suite = (
            manifest.suite(args.suite) if args.suite in manifest.suite_names else None
        )
        if not isinstance(selected_suite, GtestSuite):
            raise LifecycleError(
                "--gtest requires a chosen non-aggregate gtest suite"
            )
        if selected_gtest not in selected_suite.gtest_filter:
            raise LifecycleError(
                f"gtest {selected_gtest!r} is not a member of suite {args.suite!r}"
            )

    workspace = _workspace_root(args)
    config = _tart_config(manifest, args)
    if not config.private_key.is_file():
        raise LifecycleError(f"SSH private key does not exist: {config.private_key}")
    run_id = args.run_id or f"{_time.strftime('%Y%m%dT%H%M%SZ', _time.gmtime())}-{_uuid.uuid4().hex[:12]}"
    writer = _AtomicReceiptWriter(workspace, run_id)
    writer.run_directory.mkdir(parents=True, exist_ok=True)
    _shutil.copy2(args.manifest, writer.run_directory / "manifest.json")
    receipt = _RunReceipt.start(
        run_id,
        host_facts={
            "architecture": _platform.machine(),
            "workspace": str(workspace),
            "source_revision": _source_revision(workspace),
            "workspace_dirty": bool(
                _subprocess.run(
                    ("git", "status", "--porcelain"),
                    cwd=workspace,
                    check=False,
                    capture_output=True,
                    text=True,
                ).stdout
            ),
            "requested_suite": args.suite,
            **(
                {"requested_gtest": selected_gtest}
                if selected_gtest is not None
                else {}
            ),
            **(
                {"required_transport": required_transport}
                if required_transport is not None
                else {}
            ),
            "cancel_after_guest_ready": bool(
                getattr(args, "cancel_after_guest_ready", False)
            ),
        },
        source_image_identity=_source_image_identity(config),
        build_path=str(workspace / "chromium/src" / _DEFAULT_BUILD_OUTPUT),
    )
    writer.write_partial(receipt)

    cancellation = _CancellationToken()
    previous_handlers: dict[int, _signal.Handlers | int | _Callable[..., object] | None] = {}
    clone: OwnedClone | None = None
    running_vm: _RunningVm | None = None
    ip_address: str | None = None
    guest_run_dir = _GUEST_RUN_ROOT / run_id
    primary_status = _TerminalStatus.SUCCESS
    first_error: str | None = None
    baseline: _HostIsolationSnapshot | None = None
    during: _HostIsolationSnapshot | None = None
    after: _HostIsolationSnapshot | None = None
    artifact_paths: list[str] = []
    suite_results: tuple[_ReceiptSuiteResult, ...] = ()

    def cancel(signum: int, _frame: object) -> None:
        cancellation.cancel(f"received signal {signum}")

    for signum in (_signal.SIGINT, _signal.SIGTERM):
        previous_handlers[signum] = _signal.signal(signum, cancel)

    try:
        if getattr(args, "skip_build", False):
            dependencies = _preflight_existing_build(
                workspace,
                manifest,
                services,
                cancellation,
                writer.run_directory,
                selected_gtest,
            )
        else:
            dependencies = _run_build_and_preflight(
                workspace,
                manifest,
                services,
                cancellation,
                writer.run_directory,
                selected_gtest,
            )
        _run_command(
            services,
            ("tart", "get", config.golden_image, "--format", "json"),
            status=_TerminalStatus.PREPARE_FAILED,
            description="prepared golden image is unavailable",
            timeout=60,
            cancellation=cancellation,
        )
        baseline = _required_snapshot(services)
        receipt = _checkpoint(
            writer,
            receipt,
            isolation_snapshots=(_receipt_snapshot("baseline", baseline),),
        )
        relay_process = _maybe_start_host_relay(manifest, args.suite)
        clone = _new_clone(
            config.golden_image,
            _state_root(args.state_dir),
            services,
            cancellation,
        )
        mount = _read_only_mount_contract(workspace, manifest.guest_workspace)
        try:
            running_vm = _start_clone(
                clone, mount.tart_argument, writer.run_directory, services, workspace=workspace
            )
        except OSError as error:
            raise _StageFailure(
                _TerminalStatus.VM_START_FAILED, f"failed to start Tart: {error}"
            ) from error
        ip_address, ssh_base = _wait_for_guest(
            clone, config, services, cancellation
        )
        receipt = _checkpoint(
            writer,
            receipt,
            guest_facts={
                **receipt.guest_facts,
                "clone": clone.name,
                "ip_address": ip_address,
                "guest_workspace": str(manifest.guest_workspace),
                "guest_run_dir": str(guest_run_dir),
                "guest_ready_at": _utc_timestamp(),
            },
        )
        if getattr(args, "cancel_after_guest_ready", False):
            cancellation.cancel("cancelled after guest readiness checkpoint")
            raise _StageFailure(_TerminalStatus.CANCELLED, cancellation.reason)
        transport = _configure_transport(
            workspace,
            manifest,
            dependencies,
            config,
            ip_address,
            ssh_base,
            services,
            cancellation,
            writer.run_directory,
            required_transport=required_transport,
        )
        receipt = _checkpoint(
            writer,
            receipt,
            guest_facts={
                **receipt.guest_facts,
                "transport": transport,
                "cdp_address": manifest.cdp_address,
                "cdp_port": _DEFAULT_CDP_PORT,
            },
        )

        def capture_during(_pid: int) -> None:
            nonlocal during, receipt
            if during is not None:
                return
            try:
                during = _required_snapshot(services)
                receipt = _checkpoint(
                    writer,
                    receipt,
                    isolation_snapshots=(
                        *receipt.isolation_snapshots,
                        _receipt_snapshot("during", during),
                    ),
                )
            except BaseException:
                cancellation.cancel("host isolation snapshot failed")
                raise

        def record_suite_results(
            updated_results: tuple[_ReceiptSuiteResult, ...],
        ) -> None:
            nonlocal receipt, suite_results
            suite_results = updated_results
            receipt = _checkpoint(
                writer,
                receipt,
                suite_results=updated_results,
            )

        suite_results = _run_selected_suites(
            manifest,
            args.suite,
            guest_run_dir,
            config,
            ip_address,
            ssh_base,
            services,
            cancellation,
            writer.run_directory,
            record_results=record_suite_results,
            on_process_started=capture_during,
            selected_gtest=selected_gtest,
        )
        guest_artifacts = _collect_guest_artifacts(
            config,
            ip_address,
            guest_run_dir,
            writer.run_directory,
            services,
        )
        artifact_paths.append(str(guest_artifacts))
        receipt = _checkpoint(
            writer,
            receipt,
            artifact_paths=tuple(artifact_paths),
        )
    except _StageFailure as error:
        primary_status = error.status
        first_error = str(error)
        if ip_address is not None:
            try:
                guest_artifacts = _collect_guest_artifacts(
                    config,
                    ip_address,
                    guest_run_dir,
                    writer.run_directory,
                    services,
                )
                artifact_paths.append(str(guest_artifacts))
            except _StageFailure as artifact_error:
                artifact_detail = f"artifact collection failed: {artifact_error}"
                first_error = (
                    f"{first_error}; {artifact_detail}"
                    if first_error
                    else artifact_detail
                )
    except Exception as error:
        primary_status = _TerminalStatus.VM_START_FAILED
        first_error = str(error)
    finally:
        for signum, handler in previous_handlers.items():
            _signal.signal(signum, handler)
        _stop_host_relay(locals().get("relay_process"))

    try:
        teardown = _teardown_clone(clone, running_vm, services)
    except Exception as error:
        teardown = _TeardownResult(
            attempted=True,
            succeeded=False,
            started_at=_utc_timestamp(),
            finished_at=_utc_timestamp(),
            error=f"unexpected teardown failure: {error}",
            details={"clone": clone.name} if clone is not None else {},
        )
    try:
        after = services.snapshot(window_probe_path=_WINDOW_PROBE)
    except Exception as error:
        if primary_status is _TerminalStatus.SUCCESS:
            primary_status = _TerminalStatus.ISOLATION_FAILED
            first_error = f"after isolation snapshot failed: {error}"

    isolation_receipts = list(receipt.isolation_snapshots)
    if baseline is not None and during is not None and after is not None:
        report = _compare_run_snapshots(
            baseline, during, after, cdp_port=_DEFAULT_CDP_PORT
        )
        isolation_path = writer.run_directory / "host-isolation.json"
        isolation_path.write_text(
            json.dumps(report.to_json_dict(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        artifact_paths.append(str(isolation_path))
        isolation_receipts = [
            snapshot
            for snapshot in isolation_receipts
            if snapshot.stage != "during"
        ]
        isolation_receipts.extend(
            (
                _receipt_snapshot("during", during, report.during.violation_codes),
                _receipt_snapshot("after", after, report.after.violation_codes),
            )
        )
        if not report.is_isolated and primary_status is _TerminalStatus.SUCCESS:
            primary_status = _TerminalStatus.ISOLATION_FAILED
            first_error = "; ".join(report.violation_codes)
    elif (
        baseline is not None
        and during is None
        and after is not None
        and primary_status is not _TerminalStatus.SUCCESS
    ):
        report = _compare_early_exit_snapshots(
            baseline, after, cdp_port=_DEFAULT_CDP_PORT
        )
        isolation_path = writer.run_directory / "host-isolation.json"
        isolation_path.write_text(
            json.dumps(report.to_json_dict(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        artifact_paths.append(str(isolation_path))
        isolation_receipts = [
            snapshot
            for snapshot in isolation_receipts
            if snapshot.stage != "post_teardown_after_early_exit"
        ]
        isolation_receipts.append(
            _receipt_snapshot(
                "post_teardown_after_early_exit",
                after,
                report.post_teardown_after_early_exit.violation_codes,
            )
        )
    elif (
        baseline is not None
        and during is None
        and after is not None
        and primary_status is _TerminalStatus.SUCCESS
    ):
        primary_status = _TerminalStatus.ISOLATION_FAILED
        first_error = (
            "normal run did not capture event-bound during isolation snapshot"
        )

    receipt = _dataclasses.replace(
        receipt,
        updated_at=_utc_timestamp(),
        suite_results=suite_results or receipt.suite_results,
        artifact_paths=tuple(dict.fromkeys(artifact_paths)),
        isolation_snapshots=tuple(isolation_receipts),
        guest_facts={
            **receipt.guest_facts,
            **({"first_error": first_error} if first_error else {}),
        },
    )
    commands_path = writer.run_directory / "commands.log"
    commands_path.write_text(
        "\n".join(
            json.dumps(result.to_dict(), sort_keys=True)
            for result in services.command_results
        )
        + ("\n" if services.command_results else ""),
        encoding="utf-8",
    )
    existing_artifacts = tuple(
        str(path)
        for path in sorted(writer.run_directory.rglob("*"))
        if path.is_file()
        and path != writer.receipt_path
        and not path.name.startswith(".receipt.")
    )
    receipt = _dataclasses.replace(
        receipt,
        artifact_paths=tuple(
            dict.fromkeys((*receipt.artifact_paths, *existing_artifacts))
        ),
    )
    final_receipt = writer.finalize(
        receipt,
        primary_status=primary_status,
        teardown_result=teardown,
    )
    print(json.dumps(final_receipt.to_dict(), indent=2, sort_keys=True))
    if final_receipt.terminal_status is _TerminalStatus.SUCCESS:
        return 0
    if final_receipt.terminal_status is _TerminalStatus.SUITE_FAILED:
        return 1
    if final_receipt.terminal_status is _TerminalStatus.CANCELLED:
        return 130
    return 3


def build_parser() -> argparse.ArgumentParser:
    manifest = load_manifest()
    parser = argparse.ArgumentParser(
        description="Run isolated macOS VM desktop E2E suites."
    )
    parser.add_argument(
        "--manifest", type=Path, default=_DEFAULT_MANIFEST_PATH, help=argparse.SUPPRESS
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    doctor_parser = subparsers.add_parser("doctor", help="Check host prerequisites.")
    prepare_parser = subparsers.add_parser("prepare", help="Prepare the private golden VM.")
    list_parser = subparsers.add_parser("list", help="List manifest suites and AI scenarios.")
    list_parser.add_argument("--json", action="store_true")
    run_parser = subparsers.add_parser("run", help="Build and run one suite in a disposable VM.")
    run_parser.add_argument(
        "--suite",
        required=True,
        choices=(*manifest.suite_names, _INTENTIONAL_FAILURE_SUITE),
    )
    run_parser.add_argument(
        "--gtest",
        help="Run one exact manifest gtest from the chosen gtest suite.",
    )
    run_parser.add_argument(
        "--require-transport",
        choices=("virtiofs-read-only",),
        help="Fail instead of using fallback when the required transport is unavailable.",
    )
    run_parser.add_argument(
        "--cancel-after-guest-ready",
        action="store_true",
        help=(
            "cancel immediately after the guest_ready_at receipt checkpoint "
            "to exercise deterministic teardown"
        ),
    )
    run_parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Validate and reuse existing host build products without invoking build_maho.py.",
    )
    run_parser.add_argument("--run-id")
    run_parser.add_argument("--workspace", type=Path)
    for child in (doctor_parser, prepare_parser, run_parser):
        child.add_argument("--golden")
        child.add_argument("--identity")
        child.add_argument("--state-dir", type=Path)
    return parser


def lifecycle_main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    handlers = {
        "doctor": doctor,
        "prepare": prepare,
        "list": list_suites,
        "run": run,
    }
    try:
        return handlers[args.command](args)
    except (LifecycleError, MacosVmE2eError, _TartAdapterError) as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(lifecycle_main())
