import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { useVirtualKeyboard } from "../useVirtualKeyboard";

describe("useVirtualKeyboard", () => {
  const originalInnerHeight = window.innerHeight;

  beforeEach(() => {
    vi.clearAllMocks();
  });

  afterEach(() => {
    Object.defineProperty(window, "visualViewport", {
      value: undefined,
      configurable: true,
    });
    Object.defineProperty(window, "innerHeight", {
      value: originalInnerHeight,
      configurable: true,
    });
  });

  function installViewport({
    height,
    offsetTop,
  }: {
    height: number;
    offsetTop: number;
  }) {
    const handlers: Record<string, () => void> = {};
    const viewport = {
      height,
      offsetTop,
      addEventListener: vi.fn((event: string, handler: () => void) => {
        handlers[event] = handler;
      }),
      removeEventListener: vi.fn(),
    } as never;

    Object.defineProperty(window, "visualViewport", {
      value: viewport,
      configurable: true,
    });

    return { viewport, handlers };
  }

  it("returns a hidden keyboard state when the viewport API is unavailable", () => {
    Object.defineProperty(window, "visualViewport", {
      value: undefined,
      configurable: true,
    });

    const { result } = renderHook(() => useVirtualKeyboard());

    expect(result.current).toEqual({
      keyboardVisible: false,
      keyboardHeight: 0,
    });
  });

  it("calculates keyboard height from visualViewport measurements", () => {
    Object.defineProperty(window, "innerHeight", {
      value: 900,
      configurable: true,
    });
    installViewport({ height: 700, offsetTop: 0 });

    const { result } = renderHook(() => useVirtualKeyboard());

    expect(result.current.keyboardHeight).toBe(200);
    expect(result.current.keyboardVisible).toBe(true);
  });

  it("updates when the viewport resizes and cleans up listeners", () => {
    Object.defineProperty(window, "innerHeight", {
      value: 900,
      configurable: true,
    });
    const { viewport, handlers } = installViewport({ height: 850, offsetTop: 0 });

    const { result, unmount } = renderHook(() => useVirtualKeyboard());

    expect(result.current.keyboardVisible).toBe(false);
    expect(result.current.keyboardHeight).toBe(50);

    act(() => {
      (viewport as { height: number }).height = 700;
      handlers.resize();
    });

    expect(result.current.keyboardVisible).toBe(true);
    expect(result.current.keyboardHeight).toBe(200);

    unmount();

    expect((viewport as { removeEventListener: ReturnType<typeof vi.fn> }).removeEventListener).toHaveBeenCalledWith("resize", expect.any(Function));
    expect((viewport as { removeEventListener: ReturnType<typeof vi.fn> }).removeEventListener).toHaveBeenCalledWith("scroll", expect.any(Function));
  });
});
