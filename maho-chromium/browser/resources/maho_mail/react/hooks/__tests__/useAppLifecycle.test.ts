// @vitest-environment jsdom

import { renderHook, act } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { useAppLifecycle } from "../useAppLifecycle";

const onForeground = vi.fn();
const onBackground = vi.fn();
let addEventListenerSpy: ReturnType<typeof vi.spyOn>;
let removeEventListenerSpy: ReturnType<typeof vi.spyOn>;

describe("useAppLifecycle", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      value: "visible",
    });
    addEventListenerSpy = vi.spyOn(document, "addEventListener");
    removeEventListenerSpy = vi.spyOn(document, "removeEventListener");
  });

  afterEach(() => {
    addEventListenerSpy?.mockRestore();
    removeEventListenerSpy?.mockRestore();
    vi.useRealTimers();
  });

  it("registers visibilitychange listener on both desktop and mobile", () => {
    renderHook(() =>
      useAppLifecycle({
        onForeground,
        onBackground,
      })
    );

    expect(
      addEventListenerSpy.mock.calls.some(
        (call: unknown[]) => call[0] === "visibilitychange"
      )
    ).toBe(true);
  });

  it("calls foreground callback when document becomes visible", () => {
    renderHook(() =>
      useAppLifecycle({
        onForeground,
        onBackground,
      })
    );

    const registeredHandler = addEventListenerSpy.mock.calls.find(
      (call: unknown[]) => call[0] === "visibilitychange"
    )?.[1] as (() => void) | undefined;

    expect(registeredHandler).toBeTypeOf("function");

    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      value: "visible",
    });

    act(() => {
      registeredHandler?.();
    });

    expect(onForeground).toHaveBeenCalledTimes(1);
    expect(onBackground).not.toHaveBeenCalled();
  });

  it("calls background callback when document becomes hidden", () => {
    renderHook(() =>
      useAppLifecycle({
        onForeground,
        onBackground,
      })
    );

    const registeredHandler = addEventListenerSpy.mock.calls.find(
      (call: unknown[]) => call[0] === "visibilitychange"
    )?.[1] as (() => void) | undefined;

    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      value: "hidden",
    });

    act(() => {
      registeredHandler?.();
    });

    expect(onBackground).toHaveBeenCalledTimes(1);
    expect(onForeground).not.toHaveBeenCalled();
  });

  it("debounces foreground calls within 30 seconds", () => {
    renderHook(() =>
      useAppLifecycle({
        onForeground,
        onBackground,
      })
    );

    const registeredHandler = addEventListenerSpy.mock.calls.find(
      (call: unknown[]) => call[0] === "visibilitychange"
    )?.[1] as (() => void) | undefined;

    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      value: "visible",
    });

    act(() => {
      registeredHandler?.();
    });
    expect(onForeground).toHaveBeenCalledTimes(1);

    // Second call within 30s should be debounced
    act(() => {
      vi.advanceTimersByTime(10_000);
      registeredHandler?.();
    });
    expect(onForeground).toHaveBeenCalledTimes(1);

    // After 30s total, should fire again
    act(() => {
      vi.advanceTimersByTime(20_001);
      registeredHandler?.();
    });
    expect(onForeground).toHaveBeenCalledTimes(2);
  });

  it("does not debounce background calls", () => {
    renderHook(() =>
      useAppLifecycle({
        onForeground,
        onBackground,
      })
    );

    const registeredHandler = addEventListenerSpy.mock.calls.find(
      (call: unknown[]) => call[0] === "visibilitychange"
    )?.[1] as (() => void) | undefined;

    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      value: "hidden",
    });

    act(() => {
      registeredHandler?.();
    });
    act(() => {
      registeredHandler?.();
    });

    expect(onBackground).toHaveBeenCalledTimes(2);
  });

  it("removes the listener on unmount", () => {
    const { unmount } = renderHook(() =>
      useAppLifecycle({
        onForeground,
        onBackground,
      })
    );

    const registeredHandler = addEventListenerSpy.mock.calls.find(
      (call: unknown[]) => call[0] === "visibilitychange"
    )?.[1];

    unmount();

    expect(removeEventListenerSpy).toHaveBeenCalledWith("visibilitychange", registeredHandler);
  });
});
