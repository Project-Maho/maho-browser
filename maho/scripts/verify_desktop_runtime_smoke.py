#!/usr/bin/env python3
"""Portable macOS/Windows/Linux runtime smoke harness for Maho Browser.

Launches the real built browser, waits for CLI/MCP readiness by polling live
state with a bounded deadline (no fixed sleeps), runs a minimal smoke suite
(--version, tool list, tool describe, tab list), reports a structured JSON
summary, and always tears the browser down.

Every wait is state-conditioned: `_wait_until` returns the moment the predicate
flips; the 50ms pause inside the poll loop is a backoff between OBSERVATIONS,
never a blind delay. Paths are caller-supplied — no personal absolute paths.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

POLL_INTERVAL_S = 0.05


def _pause(seconds: float) -> None:
    """Interruptible pause used only between state observations."""
    select.select([], [], [], seconds)


def _wait_until(
    predicate: Callable[[], bool],
    timeout_s: float,
    deadline_clock: Callable[[], float] = time.monotonic,
) -> bool:
    """Return as soon as `predicate` is true, or False once `timeout_s` elapsed."""
    deadline = deadline_clock() + timeout_s
    while True:
        if predicate():
            return True
        if deadline_clock() >= deadline:
            return False
        _pause(min(POLL_INTERVAL_S, max(0.0, deadline - deadline_clock())))


def build_launch_args(
    platform: str,
    browser: Path,
    user_data_dir: Path,
    socket_path: Path,
) -> List[str]:
    args = [
        str(browser),
        "--no-first-run",
        "--no-default-browser-check",
        f"--user-data-dir={user_data_dir}",
        "--noerrdialogs",
        "--disable-gpu",
    ]
    if platform in ("macos", "linux"):
        args.append(f"--socket-path={socket_path}")
    args.append("https://example.com")
    return args


def socket_connectable(path: Path) -> bool:
    import socket as socket_mod

    s = socket_mod.socket(socket_mod.AF_UNIX, socket_mod.SOCK_STREAM)
    try:
        s.settimeout(0.25)
        s.connect(str(path))
        return True
    except OSError:
        return False
    finally:
        s.close()


@dataclass
class Runner:
    """CLI invocation helper; subprocess path is injectable for tests."""

    cli: Path
    extra_args: List[str] = field(default_factory=list)
    run_impl: Callable[[List[str]], Tuple[int, str, str]] = None  # type: ignore[assignment]

    def run(self, args: List[str], timeout_s: float = 30.0) -> Tuple[int, str, str]:
        return self.run_impl(args)  # pragma: no cover - replaced in main()


def subprocess_run(cli: Path, extra: List[str]) -> Callable[[List[str]], Tuple[int, str, str]]:
    def _run(args: List[str]) -> Tuple[int, str, str]:
        proc = subprocess.run(
            [str(cli), *extra, *args],
            capture_output=True,
            text=True,
            timeout=30,
        )
        return proc.returncode, proc.stdout, proc.stderr

    return _run


def default_socket_path(platform: str, user_data_dir: Path) -> Optional[Path]:
    if platform in ("macos", "linux"):
        return user_data_dir / "maho.sock"
    return None


def cli_ready(runner: Runner, socket_path: Optional[Path]) -> Callable[[], bool]:
    def _ready() -> bool:
        if socket_path is not None and not socket_path.exists():
            return False
        rc, _, _ = runner.run(["--version"], timeout_s=10)
        return rc == 0

    return _ready


def run_checks(
    runner: Runner,
    socket_extra: List[str],
) -> Dict[str, Dict[str, str]]:
    results: Dict[str, Dict[str, str]] = {}

    def record(name: str, rc: int, out: str, err: str, extra_expect: bool = True) -> None:
        ok = rc == 0 and extra_expect
        results[name] = {
            "pass": "true" if ok else "false",
            "detail": f"rc={rc}; {out.strip()[:160] or err.strip()[:160]}",
        }

    rc, out, err = runner.run(["--version"])
    record("cli_version", rc, out, err)

    rc, out, err = runner.run([*socket_extra, "tool", "list"])
    record("mcp_tool_list", rc, out, err, extra_expect=len(out.strip().splitlines()) > 0)

    rc, out, err = runner.run([*socket_extra, "tool", "describe", "tab.list"])
    record("mcp_tool_describe", rc, out, err)

    rc, out, err = runner.run([*socket_extra, "tab", "list", "--json"])
    parsed_ok = False
    if rc == 0:
        try:
            json.loads(out)
            parsed_ok = True
        except json.JSONDecodeError:
            parsed_ok = False
    record("tab_list_json", rc, out, err, extra_expect=parsed_ok)
    return results


def teardown(browser_proc: subprocess.Popen, user_data_dir: Path) -> bool:
    browser_proc.terminate()
    try:
        browser_proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        browser_proc.kill()
        try:
            browser_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            return False
    shutil.rmtree(user_data_dir, ignore_errors=True)
    return True


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", required=True, type=Path, help="built chrome/Maho binary path")
    parser.add_argument("--cli", required=True, type=Path, help="built maho CLI path")
    parser.add_argument(
        "--platform",
        required=False,
        choices=["macos", "windows", "linux"],
        default={  # map sys.platform -> product platform
            "darwin": "macos",
            "win32": "windows",
            "linux": "linux",
        }[sys.platform],
    )
    parser.add_argument("--url", default="https://example.com")
    parser.add_argument("--ready-timeout", type=float, default=60.0)
    parser.add_argument("--keep", action="store_true", help="skip teardown (debug)")
    args = parser.parse_args(argv)

    failures: List[str] = []
    if not args.browser.is_file():
        failures.append(f"browser binary missing: {args.browser}")
    if not args.cli.is_file():
        failures.append(f"cli binary missing: {args.cli}")
    if failures:
        print(json.dumps({"pass": False, "failures": failures}))
        return 1

    user_data_dir = Path(tempfile.mkdtemp(prefix="maho-smoke-"))
    socket_path = default_socket_path(args.platform, user_data_dir)
    launch_args = build_launch_args(args.platform, args.browser, user_data_dir, socket_path)
    if args.url:
        launch_args[-1] = args.url

    env = os.environ.copy()
    browser_proc = subprocess.Popen(launch_args, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    passed = False
    results: Dict[str, Dict[str, str]] = {}
    try:
        extra = ["--socket-path", str(socket_path)] if socket_path is not None else []
        runner = Runner(cli=args.cli, extra_args=extra, run_impl=subprocess_run(args.cli, extra))
        ready = _wait_until(cli_ready(runner, socket_path), args.ready_timeout)
        if not ready:
            failures.append(f"browser/CLI readiness not observed within {args.ready_timeout}s")
        else:
            results = run_checks(runner, extra)
            failures.extend(
                f"{name}: {info['detail']}" for name, info in results.items() if info["pass"] != "true"
            )
        if browser_proc.poll() is not None:
            failures.append(f"browser exited early with rc={browser_proc.returncode}")
    finally:
        if args.keep:
            print(f"keep: browser pid {browser_proc.pid}, dir {user_data_dir}", file=sys.stderr)
            teardown_ok = True
        else:
            teardown_ok = teardown(browser_proc, user_data_dir)
            if not teardown_ok:
                failures.append("teardown failed to terminate browser")

    summary = {
        "platform": args.platform,
        "pass": not failures,
        "checks": results,
        "failures": failures,
        "teardown": "ok" if (args.keep or teardown_ok) else "failed",
    }
    print(json.dumps(summary, indent=2))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())

