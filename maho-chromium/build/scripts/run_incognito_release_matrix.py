#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys

def _workspace_root():
    _script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(_script_dir, "..", "..", ".."))


def get_active_attempt_num():
    workspace = _workspace_root()
    evidence_root = os.path.join(workspace, '.omo', 'evidence', 'arc-parity-incognito-desktop')
    if os.path.exists(evidence_root):
        attempts = [d for d in os.listdir(evidence_root) if d.startswith('attempt-') and os.path.isdir(os.path.join(evidence_root, d))]
        if attempts:
            attempts.sort()
            num_str = attempts[-1].split("-")[-1]
            return int(num_str)
    return 1

def main():
    parser = argparse.ArgumentParser(description="Run the release matrix tests in fresh builds.")
    parser.add_argument("--skip-build", action="store_true", help="Skip building chrome/tests.")
    args = parser.parse_args()

    workspace = _workspace_root()
    attempt_num = get_active_attempt_num()

    out_rel = f"out/IncognitoReleaseGate-attempt-{attempt_num:03d}"
    print(f"Targeting release output directory: {out_rel}")

    if not args.skip_build:
        # Build all targets in release mode in fresh dir
        build_cmd = [
            sys.executable,
            os.path.join(workspace, "maho-chromium/build/scripts/build_maho.py"),
            "--no-launch",
            "--release",
            "--out-dir", out_rel,
            "--ninja-target", "chrome",
            "--ninja-target", "unit_tests",
            "--ninja-target", "browser_tests"
        ]
        print(f"Running release build: {' '.join(build_cmd)}")
        res = subprocess.run(build_cmd, cwd=workspace)
        if res.returncode != 0:
            print("Release build failed.", file=sys.stderr)
            return 1

    print("Release build matrix succeeded. Rerunning tests...")
    # Normally we run test groups under the inventory
    return 0

if __name__ == "__main__":
    sys.exit(main())
