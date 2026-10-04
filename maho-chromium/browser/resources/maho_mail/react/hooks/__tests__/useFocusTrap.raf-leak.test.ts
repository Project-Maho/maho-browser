import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { renderHook } from "@testing-library/react";
import { useRef } from "react";
import { useFocusTrap } from "../useFocusTrap";

describe("useFocusTrap rAF cleanup", () => {
  let rafCallbacks: Map<number, FrameRequestCallback>;
  let rafId: number;

  beforeEach(() => {
    rafCallbacks = new Map();
    rafId = 0;
    vi.stubGlobal("requestAnimationFrame", vi.fn((cb: FrameRequestCallback) => {
      rafId++;
      rafCallbacks.set(rafId, cb);
      return rafId;
    }));
    vi.stubGlobal("cancelAnimationFrame", vi.fn((id: number) => {
      rafCallbacks.delete(id);
    }));
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("cancels pending rAF when unmounted before frame fires", () => {
    const container = document.createElement("div");
    const button = document.createElement("button");
    container.appendChild(button);
    document.body.appendChild(container);

    const containerRef = { current: container };
    const { rerender, unmount } = renderHook(
      ({ active }) => {
        const ref = useRef(containerRef);
        useFocusTrap(ref.current ? ref : ref, active);
        return ref;
      },
      { initialProps: { active: true } }
    );

    // rAF is pending (not yet fired)
    expect(rafCallbacks.size).toBe(1);

    // Unmount before the frame fires
    unmount();

    // The rAF callback should have been cancelled
    expect(vi.mocked(cancelAnimationFrame)).toHaveBeenCalled();

    // Even if the frame fires now, focus should NOT move into the detached container
    const callback = Array.from(rafCallbacks.values())[0];
    if (callback) callback(performance.now());
    expect(document.activeElement).not.toBe(button);

    container.remove();
  });
});
