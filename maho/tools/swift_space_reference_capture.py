#!/usr/bin/env python3
import os
import sys
import time
import subprocess
from typing import Callable, Optional, Tuple

import Quartz
from AppKit import NSRunningApplication, NSWorkspace
from ApplicationServices import (
    AXUIElementCreateApplication,
    AXUIElementCopyAttributeValue,
    AXUIElementPerformAction,
    AXValueGetType,
    AXValueGetValue,
    kAXChildrenAttribute,
    kAXDescriptionAttribute,
    kAXFocusedWindowAttribute,
    kAXIdentifierAttribute,
    kAXPositionAttribute,
    kAXPressAction,
    kAXRoleAttribute,
    kAXSizeAttribute,
    kAXTitleAttribute,
    kAXWindowsAttribute,
    kAXValueCGPointType,
    kAXValueCGSizeType,
)

APP_EXECUTABLE = "/Users/indo/code/project/maho-workspace/maho/macos-shell/build/Maho_Swift.app/Contents/MacOS/Maho_Swift"
ARTIFACT_DIR = "/tmp/maho-spaces-reference"
CREATE_PATH = os.path.join(ARTIFACT_DIR, "swift-create-space.png")
MANAGE_PATH = os.path.join(ARTIFACT_DIR, "swift-manage-spaces.png")


def run(cmd: list[str], check: bool = True) -> str:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"command failed: {' '.join(cmd)}\n{result.stdout}\n{result.stderr}")
    return result.stdout.strip()


def current_swift_pid() -> Optional[int]:
    output = run(["/bin/ps", "-axo", "pid=,command="], check=False)
    for line in output.splitlines():
        if "Maho_Swift.app/Contents/MacOS/Maho_Swift" in line:
            parts = line.strip().split(maxsplit=1)
            if parts:
                return int(parts[0])
    return None


def ensure_swift_app_running() -> int:
    pid = current_swift_pid()
    if pid:
        return pid
    subprocess.Popen([APP_EXECUTABLE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 10
    while time.time() < deadline:
        pid = current_swift_pid()
        if pid:
            return pid
        time.sleep(0.2)
    raise RuntimeError("Swift app did not start")


def activate(pid: int) -> None:
    app = NSRunningApplication.runningApplicationWithProcessIdentifier_(pid)
    if app:
        app.activateWithOptions_(0)
    time.sleep(0.5)


def ax_attr(element, attr: str):
    error, value = AXUIElementCopyAttributeValue(element, attr, None)
    if error != 0:
        return None
    return value


def ax_string(element, attr: str) -> str:
    value = ax_attr(element, attr)
    return value if isinstance(value, str) else ""


def ax_children(element):
    value = ax_attr(element, kAXChildrenAttribute)
    return list(value) if value is not None else []


def ax_point(value) -> Optional[Tuple[float, float]]:
    if value is None or AXValueGetType(value) != kAXValueCGPointType:
        return None
    ok, point = AXValueGetValue(value, kAXValueCGPointType, None)
    if not ok:
        return None
    return (point.x, point.y)


def ax_size(value) -> Optional[Tuple[float, float]]:
    if value is None or AXValueGetType(value) != kAXValueCGSizeType:
        return None
    ok, size = AXValueGetValue(value, kAXValueCGSizeType, None)
    if not ok:
        return None
    return (size.width, size.height)


def frame_of(element) -> Optional[Tuple[float, float, float, float]]:
    pos = ax_point(ax_attr(element, kAXPositionAttribute))
    size = ax_size(ax_attr(element, kAXSizeAttribute))
    if not pos or not size:
        return None
    return (pos[0], pos[1], size[0], size[1])


def find(root, predicate: Callable) -> Optional[object]:
    queue = [root]
    seen = set()
    while queue:
        element = queue.pop(0)
        key = str(element)
        if key in seen:
            continue
        seen.add(key)
        if predicate(element):
            return element
        queue.extend(ax_children(element))
    return None


def focused_window(app):
    return ax_attr(app, kAXFocusedWindowAttribute)


def main_window(app):
    focused = focused_window(app)
    if focused is not None:
        return focused
    windows = ax_attr(app, kAXWindowsAttribute)
    if windows:
        return list(windows)[0]
    return None


def click_at(x: float, y: float, button: str = "left") -> None:
    button_const = Quartz.kCGMouseButtonLeft if button == "left" else Quartz.kCGMouseButtonRight
    down_type = Quartz.kCGEventLeftMouseDown if button == "left" else Quartz.kCGEventRightMouseDown
    up_type = Quartz.kCGEventLeftMouseUp if button == "left" else Quartz.kCGEventRightMouseUp
    move = Quartz.CGEventCreateMouseEvent(None, Quartz.kCGEventMouseMoved, (x, y), button_const)
    down = Quartz.CGEventCreateMouseEvent(None, down_type, (x, y), button_const)
    up = Quartz.CGEventCreateMouseEvent(None, up_type, (x, y), button_const)
    if button == "right":
        Quartz.CGEventSetIntegerValueField(down, Quartz.kCGMouseEventButtonNumber, 1)
        Quartz.CGEventSetIntegerValueField(up, Quartz.kCGMouseEventButtonNumber, 1)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, move)
    time.sleep(0.05)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, down)
    time.sleep(0.05)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, up)


def screenshot_region(frame: Tuple[float, float, float, float], out_path: str) -> None:
    x, y, w, h = frame
    rect = f"{int(x)},{int(y)},{int(w)},{int(h)}"
    run(["/usr/sbin/screencapture", "-x", f"-R{rect}", out_path])
    if not os.path.exists(out_path) or os.path.getsize(out_path) <= 1024:
        raise RuntimeError(f"invalid screenshot at {out_path}")


def wait_for(timeout: float, producer: Callable):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = producer()
        if value is not None:
            return value
        time.sleep(0.1)
    return producer()


def footer(win):
    return find(win, lambda e: ax_string(e, kAXIdentifierAttribute) == "sidebar.footer")


def add_button(win):
    return find(win, lambda e: ax_string(e, kAXIdentifierAttribute) == "sidebar.space.add")


def create_sheet(win):
    return find(win, lambda e: ax_string(e, kAXRoleAttribute) == "AXSheet" and "Create" in (ax_string(e, kAXTitleAttribute) + ax_string(e, kAXDescriptionAttribute))) or find(win, lambda e: ax_string(e, kAXIdentifierAttribute) == "space.create.name")


def any_sheet(win):
    return find(win, lambda e: ax_string(e, kAXRoleAttribute) == "AXSheet")


def menu_item_named(app, name: str):
    for running in NSWorkspace.sharedWorkspace().runningApplications():
        if running.activationPolicy() != 0:
            continue
        ax_app = AXUIElementCreateApplication(running.processIdentifier())
        found = find(ax_app, lambda e: ax_string(e, kAXTitleAttribute) == name and "Menu" in ax_string(e, kAXRoleAttribute))
        if found is not None:
            return found
    return None


def capture_create(pid: int) -> None:
    activate(pid)
    app = AXUIElementCreateApplication(pid)
    win = main_window(app)
    if win is None:
        raise RuntimeError("no focused Swift window")
    button = add_button(win)
    if button is None:
        raise RuntimeError("sidebar.space.add not found")
    bx, by, bw, bh = frame_of(button)
    click_at(bx + bw / 2, by + bh / 2)
    time.sleep(0.8)

    current_footer = footer(win)
    if current_footer is None:
        raise RuntimeError("sidebar.footer not found after add click")
    popover = wait_for(2, lambda: next((c for c in ax_children(current_footer) if ax_string(c, kAXRoleAttribute) == "AXPopover"), None))
    if popover is None:
        raise RuntimeError("popover did not appear after add click")
    rows = ax_children(popover)
    if not rows:
        raise RuntimeError("popover has no rows")
    row_frame = frame_of(rows[0])
    if row_frame is None:
        raise RuntimeError("New Space row has no frame")
    rx, ry, rw, rh = row_frame
    click_at(rx + rw / 2, ry + rh / 2)
    time.sleep(1.0)

    sheet_element = wait_for(3, lambda: any_sheet(win))
    if sheet_element is None:
        raise RuntimeError("Create Space sheet did not appear")
    sheet_frame = frame_of(sheet_element)
    if sheet_frame is None:
        raise RuntimeError("Create Space sheet has no frame")
    screenshot_region(sheet_frame, CREATE_PATH)


def capture_manage(pid: int) -> None:
    activate(pid)
    app = AXUIElementCreateApplication(pid)
    win = main_window(app)
    if win is None:
        raise RuntimeError("no focused Swift window")
    space_chip = find(win, lambda e: ax_string(e, kAXIdentifierAttribute) == "sidebar.space.0")
    if space_chip is None:
        raise RuntimeError("sidebar.space.0 not found")
    sx, sy, sw, sh = frame_of(space_chip)
    click_at(sx + sw / 2, sy + sh / 2, button="right")
    time.sleep(0.8)
    manage_item = wait_for(3, lambda: menu_item_named(app, "Manage Spaces"))
    if manage_item is None:
        raise RuntimeError("Manage Spaces menu item did not appear")
    if AXUIElementPerformAction(manage_item, kAXPressAction) != 0:
        mx, my, mw, mh = frame_of(manage_item)
        click_at(mx + mw / 2, my + mh / 2)
    time.sleep(1.0)
    sheet_element = wait_for(3, lambda: any_sheet(win))
    if sheet_element is None:
        raise RuntimeError("Manage Spaces sheet did not appear")
    sheet_frame = frame_of(sheet_element)
    if sheet_frame is None:
        raise RuntimeError("Manage Spaces sheet has no frame")
    screenshot_region(sheet_frame, MANAGE_PATH)


def main() -> int:
    os.makedirs(ARTIFACT_DIR, exist_ok=True)
    pid = ensure_swift_app_running()
    capture_create(pid)
    capture_manage(pid)
    print(CREATE_PATH)
    print(MANAGE_PATH)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
