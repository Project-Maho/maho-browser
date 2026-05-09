#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Replacement:
    old: str
    new: str
    description: str


REPLACEMENTS: dict[str, list[Replacement]] = {
    "chrome/browser/ui/views/side_panel/side_panel_helper.cc": [
        Replacement(
            old=(
                "  if (webui_browser::IsWebUIBrowserEnabled()) {\n"
                "    // TODO(webium): Consider supporting additional side panels beyond reading\n"
                "    // list and bookmarks.\n"
                "    return;\n"
                "  }\n"
            ),
            new=(
                "  if (webui_browser::IsWebUIBrowserEnabled()) {\n"
                "    // TODO(webium): Consider supporting additional side panels beyond reading\n"
                "    // list and bookmarks.\n"
                "    maho::RegisterMahoAiSidePanel(browser, window_registry);\n"
                "    return;\n"
                "  }\n"
            ),
            description="Register Maho AI side panel in Webium path",
        ),
    ],
    "chrome/browser/ui/webui_browser/webui_browser_ui.cc": [
        Replacement(
            old=(
                "    case SidePanelEntryId::kBookmarks:\n"
                "      return l10n_util::GetStringUTF8(IDS_BOOKMARK_MANAGER_TITLE);\n"
                "    default:\n"
            ),
            new=(
                "    case SidePanelEntryId::kBookmarks:\n"
                "      return l10n_util::GetStringUTF8(IDS_BOOKMARK_MANAGER_TITLE);\n"
                "    case SidePanelEntryId::kMahoAiPanel:\n"
                "      return \"Maho AI\";\n"
                "    default:\n"
            ),
            description="Add Webium side-panel title for Maho AI",
        ),
    ],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_chromium_src() -> Path:
    return repo_root().parent / "chromium" / "src"


def apply_replacements(target_file: Path, replacements: list[Replacement]) -> tuple[bool, list[str]]:
    text = target_file.read_text()
    changed = False
    applied: list[str] = []

    for replacement in replacements:
        if replacement.new in text:
            applied.append(f"already: {replacement.description}")
            continue

        if replacement.old not in text:
            raise RuntimeError(
                f"Missing expected anchor for {replacement.description} in {target_file}"
            )

        text = text.replace(replacement.old, replacement.new, 1)
        changed = True
        applied.append(f"patched: {replacement.description}")

    if changed:
        target_file.write_text(text)

    return changed, applied


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply tracked Maho chromium_src overrides into the local Chromium checkout."
    )
    parser.add_argument(
        "--chromium-src",
        type=Path,
        default=default_chromium_src(),
        help="Path to chromium/src checkout (default: workspace chromium/src)",
    )
    args = parser.parse_args()

    chromium_src = args.chromium_src.resolve()
    if not chromium_src.exists():
        print(f"chromium/src not found: {chromium_src}", file=sys.stderr)
        return 1

    any_changed = False
    for relative_path, replacements in REPLACEMENTS.items():
        target_file = chromium_src / relative_path
        if not target_file.exists():
            print(f"target file not found: {target_file}", file=sys.stderr)
            return 1

        changed, applied = apply_replacements(target_file, replacements)
        any_changed = any_changed or changed
        print(relative_path)
        for line in applied:
            print(f"  - {line}")

    print("result: changed" if any_changed else "result: no-op")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
