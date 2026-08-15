#!/usr/bin/env python3
"""Runtime primitives for the isolated macOS VM E2E controller.

This module deliberately owns no VM lifecycle policy.  It provides the small,
security-sensitive pieces shared by that lifecycle: bounded argv execution,
cancellation of owned process groups, redacted command records, and durable run
receipts.
"""

from __future__ import annotations

import dataclasses
import datetime
import enum
import json
import math
import os
import re
import signal
import subprocess
import tempfile
import threading
import time
from collections.abc import Callable, Mapping, Sequence
from pathlib import Path
from typing import Protocol


REDACTED = "<redacted>"
RECEIPT_SCHEMA_VERSION = 1

_CAMEL_CASE_BOUNDARY = re.compile(r"(?<=[a-z0-9])(?=[A-Z])")
_NAME_SEPARATOR = re.compile(r"[^a-z0-9]+")
_RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_SECRET_NAME_PARTS = frozenset({"key", "password", "secret", "token"})


def utc_timestamp() -> str:
    """Returns an RFC 3339 UTC timestamp suitable for a receipt."""

    return (
        datetime.datetime.now(datetime.timezone.utc)
        .isoformat(timespec="milliseconds")
        .replace("+00:00", "Z")
    )


def _is_secret_name(name: str) -> bool:
    expanded = _CAMEL_CASE_BOUNDARY.sub("_", name)
    parts = _NAME_SEPARATOR.split(expanded.lower())
    return any(part in _SECRET_NAME_PARTS for part in parts)


def _validate_environment(environment: Mapping[str, str]) -> None:
    for key, value in environment.items():
        if not isinstance(key, str) or not isinstance(value, str):
            raise TypeError("environment keys and values must be strings")


def _environment_secret_values(environment: Mapping[str, str]) -> tuple[str, ...]:
    return tuple(
        value
        for key, value in environment.items()
        if value and _is_secret_name(key)
    )


def _argv_secret_values(argv: Sequence[str]) -> tuple[str, ...]:
    values: list[str] = []
    redact_next = False
    for argument in argv:
        if redact_next:
            if argument:
                values.append(argument)
            redact_next = False
            continue

        option, separator, value = argument.partition("=")
        if separator and _is_secret_name(option.lstrip("-")):
            if value:
                values.append(value)
        elif argument.startswith("-") and _is_secret_name(
            argument.lstrip("-")
        ):
            redact_next = True
        for word in argument.split():
            name, assignment, assigned_value = word.partition("=")
            if assignment and _is_secret_name(name) and assigned_value:
                values.append(assigned_value)
    return tuple(values)


def _replace_secret_values(text: str, secret_values: Sequence[str]) -> str:
    redacted = text
    for secret in sorted(set(secret_values), key=len, reverse=True):
        if secret:
            redacted = redacted.replace(secret, REDACTED)
    return redacted


def redact_environment(environment: Mapping[str, str]) -> dict[str, str]:
    """Returns a stable environment mapping with secret-like values removed."""

    _validate_environment(environment)
    return {
        key: REDACTED if _is_secret_name(key) else value
        for key, value in sorted(environment.items())
    }


def redact_argv(
    argv: Sequence[str], secret_values: Sequence[str] = ()
) -> tuple[str, ...]:
    """Redacts secret options and known secret values from an argv sequence."""

    redacted: list[str] = []
    redact_next = False
    for argument in argv:
        if redact_next:
            redacted.append(REDACTED)
            redact_next = False
            continue

        option, separator, value = argument.partition("=")
        if separator and _is_secret_name(option.lstrip("-")):
            redacted.append(f"{option}={REDACTED}")
        else:
            redacted.append(_replace_secret_values(argument, secret_values))
            if argument.startswith("-") and _is_secret_name(
                argument.lstrip("-")
            ):
                redact_next = True
    return tuple(redacted)


class TerminalStatus(str, enum.Enum):
    """Exhaustive terminal states for one VM E2E run."""

    SUCCESS = "success"
    DOCTOR_FAILED = "doctor_failed"
    PREPARE_FAILED = "prepare_failed"
    BUILD_FAILED = "build_failed"
    VM_START_FAILED = "vm_start_failed"
    GUEST_NOT_READY = "guest_not_ready"
    TRANSPORT_FAILED = "transport_failed"
    SUITE_FAILED = "suite_failed"
    ARTIFACT_FAILED = "artifact_failed"
    ISOLATION_FAILED = "isolation_failed"
    CANCELLED = "cancelled"
    TEARDOWN_FAILED = "teardown_failed"


class CommandStatus(str, enum.Enum):
    """How a subprocess invocation stopped."""

    EXITED = "exited"
    CANCELLED = "cancelled"
    TIMED_OUT = "timed_out"
    START_FAILED = "start_failed"
    TERMINATION_FAILED = "termination_failed"


class CancellationRegistration:
    """An idempotent registration owned by a single cancellation token."""

    def __init__(self, token: CancellationToken | None, registration_id: int):
        self._token = token
        self._registration_id = registration_id
        self._lock = threading.Lock()

    def unregister(self) -> None:
        with self._lock:
            token = self._token
            self._token = None
        if token is not None:
            token._unregister(self._registration_id)


class CancellationToken:
    """Thread-safe cancellation with immediate callback notification."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._cancelled = False
        self._reason = "cancelled"
        self._next_registration_id = 1
        self._callbacks: dict[int, Callable[[], None]] = {}

    @property
    def is_cancelled(self) -> bool:
        with self._lock:
            return self._cancelled

    @property
    def reason(self) -> str:
        with self._lock:
            return self._reason

    def cancel(self, reason: str = "cancelled") -> bool:
        """Cancels once and synchronously notifies all current registrations."""

        if not reason:
            raise ValueError("cancellation reason must not be empty")
        with self._lock:
            if self._cancelled:
                return False
            self._cancelled = True
            self._reason = reason
            callbacks = tuple(self._callbacks.values())
            self._callbacks.clear()
        for callback in callbacks:
            callback()
        return True

    def register(self, callback: Callable[[], None]) -> CancellationRegistration:
        """Registers a callback, invoking it immediately when already cancelled."""

        with self._lock:
            if self._cancelled:
                invoke_now = True
                registration_id = 0
            else:
                invoke_now = False
                registration_id = self._next_registration_id
                self._next_registration_id += 1
                self._callbacks[registration_id] = callback
        if invoke_now:
            callback()
            return CancellationRegistration(None, 0)
        return CancellationRegistration(self, registration_id)

    def _unregister(self, registration_id: int) -> None:
        with self._lock:
            self._callbacks.pop(registration_id, None)


@dataclasses.dataclass(frozen=True)
class CommandInvocation:
    """A log-safe description of a subprocess invocation."""

    argv: tuple[str, ...]
    cwd: str | None
    environment: Mapping[str, str]
    inherits_environment: bool
    deadline: float

    def to_dict(self) -> dict[str, object]:
        return {
            "argv": list(self.argv),
            "cwd": self.cwd,
            "environment": dict(self.environment),
            "inherits_environment": self.inherits_environment,
            "deadline": self.deadline,
        }


@dataclasses.dataclass(frozen=True)
class CommandResult:
    """Structured, redacted result from an argv subprocess invocation."""

    invocation: CommandInvocation
    status: CommandStatus
    pid: int | None
    returncode: int | None
    stdout: str
    stderr: str
    started_at: str
    finished_at: str
    duration_seconds: float
    error: str | None = None

    @property
    def succeeded(self) -> bool:
        return self.status is CommandStatus.EXITED and self.returncode == 0

    def to_dict(self) -> dict[str, object]:
        return {
            "invocation": self.invocation.to_dict(),
            "status": self.status.value,
            "pid": self.pid,
            "returncode": self.returncode,
            "stdout": self.stdout,
            "stderr": self.stderr,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "duration_seconds": self.duration_seconds,
            "error": self.error,
        }


class ArgvRunner(Protocol):
    """Injectable subprocess boundary used by the VM lifecycle controller."""

    def run(
        self,
        argv: Sequence[str],
        *,
        deadline: float,
        cwd: str | os.PathLike[str] | None = None,
        environment: Mapping[str, str] | None = None,
        cancellation: CancellationToken | None = None,
        on_process_started: Callable[[int], None] | None = None,
    ) -> CommandResult:
        ...


class _TextProcess(Protocol):
    @property
    def pid(self) -> int:
        ...

    @property
    def returncode(self) -> int | None:
        ...

    def communicate(self, timeout: float | None = None) -> tuple[str, str]:
        ...


class SubprocessArgvRunner:
    """Runs explicit argv commands in owned process groups with hard deadlines."""

    def __init__(
        self,
        *,
        popen_factory: Callable[..., _TextProcess] | None = None,
        kill_process_group: Callable[[int, int], None] | None = None,
        monotonic_clock: Callable[[], float] = time.monotonic,
        timestamp_factory: Callable[[], str] = utc_timestamp,
        termination_grace_seconds: float = 5.0,
        on_invocation: Callable[[CommandInvocation], None] | None = None,
        on_process_started: Callable[[int], None] | None = None,
    ) -> None:
        if (
            not math.isfinite(termination_grace_seconds)
            or termination_grace_seconds <= 0
        ):
            raise ValueError("termination grace must be a finite positive number")
        self._popen_factory = popen_factory
        self._kill_process_group = kill_process_group or os.killpg
        self._monotonic_clock = monotonic_clock
        self._timestamp_factory = timestamp_factory
        self._termination_grace_seconds = termination_grace_seconds
        self._on_invocation = on_invocation
        self._on_process_started = on_process_started

    def deadline_after(self, seconds: float) -> float:
        if not math.isfinite(seconds) or seconds <= 0:
            raise ValueError("deadline duration must be a finite positive number")
        return self._monotonic_clock() + seconds

    def run(
        self,
        argv: Sequence[str],
        *,
        deadline: float,
        cwd: str | os.PathLike[str] | None = None,
        environment: Mapping[str, str] | None = None,
        cancellation: CancellationToken | None = None,
        on_process_started: Callable[[int], None] | None = None,
    ) -> CommandResult:
        argv_tuple = self._validate_argv(argv)
        if not math.isfinite(deadline):
            raise ValueError("deadline must be finite")
        if environment is not None:
            _validate_environment(environment)

        effective_environment = os.environ if environment is None else environment
        secret_values = (
            *_environment_secret_values(effective_environment),
            *_argv_secret_values(argv_tuple),
        )
        invocation = CommandInvocation(
            argv=redact_argv(argv_tuple, secret_values),
            cwd=os.fspath(cwd) if cwd is not None else None,
            environment=redact_environment(environment or {}),
            inherits_environment=environment is None,
            deadline=deadline,
        )
        if self._on_invocation is not None:
            self._on_invocation(invocation)

        started_monotonic = self._monotonic_clock()
        started_at = self._timestamp_factory()
        if cancellation is not None and cancellation.is_cancelled:
            return self._without_process(
                invocation=invocation,
                status=CommandStatus.CANCELLED,
                started_at=started_at,
                started_monotonic=started_monotonic,
                error=cancellation.reason,
            )
        if deadline <= started_monotonic:
            return self._without_process(
                invocation=invocation,
                status=CommandStatus.TIMED_OUT,
                started_at=started_at,
                started_monotonic=started_monotonic,
                error="deadline expired before process start",
            )

        process_factory = self._popen_factory or subprocess.Popen
        try:
            process = process_factory(
                argv_tuple,
                cwd=os.fspath(cwd) if cwd is not None else None,
                env=dict(environment) if environment is not None else None,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                shell=False,
                start_new_session=True,
            )
        except OSError as error:
            message = _replace_secret_values(str(error), secret_values)
            return self._without_process(
                invocation=invocation,
                status=CommandStatus.START_FAILED,
                started_at=started_at,
                started_monotonic=started_monotonic,
                stderr=message,
                error=message,
            )

        state_lock = threading.Lock()
        active = True
        cancel_requested = False

        def cancel_process() -> None:
            nonlocal cancel_requested
            with state_lock:
                if not active:
                    return
                cancel_requested = True
                self._signal_process_group(process.pid, signal.SIGKILL)

        registration = (
            cancellation.register(cancel_process)
            if cancellation is not None
            else CancellationRegistration(None, 0)
        )
        process_started_error: BaseException | None = None
        try:
            if self._on_process_started is not None:
                self._on_process_started(process.pid)
            if on_process_started is not None:
                on_process_started(process.pid)
        except BaseException as error:
            process_started_error = error

        status = CommandStatus.EXITED
        error_message: str | None = None
        stdout = ""
        stderr = ""
        termination_failed = False
        remaining = max(0.0, deadline - self._monotonic_clock())
        try:
            stdout, stderr = process.communicate(timeout=remaining)
        except subprocess.TimeoutExpired as initial_timeout:
            with state_lock:
                cancelled_at_deadline = cancel_requested
            if cancelled_at_deadline:
                self._signal_process_group(process.pid, signal.SIGKILL)
            else:
                self._signal_process_group(process.pid, signal.SIGTERM)
            try:
                stdout, stderr = process.communicate(
                    timeout=self._termination_grace_seconds
                )
            except subprocess.TimeoutExpired as term_timeout:
                self._signal_process_group(process.pid, signal.SIGKILL)
                try:
                    stdout, stderr = process.communicate(
                        timeout=self._termination_grace_seconds
                    )
                except subprocess.TimeoutExpired as kill_timeout:
                    stdout = self._timeout_output(
                        kill_timeout.output,
                        term_timeout.output,
                        initial_timeout.output,
                    )
                    stderr = self._timeout_output(
                        kill_timeout.stderr,
                        term_timeout.stderr,
                        initial_timeout.stderr,
                    )
                    termination_failed = True
            with state_lock:
                cancelled_at_deadline = cancel_requested
            if termination_failed:
                status = CommandStatus.TERMINATION_FAILED
                error_message = "owned process group did not terminate"
            elif cancelled_at_deadline:
                status = CommandStatus.CANCELLED
                error_message = (
                    cancellation.reason if cancellation is not None else "cancelled"
                )
            else:
                status = CommandStatus.TIMED_OUT
                error_message = "deadline exceeded"
        finally:
            with state_lock:
                active = False
                was_cancelled = cancel_requested
            registration.unregister()

        if status is CommandStatus.EXITED and was_cancelled:
            status = CommandStatus.CANCELLED
            error_message = (
                cancellation.reason if cancellation is not None else "cancelled"
            )

        finished_monotonic = self._monotonic_clock()
        finished_at = self._timestamp_factory()
        stdout = _replace_secret_values(self._coerce_output(stdout), secret_values)
        stderr = _replace_secret_values(self._coerce_output(stderr), secret_values)
        if error_message is not None:
            error_message = _replace_secret_values(error_message, secret_values)
        result = CommandResult(
            invocation=invocation,
            status=status,
            pid=process.pid,
            returncode=process.returncode,
            stdout=stdout,
            stderr=stderr,
            started_at=started_at,
            finished_at=finished_at,
            duration_seconds=max(0.0, finished_monotonic - started_monotonic),
            error=error_message,
        )
        if process_started_error is not None:
            raise process_started_error
        return result

    @staticmethod
    def _validate_argv(argv: Sequence[str]) -> tuple[str, ...]:
        if isinstance(argv, (str, bytes)):
            raise TypeError("argv must be a sequence of strings, not a string")
        argv_tuple = tuple(argv)
        if not argv_tuple:
            raise ValueError("argv must not be empty")
        if any(not isinstance(argument, str) for argument in argv_tuple):
            raise TypeError("argv entries must be strings")
        if not argv_tuple[0]:
            raise ValueError("argv executable must not be empty")
        return argv_tuple

    def _signal_process_group(self, process_group_id: int, sig: int) -> None:
        try:
            self._kill_process_group(process_group_id, sig)
        except ProcessLookupError:
            return

    def _without_process(
        self,
        *,
        invocation: CommandInvocation,
        status: CommandStatus,
        started_at: str,
        started_monotonic: float,
        stderr: str = "",
        error: str | None = None,
    ) -> CommandResult:
        finished_monotonic = self._monotonic_clock()
        return CommandResult(
            invocation=invocation,
            status=status,
            pid=None,
            returncode=None,
            stdout="",
            stderr=stderr,
            started_at=started_at,
            finished_at=self._timestamp_factory(),
            duration_seconds=max(0.0, finished_monotonic - started_monotonic),
            error=error,
        )

    @staticmethod
    def _coerce_output(output: str | bytes | None) -> str:
        if output is None:
            return ""
        if isinstance(output, bytes):
            return output.decode("utf-8", errors="replace")
        return output

    @classmethod
    def _timeout_output(cls, *outputs: str | bytes | None) -> str:
        for output in outputs:
            if output is not None:
                return cls._coerce_output(output)
        return ""


@dataclasses.dataclass(frozen=True)
class SuiteResult:
    name: str
    status: str
    started_at: str
    finished_at: str
    returncode: int | None = None
    artifact_paths: tuple[str, ...] = ()
    details: Mapping[str, object] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        if not self.name or not self.status:
            raise ValueError("suite name and status must not be empty")
        object.__setattr__(self, "artifact_paths", tuple(self.artifact_paths))
        object.__setattr__(self, "details", dict(self.details))

    def to_dict(self) -> dict[str, object]:
        return {
            "name": self.name,
            "status": self.status,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "returncode": self.returncode,
            "artifact_paths": list(self.artifact_paths),
            "details": _redact_structure(self.details),
        }


@dataclasses.dataclass(frozen=True)
class IsolationSnapshot:
    stage: str
    captured_at: str
    facts: Mapping[str, object]
    violations: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        if not self.stage:
            raise ValueError("isolation snapshot stage must not be empty")
        object.__setattr__(self, "facts", dict(self.facts))
        object.__setattr__(self, "violations", tuple(self.violations))

    def to_dict(self) -> dict[str, object]:
        return {
            "stage": self.stage,
            "captured_at": self.captured_at,
            "facts": _redact_structure(self.facts),
            "violations": list(self.violations),
        }


@dataclasses.dataclass(frozen=True)
class TeardownResult:
    attempted: bool
    succeeded: bool
    started_at: str | None = None
    finished_at: str | None = None
    error: str | None = None
    details: Mapping[str, object] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        if self.succeeded and self.error is not None:
            raise ValueError("successful teardown cannot contain an error")
        object.__setattr__(self, "details", dict(self.details))

    def to_dict(self) -> dict[str, object]:
        return {
            "attempted": self.attempted,
            "succeeded": self.succeeded,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "error": self.error,
            "details": _redact_structure(self.details),
        }


def resolve_terminal_status(
    primary_status: TerminalStatus, teardown_result: TeardownResult
) -> TerminalStatus:
    """Applies teardown precedence without hiding an earlier primary failure."""

    if (
        primary_status is TerminalStatus.SUCCESS
        and not teardown_result.succeeded
    ):
        return TerminalStatus.TEARDOWN_FAILED
    return primary_status


@dataclasses.dataclass(frozen=True)
class RunReceipt:
    """Immutable state written at checkpoints and at terminal completion."""

    run_id: str
    started_at: str
    updated_at: str
    host_facts: Mapping[str, object] = dataclasses.field(default_factory=dict)
    guest_facts: Mapping[str, object] = dataclasses.field(default_factory=dict)
    source_image_identity: Mapping[str, object] = dataclasses.field(
        default_factory=dict
    )
    build_path: str | None = None
    suite_results: tuple[SuiteResult, ...] = ()
    artifact_paths: tuple[str, ...] = ()
    isolation_snapshots: tuple[IsolationSnapshot, ...] = ()
    teardown_result: TeardownResult | None = None
    primary_status: TerminalStatus | None = None
    terminal_status: TerminalStatus | None = None
    finished_at: str | None = None
    finalized: bool = False
    schema_version: int = RECEIPT_SCHEMA_VERSION

    def __post_init__(self) -> None:
        _validate_run_id(self.run_id)
        if not self.started_at or not self.updated_at:
            raise ValueError("receipt timestamps must not be empty")
        object.__setattr__(self, "host_facts", dict(self.host_facts))
        object.__setattr__(self, "guest_facts", dict(self.guest_facts))
        object.__setattr__(
            self, "source_image_identity", dict(self.source_image_identity)
        )
        object.__setattr__(self, "suite_results", tuple(self.suite_results))
        object.__setattr__(self, "artifact_paths", tuple(self.artifact_paths))
        object.__setattr__(
            self, "isolation_snapshots", tuple(self.isolation_snapshots)
        )
        if self.finalized:
            if (
                self.primary_status is None
                or self.terminal_status is None
                or self.finished_at is None
                or self.teardown_result is None
            ):
                raise ValueError(
                    "final receipt requires statuses, finished_at, and teardown"
                )
        elif self.terminal_status is not None or self.finished_at is not None:
            raise ValueError(
                "partial receipt cannot contain terminal status or finished_at"
            )

    @classmethod
    def start(
        cls,
        run_id: str,
        *,
        timestamp: str | None = None,
        host_facts: Mapping[str, object] | None = None,
        guest_facts: Mapping[str, object] | None = None,
        source_image_identity: Mapping[str, object] | None = None,
        build_path: str | None = None,
    ) -> RunReceipt:
        started_at = timestamp or utc_timestamp()
        return cls(
            run_id=run_id,
            started_at=started_at,
            updated_at=started_at,
            host_facts=host_facts or {},
            guest_facts=guest_facts or {},
            source_image_identity=source_image_identity or {},
            build_path=build_path,
        )

    def finalize(
        self,
        *,
        primary_status: TerminalStatus,
        teardown_result: TeardownResult,
        finished_at: str | None = None,
    ) -> RunReceipt:
        if self.finalized:
            raise ValueError("receipt is already finalized")
        completed_at = finished_at or utc_timestamp()
        return dataclasses.replace(
            self,
            teardown_result=teardown_result,
            primary_status=primary_status,
            terminal_status=resolve_terminal_status(
                primary_status, teardown_result
            ),
            updated_at=completed_at,
            finished_at=completed_at,
            finalized=True,
        )

    def to_dict(self) -> dict[str, object]:
        return {
            "schema_version": self.schema_version,
            "run_id": self.run_id,
            "started_at": self.started_at,
            "updated_at": self.updated_at,
            "finished_at": self.finished_at,
            "finalized": self.finalized,
            "primary_status": (
                self.primary_status.value if self.primary_status is not None else None
            ),
            "terminal_status": (
                self.terminal_status.value
                if self.terminal_status is not None
                else None
            ),
            "host_facts": _redact_structure(self.host_facts),
            "guest_facts": _redact_structure(self.guest_facts),
            "source_image_identity": _redact_structure(
                self.source_image_identity
            ),
            "build_path": self.build_path,
            "suite_results": [result.to_dict() for result in self.suite_results],
            "artifact_paths": list(self.artifact_paths),
            "isolation_snapshots": [
                snapshot.to_dict() for snapshot in self.isolation_snapshots
            ],
            "teardown_result": (
                self.teardown_result.to_dict()
                if self.teardown_result is not None
                else None
            ),
        }


class AtomicReceiptWriter:
    """Atomically checkpoints and finalizes one canonical receipt.json file."""

    def __init__(
        self,
        workspace_root: str | os.PathLike[str],
        run_id: str,
        *,
        replace_file: Callable[[str, str], None] = os.replace,
    ) -> None:
        _validate_run_id(run_id)
        self._run_id = run_id
        self._replace_file = replace_file
        self.run_directory = (
            Path(workspace_root) / "test-results" / "macos-vm" / run_id
        )
        self.receipt_path = self.run_directory / "receipt.json"

    def write_partial(self, receipt: RunReceipt) -> Path:
        if receipt.finalized:
            raise ValueError("write_partial requires a partial receipt")
        return self._write(receipt)

    def write_final(self, receipt: RunReceipt) -> Path:
        if not receipt.finalized:
            raise ValueError("write_final requires a finalized receipt")
        return self._write(receipt)

    def finalize(
        self,
        receipt: RunReceipt,
        *,
        primary_status: TerminalStatus,
        teardown_result: TeardownResult,
        finished_at: str | None = None,
    ) -> RunReceipt:
        final_receipt = receipt.finalize(
            primary_status=primary_status,
            teardown_result=teardown_result,
            finished_at=finished_at,
        )
        self.write_final(final_receipt)
        return final_receipt

    def _write(self, receipt: RunReceipt) -> Path:
        if receipt.run_id != self._run_id:
            raise ValueError("receipt run_id does not match writer run_id")
        payload = json.dumps(
            receipt.to_dict(),
            allow_nan=False,
            indent=2,
            sort_keys=True,
        ) + "\n"
        self.run_directory.mkdir(parents=True, exist_ok=True)
        file_descriptor, temporary_name = tempfile.mkstemp(
            prefix=".receipt.", suffix=".tmp", dir=self.run_directory
        )
        temporary_path: Path | None = Path(temporary_name)
        try:
            with os.fdopen(file_descriptor, "w", encoding="utf-8") as output:
                output.write(payload)
                output.flush()
                os.fsync(output.fileno())
            self._replace_file(str(temporary_path), str(self.receipt_path))
            temporary_path = None
            self._fsync_directory()
        finally:
            if temporary_path is not None:
                try:
                    temporary_path.unlink()
                except FileNotFoundError:
                    pass
        return self.receipt_path

    def _fsync_directory(self) -> None:
        directory_descriptor = os.open(self.run_directory, os.O_RDONLY)
        try:
            os.fsync(directory_descriptor)
        finally:
            os.close(directory_descriptor)


def _validate_run_id(run_id: str) -> None:
    if not isinstance(run_id, str) or not _RUN_ID.fullmatch(run_id):
        raise ValueError(f"invalid run ID: {run_id!r}")
    if run_id in {".", ".."}:
        raise ValueError(f"invalid run ID: {run_id!r}")


def _redact_structure(value: object) -> object:
    if isinstance(value, enum.Enum):
        return value.value
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, Mapping):
        redacted: dict[str, object] = {}
        for key in sorted(value):
            if not isinstance(key, str):
                raise TypeError("receipt mapping keys must be strings")
            redacted[key] = (
                REDACTED if _is_secret_name(key) else _redact_structure(value[key])
            )
        return redacted
    if isinstance(value, (list, tuple)):
        return [_redact_structure(item) for item in value]
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    raise TypeError(f"unsupported receipt value: {type(value).__name__}")


# Concise aliases for callers that do not need to name the implementation detail.
SubprocessRunner = SubprocessArgvRunner
ReceiptWriter = AtomicReceiptWriter


__all__ = [
    "ArgvRunner",
    "AtomicReceiptWriter",
    "CancellationToken",
    "CommandInvocation",
    "CommandResult",
    "CommandStatus",
    "IsolationSnapshot",
    "REDACTED",
    "ReceiptWriter",
    "RunReceipt",
    "SubprocessArgvRunner",
    "SubprocessRunner",
    "SuiteResult",
    "TeardownResult",
    "TerminalStatus",
    "redact_argv",
    "redact_environment",
    "resolve_terminal_status",
    "utc_timestamp",
]
