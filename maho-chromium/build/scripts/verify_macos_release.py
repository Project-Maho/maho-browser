#!/usr/bin/env python3
"""Fail-closed validation for a distributable Maho macOS DMG.

This verifies the artifact users actually install, not only the build-tree app.
A valid release must be Developer ID signed, notarized/stapled, accepted by
Gatekeeper, have a launchable bundle executable, and contain no build-tree
absolute dylib references or signature-breaking Finder/resource-fork metadata.
"""

from __future__ import annotations

import argparse
import os
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_BUNDLE_ID = "com.maho.browser"
EXPECTED_PACKAGE_TYPE = "APPL"
_ALLOWED_ABSOLUTE_DYLIB_PREFIXES = ("/System/Library/", "/usr/lib/")
_ALLOWED_RELATIVE_DYLIB_PREFIXES = ("@rpath/", "@loader_path/", "@executable_path/")
_FORBIDDEN_XATTRS = ("com.apple.FinderInfo", "com.apple.ResourceFork")


class VerificationError(RuntimeError):
    """Raised when a release artifact is not safe to distribute."""


def _render_failure(command: list[str], result: subprocess.CompletedProcess[str]) -> str:
    detail = (result.stderr or result.stdout or "").strip()
    suffix = f": {detail}" if detail else ""
    return f"command failed ({result.returncode}): {' '.join(command)}{suffix}"


def _run(
    command: list[str],
    *,
    timeout: int = 180,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as exc:
        raise VerificationError(f"failed to execute {' '.join(command)}: {exc}") from exc
    if check and result.returncode != 0:
        raise VerificationError(_render_failure(command, result))
    return result


def _load_bundle_info(app_path: Path) -> dict:
    info_path = app_path / "Contents" / "Info.plist"
    if not info_path.is_file():
        raise VerificationError(f"Info.plist is missing: {info_path}")
    try:
        with info_path.open("rb") as info_file:
            info = plistlib.load(info_file)
    except (OSError, plistlib.InvalidFileException) as exc:
        raise VerificationError(f"invalid Info.plist: {info_path}: {exc}") from exc
    if not isinstance(info, dict):
        raise VerificationError(f"Info.plist root is not a dictionary: {info_path}")
    return info


def _verify_bundle_metadata(
    app_path: Path,
    expected_arch: str,
    expected_version: str | None = None,
) -> Path:
    info = _load_bundle_info(app_path)
    bundle_id = info.get("CFBundleIdentifier")
    if bundle_id != EXPECTED_BUNDLE_ID:
        raise VerificationError(
            f"unexpected CFBundleIdentifier: {bundle_id!r}; expected {EXPECTED_BUNDLE_ID!r}"
        )
    package_type = info.get("CFBundlePackageType")
    if package_type != EXPECTED_PACKAGE_TYPE:
        raise VerificationError(
            f"unexpected CFBundlePackageType: {package_type!r}; expected {EXPECTED_PACKAGE_TYPE!r}"
        )
    if expected_version is not None:
        short_version = info.get("CFBundleShortVersionString")
        if short_version != expected_version:
            raise VerificationError(
                f"unexpected CFBundleShortVersionString: {short_version!r}; expected {expected_version!r}"
            )
        bundle_version = info.get("CFBundleVersion")
        if bundle_version != expected_version:
            raise VerificationError(
                f"unexpected CFBundleVersion: {bundle_version!r}; expected {expected_version!r}"
            )
    executable_name = info.get("CFBundleExecutable")
    if not isinstance(executable_name, str) or not executable_name:
        raise VerificationError("CFBundleExecutable is missing or empty")

    executable = app_path / "Contents" / "MacOS" / executable_name
    if not executable.is_file():
        raise VerificationError(
            f"CFBundleExecutable does not exist as a regular file: {executable}"
        )
    try:
        mode = executable.stat().st_mode
    except OSError as exc:
        raise VerificationError(f"cannot stat bundle executable: {executable}: {exc}") from exc
    if not stat.S_ISREG(mode) or mode & 0o111 == 0:
        raise VerificationError(
            f"bundle executable is not executable: {executable} mode={oct(mode & 0o777)}"
        )

    archs = _run(["lipo", "-archs", str(executable)]).stdout.split()
    if expected_arch not in archs:
        raise VerificationError(
            f"bundle executable does not contain {expected_arch}: {executable}; archs={archs}"
        )
    return executable


def _verify_code_signature(app_path: Path) -> None:
    _run(
        [
            "codesign",
            "--verify",
            "--deep",
            "--strict",
            "--verbose=4",
            str(app_path),
        ],
        timeout=300,
    )
    details = _run(
        ["codesign", "-dv", "--verbose=4", str(app_path)],
        timeout=120,
    )
    signature_text = f"{details.stdout}\n{details.stderr}"
    if "Authority=Developer ID Application:" not in signature_text:
        raise VerificationError(
            "app is not signed with a Developer ID Application certificate"
        )
    if f"Identifier={EXPECTED_BUNDLE_ID}" not in signature_text:
        raise VerificationError(
            f"codesign identifier is not {EXPECTED_BUNDLE_ID}"
        )
    team = re.search(r"^TeamIdentifier=(.+)$", signature_text, re.MULTILINE)
    if team is None or team.group(1).strip() in {"", "not set"}:
        raise VerificationError("Developer ID signature has no TeamIdentifier")


def _parse_otool_dependencies(output: str) -> list[str]:
    dependencies: list[str] = []
    for raw_line in output.splitlines()[1:]:
        line = raw_line.strip()
        if not line:
            continue
        dependencies.append(line.split(" (", 1)[0])
    return dependencies


def _verify_runtime_dependencies(app_path: Path, executable: Path) -> None:
    framework = (
        app_path
        / "Contents"
        / "Frameworks"
        / "Maho Framework.framework"
        / "Versions"
        / "Current"
        / "Maho Framework"
    )
    if not framework.is_file():
        raise VerificationError(f"Maho Framework executable is missing: {framework}")

    helper_candidates = [
        app_path / "Contents" / "MacOS" / "maho_mail_helper",
        app_path / "Contents" / "Helpers" / "maho",
    ]
    binaries = [executable, framework, *[p for p in helper_candidates if p.is_file()]]
    saw_sparkle_dependency = False
    for binary in binaries:
        result = _run(["otool", "-L", str(binary)], timeout=180)
        for dependency in _parse_otool_dependencies(result.stdout):
            if dependency.startswith("@rpath/Sparkle.framework/"):
                saw_sparkle_dependency = True
            if dependency.startswith(_ALLOWED_ABSOLUTE_DYLIB_PREFIXES):
                continue
            if dependency.startswith(_ALLOWED_RELATIVE_DYLIB_PREFIXES):
                continue
            if dependency.startswith("/"):
                raise VerificationError(
                    f"non-system absolute dylib dependency in {binary}: {dependency}"
                )
            raise VerificationError(
                f"unsupported dylib install name in {binary}: {dependency}"
            )

    sparkle = app_path / "Contents" / "Frameworks" / "Sparkle.framework"
    if saw_sparkle_dependency and not sparkle.is_dir():
        raise VerificationError(
            f"Sparkle is referenced through @rpath but not bundled: {sparkle}"
        )


def _verify_xattrs(app_path: Path) -> None:
    result = _run(["xattr", "-lr", str(app_path)], timeout=180)
    for attr in _FORBIDDEN_XATTRS:
        if attr in result.stdout:
            raise VerificationError(
                f"signature-breaking extended attribute found in app bundle: {attr}"
            )
    # com.apple.quarantine is intentionally allowed. A downloaded release should
    # survive Gatekeeper *with* quarantine rather than depending on stripping it.


def _verify_media_capabilities(app_path: Path) -> None:
    """Verify that Maho Framework contains required FFmpeg audio/video decoders."""
    framework = (
        app_path
        / "Contents"
        / "Frameworks"
        / "Maho Framework.framework"
        / "Versions"
        / "Current"
        / "Maho Framework"
    )
    if not framework.is_file():
        framework = (
            app_path
            / "Contents"
            / "Frameworks"
            / "Maho Framework.framework"
            / "Maho Framework"
        )
    if not framework.is_file():
        raise VerificationError(f"Maho Framework executable is missing: {framework}")

    try:
        content = framework.read_bytes()
    except OSError as exc:
        raise VerificationError(f"cannot read Maho Framework: {exc}") from exc

    required_markers = [b"ffmpeg_video_decoder", b"ffmpeg_audio_decoder", b"avcodec_open2"]
    missing = [m.decode("ascii") for m in required_markers if m not in content]
    if missing:
        raise VerificationError(
            f"Maho Framework is missing mandatory FFmpeg media decoder symbols: {', '.join(missing)}"
        )


def verify_app_bundle(
    app_path: str | os.PathLike[str],
    *,
    expected_arch: str = "arm64",
    require_gatekeeper: bool = True,
    require_stapled: bool = False,
    expected_version: str | None = None,
) -> None:
    app = Path(app_path)
    if not app.is_dir():
        raise VerificationError(f"Maho.app is missing: {app}")
    executable = _verify_bundle_metadata(app, expected_arch, expected_version)
    _verify_code_signature(app)
    _verify_runtime_dependencies(app, executable)
    _verify_xattrs(app)
    _verify_media_capabilities(app)
    if require_stapled:
        _run(["xcrun", "stapler", "validate", str(app)], timeout=180)
    if require_gatekeeper:
        _run(["spctl", "-a", "-t", "exec", "-vv", str(app)], timeout=180)


def verify_release_dmg(
    dmg_path: str | os.PathLike[str],
    *,
    expected_arch: str = "arm64",
) -> None:
    dmg = Path(dmg_path)
    if not dmg.is_file():
        raise VerificationError(f"DMG is missing: {dmg}")

    _run(["codesign", "--verify", "--verbose=4", str(dmg)], timeout=180)
    _run(["xcrun", "stapler", "validate", str(dmg)], timeout=180)
    _run(
        [
            "spctl",
            "-a",
            "-t",
            "open",
            "--context",
            "context:primary-signature",
            "-vv",
            str(dmg),
        ],
        timeout=180,
    )

    mount_point = tempfile.mkdtemp(prefix="maho-release-verify-")
    attached = False
    try:
        _run(
            [
                "hdiutil",
                "attach",
                "-nobrowse",
                "-readonly",
                "-mountpoint",
                mount_point,
                str(dmg),
            ],
            timeout=180,
        )
        attached = True
        mounted_app = Path(mount_point) / "Maho.app"
        verify_app_bundle(
            mounted_app,
            expected_arch=expected_arch,
            require_gatekeeper=True,
            require_stapled=True,
        )
    finally:
        if attached:
            detach = _run(
                ["hdiutil", "detach", mount_point, "-quiet"],
                timeout=180,
                check=False,
            )
            if detach.returncode != 0 and sys.exc_info()[0] is None:
                raise VerificationError(_render_failure(
                    ["hdiutil", "detach", mount_point, "-quiet"], detach
                ))
        shutil.rmtree(mount_point, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dmg", required=True, help="Path to Maho-<version>.dmg")
    parser.add_argument(
        "--expected-arch",
        default="arm64",
        choices=("arm64", "x86_64"),
        help="Required architecture in Contents/MacOS/Maho (default: arm64)",
    )
    args = parser.parse_args()

    if sys.platform != "darwin":
        print("error: macOS release verification must run on macOS", file=sys.stderr)
        return 2
    try:
        verify_release_dmg(args.dmg, expected_arch=args.expected_arch)
    except VerificationError as exc:
        print(f"error: macOS release verification failed: {exc}", file=sys.stderr)
        return 1
    print(f"macOS release verification passed: {args.dmg}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
