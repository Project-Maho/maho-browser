#!/usr/bin/env python3
"""Diagnose the Maho AI panel blank-screen issue via CDP and stderr log capture.

Launch the built Maho app with an isolated profile and remote-debugging port,
poll DevTools JSON endpoints, collect stderr, and print a concise summary of
targets and suspicious log lines.

Usage (typical):
    python3 maho-chromium/build/scripts/diagnose_ai_panel.py

    # Already launched the app yourself with the right flags?
    # Reuse the same port you passed to --remote-debugging-port.
    python3 maho-chromium/build/scripts/diagnose_ai_panel.py \\
        --skip-launch --port 9222

    # With --skip-launch, pass the app's stderr log for pattern matching:
    python3 maho-chromium/build/scripts/diagnose_ai_panel.py \\
        --skip-launch --port 9222 --log-file /tmp/maho_stderr.log

    # Custom app path or longer startup timeout:
    python3 maho-chromium/build/scripts/diagnose_ai_panel.py \\
        --app-path /path/to/Maho.app/Contents/MacOS/Maho \\
        --timeout 30
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))

_DEFAULT_APP_BINARY = os.path.join(
    _WORKSPACE_ROOT,
    'chromium', 'src', 'out', 'Default',
    'Maho.app', 'Contents', 'MacOS', 'Maho',
)

# ---------------------------------------------------------------------------
# Log pattern filters
# ---------------------------------------------------------------------------

# Patterns that indicate WebUI / module / console failures relevant to the
# AI panel blank-screen issue.
_FAILURE_PATTERNS: list[re.Pattern[str]] = [
    re.compile(r'maho.?ai', re.IGNORECASE),
    re.compile(r'chrome://maho', re.IGNORECASE),
    re.compile(r'WebUI', re.IGNORECASE),
    re.compile(r'MojoBindingsSystem', re.IGNORECASE),
    re.compile(r'module.*fail', re.IGNORECASE),
    re.compile(r'fail.*module', re.IGNORECASE),
    re.compile(r'Uncaught.*Error', re.IGNORECASE),
    re.compile(r'SyntaxError', re.IGNORECASE),
    re.compile(r'Cannot find module', re.IGNORECASE),
    re.compile(r'ERR_FILE_NOT_FOUND', re.IGNORECASE),
    re.compile(r'ERR_INVALID_URL', re.IGNORECASE),
    re.compile(r'net::ERR', re.IGNORECASE),
    re.compile(r'DCHECK', re.IGNORECASE),
    re.compile(r'CHECK failed', re.IGNORECASE),
    re.compile(r'side.?panel', re.IGNORECASE),
    re.compile(r'content.*security.*policy', re.IGNORECASE),
    re.compile(r'Refused to (load|execute|connect)', re.IGNORECASE),
    # 'sandbox' alone is too generic (Chrome logs dozens of benign sandbox
    # setup lines); only match when paired with a failure keyword.
    re.compile(r'sandbox.*(fail|error|denied|block)', re.IGNORECASE),
    re.compile(r'NotAllowedError', re.IGNORECASE),
    # Broad [ERROR ...] brackets are extremely noisy during startup (GPU, font,
    # network stack, etc.).  Restrict to lines that also mention maho or WebUI.
    re.compile(r'\[ERROR.*\].*(maho|webui|panel|module)', re.IGNORECASE),
    re.compile(r'CONSOLE.*ERROR', re.IGNORECASE),
]

# Targets whose URL hints at a problem (not a normal browsing target)
_SUSPICIOUS_URL_PATTERNS: list[re.Pattern[str]] = [
    re.compile(r'^chrome-error://', re.IGNORECASE),
    re.compile(r'chromewebdata', re.IGNORECASE),
    re.compile(r'^about:blank', re.IGNORECASE),
    re.compile(r'chrome://crash', re.IGNORECASE),
    re.compile(r'chrome://kill', re.IGNORECASE),
    re.compile(r'net::ERR', re.IGNORECASE),
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_DIVIDER = '-' * 72


def _print_section(title: str) -> None:
    print()
    print(_DIVIDER)
    print(f'  {title}')
    print(_DIVIDER)


def _fetch_json(url: str, timeout: float = 5.0) -> object | None:
    """Return parsed JSON from *url*, or None on any error."""
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            raw = resp.read()
            return json.loads(raw)
    except (urllib.error.URLError, OSError, json.JSONDecodeError):
        return None


def _poll_until_ready(base_url: str, deadline: float) -> bool:
    """Block until /json/version responds or *deadline* (epoch seconds) passes."""
    version_url = f'{base_url}/json/version'
    while time.monotonic() < deadline:
        data = _fetch_json(version_url, timeout=2.0)
        if data is not None:
            return True
        time.sleep(0.5)
    return False


def _is_ai_target(target: dict[str, object]) -> bool:
    url = str(target.get('url', ''))
    title = str(target.get('title', ''))
    return (
        'maho-ai' in url.lower()
        or 'maho-ai' in title.lower()
        or 'chrome://maho-ai' in url.lower()
    )


def _is_suspicious_target(target: dict[str, object]) -> bool:
    url = str(target.get('url', ''))
    return any(pat.search(url) for pat in _SUSPICIOUS_URL_PATTERNS)


def _grep_log_lines(log_path: str) -> list[str]:
    """Return lines from *log_path* that match any failure pattern."""
    matches: list[str] = []
    if not os.path.isfile(log_path):
        return matches
    try:
        with open(log_path, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                stripped = line.rstrip('\n')
                if any(pat.search(stripped) for pat in _FAILURE_PATTERNS):
                    matches.append(stripped)
    except OSError:
        pass
    return matches


def _write_artifact(artifact_dir: str, name: str, data: object) -> str:
    path = os.path.join(artifact_dir, name)
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(data, fh, indent=2)
    return path


# ---------------------------------------------------------------------------
# Main logic
# ---------------------------------------------------------------------------

def run_diagnostics(args: argparse.Namespace) -> int:
    port: int = args.port
    timeout: float = args.timeout
    app_binary: str = args.app_path
    skip_launch: bool = args.skip_launch
    base_url = f'http://127.0.0.1:{port}'

    # ── 1. Set up temp directories ──────────────────────────────────────────
    artifact_dir = tempfile.mkdtemp(prefix='maho_ai_diag_')
    user_data_dir = tempfile.mkdtemp(prefix='maho_ai_profile_')

    # In normal launch mode the log is written to the artifact dir.
    # In --skip-launch mode use --log-file if provided, otherwise no log.
    log_path: str | None
    if skip_launch:
        log_path = args.log_file if args.log_file else None
    else:
        log_path = os.path.join(artifact_dir, 'maho_stderr.log')

    _print_section('Maho AI Panel Diagnostic')
    print(f'  Workspace root : {_WORKSPACE_ROOT}')
    print(f'  Artifact dir   : {artifact_dir}')
    print(f'  User-data dir  : {user_data_dir}')
    print(f'  Log file       : {log_path if log_path else "(none — pass --log-file to enable)"}')
    print(f'  CDP base URL   : {base_url}')

    # ── 2. Launch (unless --skip-launch) ────────────────────────────────────
    proc: subprocess.Popen[bytes] | None = None
    if skip_launch:
        _print_section('App Launch')
        print('  --skip-launch: assuming app is already running on '
              f'127.0.0.1:{port}')
    else:
        if not os.path.isfile(app_binary):
            print(
                f'\nerror: app binary not found: {app_binary}\n'
                '       Build the app first, or pass --app-path.\n',
                file=sys.stderr,
            )
            return 1

        launch_flags = [
            app_binary,
            f'--remote-debugging-port={port}',
            f'--user-data-dir={user_data_dir}',
            '--enable-logging=stderr',
            '--v=1',
            '--no-first-run',
            '--disable-features=MediaRouter',  # reduce noise
        ]

        _print_section('App Launch')
        print(f'  Binary : {app_binary}')
        print(f'  Flags  : {" ".join(launch_flags[1:])}')

        launch_log_path: str = os.path.join(artifact_dir, 'maho_stderr.log')
        with open(launch_log_path, 'wb') as log_fh:
            proc = subprocess.Popen(
                launch_flags,
                stdout=subprocess.DEVNULL,
                stderr=log_fh,
            )
        print(f'  PID    : {proc.pid}')

    # ── 3. Poll until CDP is ready ───────────────────────────────────────────
    _print_section('Waiting for CDP')
    deadline = time.monotonic() + timeout
    ready = _poll_until_ready(base_url, deadline)
    if not ready:
        print(f'  TIMEOUT: CDP did not respond within {timeout:.0f}s.')
        if proc is not None:
            print(f'  Killing pid {proc.pid}...')
            proc.terminate()
        # Still try to print whatever log we have
        _print_section('Stderr Log Excerpts (partial)')
        for line in (_grep_log_lines(log_path) if log_path else [])[:60]:
            print(f'  {line}')
        if not log_path:
            print('  (no --log-file provided; log scraping skipped)')
        return 2
    print('  CDP is ready.')

    # ── 4. Fetch /json/version ───────────────────────────────────────────────
    version_data = _fetch_json(f'{base_url}/json/version')
    version_artifact = _write_artifact(artifact_dir, 'cdp_version.json', version_data or {})

    _print_section('CDP Version Info')
    if isinstance(version_data, dict):
        for key in ('Browser', 'Protocol-Version', 'User-Agent', 'V8-Version',
                    'WebKit-Version', 'webSocketDebuggerUrl'):
            val = version_data.get(key, '(not present)')
            print(f'  {key:<26}: {val}')
    else:
        print('  (no version data returned)')
    print(f'  Artifact: {version_artifact}')

    # ── 5. Collect /json/list targets ───────────────────────────────────────
    # Both /json/list and /json (deprecated alias) for coverage.
    targets: list[dict[str, object]] = []
    for endpoint in ('/json/list', '/json'):
        data = _fetch_json(f'{base_url}{endpoint}')
        if isinstance(data, list):
            targets = data
            break

    targets_artifact = _write_artifact(artifact_dir, 'cdp_targets.json', targets)

    _print_section(f'All Targets ({len(targets)} total)')
    if targets:
        for t in targets:
            t_type = t.get('type', '?')
            t_url = t.get('url', '')
            t_title = t.get('title', '')
            t_id = t.get('id', '')
            print(f'  [{t_type:12s}] {t_url}')
            if t_title and t_title != t_url:
                print(f'               title: {t_title}')
            if t.get('webSocketDebuggerUrl'):
                print(f'               wsUrl: {t["webSocketDebuggerUrl"]}')
            if t_id:
                print(f'               id   : {t_id}')
    else:
        print('  (no targets returned)')
    print(f'  Artifact: {targets_artifact}')

    # ── 6. AI-related targets ────────────────────────────────────────────────
    ai_targets = [t for t in targets if _is_ai_target(t)]
    _print_section(f'AI Panel Targets  (chrome://maho-ai)  [{len(ai_targets)} found]')
    if ai_targets:
        for t in ai_targets:
            print(f'  url  : {t.get("url", "")}')
            print(f'  title: {t.get("title", "")}')
            print(f'  type : {t.get("type", "")}')
            ws = t.get('webSocketDebuggerUrl', '')
            if ws:
                print(f'  wsUrl: {ws}')
            print()
    else:
        print('  NONE FOUND — the AI panel WebUI has not been loaded.')
        print('  This means either:')
        print('    a) the panel was never opened (open it first, then re-run), or')
        print('    b) the page handler is failing to create the WebUI target.')

    # ── 7. Suspicious targets ────────────────────────────────────────────────
    suspicious = [t for t in targets if _is_suspicious_target(t)]
    _print_section(f'Suspicious Targets  [{len(suspicious)} found]')
    if suspicious:
        for t in suspicious:
            print(f'  url  : {t.get("url", "")}')
            print(f'  title: {t.get("title", "")}')
            print(f'  type : {t.get("type", "")}')
            print()
    else:
        print('  None detected.')

    # ── 8. Stderr log excerpts ───────────────────────────────────────────────
    _print_section('Stderr Log Excerpts  (WebUI / module / console failures)')
    matching_lines = _grep_log_lines(log_path) if log_path else []
    if log_path is None:
        print('  (no log file available — pass --log-file to enable log scraping)')
    elif matching_lines:
        # Deduplicate while preserving order, cap at 100 lines
        seen: set[str] = set()
        shown = 0
        for line in matching_lines:
            if line not in seen:
                seen.add(line)
                print(f'  {line}')
                shown += 1
                if shown >= 100:
                    remaining = len(matching_lines) - shown
                    if remaining > 0:
                        print(f'  ... ({remaining} more matching lines; see {log_path})')
                    break
    else:
        print('  No matching lines found — no obvious WebUI/module errors in log.')

    # ── 9. Summary ────────────────────────────────────────────────────────────
    _print_section('Summary')
    print(f'  Targets total    : {len(targets)}')
    print(f'  AI panel targets : {len(ai_targets)}')
    print(f'  Suspicious URLs  : {len(suspicious)}')
    print(f'  Log matches      : {len(matching_lines)}')
    print()
    print('  Artifacts written:')
    print(f'    {version_artifact}')
    print(f'    {targets_artifact}')
    if log_path and not skip_launch:
        print(f'    {log_path}')
    print()

    if not ai_targets:
        print('  ACTION REQUIRED: Open the Maho AI side panel in the running app,')
        if skip_launch:
            print(f'  then re-run this script (--skip-launch --port {port} '
                  f'--log-file <path/to/stderr.log>)')
        else:
            print(f'  then re-run this script (add --skip-launch --port {port})')
        print('  to capture the target after the panel has been activated.')

    # ── 10. Graceful shutdown ────────────────────────────────────────────────
    if proc is not None:
        print()
        print('  Press Ctrl-C or close the browser window to end the diagnostic session.')
        print('  (The launched app process will continue running — kill it manually if needed.)')
        print(f'  PID: {proc.pid}')

    print()
    return 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description='Diagnose the Maho AI panel blank-screen issue via CDP + stderr.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        '--app-path',
        default=_DEFAULT_APP_BINARY,
        metavar='PATH',
        help=(
            'Path to the Maho binary to launch '
            f'(default: {_DEFAULT_APP_BINARY})'
        ),
    )
    parser.add_argument(
        '--port',
        type=int,
        default=9222,
        metavar='PORT',
        help='Remote debugging port (default: 9222)',
    )
    parser.add_argument(
        '--timeout',
        type=float,
        default=20.0,
        metavar='SECONDS',
        help='Seconds to wait for CDP to become ready (default: 20)',
    )
    parser.add_argument(
        '--skip-launch',
        action='store_true',
        help=(
            'Do not launch the app; connect to an already-running instance '
            'on --port instead.'
        ),
    )
    parser.add_argument(
        '--log-file',
        default=None,
        metavar='PATH',
        help=(
            'Path to a stderr log file to scrape for failure patterns.  '
            'Only useful with --skip-launch (the launched app always writes '
            'its own log).  If omitted in --skip-launch mode, log scraping '
            'is skipped with a clear note.'
        ),
    )
    args = parser.parse_args()
    return run_diagnostics(args)


if __name__ == '__main__':
    sys.exit(main())
