#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


_SCRIPT_DIR = Path(__file__).resolve().parent
_SCRIPT_PATH = _SCRIPT_DIR / "macos_vm_isolation.py"
_SPEC = importlib.util.spec_from_file_location(
    "macos_vm_isolation_under_test",
    _SCRIPT_PATH,
)
assert _SPEC and _SPEC.loader
macos_vm_isolation = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = macos_vm_isolation
_SPEC.loader.exec_module(macos_vm_isolation)


def _window(
    *,
    owner_name: str = "Maho",
    owner_pid: int = 410,
    window_id: int = 9001,
    x: float = 10,
) -> dict[str, object]:
    return {
        "owner_name": owner_name,
        "owner_pid": owner_pid,
        "window_id": window_id,
        "layer": 0,
        "bounds": {
            "x": x,
            "y": 20,
            "width": 1200,
            "height": 800,
        },
    }


def _process(
    *,
    pid: int = 410,
    parent_pid: int = 1,
    executable: str = "/Applications/Maho.app/Contents/MacOS/Maho",
    command: str | None = None,
) -> dict[str, object]:
    return {
        "pid": pid,
        "parent_pid": parent_pid,
        "executable": executable,
        "command": command or executable,
    }


def _listener(
    *,
    port: int = 9222,
    pid: int = 410,
    address: str = "127.0.0.1",
) -> dict[str, object]:
    return {
        "protocol": "tcp",
        "address": address,
        "port": port,
        "pid": pid,
        "process_name": "Maho",
    }


def _snapshot(
    *,
    windows: tuple[dict[str, object], ...] = (),
    maho_processes: tuple[dict[str, object], ...] = (),
    tart_run_processes: tuple[dict[str, object], ...] = (),
    listeners: tuple[dict[str, object], ...] = (),
):
    return macos_vm_isolation.IsolationSnapshot.from_json_dict(
        {
            "schema_version": 1,
            "windows": list(windows),
            "maho_processes": list(maho_processes),
            "tart_run_processes": list(tart_run_processes),
            "listeners": list(listeners),
        }
    )


class MacosVmIsolationComparisonTest(unittest.TestCase):
    def test_preexisting_host_state_is_not_a_violation(self) -> None:
        baseline = _snapshot(
            windows=(_window(),),
            maho_processes=(_process(),),
            listeners=(_listener(),),
        )
        current = _snapshot(
            windows=(_window(),),
            maho_processes=(_process(),),
            listeners=(_listener(),),
        )

        delta = macos_vm_isolation.compare_snapshots(
            baseline,
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertTrue(delta.is_isolated)
        self.assertEqual(delta.violation_codes, ())

    def test_window_identity_ignores_mutable_bounds_and_owner_case(self) -> None:
        baseline = _snapshot(windows=(_window(owner_name="Maho", x=10),))
        current = _snapshot(windows=(_window(owner_name="MAHO", x=240),))

        delta = macos_vm_isolation.compare_snapshots(
            baseline,
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(delta.new_windows, ())
        self.assertTrue(delta.is_isolated)

    def test_new_maho_window_has_an_exact_structured_violation(self) -> None:
        baseline = _snapshot()
        current = _snapshot(windows=(_window(owner_pid=812, window_id=71),))

        delta = macos_vm_isolation.compare_snapshots(
            baseline,
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(delta.violation_codes, ("new_visible_window",))
        self.assertEqual(delta.violations[0].identity, "maho:812:71")
        self.assertEqual(delta.violations[0].phase, "during")

    def test_new_tart_window_is_a_visible_window_violation(self) -> None:
        current = _snapshot(
            windows=(
                _window(owner_name="Tart", owner_pid=913, window_id=81),
            )
        )

        delta = macos_vm_isolation.compare_snapshots(
            _snapshot(),
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(delta.violation_codes, ("new_visible_window",))
        self.assertEqual(delta.violations[0].identity, "tart:913:81")

    def test_new_host_maho_process_is_a_violation(self) -> None:
        current = _snapshot(maho_processes=(_process(pid=812),))

        delta = macos_vm_isolation.compare_snapshots(
            _snapshot(),
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(delta.violation_codes, ("new_host_maho_process",))
        self.assertEqual(
            delta.violations[0].identity,
            "812:/Applications/Maho.app/Contents/MacOS/Maho",
        )

    def test_new_listener_on_guest_cdp_port_is_a_violation(self) -> None:
        current = _snapshot(listeners=(_listener(pid=812),))

        delta = macos_vm_isolation.compare_snapshots(
            _snapshot(),
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(delta.violation_codes, ("new_host_cdp_listener",))
        self.assertEqual(
            delta.violations[0].identity,
            "tcp:127.0.0.1:9222:812",
        )

    def test_listener_on_an_unrelated_port_is_recorded_but_not_a_violation(self) -> None:
        current = _snapshot(listeners=(_listener(port=9333, pid=812),))

        delta = macos_vm_isolation.compare_snapshots(
            _snapshot(),
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(len(delta.new_listeners), 1)
        self.assertTrue(delta.is_isolated)

    def test_expected_headless_tart_process_is_recorded_separately(self) -> None:
        tart_process = _process(
            pid=700,
            executable="/opt/homebrew/bin/tart",
            command="/opt/homebrew/bin/tart run --no-graphics maho-e2e-run-1",
        )
        current = _snapshot(tart_run_processes=(tart_process,))

        delta = macos_vm_isolation.compare_snapshots(
            _snapshot(),
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(tuple(p.pid for p in delta.new_tart_run_processes), (700,))
        self.assertTrue(delta.is_isolated)

    def test_run_report_keeps_a_during_violation_that_is_gone_afterward(self) -> None:
        baseline = _snapshot()
        during = _snapshot(windows=(_window(owner_pid=812, window_id=71),))
        after = _snapshot()

        report = macos_vm_isolation.compare_run_snapshots(
            baseline,
            during,
            after,
            cdp_port=9222,
        )

        self.assertFalse(report.is_isolated)
        self.assertEqual(report.violation_codes, ("new_visible_window",))
        self.assertEqual(report.during.phase, "during")
        self.assertEqual(report.after.phase, "after")

    def test_early_exit_report_distinguishes_post_teardown_cleanup_evidence(self) -> None:
        report = macos_vm_isolation.compare_early_exit_snapshots(
            _snapshot(),
            _snapshot(),
            cdp_port=9222,
        )

        self.assertTrue(report.is_isolated)
        self.assertEqual(
            report.post_teardown_after_early_exit.phase,
            "post_teardown_after_early_exit",
        )
        self.assertEqual(
            report.to_json_dict(),
            {
                "schema_version": 1,
                "evidence_kind": "early_exit",
                "is_isolated": True,
                "violation_codes": [],
                "post_teardown_after_early_exit": {
                    "phase": "post_teardown_after_early_exit",
                    "is_isolated": True,
                    "new_windows": [],
                    "new_maho_processes": [],
                    "new_tart_run_processes": [],
                    "new_listeners": [],
                    "violations": [],
                },
            },
        )

    def test_post_teardown_tart_run_is_a_cleanup_violation(self) -> None:
        tart_process = _process(
            pid=700,
            executable="/opt/homebrew/bin/tart",
            command="/opt/homebrew/bin/tart run --no-graphics maho-e2e-run-1",
        )

        report = macos_vm_isolation.compare_early_exit_snapshots(
            _snapshot(),
            _snapshot(tart_run_processes=(tart_process,)),
            cdp_port=9222,
        )

        self.assertFalse(report.is_isolated)
        self.assertEqual(report.violation_codes, ("new_host_tart_run_process",))

    def test_live_run_allows_tart_during_but_rejects_it_after_teardown(self) -> None:
        tart_process = _process(
            pid=700,
            executable="/opt/homebrew/bin/tart",
            command="/opt/homebrew/bin/tart run --no-graphics maho-e2e-run-1",
        )

        report = macos_vm_isolation.compare_run_snapshots(
            _snapshot(),
            _snapshot(tart_run_processes=(tart_process,)),
            _snapshot(tart_run_processes=(tart_process,)),
            cdp_port=9222,
        )

        self.assertTrue(report.during.is_isolated)
        self.assertEqual(report.during.violation_codes, ())
        self.assertFalse(report.after.is_isolated)
        self.assertEqual(
            report.after.violation_codes,
            ("new_host_tart_run_process",),
        )

    def test_normal_report_schema_requires_live_during_and_after_phases(self) -> None:
        report = macos_vm_isolation.compare_run_snapshots(
            _snapshot(),
            _snapshot(),
            _snapshot(),
            cdp_port=9222,
        )

        payload = report.to_json_dict()
        self.assertEqual(payload["evidence_kind"], "live_run")
        self.assertEqual(payload["during"]["phase"], "during")
        self.assertEqual(payload["after"]["phase"], "after")

    def test_snapshot_round_trip_preserves_fixture_schema(self) -> None:
        snapshot = _snapshot(
            windows=(_window(),),
            maho_processes=(_process(),),
            listeners=(_listener(),),
        )

        self.assertEqual(
            macos_vm_isolation.IsolationSnapshot.from_json_dict(
                snapshot.to_json_dict()
            ),
            snapshot,
        )

    def test_core_graphics_probe_payload_uses_the_same_window_contract(self) -> None:
        windows = macos_vm_isolation.parse_window_probe_payload(
            {
                "schema_version": 1,
                "windows": [_window(owner_pid=812, window_id=71)],
            }
        )

        self.assertEqual(len(windows), 1)
        self.assertEqual(windows[0].identity, ("maho", 812, 71))

    def test_invalid_fixture_schema_fails_closed(self) -> None:
        with self.assertRaisesRegex(
            macos_vm_isolation.IsolationProbeError,
            "schema_version",
        ):
            macos_vm_isolation.IsolationSnapshot.from_json_dict(
                {"schema_version": 2}
            )

    def test_process_command_changes_do_not_change_stable_identity(self) -> None:
        baseline = _snapshot(
            maho_processes=(
                _process(command="/Applications/Maho.app/Contents/MacOS/Maho --one"),
            )
        )
        current = _snapshot(
            maho_processes=(
                _process(command="/Applications/Maho.app/Contents/MacOS/Maho --two"),
            )
        )

        delta = macos_vm_isolation.compare_snapshots(
            baseline,
            current,
            phase="during",
            cdp_port=9222,
        )

        self.assertEqual(delta.new_maho_processes, ())
        self.assertTrue(delta.is_isolated)


class MacosVmIsolationCaptureTest(unittest.TestCase):
    def test_ps_parser_finds_maho_and_expected_headless_tart(self) -> None:
        processes = macos_vm_isolation.parse_ps_processes(
            "\n".join(
                (
                    "410 1 /Applications/Maho.app/Contents/MacOS/Maho --guest-test",
                    "700 1 /opt/homebrew/bin/tart run --no-graphics maho-e2e-run-1",
                    "701 1 /opt/homebrew/bin/tart run maho-e2e-visible",
                )
            )
        )

        self.assertEqual(tuple(process.pid for process in processes), (410, 700, 701))

    def test_lsof_parser_handles_ipv4_ipv6_and_wildcard_listeners(self) -> None:
        listeners = macos_vm_isolation.parse_lsof_listeners(
            "\n".join(
                (
                    "p410",
                    "cMaho",
                    "n127.0.0.1:9222",
                    "n[::1]:9223",
                    "p700",
                    "cnode",
                    "n*:9333",
                )
            )
        )

        self.assertEqual(
            tuple((listener.address, listener.port) for listener in listeners),
            (("*", 9333), ("127.0.0.1", 9222), ("::1", 9223)),
        )

    def test_capture_classifies_processes_and_preserves_all_listeners(self) -> None:
        class FakeRunner:
            def __init__(self) -> None:
                self.results = [
                    macos_vm_isolation.CommandResult(
                        0,
                        '{"schema_version":1,"windows":[]}',
                    ),
                    macos_vm_isolation.CommandResult(
                        0,
                        "\n".join(
                            (
                                "410 1 /Applications/Maho.app/Contents/MacOS/Maho",
                                "700 1 /opt/homebrew/bin/tart run --no-graphics run-1",
                                "701 1 /opt/homebrew/bin/tart run visible-run",
                                "800 1 /usr/bin/python3 worker.py",
                            )
                        ),
                    ),
                    macos_vm_isolation.CommandResult(
                        0,
                        "p800\ncpython3\nn127.0.0.1:9333\n",
                    ),
                ]
                self.calls: list[tuple[str, ...]] = []

            def run(
                self,
                argv: tuple[str, ...],
                *,
                timeout: float,
            ):
                self.calls.append(tuple(argv))
                return self.results.pop(0)

        runner = FakeRunner()
        snapshot = macos_vm_isolation.capture_snapshot(
            runner=runner,
            window_probe_path=_SCRIPT_DIR / "macos_vm_window_probe.swift",
        )

        self.assertEqual(tuple(p.pid for p in snapshot.maho_processes), (410,))
        self.assertEqual(tuple(p.pid for p in snapshot.tart_run_processes), (700,))
        self.assertEqual(tuple(listener.port for listener in snapshot.listeners), (9333,))
        self.assertEqual(runner.calls[0][:2], ("xcrun", "swift"))
        self.assertEqual(runner.calls[1][0], "ps")
        self.assertEqual(runner.calls[2][0], "lsof")


if __name__ == "__main__":
    unittest.main()
