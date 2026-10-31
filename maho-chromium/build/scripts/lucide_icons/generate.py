#!/usr/bin/env python3
"""Generate Chromium .icon files from upstream Lucide SVGs.

Usage:
  python3 generate.py --write        # Regenerate all .icon files from manifest
  python3 generate.py --check        # Verify .icon files match manifest (CI mode)
  python3 generate.py --version X.Y.Z  # Override manifest version before writing
"""

import argparse
import difflib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
from urllib.error import HTTPError
from urllib.request import urlopen

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_MAHO_CHROMIUM_DIR = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_ICONS_DIR = os.path.join(
    _MAHO_CHROMIUM_DIR, 'browser', 'ui', 'views', 'maho_lucide_icons'
)
_MANIFEST_PATH = os.path.join(_ICONS_DIR, 'manifest.json')

_SVG_NS = '{http://www.w3.org/2000/svg}'
_RAW_URL_TEMPLATE = (
    'https://raw.githubusercontent.com/lucide-icons/lucide/{version}/icons/{name}.svg'
)

_COPYRIGHT_HEADER = '// Copyright 2026 Maho Browser. All rights reserved.'
_SOURCE_COMMENT_TEMPLATE = '// Source: Lucide "{name}" (ISC license)'


# ---------------------------------------------------------------------------
# Numeric formatting
# ---------------------------------------------------------------------------

def format_number(value):
    """Format a number for .icon output: integers without 'f', decimals with 'f'."""
    if value == int(value):
        return str(int(value))
    # Use repr-style float formatting, strip trailing zeros but keep at least one decimal
    s = f'{value:g}'
    if '.' not in s and 'e' not in s.lower():
        return s
    # Ensure no trailing zeros except one after decimal
    return s + 'f'


def _is_integer(value):
    return value == int(value)


# ---------------------------------------------------------------------------
# SVG path tokenizer
# ---------------------------------------------------------------------------

_PATH_COMMANDS = set('MmLlHhVvAaCcSsQqTtZz')

_NUMBER_RE = re.compile(
    r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?'
)


def tokenize_path(d):
    """Tokenize an SVG path 'd' attribute into (command, [args]) tuples.

    Handles:
    - Comma and whitespace separators
    - Sign-based token splitting (1.5-2.9 = [1.5, -2.9])
    - Consecutive decimals (.5.6 = [0.5, 0.6])
    - Implicit repeat semantics
    """
    tokens = []
    i = 0
    while i < len(d):
        c = d[i]
        if c in ' ,\t\n\r':
            i += 1
            continue
        if c in _PATH_COMMANDS:
            tokens.append(c)
            i += 1
            continue
        # Try to match a number
        m = _NUMBER_RE.match(d, i)
        if m:
            tokens.append(float(m.group()))
            i = m.end()
            # Handle consecutive decimals: if next char is '.' and not preceded by
            # a sign or command, it starts a new number
            continue
        # Fallback: skip unknown character
        i += 1

    return tokens


def _arg_count(cmd):
    """Number of args per invocation of an SVG path command."""
    cmd_upper = cmd.upper()
    if cmd_upper == 'Z':
        return 0
    if cmd_upper in ('H', 'V'):
        return 1
    if cmd_upper in ('M', 'L', 'T'):
        return 2
    if cmd_upper == 'Q':
        return 4
    if cmd_upper in ('S', 'C'):
        return 6 if cmd_upper == 'C' else 4
    if cmd_upper == 'A':
        return 7
    return 0


def parse_path_commands(d):
    """Parse SVG path 'd' attribute into structured commands.

    Returns list of (command_char, [args]) tuples with implicit repeats expanded.
    """
    tokens = tokenize_path(d)
    commands = []
    i = 0

    while i < len(tokens):
        token = tokens[i]
        if isinstance(token, str) and token in _PATH_COMMANDS:
            cmd = token
            i += 1
        elif commands:
            # Implicit repeat: after M -> L, after m -> l, others repeat themselves
            prev_cmd = commands[-1][0]
            if prev_cmd == 'M':
                cmd = 'L'
            elif prev_cmd == 'm':
                cmd = 'l'
            else:
                cmd = prev_cmd
        else:
            raise ValueError(f'Path data starts with number without command: {d!r}')

        if cmd.upper() == 'Z':
            commands.append((cmd, []))
            continue

        n_args = _arg_count(cmd)
        if n_args == 0:
            commands.append((cmd, []))
            continue

        # Collect args (may have multiple implicit repetitions)
        while i < len(tokens) and isinstance(tokens[i], (int, float)):
            args = []
            for j in range(n_args):
                if i >= len(tokens) or isinstance(tokens[i], str):
                    break
                args.append(tokens[i])
                i += 1
            if len(args) == n_args:
                commands.append((cmd, args))
                # After M implicit becomes L, after m becomes l
                if cmd == 'M':
                    cmd = 'L'
                elif cmd == 'm':
                    cmd = 'l'
            else:
                raise ValueError(
                    f'Incomplete args for command {cmd}: expected {n_args}, got {len(args)} in {d!r}'
                )

    return commands


# ---------------------------------------------------------------------------
# SVG path -> .icon commands converter
# ---------------------------------------------------------------------------

class IconConverter:
    """Converts parsed SVG elements into Chromium .icon format lines."""

    def __init__(self):
        self.lines = []
        self.cur_x = 0.0
        self.cur_y = 0.0
        self.start_x = 0.0
        self.start_y = 0.0
        # Last control point for smooth curves (S/s)
        self.last_cp2_x = 0.0
        self.last_cp2_y = 0.0
        self.last_cmd = ''

    def _emit(self, cmd, *args):
        parts = [cmd]
        for a in args:
            parts.append(format_number(a))
        self.lines.append(', '.join(parts) + ',')

    def convert_path(self, d):
        """Convert an SVG path 'd' attribute to .icon commands."""
        commands = parse_path_commands(d)
        first_move = True

        for cmd, args in commands:
            cmd_upper = cmd.upper()
            is_relative = cmd.islower() and cmd_upper != 'Z'

            if cmd_upper == 'M':
                if is_relative and not first_move:
                    # Relative moveto - compute absolute
                    x = self.cur_x + args[0]
                    y = self.cur_y + args[1]
                else:
                    # First m is treated as M per SVG spec
                    x = args[0]
                    y = args[1]
                self._emit('MOVE_TO', x, y)
                self.cur_x = x
                self.cur_y = y
                self.start_x = x
                self.start_y = y
                first_move = False
                self.last_cmd = 'M'
                self.last_cp2_x = x
                self.last_cp2_y = y

            elif cmd_upper == 'L':
                if is_relative:
                    self._emit('R_LINE_TO', args[0], args[1])
                    self.cur_x += args[0]
                    self.cur_y += args[1]
                else:
                    self._emit('LINE_TO', args[0], args[1])
                    self.cur_x = args[0]
                    self.cur_y = args[1]
                self.last_cmd = cmd_upper
                self.last_cp2_x = self.cur_x
                self.last_cp2_y = self.cur_y

            elif cmd_upper == 'H':
                if is_relative:
                    self._emit('R_H_LINE_TO', args[0])
                    self.cur_x += args[0]
                else:
                    self._emit('H_LINE_TO', args[0])
                    self.cur_x = args[0]
                self.last_cmd = cmd_upper
                self.last_cp2_x = self.cur_x
                self.last_cp2_y = self.cur_y

            elif cmd_upper == 'V':
                if is_relative:
                    self._emit('R_V_LINE_TO', args[0])
                    self.cur_y += args[0]
                else:
                    self._emit('V_LINE_TO', args[0])
                    self.cur_y = args[0]
                self.last_cmd = cmd_upper
                self.last_cp2_x = self.cur_x
                self.last_cp2_y = self.cur_y

            elif cmd_upper == 'A':
                rx, ry, xrot, laf, sf, x, y = args
                laf = int(laf)
                sf = int(sf)
                if is_relative:
                    self._emit('R_ARC_TO', rx, ry, xrot, laf, sf, x, y)
                    self.cur_x += x
                    self.cur_y += y
                else:
                    self._emit('ARC_TO', rx, ry, xrot, laf, sf, x, y)
                    self.cur_x = x
                    self.cur_y = y
                self.last_cmd = cmd_upper
                self.last_cp2_x = self.cur_x
                self.last_cp2_y = self.cur_y

            elif cmd_upper == 'C':
                x1, y1, x2, y2, x, y = args
                if is_relative:
                    self._emit('R_CUBIC_TO', x1, y1, x2, y2, x, y)
                    # Track absolute cp2 for smooth curves
                    self.last_cp2_x = self.cur_x + x2
                    self.last_cp2_y = self.cur_y + y2
                    self.cur_x += x
                    self.cur_y += y
                else:
                    self._emit('CUBIC_TO', x1, y1, x2, y2, x, y)
                    self.last_cp2_x = x2
                    self.last_cp2_y = y2
                    self.cur_x = x
                    self.cur_y = y
                self.last_cmd = 'C'

            elif cmd_upper == 'S':
                # Smooth cubic: reflect last control point
                if self.last_cmd == 'C':
                    cp1_x = 2 * self.cur_x - self.last_cp2_x
                    cp1_y = 2 * self.cur_y - self.last_cp2_y
                else:
                    cp1_x = self.cur_x
                    cp1_y = self.cur_y

                x2, y2, x, y = args
                if is_relative:
                    # Convert to absolute for cp1 calculation, then emit relative
                    abs_x2 = self.cur_x + x2
                    abs_y2 = self.cur_y + y2
                    # Emit as absolute CUBIC_TO since we computed cp1 in absolute
                    rel_cp1_x = cp1_x - self.cur_x
                    rel_cp1_y = cp1_y - self.cur_y
                    self._emit('R_CUBIC_TO', rel_cp1_x, rel_cp1_y, x2, y2, x, y)
                    self.last_cp2_x = abs_x2
                    self.last_cp2_y = abs_y2
                    self.cur_x += x
                    self.cur_y += y
                else:
                    self._emit('CUBIC_TO', cp1_x, cp1_y, x2, y2, x, y)
                    self.last_cp2_x = x2
                    self.last_cp2_y = y2
                    self.cur_x = x
                    self.cur_y = y
                self.last_cmd = 'C'

            elif cmd_upper == 'Q':
                raise ValueError(
                    f'Unsupported SVG command: {cmd} (quadratic Bézier). '
                    f'Lucide icons should not use Q/q commands.'
                )

            elif cmd_upper == 'T':
                raise ValueError(
                    f'Unsupported SVG command: {cmd} (smooth quadratic Bézier). '
                    f'Lucide icons should not use T/t commands.'
                )

            elif cmd_upper == 'Z':
                self._emit('CLOSE')
                self.cur_x = self.start_x
                self.cur_y = self.start_y
                self.last_cmd = 'Z'
                self.last_cp2_x = self.cur_x
                self.last_cp2_y = self.cur_y

            else:
                raise ValueError(f'Unknown SVG path command: {cmd}')


def _has_fill(element):
    """Check if an SVG element has an explicit fill (not none)."""
    fill = element.get('fill', '')
    return fill and fill.lower() != 'none'


def svg_to_icon_lines(svg_content, icon_name):
    """Convert SVG content to .icon file lines (without header comments)."""
    root = ET.fromstring(svg_content)

    # Verify viewBox
    viewbox = root.get('viewBox', '')
    if viewbox != '0 0 24 24':
        raise ValueError(
            f'Icon "{icon_name}" has unexpected viewBox: {viewbox!r} (expected "0 0 24 24")'
        )

    lines = ['CANVAS_DIMENSIONS, 24,']

    elements = []
    for child in root:
        tag = child.tag.replace(_SVG_NS, '')
        if tag in ('path', 'circle', 'rect', 'line', 'polyline', 'polygon'):
            elements.append((tag, child))

    if not elements:
        raise ValueError(f'Icon "{icon_name}" has no drawable SVG elements')

    for idx, (tag, elem) in enumerate(elements):
        if idx > 0:
            lines.append('NEW_PATH,')

        is_filled = _has_fill(elem)

        if tag == 'path':
            d = elem.get('d', '')
            if not d:
                raise ValueError(f'Icon "{icon_name}" has <path> without d attribute')
            if not is_filled:
                lines.append('STROKE, 2,')
            converter = IconConverter()
            converter.convert_path(d)
            lines.extend(converter.lines)

        elif tag == 'circle':
            cx = float(elem.get('cx', '0'))
            cy = float(elem.get('cy', '0'))
            r = float(elem.get('r', '0'))
            if not is_filled:
                lines.append('STROKE, 2,')
            lines.append(f'CIRCLE, {format_number(cx)}, {format_number(cy)}, {format_number(r)},')

        elif tag == 'rect':
            x = float(elem.get('x', '0'))
            y = float(elem.get('y', '0'))
            w = float(elem.get('width', '0'))
            h = float(elem.get('height', '0'))
            rx = float(elem.get('rx', '0'))
            if not is_filled:
                lines.append('STROKE, 2,')
            lines.append(
                f'ROUND_RECT, {format_number(x)}, {format_number(y)}, '
                f'{format_number(w)}, {format_number(h)}, {format_number(rx)},'
            )

        elif tag == 'line':
            x1 = float(elem.get('x1', '0'))
            y1 = float(elem.get('y1', '0'))
            x2 = float(elem.get('x2', '0'))
            y2 = float(elem.get('y2', '0'))
            lines.append('STROKE, 2,')
            lines.append(f'MOVE_TO, {format_number(x1)}, {format_number(y1)},')
            lines.append(f'LINE_TO, {format_number(x2)}, {format_number(y2)},')

        elif tag == 'polyline':
            points_str = elem.get('points', '')
            points = _parse_points(points_str)
            if len(points) < 2:
                raise ValueError(f'Icon "{icon_name}" has <polyline> with fewer than 2 points')
            lines.append('STROKE, 2,')
            lines.append(f'MOVE_TO, {format_number(points[0][0])}, {format_number(points[0][1])},')
            for px, py in points[1:]:
                lines.append(f'LINE_TO, {format_number(px)}, {format_number(py)},')

        elif tag == 'polygon':
            points_str = elem.get('points', '')
            points = _parse_points(points_str)
            if len(points) < 2:
                raise ValueError(f'Icon "{icon_name}" has <polygon> with fewer than 2 points')
            lines.append('STROKE, 2,')
            lines.append(f'MOVE_TO, {format_number(points[0][0])}, {format_number(points[0][1])},')
            for px, py in points[1:]:
                lines.append(f'LINE_TO, {format_number(px)}, {format_number(py)},')
            lines.append('CLOSE,')

    return lines


def _parse_points(points_str):
    """Parse SVG points attribute (e.g., '10,20 30,40')."""
    nums = re.findall(r'[+-]?(?:\d+\.?\d*|\.\d+)', points_str)
    points = []
    for i in range(0, len(nums) - 1, 2):
        points.append((float(nums[i]), float(nums[i + 1])))
    return points


# ---------------------------------------------------------------------------
# File generation
# ---------------------------------------------------------------------------

def generate_icon_content(icon_lines, lucide_name):
    """Generate complete .icon file content with headers."""
    header = [
        _COPYRIGHT_HEADER,
        _SOURCE_COMMENT_TEMPLATE.format(name=lucide_name),
        '',
    ]
    return '\n'.join(header + icon_lines) + '\n'


def fetch_svg(lucide_name, version):
    """Fetch an SVG from the Lucide GitHub repository."""
    url = _RAW_URL_TEMPLATE.format(version=version, name=lucide_name)
    try:
        with urlopen(url) as resp:
            return resp.read().decode('utf-8')
    except HTTPError as e:
        if e.code == 404:
            raise ValueError(
                f'Icon "{lucide_name}" not found at version {version}. '
                f'URL: {url}\n'
                f'The icon may have been renamed or removed in this version.'
            ) from e
        raise


def load_manifest():
    """Load and validate the manifest file."""
    if not os.path.isfile(_MANIFEST_PATH):
        print(f'error: manifest not found: {_MANIFEST_PATH}', file=sys.stderr)
        sys.exit(1)

    with open(_MANIFEST_PATH, 'r') as f:
        manifest = json.load(f)

    required_keys = ('lucide_version', 'canvas', 'stroke_width', 'icons')
    for key in required_keys:
        if key not in manifest:
            print(f'error: manifest missing key: {key}', file=sys.stderr)
            sys.exit(1)

    return manifest


# ---------------------------------------------------------------------------
# Main logic
# ---------------------------------------------------------------------------

def run_generate(manifest, write_mode):
    """Generate .icon files from manifest. Returns (success, diffs_report)."""
    version = manifest['lucide_version']
    icons = manifest['icons']
    diffs = []
    errors = []

    print(f'Lucide version: {version}')
    print(f'Processing {len(icons)} icons...')
    print()

    for entry in icons:
        name = entry['name']
        filename = entry['file']
        source_version = entry.get('source_version', version)
        filepath = os.path.join(_ICONS_DIR, filename)

        try:
            svg_content = fetch_svg(name, source_version)
            icon_lines = svg_to_icon_lines(svg_content, name)
            expected_content = generate_icon_content(icon_lines, name)
        except (ValueError, HTTPError) as e:
            errors.append((name, str(e)))
            print(f'  ERROR: {name} - {e}', file=sys.stderr)
            continue

        if write_mode:
            differs = True
            if os.path.isfile(filepath):
                with open(filepath, 'r') as f:
                    existing = f.read()
                if existing == expected_content:
                    differs = False
            if differs:
                with open(filepath, 'w', newline='\n') as f:
                    f.write(expected_content)
                print(f'  WROTE: {filename}')
            else:
                print(f'  UNCHANGED: {filename}')
        else:
            # Check mode: compare against existing file
            if os.path.isfile(filepath):
                with open(filepath, 'r') as f:
                    existing_content = f.read()
                if existing_content != expected_content:
                    diff = difflib.unified_diff(
                        existing_content.splitlines(keepends=True),
                        expected_content.splitlines(keepends=True),
                        fromfile=f'a/{filename}',
                        tofile=f'b/{filename}',
                    )
                    diff_str = ''.join(diff)
                    diffs.append((filename, diff_str))
                    print(f'  DRIFT: {filename}')
                else:
                    print(f'  OK:    {filename}')
            else:
                diffs.append((filename, f'File does not exist: {filepath}\n'))
                print(f'  MISSING: {filename}')

    if errors:
        print(f'\n{len(errors)} error(s) encountered:', file=sys.stderr)
        for name, msg in errors:
            print(f'  {name}: {msg}', file=sys.stderr)
        return False, diffs

    if not write_mode and diffs:
        print(f'\n{len(diffs)} file(s) have drift:')
        for filename, diff_str in diffs:
            print(f'\n--- {filename} ---')
            print(diff_str)
        print(
            '\nTo fix: run `python3 '
            'maho-chromium/build/scripts/lucide_icons/generate.py --write`'
        )
        return False, diffs

    if write_mode:
        print(f'\nSuccessfully wrote {len(icons)} icon files.')
    else:
        print(f'\nAll {len(icons)} icon files are up to date.')

    return True, diffs


def main():
    parser = argparse.ArgumentParser(
        description='Generate Chromium .icon files from upstream Lucide SVGs.'
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        '--check',
        action='store_true',
        help='Verify .icon files match manifest (read-only, for CI)',
    )
    mode.add_argument(
        '--write',
        action='store_true',
        help='Regenerate .icon files from manifest',
    )
    parser.add_argument(
        '--version',
        metavar='X.Y.Z',
        help='Override lucide_version in manifest before processing',
    )
    args = parser.parse_args()

    manifest = load_manifest()

    if args.version:
        manifest['lucide_version'] = args.version
        if args.write:
            # Also persist the version change to manifest
            with open(_MANIFEST_PATH, 'r') as f:
                manifest_data = json.load(f)
            manifest_data['lucide_version'] = args.version
            with open(_MANIFEST_PATH, 'w', newline='\n') as f:
                json.dump(manifest_data, f, indent=2)
                f.write('\n')
            print(f'Updated manifest version to {args.version}')

    success, _ = run_generate(manifest, write_mode=args.write)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
