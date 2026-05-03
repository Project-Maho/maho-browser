#!/usr/bin/env python3
"""macOS Accessibility helpers for Chromium Maho E2E."""

import time
import re
from dataclasses import dataclass
from typing import Any, Sequence

import ApplicationServices as _AS
import Foundation as _FD
import Quartz as _Q

AXIsProcessTrusted = getattr(_AS, 'AXIsProcessTrusted')
AXIsProcessTrustedWithOptions = getattr(_AS, 'AXIsProcessTrustedWithOptions')
AXUIElementCopyActionNames = getattr(_AS, 'AXUIElementCopyActionNames')
AXUIElementCopyAttributeNames = getattr(_AS, 'AXUIElementCopyAttributeNames')
AXUIElementCopyAttributeValue = getattr(_AS, 'AXUIElementCopyAttributeValue')
AXUIElementCopyParameterizedAttributeNames = getattr(_AS, 'AXUIElementCopyParameterizedAttributeNames')
AXUIElementCreateApplication = getattr(_AS, 'AXUIElementCreateApplication')
AXUIElementCreateSystemWide = getattr(_AS, 'AXUIElementCreateSystemWide')
AXUIElementPerformAction = getattr(_AS, 'AXUIElementPerformAction')
AXUIElementSetAttributeValue = getattr(_AS, 'AXUIElementSetAttributeValue')
kAXChildrenAttribute = getattr(_AS, 'kAXChildrenAttribute', 'AXChildren')
kAXDescriptionAttribute = getattr(_AS, 'kAXDescriptionAttribute', 'AXDescription')
kAXErrorSuccess = getattr(_AS, 'kAXErrorSuccess')
kAXIdentifierAttribute = getattr(_AS, 'kAXIdentifierAttribute', 'AXIdentifier')
kAXFrameAttribute = getattr(_AS, 'kAXFrameAttribute', 'AXFrame')
kAXFocusedApplicationAttribute = getattr(_AS, 'kAXFocusedApplicationAttribute', 'AXFocusedApplication')
kAXFocusedWindowAttribute = getattr(_AS, 'kAXFocusedWindowAttribute', 'AXFocusedWindow')
kAXRoleAttribute = getattr(_AS, 'kAXRoleAttribute', 'AXRole')
kAXTitleAttribute = getattr(_AS, 'kAXTitleAttribute', 'AXTitle')
kAXTrustedCheckOptionPrompt = getattr(_AS, 'kAXTrustedCheckOptionPrompt', 'AXTrustedCheckOptionPrompt')
kAXValueAttribute = getattr(_AS, 'kAXValueAttribute', 'AXValue')
NSDictionary = getattr(_FD, 'NSDictionary')
CGEventCreateMouseEvent = getattr(_Q, 'CGEventCreateMouseEvent')
CGEventCreateKeyboardEvent = getattr(_Q, 'CGEventCreateKeyboardEvent')
CGEventPost = getattr(_Q, 'CGEventPost')
CGEventSourceCreate = getattr(_Q, 'CGEventSourceCreate')
CGDisplayBounds = getattr(_Q, 'CGDisplayBounds')
CGGetActiveDisplayList = getattr(_Q, 'CGGetActiveDisplayList')
CGMainDisplayID = getattr(_Q, 'CGMainDisplayID')
kCGEventKeyDown = getattr(_Q, 'kCGEventKeyDown')
kCGEventKeyUp = getattr(_Q, 'kCGEventKeyUp')
kCGEventMouseMoved = getattr(_Q, 'kCGEventMouseMoved')
kCGEventLeftMouseDown = getattr(_Q, 'kCGEventLeftMouseDown')
kCGEventLeftMouseDragged = getattr(_Q, 'kCGEventLeftMouseDragged')
kCGEventLeftMouseUp = getattr(_Q, 'kCGEventLeftMouseUp')
kCGEventRightMouseDown = getattr(_Q, 'kCGEventRightMouseDown')
kCGEventRightMouseUp = getattr(_Q, 'kCGEventRightMouseUp')
kVK_Return = 36
kCGEventSourceStateHIDSystemState = getattr(_Q, 'kCGEventSourceStateHIDSystemState', None)
kCGHIDEventTap = getattr(_Q, 'kCGHIDEventTap')
kCGMouseButtonLeft = getattr(_Q, 'kCGMouseButtonLeft')
kCGMouseButtonRight = getattr(_Q, 'kCGMouseButtonRight')
NSMakePoint = getattr(_FD, 'NSMakePoint')


@dataclass(frozen=True)
class AXElementInfo:
    role: str | None
    title: str | None
    description: str | None
    value: Any
    frame: Any

    def short_label(self) -> str:
        parts = [p for p in (self.title, self.description, self.role) if p]
        return ' | '.join(parts) if parts else '<unknown>'


def ax_preflight(prompt: bool = False) -> bool:
    try:
        options = NSDictionary.dictionaryWithObject_forKey_(bool(prompt), kAXTrustedCheckOptionPrompt)
        return bool(AXIsProcessTrustedWithOptions(options))
    except Exception:
        return bool(AXIsProcessTrusted())


def _copy_value(element, attribute: str):
    error_code, value = AXUIElementCopyAttributeValue(element, attribute, None)
    if error_code != kAXErrorSuccess:
        return None
    return value


def _iter_children(element) -> list[Any]:
    children = _copy_value(element, kAXChildrenAttribute)
    if not children:
        return []
    try:
        return list(children)
    except TypeError:
        return [children]


def get_element_info(element) -> AXElementInfo:
    role = _copy_value(element, kAXRoleAttribute)
    title = _copy_value(element, kAXTitleAttribute)
    description = _copy_value(element, kAXDescriptionAttribute)
    value = _copy_value(element, kAXValueAttribute)
    frame = _copy_value(element, kAXFrameAttribute)
    return AXElementInfo(role=role, title=title, description=description, value=value, frame=frame)


def get_element_identifier(element) -> str | None:
    identifier = _copy_value(element, kAXIdentifierAttribute)
    if identifier is None:
        return None
    return str(identifier)


def get_element_label(element) -> str:
    info = get_element_info(element)
    for candidate in (info.title, info.description, info.value, get_element_identifier(element)):
        if candidate is None:
            continue
        text = str(candidate).strip()
        if text:
            return text
    return ''


def get_element_frame(element):
    return _copy_value(element, kAXFrameAttribute)


def _frame_parts(frame) -> tuple[float, float, float, float]:
    if frame is None:
        raise TypeError('Missing AX frame')
    if hasattr(frame, 'origin') and hasattr(frame, 'size'):
        return float(frame.origin.x), float(frame.origin.y), float(frame.size.width), float(frame.size.height)
    if isinstance(frame, (tuple, list)) and len(frame) == 2:
        origin, size = frame
        if isinstance(origin, (tuple, list)) and isinstance(size, (tuple, list)) and len(origin) == 2 and len(size) == 2:
            return float(origin[0]), float(origin[1]), float(size[0]), float(size[1])
    text = str(frame)
    match = re.search(
        r'x\s*[:=]\s*(?P<x>-?\d+(?:\.\d+)?)\D+'
        r'y\s*[:=]\s*(?P<y>-?\d+(?:\.\d+)?)\D+'
        r'w\s*[:=]\s*(?P<w>-?\d+(?:\.\d+)?)\D+'
        r'h\s*[:=]\s*(?P<h>-?\d+(?:\.\d+)?)',
        text,
    )
    if match:
        return tuple(float(match.group(name)) for name in ('x', 'y', 'w', 'h'))  # type: ignore[return-value]
    raise TypeError(f'Unsupported frame value: {frame!r}')


def _point(x: float, y: float):
    if NSMakePoint is not None:
        return NSMakePoint(x, y)
    return (x, y)


def _display_bounds_for_point(x: float, y: float):
    if CGDisplayBounds is None:
        return None
    if CGGetActiveDisplayList is not None:
        try:
            error, active_displays, display_count = CGGetActiveDisplayList(16, None, None)
            if error == 0 and active_displays is not None:
                for display_id in list(active_displays)[:int(display_count)]:
                    bounds = CGDisplayBounds(display_id)
                    min_x = float(bounds.origin.x)
                    min_y = float(bounds.origin.y)
                    max_x = min_x + float(bounds.size.width)
                    max_y = min_y + float(bounds.size.height)
                    if min_x <= x <= max_x and min_y <= y <= max_y:
                        return bounds
        except Exception:
            pass
    if CGMainDisplayID is None:
        return None
    return CGDisplayBounds(CGMainDisplayID())


def _screen_point(x: float, y: float):
    bounds = _display_bounds_for_point(x, y)
    if bounds is None or not hasattr(bounds, 'size') or not hasattr(bounds, 'origin'):
        return _point(x, y)
    screen_min_y = float(bounds.origin.y)
    screen_height = float(bounds.size.height)
    return _point(x, screen_min_y + screen_height - y)


def _center_point_for_element(element):
    frame = get_element_frame(element)
    if frame is None:
        raise TimeoutError('Missing AX frame for drag source/target')
    x, y, width, height = _frame_parts(frame)
    return _screen_point(x + (width / 2.0), y + (height / 2.0))


def _upper_point_for_element(element, fraction: float = 0.1):
    frame = get_element_frame(element)
    if frame is None:
        raise TimeoutError('Missing AX frame for drag target')
    x, y, width, height = _frame_parts(frame)
    return _screen_point(x + (width / 2.0), y + (height * fraction))


def _post_mouse_event(event_type, point, button):
    source = None
    if CGEventSourceCreate is not None and kCGEventSourceStateHIDSystemState is not None:
        try:
            source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState)
        except Exception:
            source = None
    event = CGEventCreateMouseEvent(source, event_type, point, button)
    CGEventPost(kCGHIDEventTap, event)


def _drag_mouse_sequence(start_point, end_point, button, hold_duration: float = 1.0, move_steps: int = 12):
    _post_mouse_event(kCGEventMouseMoved, start_point, button)
    time.sleep(0.05)
    _post_mouse_event(kCGEventLeftMouseDown if button == kCGMouseButtonLeft else kCGEventRightMouseDown, start_point, button)
    time.sleep(hold_duration)
    steps = max(1, move_steps)
    start_x, start_y = start_point.x, start_point.y
    end_x, end_y = end_point.x, end_point.y
    for step in range(1, steps + 1):
        progress = float(step) / float(steps)
        point = _point(start_x + ((end_x - start_x) * progress), start_y + ((end_y - start_y) * progress))
        _post_mouse_event(kCGEventLeftMouseDragged if button == kCGMouseButtonLeft else kCGEventLeftMouseDragged, point, button)
        time.sleep(0.016)
    _post_mouse_event(kCGEventLeftMouseUp if button == kCGMouseButtonLeft else kCGEventRightMouseUp, end_point, button)


def click_element_center(element) -> None:
    _drag_mouse_sequence(_center_point_for_element(element), _center_point_for_element(element), kCGMouseButtonLeft, hold_duration=0.0, move_steps=1)


def right_click_element_center(element) -> None:
    _drag_mouse_sequence(_center_point_for_element(element), _center_point_for_element(element), kCGMouseButtonRight, hold_duration=0.0, move_steps=1)


def drag_element_center(source, destination) -> None:
    _drag_mouse_sequence(_center_point_for_element(source), _center_point_for_element(destination), kCGMouseButtonLeft)


def drag_above(source, target) -> None:
    _drag_mouse_sequence(_center_point_for_element(source), _upper_point_for_element(target), kCGMouseButtonLeft)


def wait_for_label(element, expected: str, timeout_ms: int = 3000, interval_ms: int = 100):
    def predicate():
        return get_element_label(element) == expected

    return poll_until(predicate, timeout_ms, interval_ms=interval_ms, label=f'label to become {expected!r}')


def wait_for_frame_change(element, timeout_ms: int = 5000, interval_ms: int = 100, condition=None):
    def predicate():
        frame = get_element_frame(element)
        if frame is None:
            return None
        if condition is None:
            return frame
        return frame if condition(frame) else None

    return poll_until(predicate, timeout_ms, interval_ms=interval_ms, label='frame change')


def iter_ax_tree(element, depth: int = 0, max_depth: int | None = None):
    yield element, depth
    if max_depth is not None and depth >= max_depth:
        return
    for child in _iter_children(element):
        yield from iter_ax_tree(child, depth + 1, max_depth=max_depth)


def dump_ax_tree(element, max_depth: int | None = 6) -> str:
    lines: list[str] = []
    for node, depth in iter_ax_tree(element, max_depth=max_depth):
        info = get_element_info(node)
        indent = '  ' * depth
        lines.append(f'{indent}- {info.short_label()} frame={info.frame!r} value={info.value!r}')
    return '\n'.join(lines) + '\n'


def collect_matches(element, needles: Sequence[str], max_depth: int | None = 8) -> list[AXElementInfo]:
    matches: list[AXElementInfo] = []
    normalized = [needle.lower() for needle in needles]
    for node, _depth in iter_ax_tree(element, max_depth=max_depth):
        info = get_element_info(node)
        haystack = ' '.join(
            str(part).lower() for part in (info.role, info.title, info.description, info.value, get_element_identifier(node)) if part is not None
        )
        if any(needle in haystack for needle in normalized):
            matches.append(info)
    return matches


def find_first_matching_element(element, needles: Sequence[str], max_depth: int | None = 8):
    normalized = [needle.lower() for needle in needles]
    for node, _depth in iter_ax_tree(element, max_depth=max_depth):
        info = get_element_info(node)
        haystack = ' '.join(
            str(part).lower() for part in (info.role, info.title, info.description, info.value, get_element_identifier(node)) if part is not None
        )
        if any(needle in haystack for needle in normalized):
            return node
    return None


def find_first_pressable_element(element, needles: Sequence[str], max_depth: int | None = 8):
    normalized = [needle.lower() for needle in needles]
    for node, _depth in iter_ax_tree(element, max_depth=max_depth):
        info = get_element_info(node)
        haystack = ' '.join(str(part).lower() for part in (info.role, info.title, info.description, info.value) if part is not None)
        if not any(needle in haystack for needle in normalized):
            continue
        actions = element_action_names(node)
        if 'AXPress' in actions or info.role == 'AXButton':
            return node
    return None


def press_element(element) -> bool:
    error_code = AXUIElementPerformAction(element, 'AXPress')
    return error_code == kAXErrorSuccess


def press_return_key() -> None:
    if CGEventCreateKeyboardEvent is None:
        raise RuntimeError('Keyboard event APIs unavailable')
    for key_down in (True, False):
        event = CGEventCreateKeyboardEvent(None, kVK_Return, key_down)
        CGEventPost(kCGHIDEventTap, event)


def set_element_value(element, value: str) -> bool:
    error_code = AXUIElementSetAttributeValue(element, kAXValueAttribute, value)
    return error_code == kAXErrorSuccess


def ax_application(pid: int):
    return AXUIElementCreateApplication(pid)


def system_focused_window():
    system = AXUIElementCreateSystemWide()
    focused_app = _copy_value(system, kAXFocusedApplicationAttribute)
    if focused_app is None:
        return None
    return _copy_value(focused_app, kAXFocusedWindowAttribute)


def element_exists(element, attribute: str) -> bool:
    return _copy_value(element, attribute) is not None


def element_attribute_names(element) -> list[str]:
    error_code, names = AXUIElementCopyAttributeNames(element, None)
    if error_code != kAXErrorSuccess or not names:
        return []
    return list(names)


def element_action_names(element) -> list[str]:
    error_code, names = AXUIElementCopyActionNames(element, None)
    if error_code != kAXErrorSuccess or not names:
        return []
    return list(names)


def element_parameterized_attribute_names(element) -> list[str]:
    error_code, names = AXUIElementCopyParameterizedAttributeNames(element, None)
    if error_code != kAXErrorSuccess or not names:
        return []
    return list(names)


def poll_until(predicate, timeout_ms: int, interval_ms: int = 250, label: str = 'condition'):
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    last_error = None
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except Exception as exc:
            last_error = exc
        time.sleep(interval_ms / 1000.0)
    if last_error is not None:
        raise TimeoutError(f'Timed out waiting for {label}: {last_error}')
    raise TimeoutError(f'Timed out waiting for {label}')
