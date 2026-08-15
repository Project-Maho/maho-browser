#!/usr/bin/env python3
"""Dedicated build/preflight and exact-path transport contracts for macOS VM E2E.

This module is deliberately side-effect-light.  It builds canonical argv and
immutable plans; the lifecycle controller owns subprocess execution, SSH, and
VM teardown.
"""

from __future__ import annotations

import hashlib
import os
import stat
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable, Sequence


OUTPUT_DIR = Path("out/Default")
INTERACTIVE_BINARY = "interactive_ui_tests"
RUNTIME_DEPS_FILE = "interactive_ui_tests.runtime_deps"
APP_BUNDLE = "Maho.app"
EXPECTED_ARCHITECTURE = "arm64"
DEFAULT_GUEST_WORKSPACE = Path("/Users/Shared/maho-workspace")
DEFAULT_GUEST_SSH = "admin@guest"
DEFAULT_SHARE_NAME = "maho-workspace"
DEFAULT_GUEST_SHARE_ROOT = Path("/Volumes/My Shared Files")


class BuildTransportError(RuntimeError):
    """Base class for typed build/transport contract failures."""


class InvalidBuildTransportError(BuildTransportError):
    """The requested output or mount violates the Default-only boundary."""


class InvalidRuntimeDependencyError(BuildTransportError):
    """A runtime-deps entry is malformed or escapes its allowed root."""


@dataclass(frozen=True)
class MountContract:
    host_path: Path
    share_name: str
    tart_argument: str
    guest_share_path: Path
    guest_workspace: Path
    remount_argv: tuple[str, ...]


@dataclass(frozen=True)
class HashManifestEntry:
    path: str
    kind: str
    size_bytes: int
    sha256: str


@dataclass(frozen=True)
class RsyncFallbackPlan:
    rsync_argv: tuple[str, ...]
    files_from: str
    relative_paths: tuple[str, ...]
    manifest: tuple[HashManifestEntry, ...]


@dataclass(frozen=True)
class PreflightCommandPlan:
    architecture_argv: tuple[str, ...]
    app_signature_argv: tuple[str, ...]
    binary_signature_argv: tuple[str, ...]
    list_tests_argv: tuple[str, ...]


@dataclass(frozen=True)
class PreflightResult:
    ready: bool
    output_root: Path
    dependencies: tuple[Path, ...]
    missing_paths: tuple[Path, ...]
    stale_paths: tuple[Path, ...]
    missing_tests: tuple[str, ...]
    issues: tuple[str, ...]
    command_plan: PreflightCommandPlan


def canonical_build_argv(workspace_root: Path) -> tuple[str, ...]:
    """Return the sole permitted build command for the dedicated output."""
    root = Path(workspace_root).resolve()
    wrapper = root / "maho-chromium/build/scripts/build_maho.py"
    try:
        wrapper.relative_to(root)
    except ValueError as error:
        raise InvalidBuildTransportError("build wrapper escapes workspace") from error
    return (
        sys.executable,
        "maho-chromium/build/scripts/build_maho.py",
        "--skip-rust",
        "--out-dir",
        OUTPUT_DIR.as_posix(),
        "--no-launch",
        "--ninja-target",
        "chrome",
        "--ninja-target",
        "maho_mail_helper",
        "--ninja-target",
        INTERACTIVE_BINARY,
    )


def validate_output_dir(
    output_dir: Path, *, chromium_src: Path | None = None
) -> Path:
    """Fail closed unless the authored and resolved path is ``out/Default``."""
    candidate = Path(output_dir)
    if candidate.is_absolute() or candidate.parts != OUTPUT_DIR.parts:
        raise InvalidBuildTransportError(
            f"output directory must be exactly {OUTPUT_DIR.as_posix()}"
        )
    if chromium_src is not None:
        source_root = Path(chromium_src).resolve()
        canonical_root = source_root / OUTPUT_DIR
        if canonical_root.resolve(strict=False) != canonical_root:
            raise InvalidBuildTransportError(
                f"output directory must resolve to {canonical_root}"
            )
    return OUTPUT_DIR


def _is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _normalize_dependency(
    entry: str,
    *,
    chromium_src: Path,
    output_root: Path,
) -> Path:
    if "\x00" in entry:
        raise InvalidRuntimeDependencyError("runtime dependency contains NUL")
    authored = PurePosixPath(entry)
    if authored.is_absolute():
        raise InvalidRuntimeDependencyError(
            f"absolute runtime dependency is forbidden: {entry}"
        )
    # GN writes these paths relative to root_build_dir, including ../../ paths
    # for source-tree data. Resolve lexical traversal and symlinks together,
    # allow the Chromium source tree, and reject every sibling output tree.
    resolved = (output_root / Path(*authored.parts)).resolve(strict=False)
    chromium_outputs = chromium_src / "out"
    if not _is_relative_to(resolved, chromium_src) or (
        _is_relative_to(resolved, chromium_outputs)
        and not _is_relative_to(resolved, output_root)
    ):
        raise InvalidRuntimeDependencyError(
            f"runtime dependency escapes Chromium source or canonical Default "
            f"output: {entry}"
        )
    return resolved


def parse_runtime_dependencies(
    contents: str,
    *,
    chromium_src: Path,
    output_dir: Path = OUTPUT_DIR,
) -> tuple[Path, ...]:
    """Parse GN runtime deps, normalize paths, preserve order, and deduplicate."""
    source_root = Path(chromium_src).resolve()
    validated_output = validate_output_dir(output_dir, chromium_src=source_root)
    output_root = source_root / validated_output
    dependencies: list[Path] = []
    seen: set[Path] = set()
    for line_number, raw_line in enumerate(contents.splitlines(), start=1):
        entry = raw_line.strip()
        if not entry:
            continue
        try:
            dependency = _normalize_dependency(
                entry, chromium_src=source_root, output_root=output_root
            )
        except InvalidRuntimeDependencyError as error:
            raise InvalidRuntimeDependencyError(
                f"invalid runtime dependency on line {line_number}: {error}"
            ) from error
        if dependency not in seen:
            seen.add(dependency)
            dependencies.append(dependency)
    if not dependencies:
        raise InvalidRuntimeDependencyError("runtime dependency list is empty")
    return tuple(dependencies)


def read_runtime_dependencies(
    chromium_src: Path, output_dir: Path = OUTPUT_DIR
) -> tuple[Path, ...]:
    source_root = Path(chromium_src).resolve()
    output = validate_output_dir(output_dir, chromium_src=source_root)
    path = source_root / output / RUNTIME_DEPS_FILE
    try:
        contents = path.read_text(encoding="utf-8")
    except OSError as error:
        raise InvalidRuntimeDependencyError(
            f"unable to read runtime dependencies: {path}: {error}"
        ) from error
    return parse_runtime_dependencies(
        contents, chromium_src=chromium_src, output_dir=output
    )


def read_only_mount_contract(
    workspace_root: Path,
    guest_workspace: Path = DEFAULT_GUEST_WORKSPACE,
    *,
    share_name: str = DEFAULT_SHARE_NAME,
) -> MountContract:
    """Describe Tart's read-only share and exact guest-path remount."""
    host = Path(workspace_root).resolve()
    guest = Path(guest_workspace)
    if not guest.is_absolute() or guest != DEFAULT_GUEST_WORKSPACE:
        raise InvalidBuildTransportError(
            f"guest workspace must be exactly {DEFAULT_GUEST_WORKSPACE}"
        )
    if not share_name or "/" in share_name or ":" in share_name:
        raise InvalidBuildTransportError("invalid VirtioFS share name")
    guest_share = DEFAULT_GUEST_SHARE_ROOT / share_name
    return MountContract(
        host_path=host,
        share_name=share_name,
        tart_argument=f"{share_name}:{host}:ro,tag={share_name}",
        guest_share_path=guest_share,
        guest_workspace=guest,
        remount_argv=(
            "sudo",
            "mount",
            "-t",
            "virtiofs",
            "-o",
            "ro",
            share_name,
            guest.as_posix(),
        ),
    )


def _hash_regular_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


def _manifest_entry(path: Path, workspace: Path) -> HashManifestEntry:
    relative = path.relative_to(workspace).as_posix()
    mode = path.lstat().st_mode
    if stat.S_ISLNK(mode):
        target = os.readlink(path).encode("utf-8", errors="surrogateescape")
        return HashManifestEntry(
            path=relative,
            kind="symlink",
            size_bytes=len(target),
            sha256=hashlib.sha256(target).hexdigest(),
        )
    if stat.S_ISREG(mode):
        size, digest = _hash_regular_file(path)
        return HashManifestEntry(relative, "file", size, digest)
    raise InvalidRuntimeDependencyError(
        f"unsupported runtime dependency file type: {path}"
    )


def _expand_manifest_paths(
    roots: Sequence[Path], workspace: Path, output_root: Path
) -> tuple[HashManifestEntry, ...]:
    entries: list[HashManifestEntry] = []
    seen: set[Path] = set()
    for root in roots:
        if not root.exists() and not root.is_symlink():
            raise InvalidRuntimeDependencyError(f"runtime dependency is missing: {root}")
        candidates: Iterable[Path]
        if root.is_dir() and not root.is_symlink():
            candidates = sorted(
                (item for item in root.rglob("*") if not item.is_dir()),
                key=lambda item: item.relative_to(workspace).as_posix(),
            )
        else:
            candidates = (root,)
        for candidate in candidates:
            lexical = candidate.absolute()
            resolved = lexical.resolve(strict=False)
            chromium_outputs = workspace / "chromium/src/out"
            if not _is_relative_to(resolved, workspace) or (
                _is_relative_to(resolved, chromium_outputs)
                and not _is_relative_to(resolved, output_root)
            ):
                raise InvalidRuntimeDependencyError(
                    "fallback path escapes workspace or canonical Default output: "
                    f"{candidate}"
                )
            if lexical not in seen:
                seen.add(lexical)
                entries.append(_manifest_entry(lexical, workspace))
    return tuple(entries)


def plan_rsync_fallback(
    *,
    workspace_root: Path,
    dependencies: Sequence[Path],
    guest_workspace: Path = DEFAULT_GUEST_WORKSPACE,
    extra_paths: Sequence[Path] = (),
    ssh_destination: str = DEFAULT_GUEST_SSH,
) -> RsyncFallbackPlan:
    """Plan an exact runtime-deps mirror using rsync's relative-path mode."""
    workspace = Path(workspace_root).resolve()
    output_root = workspace / "chromium/src" / OUTPUT_DIR
    if output_root.resolve(strict=False) != output_root:
        raise InvalidBuildTransportError(
            f"output directory must resolve to {output_root}"
        )
    guest = Path(guest_workspace)
    if guest != DEFAULT_GUEST_WORKSPACE:
        raise InvalidBuildTransportError(
            f"fallback guest workspace must be exactly {DEFAULT_GUEST_WORKSPACE}"
        )
    roots: list[Path] = []
    relative_paths: list[str] = []
    seen: set[str] = set()
    for raw_path in dependencies:
        path = Path(raw_path).absolute()
        resolved = path.resolve(strict=False)
        chromium_outputs = workspace / "chromium/src/out"
        if not _is_relative_to(resolved, workspace) or (
            _is_relative_to(resolved, chromium_outputs)
            and not _is_relative_to(resolved, output_root)
        ):
            raise InvalidRuntimeDependencyError(
                f"fallback dependency escapes workspace or canonical Default output: "
                f"{path}"
            )
        relative = path.relative_to(workspace).as_posix()
        if relative not in seen:
            seen.add(relative)
            roots.append(path)
            relative_paths.append(relative)
    chromium_outputs = workspace / "chromium/src/out"
    for raw_path in extra_paths:
        path = Path(raw_path).absolute()
        resolved = path.resolve(strict=False)
        if not _is_relative_to(resolved, workspace) or (
            _is_relative_to(resolved, chromium_outputs)
            and not _is_relative_to(resolved, output_root)
        ):
            raise InvalidRuntimeDependencyError(
                f"fallback extra path escapes workspace or canonical Default output: "
                f"{path}"
            )
        relative = path.relative_to(workspace).as_posix()
        if relative not in seen:
            seen.add(relative)
            roots.append(path)
            relative_paths.append(relative)
    manifest = _expand_manifest_paths(roots, workspace, output_root)
    files_from = "".join(f"{path}\n" for path in relative_paths)
    return RsyncFallbackPlan(
        rsync_argv=(
            "rsync",
            "--archive",
            "--recursive",
            "--relative",
            "--files-from=-",
            f"{workspace}/./",
            f"{ssh_destination}:{guest.as_posix()}/",
        ),
        files_from=files_from,
        relative_paths=tuple(relative_paths),
        manifest=manifest,
    )


def preflight_command_plan(
    chromium_src: Path, output_dir: Path = OUTPUT_DIR
) -> PreflightCommandPlan:
    source_root = Path(chromium_src).resolve()
    output = validate_output_dir(output_dir, chromium_src=source_root)
    root = source_root / output
    binary = root / INTERACTIVE_BINARY
    app = root / APP_BUNDLE
    return PreflightCommandPlan(
        architecture_argv=("/usr/bin/file", "-b", binary.as_posix()),
        app_signature_argv=(
            "/usr/bin/codesign",
            "--verify",
            "--deep",
            "--strict",
            app.as_posix(),
        ),
        binary_signature_argv=(
            "/usr/bin/codesign",
            "--verify",
            "--strict",
            binary.as_posix(),
        ),
        list_tests_argv=(binary.as_posix(), "--gtest_list_tests"),
    )


def inspect_preflight(
    *,
    workspace_root: Path,
    output_dir: Path = OUTPUT_DIR,
    required_tests: Sequence[str],
    listed_tests: Sequence[str],
    observed_architecture: str | None = None,
    app_signature_valid: bool | None = None,
    binary_signature_valid: bool | None = None,
    list_tests_exit_code: int | None = None,
) -> PreflightResult:
    """Inspect static freshness and combine externally observed live checks.

    The caller executes ``command_plan`` before VM creation and passes the
    observations back here.  ``None`` means not yet observed and is not silently
    promoted to success.
    """
    workspace = Path(workspace_root).resolve()
    chromium_src = workspace / "chromium/src"
    output = validate_output_dir(output_dir, chromium_src=chromium_src)
    output_root = chromium_src / output
    binary = output_root / INTERACTIVE_BINARY
    app = output_root / APP_BUNDLE
    deps_file = output_root / RUNTIME_DEPS_FILE
    command_plan = preflight_command_plan(chromium_src, output)

    missing: list[Path] = [
        path for path in (binary, app, deps_file) if not path.exists()
    ]
    dependencies: tuple[Path, ...] = ()
    issues: list[str] = []
    if deps_file.exists():
        try:
            dependencies = read_runtime_dependencies(chromium_src, output)
        except InvalidRuntimeDependencyError as error:
            issues.append(str(error))
        else:
            missing.extend(
                dependency
                for dependency in dependencies
                if not dependency.exists() and not dependency.is_symlink()
            )

    stale: list[Path] = []
    if deps_file.exists():
        reference_mtime = deps_file.stat().st_mtime_ns
        for path in (binary, app):
            if path.exists() and path.stat().st_mtime_ns < reference_mtime:
                stale.append(path)
        for dependency in dependencies:
            if dependency.exists() and dependency.stat().st_mtime_ns < reference_mtime:
                stale.append(dependency)

    missing_tests = tuple(test for test in required_tests if test not in listed_tests)
    if observed_architecture is None:
        issues.append("architecture not verified")
    elif EXPECTED_ARCHITECTURE not in observed_architecture.lower():
        issues.append(
            f"interactive_ui_tests architecture is not {EXPECTED_ARCHITECTURE}: "
            f"{observed_architecture}"
        )
    if app_signature_valid is not True:
        issues.append("Maho.app signature not verified")
    if binary_signature_valid is not True:
        issues.append("interactive_ui_tests signature not verified")
    if list_tests_exit_code != 0:
        issues.append("--gtest_list_tests did not exit 0")
    if missing_tests:
        issues.append("required manifest tests are absent from gtest inventory")

    unique_missing = tuple(dict.fromkeys(missing))
    unique_stale = tuple(dict.fromkeys(stale))
    return PreflightResult(
        ready=not unique_missing and not unique_stale and not issues,
        output_root=output_root,
        dependencies=dependencies,
        missing_paths=unique_missing,
        stale_paths=unique_stale,
        missing_tests=missing_tests,
        issues=tuple(issues),
        command_plan=command_plan,
    )
