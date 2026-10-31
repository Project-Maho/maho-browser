#!/usr/bin/env python3
"""Validate occurrence-anchored normal-sidebar foreground ledger entries."""

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Final

from sidebar_theme_foreground_scanner import Hit, SourceFile, scan


_WORKSPACE: Final = Path(__file__).resolve().parents[3]
_SIDEBAR: Final = "maho-chromium/browser/ui/views/sidebar/"
_LEDGER: Final = _WORKSPACE / _SIDEBAR / "sidebar_theme_color_ledger.json"
_SCHEMA: Final = "maho.sidebar-theme-color-ledger.v2"
_CLASSES: Final = frozenset(
    {"migrate", "retain_semantic_artwork", "retain_platform_private"}
)
_ALLOW: Final = frozenset(
    {
        "maho-chromium/browser/ui/views/DESIGN.md",
        "maho-chromium/browser/ui/views/sidebar/sidebar_theme_color_ledger.json",
        "maho-chromium/build/scripts/check_sidebar_theme_color_ledger.py",
        "maho-chromium/build/scripts/sidebar_theme_foreground_scanner.py",
    }
)
_EVIDENCE: Final = ".omo/evidence/sidebar-theme-contrast-window-isolation/task-1-sidebar-theme-contrast-window-isolation/"
_STATUS: Final = re.compile(r"^(?P<code>[ MADRCU?!][ MADRCU?!m]) (?P<paths>.+)$")


@dataclass(frozen=True, slots=True)
class Entry:
    path: str
    symbol: str
    anchor: str
    expected: int
    classification: str
    rationale: str


@dataclass(frozen=True, slots=True)
class StatusTuple:
    code: str
    path: str
    original_path: str | None


def _git_sources() -> tuple[SourceFile, ...]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=_WORKSPACE,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError("git ls-files failed")
    paths = (Path(raw.decode()) for raw in result.stdout.split(b"\0") if raw)
    return tuple(
        SourceFile(str(path), (_WORKSPACE / path).read_text(encoding="utf-8"))
        for path in paths
        if str(path).startswith(_SIDEBAR) and path.suffix in {".cc", ".h"}
        # --cached still lists tracked files deleted in the working tree.
        and (_WORKSPACE / path).is_file()
    )


def _load() -> tuple[Entry, ...]:
    try:
        raw = json.loads(_LEDGER.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise RuntimeError(f"missing ledger: {_LEDGER}") from error
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid ledger JSON: {error}") from error
    if not isinstance(raw, dict) or raw.get("schema") != _SCHEMA or raw.get("version") != 2:
        raise RuntimeError("ledger must declare maho.sidebar-theme-color-ledger.v2 version 2")
    values = raw.get("entries")
    if not isinstance(values, list) or not values:
        raise RuntimeError("ledger entries must be a nonempty list")
    entries: list[Entry] = []
    for index, value in enumerate(values, 1):
        if not isinstance(value, dict):
            raise RuntimeError(f"ledger entry {index} is not an object")
        fields = ("path", "symbol", "anchor", "classification", "rationale")
        if any(not isinstance(value.get(field), str) or not value[field] for field in fields):
            raise RuntimeError(f"ledger entry {index} has an empty required field")
        expected = value.get("expected")
        if not isinstance(expected, int) or expected < 1:
            raise RuntimeError(f"ledger entry {index} expected must be a positive integer")
        if value["classification"] not in _CLASSES:
            raise RuntimeError(f"ledger entry {index} has unsupported classification")
        entries.append(
            Entry(
                value["path"], value["symbol"], value["anchor"], expected,
                value["classification"], value["rationale"],
            )
        )
    return tuple(entries)


def _validate(entries: tuple[Entry, ...], sources: tuple[SourceFile, ...]) -> tuple[str, ...]:
    diagnostics: list[str] = []
    entry_keys: set[tuple[str, str, str]] = set()
    for entry in entries:
        key = (entry.path, entry.symbol, entry.anchor)
        if key in entry_keys:
            diagnostics.append(f"ambiguous ledger entry: {entry.path} [{entry.symbol}] [{entry.anchor}]")
        entry_keys.add(key)
    hits_by_key: dict[tuple[str, str, str], list[Hit]] = {}
    for hit in scan(sources):
        hits_by_key.setdefault((hit.path, hit.symbol, hit.anchor), []).append(hit)
    entries_by_key = {
        (entry.path, entry.symbol, entry.anchor): entry for entry in entries
    }
    for key, hits in hits_by_key.items():
        entry = entries_by_key.get(key)
        if entry is None:
            candidates = tuple(
                item.symbol for item in entries
                if item.path == hits[0].path and item.anchor == hits[0].anchor
            )
            prefix = "wrong symbol" if candidates else "unclassified"
            expected = "; expected " + ", ".join(f"[{item}]" for item in candidates) if candidates else ""
            diagnostics.extend(
                f"{prefix}: {hit.path}:{hit.line}: {hit.anchor} [{hit.symbol}]{expected}"
                for hit in hits
            )
        elif len(hits) != entry.expected:
            diagnostics.append(
                f"stale occurrence count: {entry.path} [{entry.symbol}] [{entry.anchor}] "
                f"expected {entry.expected}, found {len(hits)}"
            )
    diagnostics.extend(
        f"stale occurrence: {entry.path} [{entry.symbol}] [{entry.anchor}]"
        for entry in entries
        if (entry.path, entry.symbol, entry.anchor) not in hits_by_key
    )
    return tuple(diagnostics)


def _replace_source(
    sources: tuple[SourceFile, ...], target: SourceFile, replacement: SourceFile,
) -> tuple[SourceFile, ...]:
    return tuple(replacement if source == target else source for source in sources)


def _masking_failures() -> tuple[str, ...]:
    cases = (
        ("line comment", "// ui::kColorSysOnSurface SkColorSetRGB { }"),
        ("block comment", "/* ui::kColorSysOnSurface SkColorSetRGB { } */"),
        ("ordinary string", 'const char* value = "ui::kColorSysOnSurface SkColorSetRGB { }";'),
        ("character literal", "int value = 'ui::kColorSysOnSurface SkColorSetRGB { }';"),
        ("raw string", 'const char* value = R"tag(" ui::kColorSysOnSurface SkColorSetRGB { } ")tag";'),
    )
    failures: list[str] = []
    for label, obscured in cases:
        source = SourceFile(
            f"{_SIDEBAR}maho_sidebar_space_dot_view.cc",
            f"void MaskingProbe() {{\n  {obscured}\n"
            "  auto color = ui::kColorSysOnSurface;\n}\n",
        )
        hits = scan((source,))
        if len(hits) != 1 or hits[0].symbol != "function MaskingProbe()":
            failures.append(f"{label}: {hits}")
    return tuple(failures)


def _run_check() -> int:
    try:
        diagnostics = _validate(_load(), _git_sources())
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    for diagnostic in diagnostics:
        print(diagnostic, file=sys.stderr)
    print(f"sidebar color ledger: {len(diagnostics)} diagnostics")
    return int(bool(diagnostics))


def _self_test_unclassified() -> int:
    try:
        sources = _git_sources()
        target = next(source for source in sources if source.path.endswith("maho_sidebar_top_bar_view.cc"))
        entries = _load()
        injected = SourceFile(target.path, target.text + "\nvoid LedgerProbe() { auto color = kMahoColorSidebarTertiaryText; }\n")
        unknown = _validate(entries, _replace_source(sources, target, injected))
        removed = SourceFile(target.path, target.text.replace("ui::kColorSysOnSurface", "", 1))
        stale = _validate(entries, _replace_source(sources, target, removed))
        duplicate = _validate(entries + (entries[0],), sources)
        relocated_text = target.text.replace(
            "return enabled ? ui::kColorSysOnSurface : ui::kColorSysOnSurfaceSubtle;",
            "return enabled ? ui::kColorSysPrimary : ui::kColorSysOnSurfaceSubtle;", 1,
        ).replace(
            "void ApplyButtonChrome(views::ImageButton* button) {",
            "void ApplyButtonChrome(views::ImageButton* button) {\n"
            "  auto ledger_relocation = ui::kColorSysOnSurface;", 1,
        )
        relocated_sources = _replace_source(sources, target, SourceFile(target.path, relocated_text))
        wrong_identity = _validate(entries, relocated_sources)
        original_count = sum(hit.path == target.path and hit.anchor == "ui::kColorSysOnSurface" for hit in scan(sources))
        relocated_count = sum(hit.path == target.path and hit.anchor == "ui::kColorSysOnSurface" for hit in scan(relocated_sources))
        masking_failures = _masking_failures()
    except (OSError, RuntimeError, StopIteration) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    controls = (
        (any(item.startswith("unclassified:") for item in unknown), "unknown"),
        (any(item.startswith(("stale occurrence count:", "stale occurrence:")) for item in stale), "stale"),
        (any(item.startswith("ambiguous ledger entry:") for item in duplicate), "duplicate"),
        (original_count == relocated_count, "relocation count"),
    )
    for passed, label in controls:
        if not passed:
            print(f"error: scanner-backed {label} control failed", file=sys.stderr)
            return 1
    relocation = next((item for item in wrong_identity if item.startswith("wrong symbol:")), None)
    if relocation is None or masking_failures:
        print(f"error: relocation/masking controls failed: {relocation}, {masking_failures}", file=sys.stderr)
        return 1
    print(f"same-file relocation rejected with unchanged path/token total: {relocation}")
    print("masking controls passed: line comment, block comment, ordinary string, character literal, raw string")
    print("negative controls rejected through discovery, scanner, and validation")
    return 0


def _parse_status(path: Path) -> tuple[StatusTuple, ...]:
    result: list[StatusTuple] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _STATUS.fullmatch(line)
        if match is None:
            raise RuntimeError(f"malformed status snapshot line: {line!r}")
        code, paths = match.group("code"), match.group("paths")
        split = paths.split(" -> ")
        if len(split) == 2 and code[0] in {"R", "C"}:
            result.append(StatusTuple(code, split[1], split[0]))
        elif len(split) == 1 and " -> " not in paths:
            result.append(StatusTuple(code, paths, None))
        else:
            raise RuntimeError(f"malformed rename status line: {line!r}")
    return tuple(result)


def _check_scope(baseline: Path, current: Path) -> int:
    try:
        before, after = _parse_status(baseline), _parse_status(current)
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    before_paths = {item.path for item in before} | {item.original_path for item in before if item.original_path}
    introduced = sorted(item.path for item in after if item.path not in before_paths)
    forbidden = [path for path in introduced if path not in _ALLOW and not path.startswith(_EVIDENCE)]
    for path in forbidden:
        print(f"scope violation: {path}", file=sys.stderr)
    if any(item.path in _ALLOW for item in before):
        print("scope limitation: status snapshots cannot attribute added hunks in baseline-dirty allowed paths", file=sys.stderr)
    print(f"scope check: {len(introduced)} introduced paths, {len(forbidden)} violations")
    return int(bool(forbidden))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test-unclassified", action="store_true")
    mode.add_argument("--check-scope", action="store_true")
    parser.add_argument("--baseline-status", type=Path)
    parser.add_argument("--current-status", type=Path)
    args = parser.parse_args()
    if args.check:
        return _run_check()
    if args.self_test_unclassified:
        return _self_test_unclassified()
    if args.baseline_status is None or args.current_status is None:
        parser.error("--check-scope requires --baseline-status and --current-status")
    return _check_scope(args.baseline_status, args.current_status)


if __name__ == "__main__":
    raise SystemExit(main())
