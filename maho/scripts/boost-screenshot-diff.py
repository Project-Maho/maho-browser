#!/usr/bin/env python3
"""Boost editor screenshot SSIM diff harness.

Launches Maho.app, triggers the boost editor via Cmd+Opt+B (AppleScript),
captures a screen region, and computes SSIM against a reference PNG.

REQUIREMENTS:
  - macOS with a display session (non-headless). Cannot run in CI without
    a virtual display (Xvfb or equivalent).
  - Accessibility permissions for System Events (AppleScript keystroke
    simulation requires it).
  - pip3 install -r maho/scripts/requirements-boost-diff.txt

RETINA / 2x HANDLING:
  macOS screencapture on Retina displays produces images at 2x the logical
  pixel dimensions. The script normalizes by resizing the captured image to
  match the reference image dimensions before computing SSIM. This means
  reference screenshots should be at 1x logical resolution.

EXIT CODES:
  0 — PASS (SSIM >= 0.85) or WARN (0.70 <= SSIM < 0.85)
  1 — FAIL (SSIM < 0.70)
  2 — ERROR (missing prerequisites, crash, bad arguments)
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

def check_imports() -> bool:
    """Check that required third-party packages are importable."""
    missing: list[str] = []
    for pkg in ("skimage", "numpy", "PIL"):
        try:
            __import__(pkg)
        except ImportError:
            missing.append(pkg)
    if missing:
        print(
            f"ERROR: Missing Python packages: {', '.join(missing)}\n"
            "Install with:\n"
            "  pip3 install -r maho/scripts/requirements-boost-diff.txt",
            file=sys.stderr,
        )
        return False
    return True


MODE_DIMS: dict[str, tuple[int, int]] = {
    "boost": (184, 582),
    "code": (452, 582),
}

def osascript_run(script: str, *, timeout: int = 15) -> subprocess.CompletedProcess[str]:
    """Run an AppleScript snippet via osascript."""
    return subprocess.run(
        ["osascript", "-e", script],
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compute SSIM between a reference PNG and a live Maho boost editor capture.",
    )
    p.add_argument("--reference", required=True, type=Path, help="Path to reference PNG")
    p.add_argument("--mode", required=True, choices=("boost", "code"), help="Capture mode")
    p.add_argument(
        "--offset",
        default="100,100",
        help="Window position as X,Y (default: 100,100)",
    )
    p.add_argument("--timeout", type=int, default=10, help="Subprocess timeout in seconds (default: 10)")
    p.add_argument("--keep-output", action="store_true", help="Keep Maho.app running and capture file after exit")
    return p.parse_args()


def main() -> int:
    # ---- args ----
    args = parse_args()

    # ---- prerequisite imports ----
    if not check_imports():
        return 2

    # Deferred imports (only after check_imports passes)
    from skimage.metrics import structural_similarity as ssim  # type: ignore[import-untyped]
    from skimage.io import imread  # type: ignore[import-untyped]
    from skimage.transform import resize  # type: ignore[import-untyped]
    import numpy as np

    # ---- validate reference ----
    if not args.reference.is_file():
        print(f"ERROR: Reference PNG not found: {args.reference}", file=sys.stderr)
        return 2

    # ---- validate Maho.app ----
    workspace_root = Path(__file__).resolve().parent.parent.parent  # maho/scripts -> maho -> workspace
    maho_app = workspace_root / "chromium" / "src" / "out" / "Default" / "Maho.app"
    maho_bin = maho_app / "Contents" / "MacOS" / "Maho"
    if not maho_app.is_dir():
        print(
            f"ERROR: Maho.app not found at {maho_app}\n"
            "Build first: python3 maho-chromium/build/scripts/build_maho.py",
            file=sys.stderr,
        )
        return 2

    # ---- parse offset ----
    try:
        offset_x, offset_y = (int(v) for v in args.offset.split(","))
    except ValueError:
        print(f"ERROR: Invalid --offset format '{args.offset}'. Expected X,Y (e.g. 100,100)", file=sys.stderr)
        return 2

    w, h = MODE_DIMS[args.mode]
    capture_path = f"/tmp/boost-capture-{args.mode}.png"

    # ---- launch Maho.app ----
    user_data_dir = tempfile.mkdtemp(prefix="maho-screenshot-")
    proc = subprocess.Popen([
        str(maho_bin),
        "--no-first-run",
        "--disable-sync",
        f"--user-data-dir={user_data_dir}",
        "about:blank",
    ])

    try:
        print(f"Launched Maho.app (PID {proc.pid}), waiting 5s for startup...")
        time.sleep(5)

        if proc.poll() is not None:
            print(f"ERROR: Maho.app crashed on startup (exit code {proc.returncode})", file=sys.stderr)
            return 2

        # ---- trigger boost editor ----
        print("Triggering boost editor (Cmd+Opt+B)...")
        try:
            result = osascript_run(
                'tell application "Maho" to activate\n'
                "delay 0.5\n"
                'tell application "System Events" to keystroke "b" using {command down, option down}',
                timeout=args.timeout,
            )
            if result.returncode != 0:
                print(f"WARN: AppleScript keystroke returned non-zero: {result.stderr.strip()}", file=sys.stderr)
        except subprocess.TimeoutExpired:
            print("WARN: AppleScript keystroke timed out — Maho may not be focused", file=sys.stderr)

        time.sleep(2)

        # ---- code mode (if requested) ----
        if args.mode == "code":
            # TODO: Determine the exact shortcut or DOM action to switch to code mode.
            # For now, attempt clicking the code-mode tab via AppleScript GUI scripting.
            # If a keyboard shortcut is discovered later, replace this block.
            print("WARN: Code mode switch relies on boost-mode default; "
                  "specific code-mode shortcut unknown. Capture may show boost mode.", file=sys.stderr)

        # ---- position window ----
        print(f"Positioning window to ({offset_x}, {offset_y})...")
        try:
            osascript_run(
                f'tell application "System Events"\n'
                f'  tell process "Maho"\n'
                f"    set position of window 1 to {{{offset_x}, {offset_y}}}\n"
                f"  end tell\n"
                f"end tell",
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired:
            print("WARN: Window positioning timed out", file=sys.stderr)

        time.sleep(0.5)

        # ---- capture region ----
        print(f"Capturing region {w}x{h} at ({offset_x},{offset_y})...")
        try:
            subprocess.run(
                ["screencapture", f"-R{offset_x},{offset_y},{w},{h}", capture_path],
                check=True,
                timeout=args.timeout,
            )
        except subprocess.CalledProcessError as e:
            print(f"ERROR: screencapture failed: {e}", file=sys.stderr)
            return 2
        except subprocess.TimeoutExpired:
            print("ERROR: screencapture timed out", file=sys.stderr)
            return 2

        if not os.path.isfile(capture_path):
            print(f"ERROR: Capture file not created at {capture_path}", file=sys.stderr)
            return 2

        # ---- compute SSIM ----
        print("Computing SSIM...")
        ref = imread(str(args.reference))
        cap = imread(capture_path)

        # Strip alpha channel if present
        if ref.ndim == 3 and ref.shape[2] == 4:
            ref = ref[:, :, :3]
        if cap.ndim == 3 and cap.shape[2] == 4:
            cap = cap[:, :, :3]

        # Retina / dimension mismatch: resize captured image to reference dimensions.
        # On Retina displays screencapture produces 2x images; this normalizes them.
        if cap.shape[:2] != ref.shape[:2]:
            print(f"  Resizing capture {cap.shape[:2]} -> {ref.shape[:2]} (retina/dimension normalization)")
            cap = (resize(cap, ref.shape[:2], anti_aliasing=True) * 255).astype(np.uint8)

        score: float = ssim(ref, cap, channel_axis=2, data_range=255)

        # ---- report ----
        print(f"\nSSIM: {score:.4f}")
        print(f"Capture: {capture_path}")

        if score >= 0.85:
            print("Result: PASS")
            return 0
        elif score >= 0.70:
            print("Result: WARN (informational — visual differences detected but within tolerance)")
            return 0
        else:
            print("Result: FAIL (significant visual divergence from reference)")
            return 1

    finally:
        # ---- cleanup ----
        if not args.keep_output:
            print("Terminating Maho.app...")
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
        else:
            print(f"Keeping Maho.app running (PID {proc.pid})")


if __name__ == "__main__":
    sys.exit(main())
