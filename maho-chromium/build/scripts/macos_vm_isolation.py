#!/usr/bin/env python3
"""Host-side process, listener, and visible-window isolation snapshots."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import shlex
import subprocess
import sys
from typing import Any, ClassVar, Mapping, Protocol, Sequence, TypeVar


_SCHEMA_VERSION = 1
_VISIBLE_WINDOW_OWNERS = frozenset(("maho", "tart"))
_SCRIPT_DIR = Path(__file__).resolve().parent
_DEFAULT_WINDOW_PROBE = _SCRIPT_DIR / "macos_vm_window_probe.swift"
_PROCESS_COMMAND = ("ps", "-axo", "pid=,ppid=,command=")
_LISTENER_COMMAND = (
    "lsof",
    "-nP",
    "-iTCP",
    "-sTCP:LISTEN",
    "-Fpcn",
)
_DEFAULT_TIMEOUT_SECONDS = 30.0


class IsolationProbeError(RuntimeError):
    """Raised when a host isolation snapshot cannot be trusted."""


@dataclass(frozen=True)
class CommandResult:
    returncode: int
    stdout: str = ""
    stderr: str = ""


class Runner(Protocol):
    def run(
        self,
        argv: Sequence[str],
        *,
        timeout: float,
    ) -> CommandResult: ...


class SubprocessRunner:
    def run(
        self,
        argv: Sequence[str],
        *,
        timeout: float,
    ) -> CommandResult:
        completed = subprocess.run(
            tuple(argv),
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return CommandResult(
            completed.returncode,
            completed.stdout,
            completed.stderr,
        )


def _invalid(location: str, message: str) -> IsolationProbeError:
    return IsolationProbeError(f"{location}: {message}")


def _expect_mapping(value: object, location: str) -> Mapping[str, object]:
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


def _expect_array(value: object, location: str) -> list[object]:
    if not isinstance(value, list):
        raise _invalid(location, "must be an array")
    return value


def _expect_string(value: object, location: str) -> str:
    if not isinstance(value, str) or not value or value != value.strip():
        raise _invalid(location, "must be a non-empty string without outer whitespace")
    return value


def _expect_int(
    value: object,
    location: str,
    *,
    minimum: int = 0,
    maximum: int | None = None,
) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise _invalid(location, "must be an integer")
    if value < minimum or (maximum is not None and value > maximum):
        range_text = f"{minimum} or greater"
        if maximum is not None:
            range_text = f"between {minimum} and {maximum}"
        raise _invalid(location, f"must be {range_text}")
    return value


def _expect_number(value: object, location: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise _invalid(location, "must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise _invalid(location, "must be finite")
    return result


def _expect_schema(value: object, location: str) -> None:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value != _SCHEMA_VERSION
    ):
        raise _invalid(
            f"{location}.schema_version",
            f"must be {_SCHEMA_VERSION}",
        )


def _normalized_owner(owner_name: str) -> str:
    return owner_name.casefold()


@dataclass(frozen=True)
class WindowBounds:
    x: float
    y: float
    width: float
    height: float

    @classmethod
    def from_json_dict(
        cls,
        value: object,
        location: str,
    ) -> "WindowBounds":
        data = _expect_mapping(value, location)
        _expect_exact_keys(data, {"x", "y", "width", "height"}, location)
        width = _expect_number(data["width"], f"{location}.width")
        height = _expect_number(data["height"], f"{location}.height")
        if width < 0 or height < 0:
            raise _invalid(location, "width and height must not be negative")
        return cls(
            x=_expect_number(data["x"], f"{location}.x"),
            y=_expect_number(data["y"], f"{location}.y"),
            width=width,
            height=height,
        )

    def to_json_dict(self) -> dict[str, float]:
        return {
            "x": self.x,
            "y": self.y,
            "width": self.width,
            "height": self.height,
        }


@dataclass(frozen=True)
class VisibleWindow:
    owner_name: str
    owner_pid: int
    window_id: int
    layer: int
    bounds: WindowBounds

    @property
    def identity(self) -> tuple[str, int, int]:
        return (_normalized_owner(self.owner_name), self.owner_pid, self.window_id)

    @property
    def identity_text(self) -> str:
        owner, owner_pid, window_id = self.identity
        return f"{owner}:{owner_pid}:{window_id}"

    @classmethod
    def from_json_dict(
        cls,
        value: object,
        location: str,
    ) -> "VisibleWindow":
        data = _expect_mapping(value, location)
        _expect_exact_keys(
            data,
            {"owner_name", "owner_pid", "window_id", "layer", "bounds"},
            location,
        )
        return cls(
            owner_name=_expect_string(data["owner_name"], f"{location}.owner_name"),
            owner_pid=_expect_int(
                data["owner_pid"],
                f"{location}.owner_pid",
                minimum=1,
            ),
            window_id=_expect_int(
                data["window_id"],
                f"{location}.window_id",
                minimum=1,
            ),
            layer=_expect_int(
                data["layer"],
                f"{location}.layer",
                minimum=-(2**31),
                maximum=2**31 - 1,
            ),
            bounds=WindowBounds.from_json_dict(
                data["bounds"],
                f"{location}.bounds",
            ),
        )

    def to_json_dict(self) -> dict[str, object]:
        return {
            "owner_name": self.owner_name,
            "owner_pid": self.owner_pid,
            "window_id": self.window_id,
            "layer": self.layer,
            "bounds": self.bounds.to_json_dict(),
        }


@dataclass(frozen=True)
class HostProcess:
    pid: int
    parent_pid: int
    executable: str
    command: str

    @property
    def identity(self) -> tuple[int, str]:
        return (self.pid, self.executable.casefold())

    @property
    def identity_text(self) -> str:
        return f"{self.pid}:{self.executable}"

    @classmethod
    def from_json_dict(
        cls,
        value: object,
        location: str,
    ) -> "HostProcess":
        data = _expect_mapping(value, location)
        _expect_exact_keys(
            data,
            {"pid", "parent_pid", "executable", "command"},
            location,
        )
        return cls(
            pid=_expect_int(data["pid"], f"{location}.pid", minimum=1),
            parent_pid=_expect_int(
                data["parent_pid"],
                f"{location}.parent_pid",
            ),
            executable=_expect_string(
                data["executable"],
                f"{location}.executable",
            ),
            command=_expect_string(data["command"], f"{location}.command"),
        )

    def to_json_dict(self) -> dict[str, object]:
        return {
            "pid": self.pid,
            "parent_pid": self.parent_pid,
            "executable": self.executable,
            "command": self.command,
        }


@dataclass(frozen=True)
class HostListener:
    protocol: str
    address: str
    port: int
    pid: int
    process_name: str

    @property
    def identity(self) -> tuple[str, str, int, int]:
        return (
            self.protocol.casefold(),
            self.address.casefold(),
            self.port,
            self.pid,
        )

    @property
    def identity_text(self) -> str:
        protocol, address, port, pid = self.identity
        return f"{protocol}:{address}:{port}:{pid}"

    @classmethod
    def from_json_dict(
        cls,
        value: object,
        location: str,
    ) -> "HostListener":
        data = _expect_mapping(value, location)
        _expect_exact_keys(
            data,
            {"protocol", "address", "port", "pid", "process_name"},
            location,
        )
        protocol = _expect_string(
            data["protocol"],
            f"{location}.protocol",
        ).casefold()
        if protocol != "tcp":
            raise _invalid(f"{location}.protocol", "must be 'tcp'")
        return cls(
            protocol=protocol,
            address=_expect_string(data["address"], f"{location}.address"),
            port=_expect_int(
                data["port"],
                f"{location}.port",
                minimum=1,
                maximum=65535,
            ),
            pid=_expect_int(data["pid"], f"{location}.pid", minimum=1),
            process_name=_expect_string(
                data["process_name"],
                f"{location}.process_name",
            ),
        )

    def to_json_dict(self) -> dict[str, object]:
        return {
            "protocol": self.protocol,
            "address": self.address,
            "port": self.port,
            "pid": self.pid,
            "process_name": self.process_name,
        }


def _parse_records(
    value: object,
    location: str,
    record_type: type[Any],
) -> tuple[Any, ...]:
    return tuple(
        record_type.from_json_dict(item, f"{location}[{index}]")
        for index, item in enumerate(_expect_array(value, location))
    )


def _deduplicate_and_sort(records: Sequence[Any]) -> tuple[Any, ...]:
    by_identity = {record.identity: record for record in records}
    return tuple(by_identity[key] for key in sorted(by_identity))


@dataclass(frozen=True)
class IsolationSnapshot:
    windows: tuple[VisibleWindow, ...]
    maho_processes: tuple[HostProcess, ...]
    tart_run_processes: tuple[HostProcess, ...]
    listeners: tuple[HostListener, ...]
    schema_version: ClassVar[int] = _SCHEMA_VERSION

    @classmethod
    def from_json_dict(cls, value: object) -> "IsolationSnapshot":
        data = _expect_mapping(value, "snapshot")
        if "schema_version" not in data:
            raise _invalid("snapshot", "missing keys: schema_version")
        _expect_schema(data["schema_version"], "snapshot")
        _expect_exact_keys(
            data,
            {
                "schema_version",
                "windows",
                "maho_processes",
                "tart_run_processes",
                "listeners",
            },
            "snapshot",
        )
        return cls(
            windows=_deduplicate_and_sort(
                _parse_records(data["windows"], "snapshot.windows", VisibleWindow)
            ),
            maho_processes=_deduplicate_and_sort(
                _parse_records(
                    data["maho_processes"],
                    "snapshot.maho_processes",
                    HostProcess,
                )
            ),
            tart_run_processes=_deduplicate_and_sort(
                _parse_records(
                    data["tart_run_processes"],
                    "snapshot.tart_run_processes",
                    HostProcess,
                )
            ),
            listeners=_deduplicate_and_sort(
                _parse_records(data["listeners"], "snapshot.listeners", HostListener)
            ),
        )

    def to_json_dict(self) -> dict[str, object]:
        return {
            "schema_version": self.schema_version,
            "windows": [window.to_json_dict() for window in self.windows],
            "maho_processes": [
                process.to_json_dict() for process in self.maho_processes
            ],
            "tart_run_processes": [
                process.to_json_dict() for process in self.tart_run_processes
            ],
            "listeners": [listener.to_json_dict() for listener in self.listeners],
        }


def parse_window_probe_payload(value: object) -> tuple[VisibleWindow, ...]:
    data = _expect_mapping(value, "window_probe")
    if "schema_version" not in data:
        raise _invalid("window_probe", "missing keys: schema_version")
    _expect_schema(data["schema_version"], "window_probe")
    _expect_exact_keys(data, {"schema_version", "windows"}, "window_probe")
    return _deduplicate_and_sort(
        _parse_records(data["windows"], "window_probe.windows", VisibleWindow)
    )


def parse_window_probe_output(output: str) -> tuple[VisibleWindow, ...]:
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as error:
        raise IsolationProbeError("window probe returned invalid JSON") from error
    return parse_window_probe_payload(payload)


def _executable_name(executable: str) -> str:
    return executable.rstrip("/").rsplit("/", 1)[-1].casefold()


def _is_maho_process(process: HostProcess) -> bool:
    executable = process.executable.casefold()
    name = _executable_name(process.executable)
    return (
        name == "maho"
        or name.startswith("maho helper")
        or "/maho.app/" in executable
    )


def _is_headless_tart_run(process: HostProcess) -> bool:
    if _executable_name(process.executable) != "tart":
        return False
    try:
        arguments = shlex.split(process.command)
    except ValueError:
        return False
    return "run" in arguments[1:] and "--no-graphics" in arguments[1:]


def parse_ps_processes(output: str) -> tuple[HostProcess, ...]:
    processes: list[HostProcess] = []
    for line_number, raw_line in enumerate(output.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        fields = line.split(None, 2)
        if len(fields) != 3:
            raise _invalid(f"ps line {line_number}", "expected PID, PPID, and command")
        raw_pid, raw_parent_pid, command = fields
        try:
            pid = int(raw_pid)
            parent_pid = int(raw_parent_pid)
        except ValueError as error:
            raise _invalid(
                f"ps line {line_number}",
                "PID and PPID must be integers",
            ) from error
        if pid < 1 or parent_pid < 0:
            raise _invalid(
                f"ps line {line_number}",
                "PID must be positive and PPID must not be negative",
            )
        executable = command.split(None, 1)[0]
        processes.append(
            HostProcess(
                pid=pid,
                parent_pid=parent_pid,
                executable=executable,
                command=command,
            )
        )
    return _deduplicate_and_sort(processes)


def _parse_listener_endpoint(value: str, location: str) -> tuple[str, int]:
    endpoint = value.removeprefix("TCP ").removesuffix(" (LISTEN)")
    if "->" in endpoint:
        raise _invalid(location, "expected a listening endpoint")
    address, separator, raw_port = endpoint.rpartition(":")
    if not separator or not address or not raw_port.isdigit():
        raise _invalid(location, f"invalid listening endpoint {value!r}")
    if address.startswith("[") and address.endswith("]"):
        address = address[1:-1]
    port = int(raw_port)
    if not 1 <= port <= 65535:
        raise _invalid(location, "port must be between 1 and 65535")
    return address, port


def parse_lsof_listeners(output: str) -> tuple[HostListener, ...]:
    listeners: list[HostListener] = []
    current_pid: int | None = None
    current_process_name: str | None = None
    for line_number, raw_line in enumerate(output.splitlines(), start=1):
        if not raw_line:
            continue
        field = raw_line[0]
        value = raw_line[1:]
        if field == "p":
            try:
                current_pid = int(value)
            except ValueError as error:
                raise _invalid(
                    f"lsof line {line_number}",
                    "process ID must be an integer",
                ) from error
            if current_pid < 1:
                raise _invalid(
                    f"lsof line {line_number}",
                    "process ID must be positive",
                )
            current_process_name = None
        elif field == "c":
            if current_pid is None:
                raise _invalid(
                    f"lsof line {line_number}",
                    "process name appeared before a process ID",
                )
            current_process_name = _expect_string(
                value,
                f"lsof line {line_number}",
            )
        elif field == "n":
            if current_pid is None or current_process_name is None:
                raise _invalid(
                    f"lsof line {line_number}",
                    "listener appeared before process metadata",
                )
            address, port = _parse_listener_endpoint(
                value,
                f"lsof line {line_number}",
            )
            listeners.append(
                HostListener(
                    protocol="tcp",
                    address=address,
                    port=port,
                    pid=current_pid,
                    process_name=current_process_name,
                )
            )
    return _deduplicate_and_sort(listeners)


def _run_checked(
    runner: Runner,
    argv: Sequence[str],
    *,
    timeout: float,
    empty_status_one_is_success: bool = False,
) -> str:
    try:
        result = runner.run(tuple(argv), timeout=timeout)
    except (OSError, subprocess.SubprocessError) as error:
        raise IsolationProbeError(
            f"command failed to start: {tuple(argv)!r}: {error}"
        ) from error
    if result.returncode == 0:
        return result.stdout
    if (
        empty_status_one_is_success
        and result.returncode == 1
        and not result.stdout
        and not result.stderr.strip()
    ):
        return ""
    detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
    raise IsolationProbeError(
        f"command failed ({result.returncode}): {tuple(argv)!r}: {detail}"
    )


def capture_snapshot(
    *,
    runner: Runner | None = None,
    window_probe_path: Path = _DEFAULT_WINDOW_PROBE,
    timeout: float = _DEFAULT_TIMEOUT_SECONDS,
) -> IsolationSnapshot:
    if timeout <= 0:
        raise ValueError("timeout must be positive")
    if not window_probe_path.is_file():
        raise IsolationProbeError(f"window probe does not exist: {window_probe_path}")
    runner = runner or SubprocessRunner()

    windows = parse_window_probe_output(
        _run_checked(
            runner,
            ("xcrun", "swift", str(window_probe_path)),
            timeout=timeout,
        )
    )
    processes = parse_ps_processes(
        _run_checked(runner, _PROCESS_COMMAND, timeout=timeout)
    )
    listeners = parse_lsof_listeners(
        _run_checked(
            runner,
            _LISTENER_COMMAND,
            timeout=timeout,
            empty_status_one_is_success=True,
        )
    )
    return IsolationSnapshot(
        windows=windows,
        maho_processes=_deduplicate_and_sort(
            tuple(process for process in processes if _is_maho_process(process))
        ),
        tart_run_processes=_deduplicate_and_sort(
            tuple(
                process for process in processes if _is_headless_tart_run(process)
            )
        ),
        listeners=listeners,
    )


Record = TypeVar("Record", VisibleWindow, HostProcess, HostListener)


def _new_records(
    baseline: Sequence[Record],
    current: Sequence[Record],
) -> tuple[Record, ...]:
    baseline_identities = {record.identity for record in baseline}
    return tuple(
        record
        for record in _deduplicate_and_sort(current)
        if record.identity not in baseline_identities
    )


@dataclass(frozen=True)
class IsolationViolation:
    code: str
    phase: str
    identity: str
    detail: str

    def to_json_dict(self) -> dict[str, str]:
        return {
            "code": self.code,
            "phase": self.phase,
            "identity": self.identity,
            "detail": self.detail,
        }


@dataclass(frozen=True)
class IsolationDelta:
    phase: str
    new_windows: tuple[VisibleWindow, ...]
    new_maho_processes: tuple[HostProcess, ...]
    new_tart_run_processes: tuple[HostProcess, ...]
    new_listeners: tuple[HostListener, ...]
    violations: tuple[IsolationViolation, ...]

    @property
    def is_isolated(self) -> bool:
        return not self.violations

    @property
    def violation_codes(self) -> tuple[str, ...]:
        return tuple(dict.fromkeys(violation.code for violation in self.violations))

    def to_json_dict(self) -> dict[str, object]:
        return {
            "phase": self.phase,
            "is_isolated": self.is_isolated,
            "new_windows": [window.to_json_dict() for window in self.new_windows],
            "new_maho_processes": [
                process.to_json_dict() for process in self.new_maho_processes
            ],
            "new_tart_run_processes": [
                process.to_json_dict() for process in self.new_tart_run_processes
            ],
            "new_listeners": [
                listener.to_json_dict() for listener in self.new_listeners
            ],
            "violations": [
                violation.to_json_dict() for violation in self.violations
            ],
        }


def compare_snapshots(
    baseline: IsolationSnapshot,
    current: IsolationSnapshot,
    *,
    phase: str,
    cdp_port: int,
    forbid_tart_run: bool = False,
) -> IsolationDelta:
    if not phase or phase != phase.strip():
        raise ValueError("phase must be a non-empty string without outer whitespace")
    if not 1 <= cdp_port <= 65535:
        raise ValueError("cdp_port must be between 1 and 65535")

    new_windows = _new_records(baseline.windows, current.windows)
    new_maho_processes = _new_records(
        baseline.maho_processes,
        current.maho_processes,
    )
    new_tart_run_processes = _new_records(
        baseline.tart_run_processes,
        current.tart_run_processes,
    )
    new_listeners = _new_records(baseline.listeners, current.listeners)

    violations: list[IsolationViolation] = []
    for window in new_windows:
        if _normalized_owner(window.owner_name) in _VISIBLE_WINDOW_OWNERS:
            violations.append(
                IsolationViolation(
                    code="new_visible_window",
                    phase=phase,
                    identity=window.identity_text,
                    detail=(
                        f"new on-screen {window.owner_name} window appeared on the host"
                    ),
                )
            )
    for process in new_maho_processes:
        violations.append(
            IsolationViolation(
                code="new_host_maho_process",
                phase=phase,
                identity=process.identity_text,
                detail="new Maho process appeared on the host",
            )
        )
    if forbid_tart_run:
        for process in new_tart_run_processes:
            violations.append(
                IsolationViolation(
                    code="new_host_tart_run_process",
                    phase=phase,
                    identity=process.identity_text,
                    detail="new Tart run process remained on the host after teardown",
                )
            )
    for listener in new_listeners:
        if listener.port == cdp_port:
            violations.append(
                IsolationViolation(
                    code="new_host_cdp_listener",
                    phase=phase,
                    identity=listener.identity_text,
                    detail=(
                        "new host TCP listener appeared on guest CDP port "
                        f"{cdp_port}"
                    ),
                )
            )

    return IsolationDelta(
        phase=phase,
        new_windows=new_windows,
        new_maho_processes=new_maho_processes,
        new_tart_run_processes=new_tart_run_processes,
        new_listeners=new_listeners,
        violations=tuple(violations),
    )


@dataclass(frozen=True)
class IsolationRunReport:
    during: IsolationDelta
    after: IsolationDelta

    @property
    def violations(self) -> tuple[IsolationViolation, ...]:
        return self.during.violations + self.after.violations

    @property
    def is_isolated(self) -> bool:
        return not self.violations

    @property
    def violation_codes(self) -> tuple[str, ...]:
        return tuple(dict.fromkeys(violation.code for violation in self.violations))

    def to_json_dict(self) -> dict[str, object]:
        return {
            "schema_version": _SCHEMA_VERSION,
            "evidence_kind": "live_run",
            "is_isolated": self.is_isolated,
            "violation_codes": list(self.violation_codes),
            "during": self.during.to_json_dict(),
            "after": self.after.to_json_dict(),
        }


def compare_run_snapshots(
    baseline: IsolationSnapshot,
    during: IsolationSnapshot,
    after: IsolationSnapshot,
    *,
    cdp_port: int,
) -> IsolationRunReport:
    return IsolationRunReport(
        during=compare_snapshots(
            baseline,
            during,
            phase="during",
            cdp_port=cdp_port,
        ),
        after=compare_snapshots(
            baseline,
            after,
            phase="after",
            cdp_port=cdp_port,
            forbid_tart_run=True,
        ),
    )


@dataclass(frozen=True)
class IsolationEarlyExitReport:
    post_teardown_after_early_exit: IsolationDelta

    @property
    def violations(self) -> tuple[IsolationViolation, ...]:
        return self.post_teardown_after_early_exit.violations

    @property
    def is_isolated(self) -> bool:
        return not self.violations

    @property
    def violation_codes(self) -> tuple[str, ...]:
        return tuple(dict.fromkeys(violation.code for violation in self.violations))

    def to_json_dict(self) -> dict[str, object]:
        return {
            "schema_version": _SCHEMA_VERSION,
            "evidence_kind": "early_exit",
            "is_isolated": self.is_isolated,
            "violation_codes": list(self.violation_codes),
            "post_teardown_after_early_exit": (
                self.post_teardown_after_early_exit.to_json_dict()
            ),
        }


def compare_early_exit_snapshots(
    baseline: IsolationSnapshot,
    after: IsolationSnapshot,
    *,
    cdp_port: int,
) -> IsolationEarlyExitReport:
    return IsolationEarlyExitReport(
        post_teardown_after_early_exit=compare_snapshots(
            baseline,
            after,
            phase="post_teardown_after_early_exit",
            cdp_port=cdp_port,
            forbid_tart_run=True,
        )
    )


def _load_snapshot(path: Path) -> IsolationSnapshot:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise IsolationProbeError(f"failed to read snapshot {path}: {error}") from error
    return IsolationSnapshot.from_json_dict(payload)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    snapshot_parser = subparsers.add_parser(
        "snapshot",
        help="capture one host isolation snapshot",
    )
    snapshot_parser.add_argument(
        "--window-probe",
        type=Path,
        default=_DEFAULT_WINDOW_PROBE,
    )

    compare_parser = subparsers.add_parser(
        "compare",
        help="compare during/after snapshots with a baseline",
    )
    compare_parser.add_argument("--baseline", type=Path, required=True)
    compare_parser.add_argument("--during", type=Path, required=True)
    compare_parser.add_argument("--after", type=Path, required=True)
    compare_parser.add_argument("--cdp-port", type=int, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        if args.command == "snapshot":
            payload = capture_snapshot(
                window_probe_path=args.window_probe,
            ).to_json_dict()
        else:
            payload = compare_run_snapshots(
                _load_snapshot(args.baseline),
                _load_snapshot(args.during),
                _load_snapshot(args.after),
                cdp_port=args.cdp_port,
            ).to_json_dict()
    except (IsolationProbeError, ValueError) as error:
        print(
            json.dumps(
                {
                    "error": {
                        "code": "isolation_probe_failed",
                        "message": str(error),
                    }
                },
                sort_keys=True,
            ),
            file=sys.stderr,
        )
        return 1
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
