#!/usr/bin/env python3
"""Generate changelog JSON and standalone HTML release notes from changelog Markdown.

Standard library only, no network dependencies.
"""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import re
import sys
import tempfile
from typing import Any, Dict, List, Optional, Tuple
import unittest


# ==============================================================================
# YAML Frontmatter Parser (Standard Library Only)
# ==============================================================================

def _strip_yaml_comment(line: str) -> str:
    """Strips comments from a YAML line while respecting quoted strings."""
    in_single = False
    in_double = False
    escaped = False
    for i, ch in enumerate(line):
        if escaped:
            escaped = False
            continue
        if ch == "\\" and in_double:
            escaped = True
            continue
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        elif ch == "#" and not in_single and not in_double:
            return line[:i]
    return line


def _parse_yaml_scalar(val: str) -> Any:
    """Parses a scalar YAML string into appropriate Python type."""
    val = val.strip()
    if not val:
        return ""
    if (val.startswith('"') and val.endswith('"')) or (val.startswith("'") and val.endswith("'")):
        quote = val[0]
        inner = val[1:-1]
        if quote == '"':
            # Decode standard escape sequences
            try:
                inner = inner.encode("utf-8").decode("unicode_escape")
            except Exception:
                inner = inner.replace('\\"', '"').replace('\\\\', '\\')
        else:
            inner = inner.replace("''", "'")
        return inner
    if val.lower() == "true":
        return True
    if val.lower() == "false":
        return False
    if val.lower() in ("null", "~"):
        return None
    if re.match(r"^-?\d+$", val):
        return int(val)
    if re.match(r"^-?\d+\.\d+$", val):
        return float(val)
    return val


def _parse_yaml_block(lines: List[Tuple[int, str]], base_indent: int = 0) -> Any:
    """Recursive helper to parse indentation-based YAML blocks."""
    if not lines:
        return {}

    first_indent, first_content = lines[0]
    if first_content.startswith("- "):
        items: List[Any] = []
        i = 0
        while i < len(lines):
            indent, content = lines[i]
            if indent < base_indent:
                break
            if indent == base_indent and content.startswith("- "):
                val_part = content[2:].strip()
                sub_lines: List[Tuple[int, str]] = []
                j = i + 1
                while j < len(lines) and lines[j][0] > indent:
                    sub_lines.append(lines[j])
                    j += 1

                if ":" in val_part and not (val_part.startswith('"') or val_part.startswith("'")):
                    sub_indent = sub_lines[0][0] if sub_lines else indent + 2
                    dict_lines = [(sub_indent, val_part)] + sub_lines
                    items.append(_parse_yaml_block(dict_lines, sub_indent))
                elif sub_lines:
                    if val_part:
                        items.append(_parse_yaml_scalar(val_part))
                    else:
                        items.append(_parse_yaml_block(sub_lines, sub_lines[0][0]))
                else:
                    items.append(_parse_yaml_scalar(val_part))
                i = j
            else:
                break
        return items
    else:
        res: Dict[str, Any] = {}
        i = 0
        while i < len(lines):
            indent, content = lines[i]
            if indent < base_indent:
                break
            if indent == base_indent:
                if ":" in content:
                    colon_idx = content.index(":")
                    key = content[:colon_idx].strip()
                    val_part = content[colon_idx + 1:].strip()

                    sub_lines = []
                    j = i + 1
                    while j < len(lines) and lines[j][0] > indent:
                        sub_lines.append(lines[j])
                        j += 1

                    cleaned_key = str(_parse_yaml_scalar(key))
                    if sub_lines:
                        if val_part:
                            res[cleaned_key] = _parse_yaml_scalar(val_part)
                        else:
                            res[cleaned_key] = _parse_yaml_block(sub_lines, sub_lines[0][0])
                    else:
                        res[cleaned_key] = _parse_yaml_scalar(val_part)
                    i = j
                else:
                    i += 1
            else:
                i += 1
        return res


def parse_yaml(text: str) -> Dict[str, Any]:
    """Parses a YAML string into a Python dictionary."""
    raw_lines = text.splitlines()
    indexed_lines: List[Tuple[int, str]] = []
    for line in raw_lines:
        line_no_comment = _strip_yaml_comment(line)
        stripped = line_no_comment.strip()
        if not stripped:
            continue
        indent = len(line_no_comment) - len(line_no_comment.lstrip(" "))
        indexed_lines.append((indent, stripped))

    if not indexed_lines:
        return {}
    parsed = _parse_yaml_block(indexed_lines, indexed_lines[0][0])
    return parsed if isinstance(parsed, dict) else {}


def parse_release_file(file_path: Path | str) -> Tuple[Dict[str, Any], str]:
    """Parses a markdown release file containing YAML frontmatter and body.

    Returns:
        (frontmatter_dict, body_markdown)
    """
    path = Path(file_path)
    content = path.read_text(encoding="utf-8")
    stripped = content.strip()

    if not stripped.startswith("---"):
        raise ValueError(f"{path}: File does not start with frontmatter delimiter (---)")

    rest = stripped[3:]
    if rest.startswith("\n"):
        rest = rest[1:]
    elif rest.startswith("\r\n"):
        rest = rest[2:]

    # Find closing ---
    match = re.search(r"^---\s*$", rest, flags=re.MULTILINE)
    if not match:
        raise ValueError(f"{path}: Closing frontmatter delimiter (---) not found")

    yaml_text = rest[:match.start()]
    body_text = rest[match.end():].strip()

    frontmatter = parse_yaml(yaml_text)
    return frontmatter, body_text


# ==============================================================================
# Semantic Versioning & Release Collection
# ==============================================================================

def parse_semver(v: str) -> Tuple[Tuple[int, ...], int, List[Tuple[int, Any]]]:
    """Parses a semantic version string for descending version sorting."""
    v = v.lstrip("v").strip()
    if "+" in v:
        v, _ = v.split("+", 1)

    prerelease = ""
    if "-" in v:
        v, prerelease = v.split("-", 1)

    core_parts: List[int] = []
    for part in v.split("."):
        try:
            core_parts.append(int(part))
        except ValueError:
            core_parts.append(0)

    while len(core_parts) < 3:
        core_parts.append(0)

    core_tuple = tuple(core_parts)
    if not prerelease:
        return (core_tuple, 1, [])

    pre_parts: List[Tuple[int, Any]] = []
    for p in prerelease.split("."):
        if p.isdigit():
            pre_parts.append((0, int(p)))
        else:
            pre_parts.append((1, p))

    return (core_tuple, 0, pre_parts)


def find_changelog_dir(custom_dir: Optional[str] = None) -> Path:
    """Locates the changelog releases directory."""
    if custom_dir:
        p = Path(custom_dir)
        if p.exists() and p.is_dir():
            return p
        raise FileNotFoundError(f"Changelog directory not found: {custom_dir}")

    # 1. Check relative to current working directory
    cwd_candidate = Path.cwd() / "changelog" / "releases"
    if cwd_candidate.is_dir():
        return cwd_candidate

    # 2. Check relative to script directory
    script_root = Path(__file__).resolve().parent.parent.parent.parent
    repo_candidate = script_root / "changelog" / "releases"
    if repo_candidate.is_dir():
        return repo_candidate

    # Fallback to changelog/releases in cwd
    return cwd_candidate


def collect_releases(
    changelog_dir: Path | str,
    version_filter: Optional[str] = None,
) -> Tuple[List[Dict[str, Any]], List[Path]]:
    """Reads and parses all release markdown files from changelog_dir.

    Returns:
        (sorted_releases, list_of_read_file_paths)
    """
    directory = Path(changelog_dir)
    if not directory.exists() or not directory.is_dir():
        return [], []

    md_files = sorted(directory.glob("*.md"))
    releases: List[Dict[str, Any]] = []
    read_paths: List[Path] = []

    normalized_filter = version_filter.lstrip("v").strip() if version_filter else None

    for f in md_files:
        if f.name.lower() == "readme.md":
            continue
        try:
            frontmatter, body = parse_release_file(f)
        except Exception:
            continue

        if not isinstance(frontmatter, dict) or "version" not in frontmatter:
            continue

        read_paths.append(f)
        release_entry = dict(frontmatter)
        release_entry["body"] = body
        releases.append(release_entry)

    # Sort version-descending
    releases.sort(
        key=lambda r: parse_semver(str(r.get("version", "0.0.0"))),
        reverse=True,
    )

    if normalized_filter:
        releases = [
            r for r in releases
            if str(r.get("version", "")).lstrip("v").strip() == normalized_filter
        ]

    return releases, read_paths


# ==============================================================================
# Markdown to Standalone HTML Renderer
# ==============================================================================

def _render_inline_markdown(text: str) -> str:
    """Escapes HTML and converts inline markdown syntax to HTML."""
    escaped = html.escape(text)

    # Inline code
    escaped = re.sub(r"`([^`]+)`", r"<code>\1</code>", escaped)
    # Bold **text** or __text__
    escaped = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", escaped)
    escaped = re.sub(r"__([^_]+)__", r"<strong>\1</strong>", escaped)
    # Italic *text* or _text_
    escaped = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", escaped)
    escaped = re.sub(r"(?<!_)_([^_]+)_(?!_)", r"<em>\1</em>", escaped)

    # Links [text](url) - safe URLs only
    def _replace_link(match: re.Match) -> str:
        label = match.group(1)
        url = match.group(2).strip()
        if re.match(r"^(https?://|mailto:|#|/)", url):
            return f'<a href="{url}" target="_blank" rel="noopener noreferrer">{label}</a>'
        return f'<a href="#">{label}</a>'

    escaped = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", _replace_link, escaped)
    return escaped


def render_markdown_to_html(md_text: str) -> str:
    """Converts a subset of markdown into sanitized HTML elements."""
    lines = md_text.splitlines()
    html_parts: List[str] = []
    in_code_block = False
    code_lines: List[str] = []
    in_list = False
    list_items: List[str] = []
    paragraph_lines: List[str] = []

    def flush_list():
        nonlocal in_list, list_items
        if in_list and list_items:
            html_parts.append("<ul>\n" + "".join(f"  <li>{item}</li>\n" for item in list_items) + "</ul>")
            list_items = []
            in_list = False

    def flush_paragraph():
        nonlocal paragraph_lines
        if paragraph_lines:
            combined = " ".join(paragraph_lines).strip()
            if combined:
                html_parts.append(f"<p>{_render_inline_markdown(combined)}</p>")
            paragraph_lines = []

    for line in lines:
        stripped = line.strip()

        # Code block fences
        if stripped.startswith("```"):
            if in_code_block:
                flush_paragraph()
                code_content = html.escape("\n".join(code_lines))
                html_parts.append(f"<pre><code>{code_content}</code></pre>")
                code_lines = []
                in_code_block = False
            else:
                flush_paragraph()
                flush_list()
                in_code_block = True
                code_lines = []
            continue

        if in_code_block:
            code_lines.append(line)
            continue

        if not stripped:
            flush_paragraph()
            flush_list()
            continue

        # Headers
        header_match = re.match(r"^(#{1,6})\s+(.*)$", stripped)
        if header_match:
            flush_paragraph()
            flush_list()
            level = len(header_match.group(1))
            header_text = _render_inline_markdown(header_match.group(2).strip())
            html_parts.append(f"<h{level}>{header_text}</h{level}>")
            continue

        # List items
        list_match = re.match(r"^[-*+]\s+(.*)$", stripped)
        if list_match:
            flush_paragraph()
            in_list = True
            list_items.append(_render_inline_markdown(list_match.group(1).strip()))
            continue

        # Regular paragraph line
        if in_list:
            flush_list()
        paragraph_lines.append(stripped)

    if in_code_block and code_lines:
        code_content = html.escape("\n".join(code_lines))
        html_parts.append(f"<pre><code>{code_content}</code></pre>")
    flush_paragraph()
    flush_list()

    return "\n".join(html_parts)


# Brand tokens for the standalone release-notes sheet, taken from
# docs/brand/DESIGN.md (near-black canvas, a single violet accent, hairline
# dark cards, squircle radii) so the in-app and web release notes read as Maho.
# The families are system stacks: this file ships as a release asset and must
# render with no network or font dependency.
_NOTES_STYLESHEET = """
    :root {
      color-scheme: dark;
      --canvas: #0a0a0a;
      --sheet: #101012;
      --card: #151517;
      --ink: #fafafa;
      --ink-muted: #a1a1aa;
      --ink-faint: #71717a;
      --hairline: #27272a;
      --accent: #8b7ff5;
      --accent-ink: #8b7ff5;
      --accent-soft: rgba(139, 127, 245, 0.12);
      --wash: rgba(250, 250, 250, 0.04);
      --wash-strong: rgba(250, 250, 250, 0.07);
      --dot: rgba(250, 250, 250, 0.22);
      --radius-sm: 8px;
      --radius-md: 11px;
      --radius-lg: 17px;
      --font-sans: ui-sans-serif, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      --font-display: ui-serif, "New York", "Iowan Old Style", Palatino, Georgia, serif;
      --font-mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
    }
    @media (prefers-color-scheme: light) {
      :root {
        color-scheme: light;
        --canvas: #fafaf9;
        --sheet: #ffffff;
        --card: #f7f7f6;
        --ink: #18181b;
        --ink-muted: #52525b;
        --ink-faint: #8a8a93;
        --hairline: #e4e4e7;
        /* Same violet, one ramp step darker so 10px mono labels stay legible. */
        --accent-ink: #6355d8;
        --accent-soft: rgba(139, 127, 245, 0.10);
        --wash: rgba(24, 24, 27, 0.03);
        --wash-strong: rgba(24, 24, 27, 0.06);
        --dot: rgba(24, 24, 27, 0.24);
      }
    }
    * {
      box-sizing: border-box;
    }
    html {
      -webkit-text-size-adjust: 100%;
    }
    body {
      margin: 0;
      padding: 32px 20px 48px;
      background-color: var(--canvas);
      background-image: radial-gradient(760px 340px at 10% -8%, var(--accent-soft), transparent 64%);
      background-repeat: no-repeat;
      color: var(--ink);
      font-family: var(--font-sans);
      font-size: 15px;
      line-height: 1.6;
      -webkit-font-smoothing: antialiased;
    }
    .sheet {
      max-width: 720px;
      margin: 0 auto;
      padding: 28px 26px 30px;
      background-color: var(--sheet);
      border: 1px solid var(--hairline);
      border-radius: var(--radius-lg);
      box-shadow: inset 0 1px 0 var(--wash-strong), 0 24px 60px -34px rgba(0, 0, 0, 0.95);
    }
    .eyebrow-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 14px;
    }
    .eyebrow {
      font-family: var(--font-mono);
      font-size: 10.5px;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--accent-ink);
    }
    .channel {
      padding: 3px 10px;
      border: 1px solid var(--hairline);
      border-radius: 999px;
      font-family: var(--font-mono);
      font-size: 10px;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--ink-muted);
      white-space: nowrap;
    }
    .release-version {
      margin: 0 0 10px;
      font-family: var(--font-display);
      font-weight: 400;
      font-size: clamp(2.15rem, 7.4vw, 2.9rem);
      line-height: 1.02;
      letter-spacing: -0.02em;
      text-wrap: balance;
    }
    .release-date {
      display: flex;
      align-items: baseline;
      gap: 8px;
      margin: 0 0 20px;
      font-family: var(--font-mono);
      font-size: 11px;
      letter-spacing: 0.12em;
      text-transform: uppercase;
      color: var(--ink-faint);
      font-variant-numeric: tabular-nums;
    }
    .release-lede {
      margin: 0 0 20px;
      max-width: 62ch;
      font-size: 15.5px;
      line-height: 1.68;
      color: var(--ink-muted);
      text-wrap: pretty;
    }
    .release-lede p {
      margin: 0 0 10px;
    }
    .release-lede p:last-child {
      margin-bottom: 0;
    }
    ul.glance {
      display: flex;
      flex-wrap: wrap;
      gap: 1px;
      margin: 0 0 26px;
      padding: 0;
      list-style: none;
      background-color: var(--hairline);
      border: 1px solid var(--hairline);
      border-radius: var(--radius-md);
      overflow: hidden;
    }
    .tally {
      flex: 1 1 0;
      min-width: 106px;
      display: flex;
      align-items: baseline;
      gap: 7px;
      padding: 12px 14px;
      background-color: var(--card);
    }
    .tally-glyph {
      font-family: var(--font-mono);
      font-size: 12px;
      color: var(--ink-faint);
    }
    .tally-count {
      font-family: var(--font-mono);
      font-size: 17px;
      font-variant-numeric: tabular-nums;
      color: var(--ink);
    }
    .tally-label {
      font-family: var(--font-mono);
      font-size: 10px;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--ink-faint);
    }
    .section-label {
      margin: 0 0 12px;
      font-family: var(--font-mono);
      font-size: 10px;
      font-weight: 400;
      letter-spacing: 0.18em;
      text-transform: uppercase;
      color: var(--ink-faint);
    }
    .highlights {
      margin: 0 0 26px;
      padding: 18px 20px 16px;
      background-color: var(--card);
      border: 1px solid var(--hairline);
      border-radius: var(--radius-md);
    }
    .highlights ol {
      margin: 0;
      padding: 0;
      list-style: none;
      counter-reset: highlight;
    }
    .highlights li {
      position: relative;
      padding-left: 30px;
      margin-bottom: 10px;
      font-size: 14.5px;
      line-height: 1.55;
      color: var(--ink);
    }
    .highlights li:last-child {
      margin-bottom: 0;
    }
    .highlights li::before {
      counter-increment: highlight;
      content: counter(highlight, decimal-leading-zero);
      position: absolute;
      left: 0;
      top: 2px;
      font-family: var(--font-mono);
      font-size: 10.5px;
      letter-spacing: 0.08em;
      color: var(--ink-faint);
    }
    .changes {
      display: grid;
      grid-template-columns: minmax(0, 1fr);
      gap: 10px;
      align-items: start;
    }
    @media (min-width: 760px) {
      .changes {
        grid-template-columns: repeat(2, minmax(0, 1fr));
      }
    }
    .change-group {
      padding: 15px 17px 14px;
      background-color: var(--card);
      border: 1px solid var(--hairline);
      border-radius: var(--radius-md);
    }
    .change-header {
      display: flex;
      align-items: center;
      gap: 9px;
      margin-bottom: 11px;
    }
    .change-type {
      padding: 3px 7px;
      background-color: var(--wash-strong);
      border-radius: var(--radius-sm);
      font-family: var(--font-mono);
      font-size: 10px;
      letter-spacing: 0.14em;
      text-transform: uppercase;
      color: var(--ink-muted);
      white-space: nowrap;
    }
    .change-glyph {
      margin-right: 5px;
      color: var(--ink-faint);
    }
    .change-title {
      margin: 0;
      font-size: 14.5px;
      font-weight: 600;
      letter-spacing: -0.005em;
      color: var(--ink);
    }
    .change-count {
      margin-left: auto;
      font-family: var(--font-mono);
      font-size: 11px;
      font-variant-numeric: tabular-nums;
      color: var(--ink-faint);
    }
    .change-group ul {
      margin: 0;
      padding: 0;
      list-style: none;
    }
    .change-group li {
      position: relative;
      padding-left: 13px;
      margin-bottom: 7px;
      font-size: 13.5px;
      line-height: 1.55;
      color: var(--ink-muted);
    }
    .change-group li:last-child {
      margin-bottom: 0;
    }
    .change-group li::before {
      content: "";
      position: absolute;
      left: 0;
      top: 8px;
      width: 4px;
      height: 4px;
      border-radius: 50%;
      background-color: var(--dot);
    }
    .full-notes {
      margin-top: 26px;
      padding-top: 22px;
      border-top: 1px solid var(--hairline);
      color: var(--ink-muted);
    }
    .full-notes h1,
    .full-notes h2,
    .full-notes h3,
    .full-notes h4 {
      margin: 20px 0 8px;
      font-family: var(--font-display);
      font-weight: 400;
      letter-spacing: -0.01em;
      color: var(--ink);
    }
    .full-notes h1 {
      font-size: 1.6rem;
    }
    .full-notes h2 {
      font-size: 1.3rem;
    }
    .full-notes h3 {
      font-size: 1.1rem;
    }
    .full-notes p {
      max-width: 66ch;
      margin: 0 0 12px;
    }
    .full-notes ul {
      margin: 0 0 12px;
      padding-left: 20px;
    }
    .full-notes li {
      margin-bottom: 5px;
    }
    code {
      padding: 0.15em 0.36em;
      background-color: var(--wash-strong);
      border-radius: 5px;
      font-family: var(--font-mono);
      font-size: 0.88em;
      color: var(--ink);
    }
    pre {
      margin: 0 0 14px;
      padding: 14px 16px;
      background-color: var(--canvas);
      border: 1px solid var(--hairline);
      border-radius: var(--radius-md);
      overflow-x: auto;
    }
    pre code {
      padding: 0;
      background-color: transparent;
      font-size: 12.5px;
    }
    a {
      color: var(--accent-ink);
      text-decoration: none;
      text-underline-offset: 3px;
    }
    a:hover {
      text-decoration: underline;
    }
    a:focus-visible {
      outline: 2px solid var(--accent-ink);
      outline-offset: 2px;
      border-radius: 3px;
    }
    @media (max-width: 420px) {
      body {
        padding: 20px 12px 32px;
      }
      .sheet {
        padding: 22px 18px 24px;
      }
    }
    @media print {
      body {
        padding: 0;
        background-image: none;
      }
      .sheet {
        max-width: none;
        border: 0;
        box-shadow: none;
      }
      .change-group,
      .highlights {
        break-inside: avoid;
      }
    }
"""

def generate_notes_html(release: Dict[str, Any]) -> str:
    """Renders a standalone, self-contained HTML release-notes sheet.

    The sheet is rendered in two places: inline in the macOS Sparkle update
    dialog (a narrow WebView, roughly 560px wide) and standalone in a browser.
    It is therefore a single compact column that only widens to a two-column
    change grid when there is room. Everything is inline: the file ships as a
    release asset with no network or font dependency.
    """
    version = html.escape(str(release.get("version", "Unknown")))
    date = html.escape(str(release.get("date", "Unknown")))
    channel = html.escape(str(release.get("channel", "stable")))
    highlights: List[str] = release.get("highlights", [])
    sections: List[Dict[str, Any]] = release.get("sections", [])
    body_markdown: str = release.get("body", "")

    # One glance row: the shape of a release (how much was added, fixed, ...)
    # is legible before any prose is read. Glyphs carry the change type so the
    # palette stays a single neutral ink ramp plus one violet accent.
    type_glyphs = {
        "added": "\u002b",
        "fixed": "\u2713",
        "changed": "\u223c",
        "removed": "\u2212",
    }
    tallies: List[Tuple[str, int]] = []
    for key in ("added", "fixed", "changed", "removed"):
        count = sum(
            len(sec.get("items", []))
            for sec in sections
            if str(sec.get("type", "")).lower() == key
        )
        if count:
            tallies.append((key, count))
    total_changes = sum(count for _, count in tallies)

    glance_html = ""
    if tallies:
        cells_html = "".join(
            '        <li class="tally">\n'
            '          <span class="tally-glyph" aria-hidden="true">%s</span>\n'
            '          <span class="tally-count">%d</span>\n'
            '          <span class="tally-label">%s</span>\n'
            '        </li>\n' % (type_glyphs[key], count, key)
            for key, count in tallies
        )
        glance_html = (
            '      <ul class="glance" aria-label="%d changes in this release">\n'
            '%s'
            '      </ul>\n' % (total_changes, cells_html)
        )

    # Highlights: the short "what changed for me" list, numbered so it can be
    # scanned without reading every line.
    highlights_html = ""
    if highlights:
        items_html = "".join(
            "        <li>%s</li>\n" % _render_inline_markdown(str(h))
            for h in highlights
        )
        highlights_html = (
            '    <section class="highlights">\n'
            '      <h2 class="section-label">In this release</h2>\n'
            '      <ol>\n%s      </ol>\n'
            '    </section>\n' % items_html
        )

    # Changes, grouped by area, each card labelled with its type and size.
    change_parts: List[str] = []
    for sec in sections:
        sec_type = str(sec.get("type", "changed")).lower()
        sec_title = html.escape(str(sec.get("title", "Changes")))
        sec_items: List[str] = sec.get("items", [])
        if not sec_items:
            continue
        glyph = type_glyphs.get(sec_type, "\u2022")
        items_str = "".join(
            "          <li>%s</li>\n" % _render_inline_markdown(str(item))
            for item in sec_items
        )
        change_parts.append(
            '      <article class="change-group">\n'
            '        <div class="change-header">\n'
            '          <span class="change-type"><span class="change-glyph" '
            'aria-hidden="true">%s</span>%s</span>\n'
            '          <h3 class="change-title">%s</h3>\n'
            '          <span class="change-count" aria-label="%d changes">%d</span>\n'
            '        </div>\n'
            '        <ul>\n%s        </ul>\n'
            '      </article>\n'
            % (
                glyph,
                html.escape(sec_type.upper()),
                sec_title,
                len(sec_items),
                len(sec_items),
                items_str,
            )
        )

    changes_html = ""
    if change_parts:
        changes_html = (
            '    <section class="changes-section">\n'
            '      <h2 class="section-label">Changelog</h2>\n'
            '      <div class="changes">\n%s      </div>\n'
            '    </section>\n' % "".join(change_parts)
        )

    # The markdown body is either a short summary, rendered as the lede under
    # the version headline, or a full write-up carrying its own headings, which
    # renders as a "Full notes" section instead. Never both.
    body_rendered = (
        render_markdown_to_html(body_markdown) if body_markdown.strip() else ""
    )
    body_has_headings = bool(re.search(r"^#{1,6}\s", body_markdown, re.MULTILINE))
    lede_html = ""
    full_notes_html = ""
    if body_rendered and body_has_headings:
        full_notes_html = (
            '    <section class="full-notes">\n'
            '      <h2 class="section-label">Full notes</h2>\n'
            '%s\n'
            '    </section>\n' % body_rendered
        )
    elif body_rendered:
        lede_html = (
            '      <div class="release-lede">\n%s\n      </div>\n' % body_rendered
        )

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Maho {version} Release Notes</title>
  <style>{_NOTES_STYLESHEET}</style>
</head>
<body>
  <main class="sheet">
    <header class="sheet-head">
      <div class="eyebrow-row">
        <span class="eyebrow">Maho release notes</span>
        <span class="channel">{channel}</span>
      </div>
      <h1 class="release-version">{version}</h1>
      <p class="release-date">Released <time datetime="{date}">{date}</time></p>
{lede_html}{glance_html}    </header>
{highlights_html}{changes_html}{full_notes_html}  </main>
</body>
</html>
"""


# ==============================================================================
# Depfile Writer
# ==============================================================================

def write_depfile(depfile_path: Path | str, out_target: Path | str, input_files: List[Path]) -> None:
    """Writes a Makefile-style depfile for GN/Ninja incremental build tracking."""
    dep_path = Path(depfile_path)
    dep_path.parent.mkdir(parents=True, exist_ok=True)

    def escape_path(p: Path | str) -> str:
        s = str(p).replace("\\", "/")
        return s.replace(" ", "\\ ")

    target_str = escape_path(out_target)
    deps_str = " ".join(escape_path(p) for p in sorted(input_files))
    content = f"{target_str}: {deps_str}\n"

    dep_path.write_text(content, encoding="utf-8")


# ==============================================================================
# Self-Test Suite
# ==============================================================================

class GenerateChangelogSelfTest(unittest.TestCase):
    """Test suite covering YAML frontmatter parsing, semver sorting, HTML escaping, and depfile."""

    def test_yaml_frontmatter_parser(self):
        sample_yaml = """
version: "1.2.3"
date: "2026-08-31"
channel: "beta"
is_released: true
num_features: 42
highlights:
  - "First highlight with \\"escaped\\" quotes"
  - 'Second highlight with single quotes'
sections:
  - type: "added"
    title: "New Features"
    items:
      - "Feature 1"
      - "Feature 2"
  - type: "fixed"
    title: "Bug Fixes"
    items:
      - "Fix issue with spaces"
"""
        parsed = parse_yaml(sample_yaml)
        self.assertEqual(parsed.get("version"), "1.2.3")
        self.assertEqual(parsed.get("date"), "2026-08-31")
        self.assertEqual(parsed.get("channel"), "beta")
        self.assertTrue(parsed.get("is_released"))
        self.assertEqual(parsed.get("num_features"), 42)
        self.assertEqual(len(parsed.get("highlights", [])), 2)
        self.assertEqual(parsed["highlights"][0], 'First highlight with "escaped" quotes')
        self.assertEqual(parsed["highlights"][1], "Second highlight with single quotes")
        self.assertEqual(len(parsed.get("sections", [])), 2)
        self.assertEqual(parsed["sections"][0]["type"], "added")
        self.assertEqual(parsed["sections"][0]["items"], ["Feature 1", "Feature 2"])

    def test_semver_sorting(self):
        versions = [
            "0.9.0",
            "1.0.0-rc.1",
            "1.0.0",
            "1.0.1",
            "1.2.0",
            "1.10.0",
            "v1.0.0-beta.2",
            "2.0.0",
        ]
        sorted_vers = sorted(versions, key=parse_semver, reverse=True)
        expected = [
            "2.0.0",
            "1.10.0",
            "1.2.0",
            "1.0.1",
            "1.0.0",
            "1.0.0-rc.1",
            "v1.0.0-beta.2",
            "0.9.0",
        ]
        self.assertEqual(sorted_vers, expected)

    def test_html_escaping_and_markdown(self):
        raw_md = (
            "# Header & <script>alert(1)</script>\n\n"
            "This is **bold & safe** and `code <tag>`.\n\n"
            "- Item 1 with <style>\n"
            "- Item 2 with [link](https://example.com/test?a=1&b=2)\n\n"
            "```html\n"
            "<div>Raw block</div>\n"
            "```"
        )
        rendered = render_markdown_to_html(raw_md)
        self.assertNotIn("<script>", rendered)
        self.assertIn("&lt;script&gt;alert(1)&lt;/script&gt;", rendered)
        self.assertIn("<strong>bold &amp; safe</strong>", rendered)
        self.assertIn("<code>code &lt;tag&gt;</code>", rendered)
        self.assertIn("&lt;style&gt;", rendered)
        self.assertIn('<a href="https://example.com/test?a=1&amp;b=2"', rendered)
        self.assertIn("<pre><code>&lt;div&gt;Raw block&lt;/div&gt;</code></pre>", rendered)

    def test_notes_html_generator(self):
        release = {
            "version": "1.0.0",
            "date": "2026-08-12",
            "channel": "stable",
            "highlights": ["Initial release <marquee>"],
            "sections": [
                {
                    "type": "added",
                    "title": "New Features",
                    "items": ["Support <b>tags</b>"],
                }
            ],
            "body": "## Narrative\nDetailed text here.",
        }
        html_out = generate_notes_html(release)
        self.assertIn("<!DOCTYPE html>", html_out)
        self.assertIn("Maho 1.0.0 Release Notes", html_out)
        self.assertIn("&lt;marquee&gt;", html_out)
        self.assertIn("&lt;b&gt;tags&lt;/b&gt;", html_out)
        self.assertIn('class="highlights"', html_out)
        self.assertIn("New Features", html_out)
        self.assertIn("Narrative", html_out)
        self.assertNotIn("<link rel=", html_out)  # Inline CSS only

    def test_depfile_writing(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            out_json = tmp / "dist" / "changelog.json"
            dep_file = tmp / "dist" / "changelog.d"
            file1 = tmp / "changelog" / "v1.0.0.md"
            file2 = tmp / "changelog" / "v1.1.0.md"
            file1.parent.mkdir(parents=True, exist_ok=True)
            file1.write_text("---\nversion: '1.0.0'\n---\n", encoding="utf-8")
            file2.write_text("---\nversion: '1.1.0'\n---\n", encoding="utf-8")

            write_depfile(dep_file, out_json, [file1, file2])
            self.assertTrue(dep_file.exists())
            content = dep_file.read_text(encoding="utf-8")
            self.assertIn(str(out_json).replace("\\", "/"), content)
            self.assertIn(str(file1).replace("\\", "/"), content)
            self.assertIn(str(file2).replace("\\", "/"), content)

    def test_collect_releases_and_filter(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp = Path(tmp_dir)
            (tmp / "v0.9.0.md").write_text(
                '---\nversion: "0.9.0"\ndate: "2026-07-01"\nchannel: "beta"\nhighlights:\n  - "Beta 1"\n---\nBody 0.9',
                encoding="utf-8",
            )
            (tmp / "v1.0.0.md").write_text(
                '---\nversion: "1.0.0"\ndate: "2026-08-12"\nchannel: "stable"\nhighlights:\n  - "GA 1"\n---\nBody 1.0',
                encoding="utf-8",
            )
            (tmp / "README.md").write_text("# Spec\nNot a release.", encoding="utf-8")

            # Collect all sorted
            all_rels, read_files = collect_releases(tmp)
            self.assertEqual(len(all_rels), 2)
            self.assertEqual(all_rels[0]["version"], "1.0.0")
            self.assertEqual(all_rels[1]["version"], "0.9.0")
            self.assertEqual(len(read_files), 2)

            # Filter single version
            filtered_rels, _ = collect_releases(tmp, version_filter="0.9.0")
            self.assertEqual(len(filtered_rels), 1)
            self.assertEqual(filtered_rels[0]["version"], "0.9.0")


def run_self_tests() -> int:
    """Executes unittest suite and returns exit code."""
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(GenerateChangelogSelfTest)
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


# ==============================================================================
# CLI Entry Point
# ==============================================================================

def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate changelog JSON and standalone HTML release notes."
    )
    parser.add_argument(
        "--mode",
        choices=["json", "notes"],
        default="json",
        help="Artifact generation mode (default: json).",
    )
    parser.add_argument(
        "--version",
        dest="version_filter",
        help="Filter releases to specified version (e.g. 1.0.0 or v1.0.0). Required in --mode notes.",
    )
    parser.add_argument(
        "--changelog-dir",
        help="Directory containing release markdown files (default: changelog/releases).",
    )
    parser.add_argument(
        "--out",
        help="Output file path (default: stdout).",
    )
    parser.add_argument(
        "--depfile",
        help="Write Makefile-style depfile for GN/Ninja incremental builds.",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run embedded self-test suite and exit.",
    )

    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_tests()

    try:
        ch_dir = find_changelog_dir(args.changelog_dir)
    except Exception as e:
        sys.stderr.write(f"error: {e}\n")
        return 1

    releases, read_files = collect_releases(ch_dir, version_filter=args.version_filter)

    # In notes mode, --version is required and must match a release
    if args.mode == "notes":
        if not args.version_filter:
            sys.stderr.write("error: --mode notes requires --version <VERSION>\n")
            return 2
        if not releases:
            sys.stderr.write(
                f"error: no release notes found matching version '{args.version_filter}' in {ch_dir}\n"
            )
            return 1
        output_content = generate_notes_html(releases[0])
    else:
        # Default JSON mode: format {releases: [...]}
        payload = {"releases": releases}
        output_content = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"

    # Write output
    if args.out and args.out != "-":
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(output_content, encoding="utf-8")
    else:
        sys.stdout.write(output_content)

    # Write depfile if requested
    if args.depfile:
        depfile_target = args.out if (args.out and args.out != "-") else "changelog_output"
        write_depfile(args.depfile, depfile_target, read_files)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
