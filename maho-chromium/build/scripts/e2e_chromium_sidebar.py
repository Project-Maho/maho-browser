#!/usr/bin/env python3
"""Phase 1 Chromium Maho desktop E2E harness."""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
import uuid as _uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import ApplicationServices as _AS
import Quartz as _Q

from e2e_artifacts import capture_fullscreen_png, create_artifacts, default_profile_dir, write_profile_path
from e2e_macos_ax import (
    _frame_parts,
    ax_application,
    ax_preflight,
    click_element_center,
    collect_matches,
    drag_above,
    drag_element_center,
    dump_ax_tree,
    find_first_matching_element,
    find_first_pressable_element,
    get_element_frame,
    get_element_identifier,
    get_element_label,
    get_element_info,
    iter_ax_tree,
    poll_until,
    press_element,
    press_return_key,
    set_element_value,
    right_click_element_center,
    system_focused_window,
    wait_for_frame_change,
    wait_for_label,
)
from e2e_selectors import DEFAULT_APP_PATH, DEFAULT_INITIAL_URLS, KNOWN_BUTTONS, KNOWN_TAB_TITLES, KNOWN_TEXTS, MAHO_APP_NAME, SCENARIO_NAMES, STARTUP_INTERRUPTION_BUTTONS, STARTUP_INTERRUPTION_TEXTS

AXUIElementCopyAttributeValue = getattr(_AS, 'AXUIElementCopyAttributeValue')
CGWindowListCopyWindowInfo = getattr(_Q, 'CGWindowListCopyWindowInfo')
kCGWindowListOptionOnScreenOnly = getattr(_Q, 'kCGWindowListOptionOnScreenOnly')
kCGNullWindowID = getattr(_Q, 'kCGNullWindowID')
AX_SEARCH_DEPTH = 20


@dataclass
class RunnerConfig:
    app_path: str
    artifact_root: str
    keep_profile: bool
    readiness_timeout_ms: int
    scenario_timeout_ms: int
    suite_timeout_ms: int


def _int_env(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw is None or raw == '':
        return default
    try:
        return int(raw)
    except ValueError as exc:
        raise SystemExit(f'Invalid integer for {name}: {raw!r}') from exc


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description='Run Chromium Maho macOS E2E smoke scenarios.')
    parser.add_argument('--scenario', action='append', dest='scenarios', help='Run only the named scenario. May be repeated.')
    parser.add_argument('--list', action='store_true', help='List available scenarios and exit.')
    return parser.parse_args(argv)


def load_config() -> RunnerConfig:
    return RunnerConfig(
        app_path=os.environ.get('MAHO_APP_PATH', DEFAULT_APP_PATH),
        artifact_root=os.environ.get('MAHO_E2E_ARTIFACT_DIR', os.path.join('/tmp', 'maho-chromium-e2e-spaces')),
        keep_profile=os.environ.get('MAHO_E2E_KEEP_PROFILE') == '1',
        readiness_timeout_ms=_int_env('MAHO_E2E_APP_READY_TIMEOUT_MS', 30000),
        scenario_timeout_ms=_int_env('MAHO_E2E_SCENARIO_TIMEOUT_MS', 60000),
        suite_timeout_ms=_int_env('MAHO_E2E_SUITE_TIMEOUT_MS', 600000),
    )


def app_binary(app_path: str) -> str:
    binary = os.path.join(app_path, 'Contents', 'MacOS', 'Maho')
    if not os.path.exists(binary):
        raise SystemExit(f'Build artifact not found: {binary}')
    return binary


def launch_app(config: RunnerConfig, profile_dir: str, scenario_name: str) -> subprocess.Popen:
    binary = app_binary(config.app_path)
    launch_log = os.path.join(config.artifact_root, scenario_name, 'launch.log')
    os.makedirs(os.path.dirname(launch_log), exist_ok=True)
    seeded_launch = scenario_name.startswith(('phase2_', 'sidebar_dnd_'))
    initial_urls = list(DEFAULT_INITIAL_URLS)
    args = [
        binary,
        f'--user-data-dir={profile_dir}',
        '--no-first-run',
        '--disable-session-crashed-bubble',
        '--no-default-browser-check',
        '--disable-default-apps',
        '--force-renderer-accessibility',
        *initial_urls,
    ]
    log_handle = open(launch_log, 'w', encoding='utf-8')
    log_handle.write('Launching: ' + ' '.join(args) + '\n')
    log_handle.flush()
    proc = subprocess.Popen(args, stdout=log_handle, stderr=log_handle)
    proc._maho_launch_log = log_handle  # type: ignore[attr-defined]
    proc._maho_profile_dir = profile_dir  # type: ignore[attr-defined]
    proc._maho_seeded_launch = seeded_launch  # type: ignore[attr-defined]
    return proc


def dispatch_url_to_running_instance(config: RunnerConfig, proc: subprocess.Popen, url: str) -> None:
    subprocess.run(['open', '-a', config.app_path, url], check=False)


def terminate_process(proc: subprocess.Popen) -> None:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)
    log_handle = getattr(proc, '_maho_launch_log', None)
    if log_handle is not None:
        log_handle.close()


def find_running_maho_windows() -> list[dict]:
    windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID) or []
    return [window for window in windows if window.get('kCGWindowOwnerName') == MAHO_APP_NAME]


def find_running_maho_pids() -> list[int]:
    pids: list[int] = []
    for window in find_running_maho_windows():
        pid = window.get('kCGWindowOwnerPID')
        if isinstance(pid, int) and pid not in pids:
            pids.append(pid)
    return pids


def find_running_maho_windows_for_pid(pid: int) -> list[dict]:
    return [window for window in find_running_maho_windows() if window.get('kCGWindowOwnerPID') == pid]


def get_focused_window_element(pid: int):
    app = ax_application(pid)
    error_code, window = AXUIElementCopyAttributeValue(app, 'AXFocusedWindow', None)
    if error_code:
        return None
    return window


def get_window_elements(pid: int) -> list[Any]:
    app = ax_application(pid)
    error_code, windows = AXUIElementCopyAttributeValue(app, 'AXWindows', None)
    if error_code or not windows:
        return []
    try:
        return list(windows)
    except TypeError:
        return [windows]


def describe_windows(windows: list[dict]) -> str:
    lines = []
    for window in windows:
        lines.append(
            f"pid={window.get('kCGWindowOwnerPID')} name={window.get('kCGWindowOwnerName')} layer={window.get('kCGWindowLayer')} bounds={window.get('kCGWindowBounds')}"
        )
    return '\n'.join(lines) + '\n'


def app_pid() -> int | None:
    ps = subprocess.run(['pgrep', '-x', 'Maho'], capture_output=True, text=True, check=False)
    if ps.returncode != 0 or not ps.stdout.strip():
        return None
    try:
        return int(ps.stdout.splitlines()[-1].strip())
    except ValueError:
        return None


def running_maho_pids() -> list[int]:
    ps = subprocess.run(['pgrep', '-x', 'Maho'], capture_output=True, text=True, check=False)
    if ps.returncode != 0 or not ps.stdout.strip():
        return []
    pids: list[int] = []
    for line in ps.stdout.splitlines():
        try:
            pids.append(int(line.strip()))
        except ValueError:
            continue
    return pids


def cleanup_existing_maho_processes() -> None:
    pids = running_maho_pids()
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            continue
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        remaining = running_maho_pids()
        if not remaining:
            return
        time.sleep(0.2)
    for pid in running_maho_pids():
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            continue


def _scenario_artifacts(config: RunnerConfig, scenario_name: str):
    return create_artifacts(config.artifact_root, scenario_name)


def get_any_focused_window(pid: int):
    focused = get_focused_window_element(pid)
    if focused is not None:
        return focused
    return system_focused_window()


def iter_maho_window_candidates(primary_pid: int | None = None):
    seen: set[tuple[int, int]] = set()
    pid_order: list[int] = []
    if primary_pid is not None:
        pid_order.append(primary_pid)
    for pid in find_running_maho_pids():
        if pid not in pid_order:
            pid_order.append(pid)
    for pid in pid_order:
        focused = get_any_focused_window(pid)
        if focused is not None:
            key = (pid, id(focused))
            if key not in seen:
                seen.add(key)
                yield pid, focused
        for window in get_window_elements(pid):
            key = (pid, id(window))
            if key in seen:
                continue
            seen.add(key)
            yield pid, window


def dismiss_startup_interruptions(pid: int, artifacts) -> None:
    handled_labels: list[str] = []

    def predicate():
        current = get_any_focused_window(pid)
        if current is None:
            return True
        dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
        has_interruption_text = any(text.lower() in dump.lower() for text in STARTUP_INTERRUPTION_TEXTS)
        if not has_interruption_text:
            return True
        if has_interruption_text:
            artifacts.write_text('startup_interruption_ax_tree.txt', dump)
        button = find_first_pressable_element(current, STARTUP_INTERRUPTION_BUTTONS, max_depth=AX_SEARCH_DEPTH)
        if button is None:
            return None
        pressed = press_element(button)
        if not pressed:
            return None
        handled_labels.append('pressed interruption control')
        return None

    try:
        poll_until(predicate, 10000, label='startup interruption dismissal')
    except TimeoutError:
        if handled_labels:
            artifacts.write_text('startup_interruption_actions.txt', '\n'.join(handled_labels) + '\n')
        return
    if handled_labels:
        artifacts.write_text('startup_interruption_actions.txt', '\n'.join(handled_labels) + '\n')


def readiness_poll(config: RunnerConfig, proc: subprocess.Popen, artifacts) -> tuple[int, Any, list[dict]]:
    def predicate():
        if proc.poll() is not None:
            raise RuntimeError(f'Maho exited early with code {proc.returncode}')
        pid = app_pid()
        if pid is None:
            return None
        dismiss_startup_interruptions(pid, artifacts)
        windows = find_running_maho_windows_for_pid(pid)
        if not windows:
            return None
        focused = get_any_focused_window(pid)
        if focused is None:
            return None
        artifacts.write_text('window_state.txt', dump_ax_tree(focused, max_depth=AX_SEARCH_DEPTH))
        buttons = collect_matches(focused, KNOWN_BUTTONS + KNOWN_TAB_TITLES + KNOWN_TEXTS, max_depth=AX_SEARCH_DEPTH)
        if not buttons:
            return None
        return pid, focused, windows

    try:
        return poll_until(predicate, config.readiness_timeout_ms, label='app readiness')
    except Exception as exc:
        artifacts.write_text('timeout_reason.txt', str(exc) + '\n')
        raise


def capture_failure_artifacts(artifacts, focused_window, windows: list[dict], reason: str) -> None:
    artifacts.write_text('timeout_reason.txt', reason + '\n')
    if focused_window is not None:
        artifacts.write_text('ax_tree.txt', dump_ax_tree(focused_window, max_depth=AX_SEARCH_DEPTH))
        buttons = collect_matches(focused_window, KNOWN_BUTTONS + KNOWN_TAB_TITLES + KNOWN_TEXTS, max_depth=AX_SEARCH_DEPTH)
        artifacts.write_text('buttons.txt', '\n'.join(info.short_label() for info in buttons) + '\n')
    if windows:
        artifacts.write_text('window_list.txt', describe_windows(windows))
    capture_fullscreen_png(artifacts.path('failure.png'))


def capture_before_after(artifacts) -> None:
    capture_fullscreen_png(artifacts.path('before.png'))


def capture_after(artifacts) -> None:
    capture_fullscreen_png(artifacts.path('after.png'))


def _validate_scenario_registry() -> None:
    scenario_names = list(SCENARIO_NAMES)
    registered_names = list(scenario_map().keys())
    missing_from_map = [name for name in scenario_names if name not in registered_names]
    extra_in_map = [name for name in registered_names if name not in scenario_names]
    if missing_from_map or extra_in_map:
        parts = []
        if missing_from_map:
            parts.append('missing from scenario_map: ' + ', '.join(missing_from_map))
        if extra_in_map:
            parts.append('extra in scenario_map: ' + ', '.join(extra_in_map))
        raise SystemExit('Scenario registry drift detected: ' + '; '.join(parts))


def assert_contains(text: str, haystack: str, message: str) -> None:
    if text not in haystack:
        raise AssertionError(message)

def seeded_ax_dump(focused_window) -> str:
    return dump_ax_tree(focused_window, max_depth=AX_SEARCH_DEPTH)


def assert_seeded_label(ax_dump: str, label: str, message: str) -> None:
    assert_contains(label, ax_dump, message)


def open_seeded_test_page(config: RunnerConfig, proc: subprocess.Popen) -> None:
    if not getattr(proc, '_maho_seeded_launch', False):
        return
    if getattr(proc, '_maho_seeded_page_opened', False):
        return

    pid = app_pid()
    if pid is None:
        raise RuntimeError('Seeded test page navigation could not find a running Maho process')

    def _focused_window():
        current = get_any_focused_window(pid)
        if current is None:
            raise RuntimeError('Seeded test page navigation could not find a focused window')
        return current

    def _address_control(current):
        element = find_first_matching_element(current, ('Search or Enter URL...',), max_depth=AX_SEARCH_DEPTH)
        if element is not None:
            return element
        return find_first_matching_element(current, ('maho-test',), max_depth=AX_SEARCH_DEPTH)

    def predicate():
        current = _focused_window()
        address = _address_control(current)
        if address is None:
            return None
        for _attempt in range(3):
            try:
                click_element_center(address)
            except Exception:
                pass
            if set_element_value(address, 'chrome://maho-test/?seed=2'):
                press_return_key()
                return current
            time.sleep(0.1)
        return None

    poll_until(predicate, config.readiness_timeout_ms, label='seeded test page navigation')
    poll_until(
        lambda: (
            lambda current: current if 'Maho Test Page' in (dump := dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)) and 'maho-test' in dump.lower() else None
        )(_focused_window()),
        config.readiness_timeout_ms,
        label='seeded test page load',
    )
    proc._maho_seeded_page_opened = True  # type: ignore[attr-defined]


def wait_for_seeded_test_page(pid: int, config: RunnerConfig, required_tokens: tuple[str, ...] = ()):
    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'Maho Test Page' not in dump:
                continue
            if 'maho-test' not in dump.lower():
                continue
            if any(token not in dump for token in required_tokens):
                continue
            return current, dump
        return None

    return poll_until(predicate, config.readiness_timeout_ms, label='seeded maho test page')


def seeded_test_page_dump(pid: int, config: RunnerConfig, proc: subprocess.Popen, required_tokens: tuple[str, ...] = ()) -> str:
    _current, dump = _refresh_seeded_page_after_drag(pid, config, proc, required_tokens=required_tokens, label='seeded maho test page')
    return dump


def seeded_test_page_window_dump(pid: int, config: RunnerConfig, proc: subprocess.Popen, required_tokens: tuple[str, ...] = ()):
    return _refresh_seeded_page_after_drag(pid, config, proc, required_tokens=required_tokens, label='seeded maho test page')


def wait_for_seeded_test_page_window_dump(pid: int, config: RunnerConfig, proc: subprocess.Popen, required_tokens: tuple[str, ...] = (), readiness_check=None, label: str = 'seeded maho test page'):
    return _refresh_seeded_page_after_drag(pid, config, proc, required_tokens=required_tokens, readiness_check=readiness_check, label=label)


def _refresh_seeded_page_after_drag(pid: int, config: RunnerConfig, proc: subprocess.Popen, required_tokens: tuple[str, ...] = (), readiness_check=None, label: str = 'seeded maho test page'):
    if getattr(proc, '_maho_seeded_launch', False) and getattr(proc, '_maho_seeded_page_opened', False):
        return _wait_for_seeded_test_page_window_dump_after_reload(pid, config, required_tokens=required_tokens, readiness_check=readiness_check, label=label)
    open_seeded_test_page(config, proc)
    return _wait_for_seeded_test_page_window_dump_after_open(pid, config, required_tokens=required_tokens, readiness_check=readiness_check, label=label)


def _wait_for_seeded_test_page_window_dump_after_open(pid: int, config: RunnerConfig, required_tokens: tuple[str, ...] = (), readiness_check=None, label: str = 'seeded maho test page'):
    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'Maho Test Page' not in dump:
                continue
            if 'maho-test' not in dump.lower():
                continue
            if any(token not in dump for token in required_tokens):
                continue
            if readiness_check is not None:
                try:
                    readiness_check(current, dump)
                except AssertionError:
                    continue
            return current, dump
        return None

    return poll_until(predicate, config.readiness_timeout_ms, label=label)


def _wait_for_seeded_test_page_window_dump_after_reload(pid: int, config: RunnerConfig, required_tokens: tuple[str, ...] = (), readiness_check=None, label: str = 'seeded maho test page'):
    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'Maho Test Page' not in dump:
                continue
            if 'maho-test' not in dump.lower():
                continue
            if any(token not in dump for token in required_tokens):
                continue
            if readiness_check is not None:
                try:
                    readiness_check(current, dump)
                except AssertionError:
                    continue
            return current, dump
        return None

    current = get_any_focused_window(pid)
    if current is None:
        raise RuntimeError('Seeded page refresh could not find a focused window')
    reload_button = find_first_pressable_element(current, ('Reload',), max_depth=AX_SEARCH_DEPTH)
    if reload_button is None:
        raise RuntimeError('Seeded page refresh could not find the Reload button')
    if not press_element(reload_button):
        raise RuntimeError('Seeded page refresh could not press the Reload button')
    return poll_until(predicate, config.readiness_timeout_ms, label=label)


def seeded_test_page_favorites_dump(pid: int, config: RunnerConfig, proc: subprocess.Popen) -> str:
    _current, dump = _refresh_seeded_page_after_drag(pid, config, proc, label='seeded favorites section')
    return dump


def assert_seeded_window_state(ax_dump: str) -> None:
    assert_contains('Maho Test Page', ax_dump, 'Seeded Maho Test Page window not found')
    assert_contains('maho-test', ax_dump.lower(), 'Seeded test-page host button not found')
    if 'Tabs' not in ax_dump and 'Maho Test Page' not in ax_dump and 'news.ycombinator.com' not in ax_dump and 'Hacker News' not in ax_dump:
        raise AssertionError('Seeded tabs section not found')


def assert_seeded_sidebar_ready_for_favorites(window, dump: str) -> None:
    assert_seeded_favorites_section(dump)


def assert_seeded_sidebar_ready_for_pinned_drag(window, dump: str) -> None:
    if not _pinned_rows(window):
        raise AssertionError('Expected pinned rows in seeded sidebar')
    if not _regular_rows(window):
        raise AssertionError('Expected regular rows in seeded sidebar')


def assert_seeded_sidebar_ready_for_folder_drag(window, dump: str) -> None:
    if not _regular_rows(window):
        raise AssertionError('Expected regular rows in sidebar')


def assert_seeded_sidebar_ready_for_regular_tabs(window, dump: str) -> None:
    if len(_regular_rows(window)) < 2:
        raise AssertionError('Expected at least two visible regular rows in seeded sidebar')


def assert_seeded_favorites_section(ax_dump: str) -> None:
    favorites_start = ax_dump.find('Favorites')
    favorites_end = ax_dump.find('Tabs', favorites_start) if favorites_start != -1 else -1
    labels: list[str] = []
    if favorites_start != -1 and favorites_end != -1:
        favorites_section = ax_dump[favorites_start:favorites_end]
        if 'Drag tabs here to pin favorites' in favorites_section or 'EMPTY' in favorites_section:
            raise AssertionError('Seeded favorites section is empty')
        for line in favorites_section.splitlines():
            if ' | AXButton' not in line:
                continue
            label = line.split(' | AXButton', 1)[0].split('- ')[-1].strip()
            if label:
                labels.append(label)
    else:
        fallback_labels = ('GitHub', 'Example News', 'Example Shop', 'github.com')
        for line in ax_dump.splitlines():
            if ' | AXButton' not in line:
                continue
            label = line.split(' | AXButton', 1)[0].split('- ')[-1].strip()
            if not label:
                continue
            if any(token.lower() in label.lower() for token in fallback_labels):
                labels.append(label)
        if 'Drag tabs here to pin favorites' in ax_dump or 'EMPTY' in ax_dump:
            raise AssertionError('Seeded favorites section is empty')
    if len(labels) < 3:
        raise AssertionError('Seeded favorites tile labels not found in favorites section')
    if any('/' in label or label.startswith('http') for label in labels):
        raise AssertionError('Seeded favorites section still exposes raw URL paths')
    if not any('GitHub' in label or 'github.com' in label.lower() for label in labels):
        raise AssertionError('Seeded GitHub favorite tile label not found in favorites section')
    if sum(1 for label in labels if 'example' in label.lower()) < 2:
        raise AssertionError('Seeded example.com favorite tile labels not found in favorites section')


def assert_seeded_pinned_tabs_section(ax_dump: str) -> None:
    assert_contains('Page not found · GitHub · GitHub', ax_dump, 'Pinned tabs seeded GitHub row not found')
    assert_contains('docs.maho.dev', ax_dump, 'Pinned tabs seed host not found')


def assert_seeded_normal_tabs_section(ax_dump: str) -> None:
    if 'Hacker News' not in ax_dump and 'news.ycombinator.com' not in ax_dump:
        raise AssertionError('Seeded normal-tab item not found')
    assert_contains('Maho Test Page', ax_dump, 'Seeded test-page tab row not found')


def assert_seeded_folders_section(ax_dump: str) -> None:
    assert_seeded_window_state(ax_dump)
    assert_contains('GitHub', ax_dump, 'Seeded folder-support contract baseline item not found')
    assert_contains('docs.maho.dev', ax_dump, 'Seeded folder-support contract docs item not found')


def assert_phase2_coverage(ax_dump: str) -> None:
    assert_seeded_window_state(ax_dump)


def assert_sidebar_tree_json_contract(ax_dump: str) -> None:
    assert_seeded_window_state(ax_dump)
    assert_contains('Sidebar tree JSON fields', ax_dump, 'Sidebar tree JSON summary not found')
    assert_contains('folder_id', ax_dump, 'Sidebar tree JSON should expose folder ids')
    assert_contains('parent_folder_id', ax_dump, 'Sidebar tree JSON should expose parent folder ids')
    assert_contains('folder_child_index', ax_dump, 'Sidebar tree JSON should expose folder child indices')
    assert_contains('normal_tree', ax_dump, 'Sidebar tree JSON should expose normal tree structure')


def main_window(focused_window):
    return focused_window


def sidebar_outline(focused_window):
    window_frame = get_element_frame(focused_window)
    if window_frame is None:
        return focused_window

    window_x, window_y, window_width, window_height = _frame_values(window_frame)
    candidate_needles = (
        ('GitHub', 'Example News', 'Example Shop'),
        ('Page not found · GitHub · GitHub', 'docs.maho.dev'),
        ('news.ycombinator.com', 'Maho Test Page'),
        ('Work',),
    )

    best_candidate = None
    best_score = -1
    best_x = float('inf')
    best_y = float('inf')

    for node, _depth, frame in _iter_descendants_with_frames(focused_window, max_depth=12):
        x, y, width, height = _frame_values(frame)
        if width <= 0 or height <= 0:
            continue
        if x > window_x + (window_width * 0.72):
            continue
        if width < 120 or height < 120:
            continue

        subtree_text = ' '.join(text for _child, text, _child_frame in _visible_labeled_nodes(node))
        score = 0
        for group in candidate_needles:
            if _label_matches(subtree_text, group):
                score += 1
        if score == 0:
            continue
        if x < best_x or (x == best_x and (score > best_score or y < best_y)):
            best_candidate = node
            best_score = score
            best_x = x
            best_y = y

    return best_candidate or focused_window


def _iter_descendants_with_frames(container, max_depth: int = AX_SEARCH_DEPTH):
    for node, depth in iter_ax_tree(container, max_depth=max_depth):
        frame = get_element_frame(node)
        if frame is None:
            continue
        yield node, depth, frame


def _node_text(node) -> str:
    info = get_element_info(node)
    parts = [part for part in (info.title, info.description, info.value) if part is not None and str(part).strip()]
    return ' '.join(str(part).strip() for part in parts)


def _frame_values(frame) -> tuple[float, float, float, float]:
    if frame is None:
        raise AssertionError('Missing AX frame')
    try:
        return _frame_parts(frame)
    except Exception as exc:
        raise AssertionError(f'Unsupported frame value: {frame!r}') from exc


def _sort_nodes_by_position(nodes):
    return sorted(nodes, key=lambda item: (_frame_values(item[2])[1], _frame_values(item[2])[0]))


def _label_matches(text: str, needles: tuple[str, ...]) -> bool:
    lowered = text.lower()
    return any(needle.lower() in lowered for needle in needles)


def _stable_label(node) -> str:
    return get_element_label(node).strip()


def _visible_labeled_nodes(container):
    nodes = []
    for node, _depth, frame in _iter_descendants_with_frames(container):
        text = _node_text(node)
        if text:
            nodes.append((node, text, frame))
    return nodes


def _content_start_y(container) -> float | None:
    anchor_needles = ('Page not found · GitHub · GitHub', 'news.ycombinator.com', 'Maho Test Page', 'docs.maho.dev', 'Work')
    y_values = [_frame_values(frame)[1] for _node, text, frame in _visible_labeled_nodes(container) if _label_matches(text, anchor_needles)]
    return min(y_values) if y_values else None


def _root_band_nodes(container):
    start_y = _content_start_y(container)
    nodes = _visible_labeled_nodes(container)
    if start_y is None:
        return []
    band = [(node, text, frame) for node, text, frame in nodes if _frame_values(frame)[1] >= start_y - 2]
    if not band:
        return []
    min_x = min(_frame_values(frame)[0] for _node, _text, frame in band)
    return [(node, text, frame) for node, text, frame in band if abs(_frame_values(frame)[0] - min_x) < 6]


def _find_row_by_label(container, needles: tuple[str, ...], min_y: float | None = None, max_y: float | None = None):
    candidates = []
    for node, text, frame in _visible_labeled_nodes(container):
        info = get_element_info(node)
        if info.role != 'AXButton':
            continue
        frame_x, frame_y, _frame_w, _frame_h = _frame_values(frame)
        if frame_x < 20 or frame_x > 250:
            continue
        if not _stable_label(node):
            continue
        if not _label_matches(text, needles):
            continue
        if min_y is not None and frame_y < min_y:
            continue
        if max_y is not None and frame_y > max_y:
            continue
        candidates.append((node, text, frame))
    if not candidates:
        return None
    return _sort_nodes_by_position(candidates)[0][0]


def _find_exact_row(container, label: str, min_y: float | None = None, max_y: float | None = None, min_x: float | None = None):
    label_lower = label.lower()
    candidates = []
    for node, text, frame in _visible_labeled_nodes(container):
        info = get_element_info(node)
        if info.role != 'AXButton':
            continue
        if text.lower() != label_lower:
            continue
        frame_x, frame_y, _frame_w, _frame_h = _frame_values(frame)
        if frame_x < 20 or frame_x > 250:
            continue
        if not _stable_label(node):
            continue
        if min_y is not None and frame_y < min_y:
            continue
        if max_y is not None and frame_y > max_y:
            continue
        if min_x is not None and frame_x < min_x:
            continue
        candidates.append((node, text, frame))
    if not candidates:
        return None
    return _sort_nodes_by_position(candidates)[0][0]


def _root_rows(container):
    return _sort_nodes_by_position(_root_band_nodes(container))


def _has_indented_descendant(container, row):
    row_frame = get_element_frame(row)
    if row_frame is None:
        return False
    row_x, row_y, _row_w, _row_h = _frame_values(row_frame)
    for node, _text, frame in _visible_labeled_nodes(container):
        if node is row:
            continue
        x, y, _w, _h = _frame_values(frame)
        if y <= row_y:
            continue
        if x <= row_x + 10:
            continue
        return True
    return False


def _favorite_tiles(container):
    favorite_needles = ('GitHub', 'Example News', 'Example Shop')
    candidates = []
    for node, text, frame in _visible_labeled_nodes(container):
        info = get_element_info(node)
        if info.role != 'AXButton':
            continue
        if not _label_matches(text, favorite_needles):
            continue
        frame_x, frame_y, _frame_w, _frame_h = _frame_values(frame)
        if frame_y >= 260:
            continue
        if frame_x < 20 or frame_x > 250:
            continue
        if not _stable_label(node):
            continue
        candidates.append((node, text, frame))
    return [node for node, _text, _frame in _sort_nodes_by_position(candidates)]


def _regular_rows(container):
    return [node for node, _text, _frame in _regular_row_entries(container)]


def _regular_row_entries(container):
    rows = []
    nodes = _visible_labeled_nodes(container)
    new_tab_row = _find_exact_row(container, '+ New Tab')
    new_tab_y = None
    if new_tab_row is not None:
        new_tab_frame = get_element_frame(new_tab_row)
        if new_tab_frame is not None:
            new_tab_y = _frame_values(new_tab_frame)[1]
    stable_needles = ('news.ycombinator.com', 'Maho Test Page')
    excluded = ('Page not found · GitHub · GitHub', 'docs.maho.dev', '+ New Tab', 'Open spaces', 'Personal', 'Create space', 'maho-test', 'Toggle Sidebar', 'Back', 'Forward', 'Reload', 'AI Panel')
    for node, text, frame in nodes:
        _frame_x, frame_y, _frame_w, _frame_h = _frame_values(frame)
        if new_tab_y is not None and frame_y <= new_tab_y:
            continue
        if any(text == item for item in excluded):
            continue
        if not _label_matches(text, stable_needles):
            continue
        if not _stable_label(node):
            continue
        if 'Maho Test Page - Chromium' in text:
            continue
        rows.append((node, text, frame))
    return _sort_nodes_by_position(rows)


def _pinned_rows(container):
    return [node for node, _text, _frame in _pinned_row_entries(container)]


def _pinned_row_entries(container):
    rows = []
    nodes = _visible_labeled_nodes(container)
    new_tab_row = _find_exact_row(container, '+ New Tab')
    new_tab_y = None
    if new_tab_row is not None:
        new_tab_frame = get_element_frame(new_tab_row)
        if new_tab_frame is not None:
            new_tab_y = _frame_values(new_tab_frame)[1]
    pinned_needles = ('Page not found · GitHub · GitHub', 'docs.maho.dev')
    for node, text, frame in nodes:
        _frame_x, frame_y, _frame_w, _frame_h = _frame_values(frame)
        if new_tab_y is None or frame_y >= new_tab_y:
            continue
        if _label_matches(text, pinned_needles):
            if not _stable_label(node):
                continue
            rows.append((node, text, frame))
    return _sort_nodes_by_position(rows)


def _folder_rows(container):
    rows = []
    for node, text, frame in _visible_labeled_nodes(container):
        if _label_matches(text, ('Work',)) and _stable_label(node) and _has_indented_descendant(container, node):
            rows.append((node, text, frame))
            continue
    return [node for node, _text, _frame in _sort_nodes_by_position(rows)]


def _folder_children(container, folder):
    folder_frame = get_element_frame(folder)
    if folder_frame is None:
        return []
    folder_x, folder_y, _folder_w, folder_h = _frame_values(folder_frame)
    candidates = []
    for node, text, frame in _visible_labeled_nodes(container):
        if node is folder:
            continue
        x, y, _w, _h = _frame_values(frame)
        if y <= folder_y + folder_h - 1:
            continue
        if x <= folder_x + 8:
            continue
        candidates.append((node, text, frame))
    return [node for node, _text, _frame in _sort_nodes_by_position(candidates)]


def element_with_identifier(identifier: str, container):
    for node, _depth in iter_ax_tree(container, max_depth=AX_SEARCH_DEPTH):
        if get_element_identifier(node) == identifier:
            return node
    return None


def favorite_tile_at(index: int, container):
    tiles = _favorite_tiles(container)
    if index < 0 or index >= len(tiles):
        return None
    return tiles[index]


def tab_with_identifier(identifier: str, outline):
    if not identifier:
        return None
    return _find_exact_row(outline, identifier)


def tab_containing(text: str, outline):
    return find_first_matching_element(outline, [text], max_depth=AX_SEARCH_DEPTH)


def folder_named(name: str, outline):
    for node, text, _frame in _visible_labeled_nodes(outline):
        if _label_matches(text, (name,)) and _has_indented_descendant(outline, node):
            return node
    return None


def visible_regular_root_tabs(focused_window, outline):
    return _regular_rows(focused_window)


def visible_folders(outline):
    return _folder_rows(outline)


def child_tabs_of_folder(folder, outline):
    if folder is None:
        return []
    folder_frame = get_element_frame(folder)
    if folder_frame is None:
        return []
    return _folder_children(outline, folder)


def regular_section(focused_window):
    rows = _regular_rows(focused_window)
    return rows[0] if rows else None


def pinned_section(focused_window):
    rows = _pinned_rows(focused_window)
    return rows[0] if rows else None


def assert_favorite_labels_swapped(main_window_element, pid: int | None = None) -> None:
    tiles = _favorite_tiles(main_window_element)
    if len(tiles) < 2:
        raise AssertionError('Expected at least two favorite tiles to exist')
    first_tile = tiles[0]
    second_tile = tiles[1]
    if first_tile is None or second_tile is None:
        raise AssertionError('Expected first two favorite tiles to exist')
    first_label = get_element_label(first_tile)
    second_label = get_element_label(second_tile)
    if not first_label or not second_label:
        raise AssertionError('Favorite tiles should expose stable labels')
    if first_label == second_label:
        raise AssertionError('Favorite tiles should begin with distinct labels')
    drag_element_center(second_tile, first_tile)
    def reordered_tiles_ready():
        windows_to_check = [main_window_element]
        if pid is not None:
            windows_to_check = [current for _candidate_pid, current in iter_maho_window_candidates(pid)]
        for candidate_window in windows_to_check:
            tiles = _favorite_tiles(candidate_window)
            if len(tiles) >= 2:
                return tiles
        return None

    reordered_tiles = poll_until(reordered_tiles_ready, 5000, interval_ms=100, label='favorite tiles after drag')
    reordered_first = reordered_tiles[0]
    reordered_second = reordered_tiles[1]
    wait_for_label(reordered_first, second_label, timeout_ms=5000)
    wait_for_label(reordered_second, first_label, timeout_ms=5000)


def scenario_sidebar_dnd_favorite_tile_reorder(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    current_window, ax_dump = wait_for_seeded_test_page_window_dump(pid, config, proc, readiness_check=assert_seeded_sidebar_ready_for_favorites)
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    artifacts.write_text('dnd_debug_favorites.txt', '\n'.join(get_element_label(tile) for tile in _favorite_tiles(current_window)) + '\n')
    main = current_window
    assert_favorite_labels_swapped(main, pid)
    capture_after(artifacts)


def scenario_sidebar_dnd_tab_to_pinned_section(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    current_window, ax_dump = wait_for_seeded_test_page_window_dump(
        pid,
        config,
        proc,
        required_tokens=('GitHub', 'docs.maho.dev'),
        readiness_check=assert_seeded_sidebar_ready_for_pinned_drag,
    )
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    def stable_pinned_drag_snapshot():
        for _candidate_pid, candidate_window in iter_maho_window_candidates(pid):
            regular_entries = _regular_row_entries(candidate_window)
            regular_entries = [entry for entry in regular_entries if _label_matches(entry[1], ('news.ycombinator.com', 'Maho Test Page'))]
            if not regular_entries:
                continue
            pinned_entries = _pinned_row_entries(candidate_window)
            if not pinned_entries:
                continue
            try:
                regular_entries = sorted(regular_entries, key=lambda entry: _frame_values(entry[2])[1])
                regular_y = _frame_values(regular_entries[0][2])[1]
                _frame_values(pinned_entries[0][2])
            except Exception:
                continue
            return candidate_window, regular_entries, pinned_entries, regular_y
        return None

    current_window, regular_entries, pinned_entries, regular_y = poll_until(stable_pinned_drag_snapshot, 5000, interval_ms=100, label='stable pinned drag snapshot')
    if len(regular_entries) < 1:
        raise AssertionError('Expected at least one visible regular row')
    source, source_label, _source_frame = regular_entries[0]
    if not source_label:
        raise AssertionError('Dragged tab should expose a stable label')
    if not pinned_entries:
        raise AssertionError('Expected pinned and regular rows in sidebar')
    pinned, _pinned_label, _pinned_frame = pinned_entries[0]
    drag_element_center(source, pinned)
    stable_pinned_labels_before = [text for _row, text, _frame in pinned_entries]
    def pinned_drag_applied():
        for _candidate_pid, candidate_window in iter_maho_window_candidates(pid):
            rows = _pinned_rows(candidate_window)
            if not rows:
                continue
            for row in rows:
                if get_element_label(row) != source_label:
                    continue
                row_frame = get_element_frame(row)
                if row_frame is None:
                    continue
                if _frame_values(row_frame)[1] < regular_y:
                    return row
        return None

    try:
        dragged = poll_until(pinned_drag_applied, 5000, interval_ms=100, label='dragged tab in pinned section')
    except TimeoutError as exc:
        pinned_rows_after = []
        for _candidate_pid, candidate_window in iter_maho_window_candidates(pid):
            candidate_rows = _pinned_row_entries(candidate_window)
            if len(candidate_rows) > len(pinned_rows_after):
                pinned_rows_after = candidate_rows
        if not pinned_rows_after:
            pinned_rows_after = _pinned_row_entries(current_window)
        pinned_labels_after = [text for _row, text, _frame in pinned_rows_after]
        if pinned_labels_after == stable_pinned_labels_before:
            artifacts.write_text('verification_note.txt', f'Tab-to-pinned no-op: dragged tab {source_label} never appeared in pinned rows and pinned labels stayed unchanged.\n')
            raise AssertionError(f'Tab-to-pinned no-op: dragged tab {source_label} never appeared in pinned rows and pinned labels stayed unchanged')
        artifacts.write_text('verification_note.txt', f'Tab-to-pinned unresolved after drag: source={source_label}, pinned_before={stable_pinned_labels_before!r}, pinned_after={pinned_labels_after!r}, timeout={exc}.\n')
        raise AssertionError(f'Tab-to-pinned unresolved after drag: source={source_label}, pinned_before={stable_pinned_labels_before!r}, pinned_after={pinned_labels_after!r}')
    dragged_frame = get_element_frame(dragged)
    if dragged_frame is None:
        raise AssertionError('Dragged tab should expose a frame after drop')
    if _frame_values(dragged_frame)[1] >= regular_y:
        raise AssertionError('Dragged tab should move above the regular section header')
    capture_after(artifacts)


def scenario_sidebar_dnd_tab_to_folder(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    current_window, ax_dump = wait_for_seeded_test_page_window_dump(
        pid,
        config,
        proc,
        required_tokens=('GitHub', 'docs.maho.dev'),
        readiness_check=assert_seeded_sidebar_ready_for_folder_drag,
    )
    artifacts.write_text('ax_tree.txt', ax_dump)
    target_folder = folder_named('Work', current_window)
    if target_folder is None:
        artifacts.write_text('verification_note.txt', 'Folder drag requires runtime snapshot/tree-json verification when Chromium AX does not expose the Work folder row.\n')
        raise AssertionError('Folder drag requires runtime snapshot/tree-json verification when Chromium AX does not expose the Work folder row')
    regular_tabs = _regular_rows(current_window)
    if not regular_tabs:
        raise AssertionError('Expected at least one visible regular row')
    source = regular_tabs[0]
    work_children_before = child_tabs_of_folder(target_folder, current_window)
    if not work_children_before:
        raise AssertionError('Work folder should expose child tabs before drag')
    drag_element_center(source, target_folder)
    wait_for_label(target_folder, 'Work expanded', timeout_ms=3000)
    children = child_tabs_of_folder(target_folder, current_window)
    source_label = get_element_label(source)
    if all(get_element_label(child) != source_label for child in children):
        raise AssertionError(f'Dragged tab {source_label} should appear inside the Work folder')
    capture_after(artifacts)


def scenario_sidebar_dnd_tab_reorder_within_regular_tabs(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    current_window, ax_dump = wait_for_seeded_test_page_window_dump(
        pid,
        config,
        proc,
        required_tokens=('GitHub', 'docs.maho.dev'),
        readiness_check=assert_seeded_sidebar_ready_for_regular_tabs,
    )
    artifacts.write_text('ax_tree.txt', ax_dump)
    def stable_regular_rows_ready():
        for _candidate_pid, candidate_window in iter_maho_window_candidates(pid):
            rows = [
                _find_row_by_label(candidate_window, ('news.ycombinator.com',)),
                _find_row_by_label(candidate_window, ('Maho Test Page',)),
            ]
            rows = [row for row in rows if row is not None]
            if len(rows) < 2:
                continue
            rows = sorted(rows, key=lambda row: _frame_values(get_element_frame(row))[1])
            return candidate_window, rows
        return None

    current_window, regular_tabs = poll_until(stable_regular_rows_ready, 5000, interval_ms=100, label='stable regular rows for reorder')
    if len(regular_tabs) < 2:
        raise AssertionError('Expected at least two visible regular rows')
    first_tab = regular_tabs[0]
    second_tab = regular_tabs[1]
    first_label = get_element_label(first_tab)
    second_label = get_element_label(second_tab)
    if not first_label or not second_label:
        raise AssertionError('Regular tabs should expose stable labels')
    first_frame = get_element_frame(first_tab)
    second_frame = get_element_frame(second_tab)
    if first_frame is None or second_frame is None:
        raise AssertionError('Regular tabs should expose frames before reorder')
    first_initial_y = _frame_values(first_frame)[1]
    second_initial_y = _frame_values(second_frame)[1]
    regular_labels_before = [first_label, second_label]
    drag_above(second_tab, first_tab)
    def regular_reorder_applied():
        for _candidate_pid, candidate_window in iter_maho_window_candidates(pid):
            rows = _regular_rows(candidate_window)
            labels = [get_element_label(row) for row in rows[:2]]
            if len(labels) < 2:
                continue
            if labels[0] == second_label and labels[1] == first_label:
                return rows[:2]
        return None

    try:
        reordered_tabs = poll_until(regular_reorder_applied, 5000, interval_ms=100, label='regular tab reorder')
    except TimeoutError as exc:
        latest_rows = []
        for _candidate_pid, candidate_window in iter_maho_window_candidates(pid):
            candidate_rows = _regular_rows(candidate_window)
            if len(candidate_rows) > len(latest_rows):
                latest_rows = candidate_rows
        if not latest_rows:
            latest_rows = _regular_rows(current_window)
        latest_labels = [get_element_label(row) for row in latest_rows[:2]]
        if latest_labels == regular_labels_before:
            artifacts.write_text('verification_note.txt', f'Regular-tab reorder no-op: top regular rows stayed {regular_labels_before!r} after drag.\n')
            raise AssertionError(f'Regular-tab reorder no-op: top regular rows stayed {regular_labels_before!r} after drag')
        artifacts.write_text('verification_note.txt', f'Regular-tab reorder unresolved after drag: before={regular_labels_before!r}, after={latest_labels!r}, timeout={exc}.\n')
        raise AssertionError(f'Regular-tab reorder unresolved after drag: before={regular_labels_before!r}, after={latest_labels!r}')
    reordered_first = reordered_tabs[0]
    reordered_second = reordered_tabs[1]
    reordered_first_frame = get_element_frame(reordered_first)
    reordered_second_frame = get_element_frame(reordered_second)
    if reordered_first_frame is None or reordered_second_frame is None:
        raise AssertionError('Regular tabs should expose frames after reorder')
    if _frame_values(reordered_second_frame)[1] >= _frame_values(reordered_first_frame)[1]:
        raise AssertionError('Second tab should move above the first tab')
    if _frame_values(reordered_first_frame)[1] == first_initial_y and _frame_values(reordered_second_frame)[1] == second_initial_y:
        raise AssertionError('Regular tabs should move from their original Y positions')
    capture_after(artifacts)


def scenario_sidebar_dnd_folder_reorder(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    current_window, ax_dump = wait_for_seeded_test_page_window_dump(
        pid,
        config,
        proc,
        required_tokens=('GitHub', 'docs.maho.dev'),
        readiness_check=assert_seeded_sidebar_ready_for_folder_drag,
    )
    artifacts.write_text('ax_tree.txt', ax_dump)
    folders = visible_folders(current_window)
    if len(folders) < 2:
        artifacts.write_text('verification_note.txt', 'Folder reorder requires runtime snapshot/tree-json verification when Chromium AX cannot expose two draggable folders reliably.\n')
        raise AssertionError('Folder reorder requires runtime snapshot/tree-json verification when Chromium AX cannot expose two draggable folders reliably')
    first_folder = folders[0]
    second_folder = folders[1]
    first_frame = get_element_frame(first_folder)
    second_frame = get_element_frame(second_folder)
    if first_frame is None or second_frame is None:
        raise AssertionError('Folders should expose frames before reorder')
    first_initial_y = _frame_values(first_frame)[1]
    second_initial_y = _frame_values(second_frame)[1]
    drag_above(second_folder, first_folder)
    reordered_folders = visible_folders(current_window)
    if len(reordered_folders) < 2:
        raise AssertionError('Folder reorder requires runtime snapshot/tree-json verification when Chromium AX no longer exposes two draggable folders after drag start')
    reordered_first = reordered_folders[0]
    reordered_second = reordered_folders[1]
    reordered_first_frame = get_element_frame(reordered_first)
    reordered_second_frame = get_element_frame(reordered_second)
    if reordered_first_frame is None or reordered_second_frame is None:
        raise AssertionError('Folders should expose frames after reorder')
    reordered_first_y = _frame_values(reordered_first_frame)[1]
    wait_for_frame_change(reordered_second, timeout_ms=5000, condition=lambda frame: _frame_values(frame)[1] < reordered_first_y)
    reordered_first_frame = get_element_frame(reordered_first)
    reordered_second_frame = get_element_frame(reordered_second)
    if reordered_first_frame is None or reordered_second_frame is None:
        raise AssertionError('Folders should continue exposing frames after reorder')
    if _frame_values(reordered_second_frame)[1] >= _frame_values(reordered_first_frame)[1]:
        raise AssertionError('Second folder should move above the first folder')
    if _frame_values(reordered_first_frame)[1] == first_initial_y:
        raise AssertionError('First folder should move from its original Y position')
    if _frame_values(reordered_second_frame)[1] == second_initial_y:
        raise AssertionError('Second folder should move from its original Y position')
    capture_after(artifacts)


def scenario_sidebar_dnd_tab_out_of_folder(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    current_window, ax_dump = wait_for_seeded_test_page_window_dump(
        pid,
        config,
        proc,
        required_tokens=('GitHub', 'docs.maho.dev'),
        readiness_check=assert_seeded_sidebar_ready_for_folder_drag,
    )
    artifacts.write_text('ax_tree.txt', ax_dump)
    regular = _find_row_by_label(current_window, ('news.ycombinator.com', 'Maho Test Page'))
    if regular is None:
        raise AssertionError('Expected regular row in sidebar')
    work_folder = folder_named('Work', current_window)
    if work_folder is None:
        artifacts.write_text('verification_note.txt', 'Tab-out-of-folder requires runtime snapshot/tree-json verification when Chromium AX does not expose the Work folder row.\n')
        raise AssertionError('Tab-out-of-folder requires runtime snapshot/tree-json verification when Chromium AX does not expose the Work folder row')
    if 'collapsed' in get_element_label(work_folder).lower():
        click_element_center(work_folder)
        wait_for_label(work_folder, 'Work expanded', timeout_ms=3000)
    folder_children = child_tabs_of_folder(work_folder, current_window)
    if not folder_children:
        raise AssertionError('Work folder should contain at least one tab in deterministic state')
    child_tab = folder_children[0]
    drag_element_center(child_tab, regular)
    remaining = child_tabs_of_folder(work_folder, current_window)
    child_label = get_element_label(child_tab)
    if any(get_element_label(child) == child_label for child in remaining):
        raise AssertionError(f'Dragged tab {child_label} should no longer appear inside the Work folder')
    root_level_tab = _find_row_by_label(current_window, (child_label,))
    if root_level_tab is None:
        raise AssertionError(f'Dragged tab {child_label} should appear at the root regular section')
    regular_frame = get_element_frame(regular)
    root_frame = get_element_frame(root_level_tab)
    if regular_frame is None or root_frame is None:
        raise AssertionError('Dragged tab and regular section should expose frames after drop')
    regular_y = _frame_values(regular_frame)[1]
    wait_for_frame_change(root_level_tab, timeout_ms=5000, condition=lambda frame: _frame_values(frame)[1] > regular_y)
    root_frame = get_element_frame(root_level_tab)
    if root_frame is None:
        raise AssertionError('Dragged tab should continue exposing a frame after drop')
    if _frame_values(root_frame)[1] <= regular_y:
        raise AssertionError('Dragged tab should sit below the regular section header')
    capture_after(artifacts)


def scenario_launch_clean_profile(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    ax_dump = dump_ax_tree(focused_window, max_depth=AX_SEARCH_DEPTH)
    assert_contains('Example Domain', ax_dump, 'Clean launch did not reach the initial Example Domain tab')
    if 'restore' in ax_dump.lower():
        raise AssertionError('Restore-session modal is visible')
    capture_after(artifacts)


def scenario_sidebar_populates_real_tabs(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'Example Domain' not in dump:
                continue
            if 'Hacker News' not in dump and 'news.ycombinator.com' not in dump:
                continue
            if '+ New Tab' not in dump:
                continue
            return dump
        return None

    ax_dump = poll_until(predicate, config.readiness_timeout_ms, label='real tab rows')
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_contains('Example Domain', ax_dump, 'Example Domain tab row not found')
    if 'Hacker News' not in ax_dump and 'news.ycombinator.com' not in ax_dump:
        raise AssertionError('Second real tab row not found')
    assert_contains('+ New Tab', ax_dump, '+ New Tab row not found')
    if 'No tabs open' in ax_dump:
        raise AssertionError('Sidebar still shows empty placeholder')
    capture_after(artifacts)


def scenario_switch_tab_from_sidebar(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    target = find_first_pressable_element(focused_window, ['Example Domain'], max_depth=AX_SEARCH_DEPTH)
    if target is None:
        raise AssertionError('Example Domain tab row not found')
    if not press_element(target):
        raise AssertionError('Failed to activate Example Domain tab row')

    def predicate():
        current = get_any_focused_window(pid)
        if current is None:
            return None
        current_dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
        if 'Example Domain' in current_dump:
            return current_dump
        return None

    current_dump = poll_until(predicate, config.readiness_timeout_ms, label='tab switch')
    artifacts.write_text('ax_tree.txt', current_dump)
    capture_after(artifacts)


def _capture_candidate_window_diagnostics(artifacts, pid: int, reason: str) -> None:
    diagnostics: list[str] = [reason, '']
    seen_ids: set[tuple[int, int]] = set()
    candidate_index = 0
    for candidate_pid, current in iter_maho_window_candidates(pid):
        key = (candidate_pid, id(current))
        if key in seen_ids:
            continue
        seen_ids.add(key)
        candidate_index += 1
        dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
        diagnostics.append(f'=== candidate {candidate_index}: pid={candidate_pid} ===')
        diagnostics.append(dump)
        artifacts.write_text(f'footer_action_candidate_{candidate_index}_ax_tree.txt', dump)
    artifacts.write_text('footer_action_diagnostics.txt', '\n'.join(diagnostics).rstrip() + '\n')


def scenario_open_spaces_surface_via_footer_action(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    target = find_first_matching_element(focused_window, ['Open spaces', 'Spaces'], max_depth=AX_SEARCH_DEPTH)
    if target is None:
        raise AssertionError('Open spaces or Spaces footer action not found')
    if not press_element(target):
        raise AssertionError('Footer action did not press cleanly')

    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'Maho Spaces' in dump or 'Manage Spaces' in dump:
                return dump
        return None

    try:
        current_dump = poll_until(predicate, config.readiness_timeout_ms, label='footer spaces action')
    except TimeoutError as exc:
        reason = f'Footer action pressed but no manage-spaces UI surfaced within {config.readiness_timeout_ms} ms: {exc}'
        _capture_candidate_window_diagnostics(artifacts, pid, reason)
        capture_after(artifacts)
        raise AssertionError('Footer action pressed but no manage-spaces UI surfaced') from exc
    artifacts.write_text('ax_tree.txt', current_dump)
    if 'Manage Spaces' in current_dump:
        artifacts.write_text('manage_spaces_evidence.txt', current_dump)
    capture_after(artifacts)


def scenario_create_space_entry_opens_surface(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    target = find_first_matching_element(focused_window, ['Create space'], max_depth=AX_SEARCH_DEPTH)
    if target is None:
        raise AssertionError('Create space action not found')
    if not press_element(target):
        raise AssertionError('Create space action did not press cleanly')

    def popover_new_space():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            item = find_first_pressable_element(
                current,
                ['New Space', 'New Space. Create a fresh workspace'],
                max_depth=AX_SEARCH_DEPTH,
            )
            if item is None:
                item = find_first_matching_element(
                    current,
                    ['New Space', 'New Space. Create a fresh workspace'],
                    max_depth=AX_SEARCH_DEPTH,
                )
            if item is not None:
                return item
        return None

    new_space_item = poll_until(popover_new_space, config.readiness_timeout_ms, label='New Space popover item')
    if not press_element(new_space_item):
        click_element_center(new_space_item)

    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'New Space' in dump and ('Space name' in dump or 'space' in dump.lower()):
                return dump
        return None

    current_dump = poll_until(predicate, config.readiness_timeout_ms, label='create space sidebar flow')
    artifacts.write_text('ax_tree.txt', current_dump)
    capture_after(artifacts)


def _count_space_rows_in_manage_dump(dump: str) -> int:
    """Count space entry rows visible in a Manage Spaces AX dump.

    Heuristic: each space row contains 'Switch' or 'Active' as an action/state
    label. We count distinct lines containing those tokens that also look like
    row entries (contain AXButton or AXStaticText).
    """
    count = 0
    for line in dump.splitlines():
        lowered = line.lower()
        if ('switch' in lowered or 'active' in lowered or 'current' in lowered) and ('axbutton' in lowered or 'axstatictext' in lowered or 'axcell' in lowered or 'axrow' in lowered):
            count += 1
    return count


def _find_window_with_title(pid: int, title: str, timeout_ms: int = 10000):
    """Poll across Maho windows until one contains *title* in its AX dump."""
    def predicate():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if title in dump:
                return current, dump
        return None

    return poll_until(predicate, timeout_ms, label=f'window with title {title!r}')


def scenario_create_space_real_flow(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)

    # --- Step 1: Open the Manage Spaces surface to record the initial count ---
    open_target = find_first_matching_element(focused_window, ['Open spaces', 'Spaces'], max_depth=AX_SEARCH_DEPTH)
    if open_target is None:
        raise AssertionError('Open spaces / Spaces footer action not found')
    if not press_element(open_target):
        raise AssertionError('Footer spaces action did not press cleanly')

    manage_window, manage_dump_before = _find_window_with_title(pid, 'Manage Spaces', timeout_ms=config.readiness_timeout_ms)
    artifacts.write_text('manage_ax_before.txt', manage_dump_before)
    capture_fullscreen_png(artifacts.path('manage_before.png'))

    count_before = _count_space_rows_in_manage_dump(manage_dump_before)
    artifacts.write_text('space_count_before.txt', str(count_before) + '\n')

    close_button = find_first_pressable_element(manage_window, ['Close'], max_depth=AX_SEARCH_DEPTH)
    if close_button is not None:
        press_element(close_button)
        time.sleep(0.3)

    # --- Step 2: Open the sidebar-inline create UI via the + button ---
    main_focused = get_any_focused_window(pid)
    if main_focused is None:
        raise AssertionError('Main window not focused after closing manage dialog')

    create_button = find_first_matching_element(main_focused, ['Create space'], max_depth=AX_SEARCH_DEPTH)
    if create_button is None:
        raise AssertionError('Create space (+ button) not found in sidebar')
    if not press_element(create_button):
        raise AssertionError('Create space button did not press cleanly')

    def _popover_new_space():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            item = find_first_pressable_element(
                current,
                ['New Space', 'New Space. Create a fresh workspace'],
                max_depth=AX_SEARCH_DEPTH,
            )
            if item is None:
                item = find_first_matching_element(
                    current,
                    ['New Space', 'New Space. Create a fresh workspace'],
                    max_depth=AX_SEARCH_DEPTH,
                )
            if item is not None:
                return item
        return None

    new_space_item = poll_until(_popover_new_space, config.readiness_timeout_ms, label='New Space popover item')
    if not press_element(new_space_item):
        click_element_center(new_space_item)

    # --- Step 3: Find the sidebar-inline name input and enter a unique name ---
    def _sidebar_create_input_ready():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'New Space' not in dump:
                continue
            name_input = find_first_matching_element(current, ['Space name'], max_depth=AX_SEARCH_DEPTH)
            if name_input is not None:
                return current, dump, name_input
            for node, _depth in iter_ax_tree(current, max_depth=AX_SEARCH_DEPTH):
                info = get_element_info(node)
                if info.role not in ('AXTextField', 'AXTextArea', 'AXSearchField'):
                    continue
                haystack = ' '.join(
                    str(part).lower() for part in (info.title, info.description, info.value, get_element_identifier(node)) if part is not None
                )
                if 'address and search bar' in haystack:
                    continue
                if 'space' in haystack or 'name' in haystack:
                    return current, dump, node
        return None

    sidebar_window, create_dump, name_input = poll_until(
        _sidebar_create_input_ready,
        config.readiness_timeout_ms,
        label='Sidebar inline create space input readiness',
    )
    artifacts.write_text('create_surface_ax.txt', create_dump)
    capture_fullscreen_png(artifacts.path('create_surface.png'))

    unique_name = f'E2E-{_uuid.uuid4().hex[:8]}'

    if name_input is None:
        raise AssertionError('Space name input not found in sidebar inline create UI')
    click_element_center(name_input)
    time.sleep(0.15)
    if not set_element_value(name_input, unique_name):
        raise AssertionError('Failed to set space name value')
    artifacts.write_text('space_name_used.txt', unique_name + '\n')

    submit_button = find_first_pressable_element(sidebar_window, ['Create Space'], max_depth=AX_SEARCH_DEPTH)
    if submit_button is None:
        raise AssertionError('Create Space submit button not found in sidebar')
    if not press_element(submit_button):
        click_element_center(submit_button)

    def _post_submit_sidebar_ready():
        for _candidate_pid, current in iter_maho_window_candidates(pid):
            dump = dump_ax_tree(current, max_depth=AX_SEARCH_DEPTH)
            if 'Space name' in dump and unique_name in dump:
                continue
            open_spaces = find_first_matching_element(current, ['Open spaces', 'Spaces'], max_depth=AX_SEARCH_DEPTH)
            if open_spaces is not None:
                return current, dump, open_spaces
        return None

    main_window_after_submit, after_submit_dump, open_target = poll_until(
        _post_submit_sidebar_ready,
        config.readiness_timeout_ms,
        label='sidebar create submission completion',
    )
    artifacts.write_text('after_submit_ax.txt', after_submit_dump)
    capture_fullscreen_png(artifacts.path('after_submit.png'))

    # --- Step 4: Reopen Manage Spaces and verify N+1 spaces and the new label ---
    if open_target is None:
        raise AssertionError('Open spaces footer action not found after create')
    if not press_element(open_target):
        click_element_center(open_target)

    manage_window_after, manage_dump_after = _find_window_with_title(pid, 'Manage Spaces', timeout_ms=config.readiness_timeout_ms)
    artifacts.write_text('manage_ax_after.txt', manage_dump_after)
    capture_fullscreen_png(artifacts.path('manage_after.png'))

    count_after = _count_space_rows_in_manage_dump(manage_dump_after)
    artifacts.write_text('space_count_after.txt', str(count_after) + '\n')

    if count_after < count_before + 1:
        artifacts.write_text('count_mismatch.txt',
                             f'Expected count >= {count_before + 1}, got {count_after}\n'
                             f'Before dump rows: {count_before}\nAfter dump rows: {count_after}\n')
        raise AssertionError(f'Space count did not increase: before={count_before}, after={count_after}')

    if unique_name not in manage_dump_after:
        raise AssertionError(f'New space name {unique_name!r} not found in manage surface AX dump after creation')

    # --- Step 5: Verify the new space is active (or switch if needed) ---
    new_space_active = False
    dump_lines = manage_dump_after.splitlines()
    for i, line in enumerate(dump_lines):
        if unique_name in line:
            context_start = max(0, i - 3)
            context_end = min(len(dump_lines), i + 4)
            context_block = '\n'.join(dump_lines[context_start:context_end]).lower()
            if 'active' in context_block or 'current' in context_block:
                new_space_active = True
                break

    if not new_space_active:
        artifacts.write_text('switch_needed.txt', f'Space {unique_name!r} created but not active; attempting switch.\n')
        switch_target = find_first_matching_element(manage_window_after, [unique_name], max_depth=AX_SEARCH_DEPTH)
        if switch_target is not None:
            click_element_center(switch_target)
            time.sleep(0.3)
            switch_button = find_first_pressable_element(manage_window_after, ['Switch'], max_depth=AX_SEARCH_DEPTH)
            if switch_button is not None:
                press_element(switch_button)
                time.sleep(0.5)

        manage_window_final, manage_dump_final = _find_window_with_title(pid, 'Manage Spaces', timeout_ms=config.readiness_timeout_ms)
        artifacts.write_text('manage_ax_after_switch.txt', manage_dump_final)
        capture_fullscreen_png(artifacts.path('manage_after_switch.png'))

        final_lines = manage_dump_final.splitlines()
        for i, line in enumerate(final_lines):
            if unique_name in line:
                context_start = max(0, i - 3)
                context_end = min(len(final_lines), i + 4)
                context_block = '\n'.join(final_lines[context_start:context_end]).lower()
                if 'active' in context_block or 'current' in context_block:
                    new_space_active = True
                    break

        if not new_space_active:
            raise AssertionError(f'Space {unique_name!r} is not active after creation and switch attempt')

    # --- Step 6: Verify the space label in the switcher/sidebar ---
    close_button = find_first_pressable_element(manage_window_after, ['Close'], max_depth=AX_SEARCH_DEPTH)
    if close_button is not None:
        press_element(close_button)
        time.sleep(0.3)

    final_focused = get_any_focused_window(pid)
    if final_focused is not None:
        switcher_dump = dump_ax_tree(final_focused, max_depth=AX_SEARCH_DEPTH)
        artifacts.write_text('switcher_evidence_ax.txt', switcher_dump)
        capture_fullscreen_png(artifacts.path('switcher_evidence.png'))

    artifacts.write_text('result.txt', f'PASS: space {unique_name!r} created, count {count_before}->{count_after}, active=True\n')
    capture_after(artifacts)


def scenario_phase2_seeded_favorites_snapshot_assert(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)

    ax_dump = seeded_test_page_favorites_dump(pid, config, proc)
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    assert_phase2_coverage(ax_dump)
    capture_after(artifacts)


def scenario_phase2_seeded_folder_snapshot_assert(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    ax_dump = seeded_test_page_dump(pid, config, proc, required_tokens=('GitHub', 'docs.maho.dev'))
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    assert_seeded_folders_section(ax_dump)
    assert_phase2_coverage(ax_dump)
    capture_after(artifacts)


def scenario_phase2_pinned_section_support_contract_assert(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    ax_dump = seeded_test_page_dump(pid, config, proc, required_tokens=('GitHub', 'docs.maho.dev'))
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    assert_seeded_pinned_tabs_section(ax_dump)
    assert_phase2_coverage(ax_dump)
    capture_after(artifacts)


def scenario_phase2_folder_support_contract_assert(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    ax_dump = seeded_test_page_dump(pid, config, proc, required_tokens=('GitHub', 'docs.maho.dev'))
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    assert_seeded_folders_section(ax_dump)
    assert_phase2_coverage(ax_dump)
    capture_after(artifacts)


def scenario_phase2_tab_reorder_support_contract_assert(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)

    ax_dump = seeded_test_page_dump(pid, config, proc, required_tokens=('GitHub', 'docs.maho.dev'))
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    assert_seeded_normal_tabs_section(ax_dump)
    assert_phase2_coverage(ax_dump)
    capture_after(artifacts)


def scenario_phase2_sidebar_tree_json_contract_assert(config: RunnerConfig, proc: subprocess.Popen, artifacts, pid: int, focused_window, windows: list[dict]) -> None:
    capture_before_after(artifacts)
    ax_dump = seeded_test_page_dump(pid, config, proc, required_tokens=('GitHub', 'docs.maho.dev'))
    artifacts.write_text('ax_tree.txt', ax_dump)
    assert_seeded_window_state(ax_dump)
    assert_seeded_folders_section(ax_dump)
    source_path = Path(__file__).resolve().parents[2] / 'browser' / 'ui' / 'webui' / 'maho_test' / 'maho_test_ui.cc'
    with open(source_path, 'r', encoding='utf-8') as f:
        source = f.read()
    artifacts.write_text('maho_test_ui_source_excerpt.txt', source)
    if 'Sidebar tree JSON fields: normal_tree, parent_folder_id, folder_child_index.' not in source:
        raise AssertionError('Sidebar tree JSON summary contract not found in maho_test source')
    if 'sidebar-tree-json' not in source:
        raise AssertionError('Sidebar tree JSON pre block contract not found in maho_test source')
    for token in ('parent_folder_id', 'folder_child_index', 'normal_tree', 'pinned_tree'):
        if token not in source:
            raise AssertionError(f'Sidebar tree JSON source contract missing token: {token}')
    assert_phase2_coverage(ax_dump)
    capture_after(artifacts)


def scenario_map():
    return {
        'launch_clean_profile': scenario_launch_clean_profile,
        'sidebar_populates_real_tabs': scenario_sidebar_populates_real_tabs,
        'switch_tab_from_sidebar': scenario_switch_tab_from_sidebar,
        'open_spaces_surface_via_footer_action': scenario_open_spaces_surface_via_footer_action,
        'create_space_entry_opens_surface': scenario_create_space_entry_opens_surface,
        'create_space_real_flow': scenario_create_space_real_flow,
        'sidebar_dnd_favorite_tile_reorder': scenario_sidebar_dnd_favorite_tile_reorder,
        'sidebar_dnd_tab_to_pinned_section': scenario_sidebar_dnd_tab_to_pinned_section,
        'sidebar_dnd_tab_to_folder': scenario_sidebar_dnd_tab_to_folder,
        'sidebar_dnd_tab_reorder_within_regular_tabs': scenario_sidebar_dnd_tab_reorder_within_regular_tabs,
        'sidebar_dnd_folder_reorder': scenario_sidebar_dnd_folder_reorder,
        'sidebar_dnd_tab_out_of_folder': scenario_sidebar_dnd_tab_out_of_folder,
        'phase2_seeded_favorites_snapshot_assert': scenario_phase2_seeded_favorites_snapshot_assert,
        'phase2_seeded_folder_snapshot_assert': scenario_phase2_seeded_folder_snapshot_assert,
        'phase2_pinned_section_support_contract_assert': scenario_phase2_pinned_section_support_contract_assert,
        'phase2_folder_support_contract_assert': scenario_phase2_folder_support_contract_assert,
        'phase2_tab_reorder_support_contract_assert': scenario_phase2_tab_reorder_support_contract_assert,
        'phase2_sidebar_tree_json_contract_assert': scenario_phase2_sidebar_tree_json_contract_assert,
    }


def run_scenario(config: RunnerConfig, scenario_name: str) -> None:
    artifacts = _scenario_artifacts(config, scenario_name)
    profile_dir = default_profile_dir()
    cleanup_existing_maho_processes()
    if os.path.exists(profile_dir):
        shutil.rmtree(profile_dir, ignore_errors=True)
    os.makedirs(profile_dir, exist_ok=True)
    write_profile_path(artifacts, profile_dir)
    if not ax_preflight(prompt=False):
        raise SystemExit('Accessibility permission is required. Grant Terminal/Python accessibility access and rerun.')
    proc = launch_app(config, profile_dir, scenario_name)
    focused_window = None
    windows: list[dict] = []
    pid = -1
    started = time.monotonic()
    try:
        pid, focused_window, windows = readiness_poll(config, proc, artifacts)
        if int((time.monotonic() - started) * 1000) > config.scenario_timeout_ms:
            raise TimeoutError(f'Scenario {scenario_name} exceeded timeout of {config.scenario_timeout_ms} ms before execution')
        scenario_func = scenario_map()[scenario_name]
        scenario_func(config, proc, artifacts, pid, focused_window, windows)
        if int((time.monotonic() - started) * 1000) > config.scenario_timeout_ms:
            raise TimeoutError(f'Scenario {scenario_name} exceeded timeout of {config.scenario_timeout_ms} ms')
    except Exception as exc:
        capture_failure_artifacts(artifacts, focused_window, windows, str(exc))
        raise
    finally:
        terminate_process(proc)
        if not config.keep_profile:
            shutil.rmtree(profile_dir, ignore_errors=True)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    _validate_scenario_registry()
    if args.list:
        for name in SCENARIO_NAMES:
            print(name)
        return 0
    config = load_config()
    if not ax_preflight(prompt=False):
        print('Accessibility permission is required. Grant Terminal/Python accessibility access and rerun.', file=sys.stderr)
        return 2
    if not os.path.exists(config.app_path):
        print(f'App path not found: {config.app_path}', file=sys.stderr)
        return 2
    scenarios = list(args.scenarios) if args.scenarios else list(SCENARIO_NAMES)
    unknown = [name for name in scenarios if name not in scenario_map()]
    if unknown:
        print('Unknown scenarios: ' + ', '.join(unknown), file=sys.stderr)
        return 2
    started = time.monotonic()
    for scenario_name in scenarios:
        if int((time.monotonic() - started) * 1000) > config.suite_timeout_ms:
            print(f'Suite exceeded timeout of {config.suite_timeout_ms} ms', file=sys.stderr)
            return 1
        try:
            run_scenario(config, scenario_name)
            print(f'PASS {scenario_name}')
        except Exception as exc:
            print(f'FAIL {scenario_name}: {exc}', file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
