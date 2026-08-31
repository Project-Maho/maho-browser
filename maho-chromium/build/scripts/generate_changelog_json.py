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


def generate_notes_html(release: Dict[str, Any]) -> str:
    """Renders a standalone HTML page with inline CSS for a single release."""
    version = html.escape(str(release.get("version", "Unknown")))
    date = html.escape(str(release.get("date", "Unknown")))
    channel = html.escape(str(release.get("channel", "stable")))
    highlights: List[str] = release.get("highlights", [])
    sections: List[Dict[str, Any]] = release.get("sections", [])
    body_markdown: str = release.get("body", "")

    # Render highlights
    highlights_html = ""
    if highlights:
        items_html = "".join(f"      <li>{_render_inline_markdown(str(h))}</li>\n" for h in highlights)
        highlights_html = (
            '    <section class="highlights-section">\n'
            '      <h2>Key Highlights</h2>\n'
            f'      <ul class="highlights-list">\n{items_html}      </ul>\n'
            '    </section>\n'
        )

    # Render sections (added, fixed, changed, removed)
    sections_html_parts: List[str] = []
    for sec in sections:
        sec_type = html.escape(str(sec.get("type", "changed")).lower())
        sec_title = html.escape(str(sec.get("title", "Changes")))
        sec_items: List[str] = sec.get("items", [])
        items_str = "".join(f"        <li>{_render_inline_markdown(str(item))}</li>\n" for item in sec_items)
        sections_html_parts.append(
            f'      <div class="change-group type-{sec_type}">\n'
            f'        <div class="change-header">\n'
            f'          <span class="badge badge-{sec_type}">{sec_type.upper()}</span>\n'
            f'          <h3>{sec_title}</h3>\n'
            f'        </div>\n'
            f'        <ul>\n{items_str}        </ul>\n'
            f'      </div>\n'
        )

    sections_html = ""
    if sections_html_parts:
        sections_html = (
            '    <section class="changelog-sections">\n'
            '      <h2>Changelog</h2>\n'
            + "".join(sections_html_parts)
            + '    </section>\n'
        )

    # Render markdown body
    body_html = ""
    if body_markdown.strip():
        rendered_body = render_markdown_to_html(body_markdown)
        body_html = (
            '    <section class="release-body">\n'
            f'{rendered_body}\n'
            '    </section>\n'
        )

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Maho {version} Release Notes</title>
  <style>
    :root {{
      --bg: #0d1117;
      --card-bg: #161b22;
      --border: #30363d;
      --text: #c9d1d9;
      --heading: #f0f6fc;
      --accent: #58a6ff;
      --added: #2ea043;
      --added-bg: rgba(46, 160, 67, 0.15);
      --fixed: #388bfd;
      --fixed-bg: rgba(56, 139, 253, 0.15);
      --changed: #d29922;
      --changed-bg: rgba(210, 153, 34, 0.15);
      --removed: #f85149;
      --removed-bg: rgba(248, 81, 73, 0.15);
    }}
    * {{
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }}
    body {{
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      line-height: 1.6;
      padding: 2.5rem 1.5rem;
    }}
    .container {{
      max-width: 860px;
      margin: 0 auto;
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 2.5rem;
      box-shadow: 0 4px 20px rgba(0,0,0,0.3);
    }}
    header.release-header {{
      border-bottom: 1px solid var(--border);
      padding-bottom: 1.5rem;
      margin-bottom: 2rem;
    }}
    .release-title-row {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
      gap: 1rem;
      margin-bottom: 0.5rem;
    }}
    h1 {{
      font-size: 2rem;
      color: var(--heading);
      font-weight: 700;
    }}
    h2 {{
      font-size: 1.35rem;
      color: var(--heading);
      margin: 1.5rem 0 1rem 0;
      font-weight: 600;
    }}
    h3 {{
      font-size: 1.1rem;
      color: var(--heading);
      font-weight: 600;
    }}
    p {{
      margin-bottom: 1rem;
    }}
    .meta-bar {{
      display: flex;
      gap: 1.5rem;
      font-size: 0.9rem;
      color: #8b949e;
      align-items: center;
    }}
    .badge {{
      display: inline-block;
      padding: 0.2rem 0.55rem;
      border-radius: 4px;
      font-size: 0.75rem;
      font-weight: 700;
      letter-spacing: 0.05em;
      text-transform: uppercase;
    }}
    .badge-channel {{
      background: rgba(88, 166, 255, 0.15);
      color: var(--accent);
      border: 1px solid var(--accent);
    }}
    .badge-added {{ background: var(--added-bg); color: var(--added); }}
    .badge-fixed {{ background: var(--fixed-bg); color: var(--fixed); }}
    .badge-changed {{ background: var(--changed-bg); color: var(--changed); }}
    .badge-removed {{ background: var(--removed-bg); color: var(--removed); }}
    .highlights-section {{
      background: rgba(88, 166, 255, 0.05);
      border-left: 4px solid var(--accent);
      padding: 1.25rem 1.5rem;
      border-radius: 0 6px 6px 0;
      margin-bottom: 2rem;
    }}
    .highlights-section h2 {{
      margin-top: 0;
      font-size: 1.15rem;
      color: var(--accent);
    }}
    ul {{
      padding-left: 1.5rem;
      margin-bottom: 1rem;
    }}
    li {{
      margin-bottom: 0.4rem;
    }}
    .change-group {{
      margin-bottom: 1.5rem;
      padding: 1rem 1.25rem;
      background: rgba(255, 255, 255, 0.02);
      border: 1px solid var(--border);
      border-radius: 6px;
    }}
    .change-header {{
      display: flex;
      align-items: center;
      gap: 0.75rem;
      margin-bottom: 0.75rem;
    }}
    .release-body {{
      margin-top: 2rem;
      padding-top: 1.5rem;
      border-top: 1px solid var(--border);
    }}
    code {{
      font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
      font-size: 0.875em;
      background: rgba(110, 118, 129, 0.2);
      padding: 0.15em 0.35em;
      border-radius: 4px;
    }}
    pre {{
      background: #0d1117;
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 1rem;
      overflow-x: auto;
      margin-bottom: 1rem;
    }}
    pre code {{
      background: transparent;
      padding: 0;
    }}
    a {{
      color: var(--accent);
      text-decoration: none;
    }}
    a:hover {{
      text-decoration: underline;
    }}
  </style>
</head>
<body>
  <div class="container">
    <header class="release-header">
      <div class="release-title-row">
        <h1>Maho Browser {version}</h1>
        <span class="badge badge-channel">{channel}</span>
      </div>
      <div class="meta-bar">
        <span><strong>Released:</strong> {date}</span>
      </div>
    </header>

{highlights_html}{sections_html}{body_html}
  </div>
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
        self.assertIn("Key Highlights", html_out)
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
