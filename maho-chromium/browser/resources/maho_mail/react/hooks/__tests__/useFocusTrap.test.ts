import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { useFocusTrap } from "../useFocusTrap";

describe("useFocusTrap", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
      callback(0);
      return 0;
    });
  });

  afterEach(() => {
    vi.unstubAllGlobals();
    document.body.innerHTML = "";
  });

  function createContainer() {
    const previous = document.createElement("button");
    previous.textContent = "Previous";
    document.body.append(previous);

    const container = document.createElement("div");
    container.innerHTML = `
      <button id="first">First</button>
      <button id="second">Second</button>
    `;
    document.body.append(container);

    return { previous, container };
  }

  it("moves focus to the first focusable element when activated", () => {
    const { previous, container } = createContainer();
    previous.focus();
    const ref = { current: container };

    renderHook(() => useFocusTrap(ref, true));

    expect(document.activeElement).toBe(container.querySelector("#first"));
  });

  it("cycles focus with Tab and Shift+Tab inside the trap", () => {
    const { container } = createContainer();
    const ref = { current: container };

    renderHook(() => useFocusTrap(ref, true));

    const first = container.querySelector<HTMLButtonElement>("#first")!;
    const second = container.querySelector<HTMLButtonElement>("#second")!;

    act(() => {
      second.focus();
      const tabEvent = new KeyboardEvent("keydown", { key: "Tab" });
      const preventDefault = vi.spyOn(tabEvent, "preventDefault");
      document.dispatchEvent(tabEvent);
      expect(preventDefault).toHaveBeenCalledTimes(1);
    });

    expect(document.activeElement).toBe(first);

    act(() => {
      first.focus();
      const shiftTabEvent = new KeyboardEvent("keydown", {
        key: "Tab",
        shiftKey: true,
      });
      const preventDefault = vi.spyOn(shiftTabEvent, "preventDefault");
      document.dispatchEvent(shiftTabEvent);
      expect(preventDefault).toHaveBeenCalledTimes(1);
    });

    expect(document.activeElement).toBe(second);
  });

  it("restores the previously focused element and removes the listener on cleanup", () => {
    const { previous, container } = createContainer();
    previous.focus();
    const ref = { current: container };

    const { unmount } = renderHook(() => useFocusTrap(ref, true));

    expect(document.activeElement).toBe(container.querySelector("#first"));

    unmount();

    expect(document.activeElement).toBe(previous);
  });
});
