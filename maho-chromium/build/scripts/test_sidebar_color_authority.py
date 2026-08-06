#!/usr/bin/env python3
"""Reject process-global surface ColorIds in sidebar-owned production views."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Final, Iterable


_WORKSPACE: Final = Path(__file__).resolve().parents[3]
_DEFAULT_ROOTS: Final = (
    _WORKSPACE / "maho-chromium/browser/ui/views/sidebar",
    _WORKSPACE / "maho-chromium/browser/ui/views/spaces_overlay",
)
_FORBIDDEN: Final = re.compile(
    r"\bkMahoColor(?:"
    r"Sidebar(?:[A-Za-z0-9_]*Text|Selected|Hover|Background)|"
    r"Library[A-Za-z0-9_]*|"
    r"Archive[A-Za-z0-9_]*|"
    r"TabRow[A-Za-z0-9_]*|"
    r"PrimaryText|SecondaryText|TertiaryText|"
    r"InteractiveChip[A-Za-z0-9_]*|"
    r"Card[A-Za-z0-9_]*|"
    r"Divider"
    r")\b"
)
_SOURCE_SUFFIXES: Final = frozenset({".cc", ".h"})
_TEST_SUFFIXES: Final = ("_test.cc", "_unittest.cc", "_test.h", "_unittest.h")
_TESTONLY_MARKERS: Final = ("testonly", "test_only")


@dataclass(frozen=True, slots=True)
class Match:
    path: Path
    line: int
    column: int
    symbol: str


def _is_production_source(path: Path) -> bool:
    name = path.name.lower()
    return (
        path.is_file()
        and path.suffix in _SOURCE_SUFFIXES
        and not name.endswith(_TEST_SUFFIXES)
        and not any(marker in name for marker in _TESTONLY_MARKERS)
    )


def _sources(paths: Iterable[Path]) -> tuple[Path, ...]:
    sources: set[Path] = set()
    for path in paths:
        resolved = path.resolve()
        if resolved.is_dir():
            sources.update(item for item in resolved.rglob("*") if _is_production_source(item))
        elif _is_production_source(resolved):
            sources.add(resolved)
        elif not resolved.exists():
            raise RuntimeError(f"path does not exist: {path}")
    return tuple(sorted(sources))


def _display_path(path: Path) -> str:
    try:
        return str(path.relative_to(_WORKSPACE))
    except ValueError:
        return str(path)


def _scan(paths: Iterable[Path]) -> tuple[Match, ...]:
    matches: list[Match] = []
    for path in _sources(paths):
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            matches.extend(
                Match(path, line_number, match.start() + 1, match.group())
                for match in _FORBIDDEN.finditer(line)
            )
    return tuple(matches)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="files or directories to scan instead of the production sidebar roots",
    )
    args = parser.parse_args()
    try:
        matches = _scan(args.paths or _DEFAULT_ROOTS)
    except (OSError, RuntimeError, UnicodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    for match in matches:
        print(
            f"{_display_path(match.path)}:{match.line}:{match.column}: "
            f"forbidden sidebar surface ColorId {match.symbol}",
            file=sys.stderr,
        )
    return int(bool(matches))


if __name__ == "__main__":
    raise SystemExit(main())
