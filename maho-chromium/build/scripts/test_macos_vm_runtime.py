#!/usr/bin/env python3

from __future__ import annotations

import dataclasses
import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
if str(SCRIPT_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIRECTORY))

import macos_vm_runtime as runtime


TIMESTAMP = "2026-08-13T00:00:00.000Z"
FINISHED_TIMESTAMP = "2026-08-13T00:01:00.000Z"


class ImmediateProcess:
    def __init__(
        self,
        *,
        pid: int = 4100,
        returncode: int | None = 0,
        stdout: str = "",
        stderr: str = "",
    ) -> None:
        self.pid = pid
        self.returncode = returncode
        self._stdout = stdout
        self._stderr = stderr

    def communicate(self, timeout: float | None = None) -> tuple[str, str]:
        del timeout
        return self._stdout, self._stderr


class BlockingProcess:
    def __init__(self, pid: int = 4200) -> None:
        self.pid = pid
        self.returncode: int | None = None
        self._finished = threading.Event()

    def communicate(self, timeout: float | None = None) -> tuple[str, str]:
        if not self._finished.wait(timeout):
            raise subprocess.TimeoutExpired(
                ["blocking-command"], 0.0 if timeout is None else timeout
            )
        return "cancelled cleanly", ""

    def finish_from_signal(self, sig: int) -> None:
        self.returncode = -sig
        self._finished.set()


class EscalatingTimeoutProcess:
    def __init__(self, pid: int = 4300) -> None:
        self.pid = pid
        self.returncode: int | None = None
        self._killed = False
        self.communicate_calls = 0

    def communicate(self, timeout: float | None = None) -> tuple[str, str]:
        self.communicate_calls += 1
        if self._killed:
            return "partial output", ""
        raise subprocess.TimeoutExpired(
            ["timeout-command"],
            0.0 if timeout is None else timeout,
            output="partial output",
        )

    def receive_signal(self, sig: int) -> None:
        if sig == signal.SIGKILL:
            self._killed = True
            self.returncode = -sig


def make_partial_receipt() -> runtime.RunReceipt:
    return runtime.RunReceipt(
        run_id="run-123",
        started_at=TIMESTAMP,
        updated_at=TIMESTAMP,
        host_facts={"architecture": "arm64", "api_token": "host-secret"},
        guest_facts={"console_user": "admin"},
        source_image_identity={"name": "tahoe", "password": "image-secret"},
        build_path="out/Default",
        suite_results=(
            runtime.SuiteResult(
                name="interactive-smoke",
                status="success",
                started_at=TIMESTAMP,
                finished_at=FINISHED_TIMESTAMP,
                returncode=0,
                artifact_paths=("interactive/summary.json",),
            ),
        ),
        artifact_paths=("commands.log",),
        isolation_snapshots=(
            runtime.IsolationSnapshot(
                stage="baseline",
                captured_at=TIMESTAMP,
                facts={"host_maho_pids": []},
            ),
        ),
    )


def successful_teardown() -> runtime.TeardownResult:
    return runtime.TeardownResult(
        attempted=True,
        succeeded=True,
        started_at=TIMESTAMP,
        finished_at=FINISHED_TIMESTAMP,
    )


class SubprocessArgvRunnerTest(unittest.TestCase):
    def test_per_command_process_started_callback_runs_before_process_exit(self) -> None:
        events: list[str] = []

        class OrderedProcess(ImmediateProcess):
            def communicate(self, timeout: float | None = None) -> tuple[str, str]:
                events.append("process_exit")
                return super().communicate(timeout)

        runner = runtime.SubprocessArgvRunner(
            popen_factory=lambda *args, **kwargs: OrderedProcess()
        )

        result = runner.run(
            ("guest-suite",),
            deadline=runner.deadline_after(5.0),
            on_process_started=lambda pid: events.append(f"snapshot:{pid}"),
        )

        self.assertTrue(result.succeeded)
        self.assertEqual(events, ["snapshot:4100", "process_exit"])

    def test_runs_explicit_argv_and_redacts_every_recorded_secret(self) -> None:
        captured_argv: tuple[str, ...] | None = None
        captured_kwargs: dict[str, object] | None = None
        invocations: list[runtime.CommandInvocation] = []

        def popen_factory(argv: tuple[str, ...], **kwargs: object) -> ImmediateProcess:
            nonlocal captured_argv, captured_kwargs
            captured_argv = argv
            captured_kwargs = kwargs
            return ImmediateProcess(
                stdout="api-value password-value visible",
                stderr="password-value",
            )

        runner = runtime.SubprocessArgvRunner(
            popen_factory=popen_factory,
            on_invocation=invocations.append,
        )
        environment = {
            "API_KEY": "api-value",
            "NORMAL_VALUE": "visible",
            "servicePassword": "password-value",
        }
        result = runner.run(
            (
                "tool",
                "--token",
                "api-value",
                "--password=password-value",
            ),
            deadline=runner.deadline_after(5.0),
            cwd="/tmp/workspace",
            environment=environment,
        )

        self.assertTrue(result.succeeded)
        self.assertEqual(
            captured_argv,
            (
                "tool",
                "--token",
                "api-value",
                "--password=password-value",
            ),
        )
        kwargs = captured_kwargs
        self.assertIsNotNone(kwargs)
        assert kwargs is not None
        self.assertIs(kwargs["shell"], False)
        self.assertIs(kwargs["start_new_session"], True)
        self.assertEqual(kwargs["env"], environment)
        self.assertEqual(
            invocations[0].argv,
            ("tool", "--token", runtime.REDACTED, "--password=<redacted>"),
        )
        self.assertEqual(
            invocations[0].environment,
            {
                "API_KEY": runtime.REDACTED,
                "NORMAL_VALUE": "visible",
                "servicePassword": runtime.REDACTED,
            },
        )
        self.assertNotIn("api-value", json.dumps(result.to_dict()))
        self.assertNotIn("password-value", json.dumps(result.to_dict()))
        self.assertEqual(result.stdout, "<redacted> <redacted> visible")
        self.assertEqual(result.stderr, runtime.REDACTED)

    def test_redacts_secret_environment_assignments_inside_ssh_command(self) -> None:
        invocations: list[runtime.CommandInvocation] = []
        runner = runtime.SubprocessArgvRunner(
            popen_factory=lambda *args, **kwargs: ImmediateProcess(
                stdout="guest-secret"
            ),
            on_invocation=invocations.append,
        )

        result = runner.run(
            (
                "ssh",
                "admin@guest",
                "/usr/bin/env MAHO_E2E_AI_API_KEY=guest-secret node suite.js",
            ),
            deadline=runner.deadline_after(5.0),
        )

        serialized = json.dumps(
            {
                "invocation": invocations[0].to_dict(),
                "result": result.to_dict(),
            }
        )
        self.assertNotIn("guest-secret", serialized)
        self.assertIn(runtime.REDACTED, serialized)

    def test_cancellation_kills_the_owned_process_group(self) -> None:
        process = BlockingProcess()
        started = threading.Event()
        signals: list[tuple[int, int]] = []
        token = runtime.CancellationToken()

        def kill_process_group(process_group_id: int, sig: int) -> None:
            signals.append((process_group_id, sig))
            process.finish_from_signal(sig)

        runner = runtime.SubprocessArgvRunner(
            popen_factory=lambda *args, **kwargs: process,
            kill_process_group=kill_process_group,
            on_process_started=lambda pid: started.set(),
        )
        results: list[runtime.CommandResult] = []
        errors: list[BaseException] = []

        def invoke() -> None:
            try:
                results.append(
                    runner.run(
                        ("blocking-command",),
                        deadline=runner.deadline_after(5.0),
                        cancellation=token,
                    )
                )
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=invoke)
        worker.start()
        self.assertTrue(started.wait(timeout=1.0))
        self.assertTrue(token.cancel("operator interrupt"))
        worker.join(timeout=1.0)

        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(signals, [(process.pid, signal.SIGKILL)])
        self.assertEqual(results[0].status, runtime.CommandStatus.CANCELLED)
        self.assertEqual(results[0].returncode, -signal.SIGKILL)
        self.assertEqual(results[0].error, "operator interrupt")

    def test_timeout_terminates_then_kills_the_process_group(self) -> None:
        process = EscalatingTimeoutProcess()
        signals: list[tuple[int, int]] = []

        def kill_process_group(process_group_id: int, sig: int) -> None:
            signals.append((process_group_id, sig))
            process.receive_signal(sig)

        runner = runtime.SubprocessArgvRunner(
            popen_factory=lambda *args, **kwargs: process,
            kill_process_group=kill_process_group,
            termination_grace_seconds=0.1,
        )
        result = runner.run(
            ("timeout-command",), deadline=runner.deadline_after(5.0)
        )

        self.assertEqual(result.status, runtime.CommandStatus.TIMED_OUT)
        self.assertEqual(
            signals,
            [
                (process.pid, signal.SIGTERM),
                (process.pid, signal.SIGKILL),
            ],
        )
        self.assertEqual(process.communicate_calls, 3)
        self.assertEqual(result.stdout, "partial output")

    def test_expired_deadline_and_precancelled_token_do_not_start_process(self) -> None:
        starts = 0

        def popen_factory(*args: object, **kwargs: object) -> ImmediateProcess:
            nonlocal starts
            starts += 1
            return ImmediateProcess()

        runner = runtime.SubprocessArgvRunner(popen_factory=popen_factory)
        expired = runner.run(("tool",), deadline=0.0)
        token = runtime.CancellationToken()
        token.cancel("cancelled before start")
        cancelled = runner.run(
            ("tool",),
            deadline=runner.deadline_after(5.0),
            cancellation=token,
        )

        self.assertEqual(starts, 0)
        self.assertEqual(expired.status, runtime.CommandStatus.TIMED_OUT)
        self.assertEqual(cancelled.status, runtime.CommandStatus.CANCELLED)


class RunReceiptTest(unittest.TestCase):
    def test_terminal_status_enum_is_exhaustive_and_each_state_serializes(self) -> None:
        expected = {
            "success",
            "doctor_failed",
            "prepare_failed",
            "build_failed",
            "vm_start_failed",
            "guest_not_ready",
            "transport_failed",
            "suite_failed",
            "artifact_failed",
            "isolation_failed",
            "cancelled",
            "teardown_failed",
        }
        self.assertEqual({status.value for status in runtime.TerminalStatus}, expected)

        partial = make_partial_receipt()
        teardown = successful_teardown()
        for status in runtime.TerminalStatus:
            with self.subTest(status=status.value):
                final = partial.finalize(
                    primary_status=status,
                    teardown_result=teardown,
                    finished_at=FINISHED_TIMESTAMP,
                )
                serialized = json.loads(json.dumps(final.to_dict()))
                self.assertEqual(serialized["terminal_status"], status.value)

    def test_teardown_failure_preserves_primary_failure(self) -> None:
        failed_teardown = runtime.TeardownResult(
            attempted=True,
            succeeded=False,
            started_at=TIMESTAMP,
            finished_at=FINISHED_TIMESTAMP,
            error="tart delete failed",
        )

        suite_failure = make_partial_receipt().finalize(
            primary_status=runtime.TerminalStatus.SUITE_FAILED,
            teardown_result=failed_teardown,
            finished_at=FINISHED_TIMESTAMP,
        )
        success_before_teardown = make_partial_receipt().finalize(
            primary_status=runtime.TerminalStatus.SUCCESS,
            teardown_result=failed_teardown,
            finished_at=FINISHED_TIMESTAMP,
        )

        self.assertEqual(
            suite_failure.terminal_status, runtime.TerminalStatus.SUITE_FAILED
        )
        teardown_result = suite_failure.teardown_result
        self.assertIsNotNone(teardown_result)
        assert teardown_result is not None
        self.assertFalse(teardown_result.succeeded)
        self.assertEqual(
            success_before_teardown.terminal_status,
            runtime.TerminalStatus.TEARDOWN_FAILED,
        )

    def test_receipt_serializes_required_fields_and_redacts_nested_secrets(self) -> None:
        serialized = make_partial_receipt().to_dict()
        host_facts = serialized["host_facts"]
        source_identity = serialized["source_image_identity"]
        suite_results = serialized["suite_results"]
        isolation_snapshots = serialized["isolation_snapshots"]
        assert isinstance(host_facts, dict)
        assert isinstance(source_identity, dict)
        assert isinstance(suite_results, list)
        assert isinstance(suite_results[0], dict)
        assert isinstance(isolation_snapshots, list)
        assert isinstance(isolation_snapshots[0], dict)

        self.assertEqual(serialized["run_id"], "run-123")
        self.assertEqual(serialized["build_path"], "out/Default")
        self.assertEqual(host_facts["architecture"], "arm64")
        self.assertEqual(host_facts["api_token"], runtime.REDACTED)
        self.assertEqual(source_identity["password"], runtime.REDACTED)
        self.assertEqual(suite_results[0]["name"], "interactive-smoke")
        self.assertEqual(isolation_snapshots[0]["stage"], "baseline")
        self.assertFalse(serialized["finalized"])
        self.assertIsNone(serialized["terminal_status"])

    def test_partial_receipt_survives_a_failed_atomic_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            writer = runtime.AtomicReceiptWriter(
                temporary_directory, "run-123"
            )
            partial = make_partial_receipt()
            receipt_path = writer.write_partial(partial)
            original_bytes = receipt_path.read_bytes()
            self.assertEqual(
                receipt_path,
                Path(temporary_directory)
                / "test-results"
                / "macos-vm"
                / "run-123"
                / "receipt.json",
            )

            updated = dataclasses.replace(
                partial,
                updated_at=FINISHED_TIMESTAMP,
                guest_facts={"console_user": "admin", "ready": True},
            )

            def fail_replace(source: str, destination: str) -> None:
                del source, destination
                raise OSError("injected replacement failure")

            failing_writer = runtime.AtomicReceiptWriter(
                temporary_directory,
                "run-123",
                replace_file=fail_replace,
            )
            with self.assertRaisesRegex(OSError, "injected replacement failure"):
                failing_writer.write_partial(updated)

            self.assertEqual(receipt_path.read_bytes(), original_bytes)
            self.assertEqual(
                list(receipt_path.parent.glob(".receipt.*.tmp")), []
            )

            final = writer.finalize(
                updated,
                primary_status=runtime.TerminalStatus.SUCCESS,
                teardown_result=successful_teardown(),
                finished_at=FINISHED_TIMESTAMP,
            )
            persisted = json.loads(receipt_path.read_text(encoding="utf-8"))
            self.assertTrue(final.finalized)
            self.assertTrue(persisted["finalized"])
            self.assertEqual(persisted["terminal_status"], "success")
            self.assertTrue(persisted["guest_facts"]["ready"])

    def test_writer_rejects_path_traversal_and_mismatched_run_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaises(ValueError):
                runtime.AtomicReceiptWriter(temporary_directory, "../escape")
            writer = runtime.AtomicReceiptWriter(
                temporary_directory, "another-run"
            )
            with self.assertRaisesRegex(ValueError, "does not match"):
                writer.write_partial(make_partial_receipt())


if __name__ == "__main__":
    unittest.main()
