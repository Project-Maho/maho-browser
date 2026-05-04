#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

DEFAULT_APP_PATH = Path("/Users/indo/code/project/maho-workspace/chromium/src/out/Default/Maho.app")
DEFAULT_ARTIFACT_DIR = Path("/tmp/maho-chromium-e2e-spaces")
SCENARIO_NAME = "launch_clean_profile"
SCENARIO_NAMES = (SCENARIO_NAME,)


class HarnessError(RuntimeError):
    pass


def env_path(name: str, default: Path) -> Path:
    value = os.environ.get(name)
    if value:
        return Path(value).expanduser()
    return default


def require_file(path: Path, message: str) -> None:
    if not path.exists():
        raise HarnessError(message)


def require_executable(path: Path, message: str) -> None:
    require_file(path, message)
    if not path.is_file():
        raise HarnessError(message)
    if not os.access(path, os.X_OK):
        raise HarnessError(message)


def ensure_clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def run_command(cmd: list[str]) -> None:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        stderr = result.stderr.strip()
        stdout = result.stdout.strip()
        details = ""
        if stdout:
            details += f"\nstdout: {stdout}"
        if stderr:
            details += f"\nstderr: {stderr}"
        raise HarnessError(f"Command failed ({result.returncode}): {' '.join(shlex.quote(part) for part in cmd)}{details}")


def capture_screen(out_path: Path) -> None:
    run_command(["/usr/sbin/screencapture", "-x", str(out_path)])
    if not out_path.exists() or out_path.stat().st_size <= 0:
        raise HarnessError(f"Screenshot capture failed: {out_path}")


def validate_app_bundle(app_path: Path) -> Path:
    if not app_path.exists():
        raise HarnessError(f"App path not found: {app_path}")
    if not app_path.is_dir():
        raise HarnessError(f"App path is not a bundle directory: {app_path}")

    executable = app_path / "Contents" / "MacOS" / "Maho"
    if not executable.exists():
        raise HarnessError(f"Build artifact not found: {executable}")
    if not executable.is_file():
        raise HarnessError(f"Build artifact is not a file: {executable}")
    if not os.access(executable, os.X_OK):
        raise HarnessError(f"Build artifact is not executable: {executable}")
    return executable


def wait_for_process(proc: subprocess.Popen[bytes], timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if proc.poll() is not None:
            raise HarnessError(f"Maho exited early with code {proc.returncode}")
        time.sleep(0.1)


def launch_clean_profile() -> int:
    app_path = env_path("MAHO_APP_PATH", DEFAULT_APP_PATH)
    artifact_root = env_path("MAHO_E2E_ARTIFACT_DIR", DEFAULT_ARTIFACT_DIR)
    keep_profile = os.environ.get("MAHO_E2E_KEEP_PROFILE") == "1"

    executable = validate_app_bundle(app_path)

    scenario_dir = artifact_root / SCENARIO_NAME
    ensure_clean_dir(scenario_dir)

    profile_dir = Path(tempfile.mkdtemp(prefix="profile-", dir=scenario_dir))
    before_png = scenario_dir / "before.png"
    after_png = scenario_dir / "after.png"
    launch_log = scenario_dir / "launch.log"
    profile_path_txt = scenario_dir / "profile_path.txt"
    profile_path_txt.write_text(f"{profile_dir}\n", encoding="utf-8")

    launch_args = [
        str(executable),
        f"--user-data-dir={profile_dir}",
        "--no-first-run",
        "--no-default-browser-check",
    ]

    proc: subprocess.Popen[bytes] | None = None
    try:
        capture_screen(before_png)

        with launch_log.open("w", encoding="utf-8") as log_file:
            log_file.write(f"app_path: {app_path}\n")
            log_file.write(f"executable: {executable}\n")
            log_file.write(f"artifact_root: {artifact_root}\n")
            log_file.write(f"scenario_dir: {scenario_dir}\n")
            log_file.write(f"profile_dir: {profile_dir}\n")
            log_file.write(f"MAHO_APP_PATH={app_path}\n")
            log_file.write(f"MAHO_E2E_ARTIFACT_DIR={artifact_root}\n")
            log_file.write(f"MAHO_E2E_KEEP_PROFILE={os.environ.get('MAHO_E2E_KEEP_PROFILE', '')}\n")
            log_file.write(f"command: {' '.join(shlex.quote(part) for part in launch_args)}\n")
            log_file.flush()

            proc = subprocess.Popen(launch_args, stdout=log_file, stderr=log_file)
            log_file.write(f"pid: {proc.pid}\n")
            log_file.flush()

            wait_for_process(proc, timeout_s=10.0)
            time.sleep(2.0)
            capture_screen(after_png)

            if proc.poll() is not None:
                raise HarnessError(f"Maho exited early with code {proc.returncode}")

            log_file.write("status: capture complete\n")
            log_file.flush()

        return 0
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
        if not keep_profile:
            shutil.rmtree(profile_dir, ignore_errors=True)


def list_scenarios() -> int:
    for name in SCENARIO_NAMES:
        print(name)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Minimal Chromium desktop E2E harness")
    parser.add_argument(
        "--list",
        action="store_true",
        help="Print the available scenario names and exit",
    )
    parser.add_argument(
        "--scenario",
        action="append",
        choices=SCENARIO_NAMES,
        help="Run a named scenario; may be repeated",
    )
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.list:
        return list_scenarios()

    scenarios = args.scenario or [SCENARIO_NAME]
    invalid = [name for name in scenarios if name not in SCENARIO_NAMES]
    if invalid:
        raise HarnessError(f"Unknown scenarios: {', '.join(invalid)}")

    for scenario in scenarios:
        if scenario == SCENARIO_NAME:
            return launch_clean_profile()

    raise HarnessError(f"No runnable scenarios selected: {', '.join(scenarios)}")


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except HarnessError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
    except Exception as exc:
        print(f"ERROR: unexpected failure: {exc}", file=sys.stderr)
        sys.exit(1)
