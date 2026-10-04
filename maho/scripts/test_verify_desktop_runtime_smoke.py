"""Deterministic unit tests for the portable runtime smoke harness.

No browser is launched: runners/predicates/clocks are injected fakes. Covers
readiness (state-driven, deadline-bounded), the smoke check matrix, teardown
ordering, and malformed-CLI failure handling.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import verify_desktop_runtime_smoke as harness  # noqa: E402


class FakeClock:
    """Advances on every observation so deadline logic terminates."""

    def __init__(self, step: float = 0.05) -> None:
        self.now = 0.0
        self.step = step

    def __call__(self) -> float:
        self.now += self.step
        return self.now


class WaitUntilTests(unittest.TestCase):
    def test_returns_on_first_state_observation(self):
        clock = FakeClock()
        observations = []

        def ready() -> bool:
            observations.append(clock.now)
            return True

        ok = harness._wait_until(ready, timeout_s=5, deadline_clock=clock)
        self.assertTrue(ok)
        # exactly one observation before returning; deadline never reached
        self.assertEqual(len(observations), 1)
        self.assertLess(clock.now, 5.0)

    def test_deadline_bounds_a_never_ready_state(self):
        clock = FakeClock()
        ok = harness._wait_until(lambda: False, timeout_s=0.15, deadline_clock=clock)
        self.assertFalse(ok)
        self.assertGreaterEqual(clock.now, 0.15)


class LaunchArgsTests(unittest.TestCase):
    def test_unix_platforms_get_socket_path(self):
        for platform in ("macos", "linux"):
            args = harness.build_launch_args(
                platform, Path("/b/chrome"), Path("/tmp/d"), Path("/tmp/d/maho.sock")
            )
            self.assertIn("--socket-path=/tmp/d/maho.sock", args)
            self.assertIn("--user-data-dir=/tmp/d", args)

    def test_windows_has_no_socket_flag(self):
        args = harness.build_launch_args("windows", Path("/b/chrome.exe"), Path("/tmp/d"), None)
        self.assertFalse(any(a.startswith("--socket-path") for a in args))


class RunChecksTests(unittest.TestCase):
    def _runner(self, responses):
        calls = []

        def run(args, timeout_s=30.0):
            calls.append(args)
            key = " ".join(a for a in args if not a.startswith("--socket"))
            for pattern, (rc, out, err) in responses.items():
                if key.endswith(pattern) or pattern == "*":
                    return rc, out, err
            return 1, "", "no fake response"

        r = harness.Runner(cli=Path("/fake/maho"), run_impl=run)
        return r, calls

    def test_all_pass_on_healthy_cli(self):
        responses = {
            "--version": (0, "Maho 1.0.0\n", ""),
            "tool list": (0, "tab.list\nnav.open\n", ""),
            "tool describe tab.list": (0, '{"name":"tab.list"}', ""),
            "tab list --json": (0, "[]", ""),
        }
        runner, calls = self._runner(responses)
        results = harness.run_checks(runner, ["--socket-path", "/tmp/s"])
        self.assertTrue(all(v["pass"] == "true" for v in results.values()), results)
        self.assertEqual(len(calls), 4)

    def test_bad_json_fails_tab_list_even_with_rc0(self):
        responses = {
            "--version": (0, "1.0", ""),
            "tool list": (0, "t\n", ""),
            "tool describe tab.list": (0, "{}", ""),
            "tab list --json": (0, "not-json", ""),
        }
        runner, _ = self._runner(responses)
        results = harness.run_checks(runner, [])
        self.assertEqual(results["tab_list_json"]["pass"], "false")

    def test_cli_error_is_structured_not_raised(self):
        runner, _ = self._runner({"*": (3, "", "boom")})
        results = harness.run_checks(runner, [])
        self.assertFalse(any(v["pass"] == "true" for v in results.values()))


class TeardownTests(unittest.TestCase):
    class Proc:
        def __init__(self, hang=False):
            self.terminated = False
            self.killed = False
            self._hang = hang
            self.returncode = None

        def terminate(self):
            self.terminated = True

        def wait(self, timeout=None):
            if self._hang and not self.killed:
                raise harness.subprocess.TimeoutExpired(cmd="b", timeout=timeout)
            return 0

        def kill(self):
            self.killed = True

    def test_terminate_then_cleanup(self):
        proc = self.Proc()
        with tempfile.TemporaryDirectory() as d:
            ok = harness.teardown(proc, Path(d))
        self.assertTrue(ok)
        self.assertTrue(proc.terminated)
        self.assertFalse(proc.killed)

    def test_escalates_to_kill_when_hanging(self):
        proc = self.Proc(hang=True)
        with tempfile.TemporaryDirectory() as d:
            ok = harness.teardown(proc, Path(d))
        self.assertTrue(ok)
        self.assertTrue(proc.terminated)
        self.assertTrue(proc.killed)


class MainFailurePathsTests(unittest.TestCase):
    def test_missing_binaries_fail_before_launch(self):
        rc = harness.main(["--browser", "/nonexistent/chrome", "--cli", "/nonexistent/maho"])
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()
