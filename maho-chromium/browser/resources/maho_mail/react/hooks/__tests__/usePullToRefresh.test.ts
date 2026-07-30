import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { usePullToRefresh } from "../usePullToRefresh";

describe("usePullToRefresh", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  function createTouchEvent(y: number, scrollTop = 0) {
    return {
      currentTarget: { scrollTop },
      touches: [{ clientY: y }],
      preventDefault: vi.fn(),
    } as never;
  }

  it("tracks pull distance and progress while dragging downward", () => {
    const onRefresh = vi.fn();
    const { result } = renderHook(() =>
      usePullToRefresh({ onRefresh, threshold: 72, maxPull: 120 })
    );

    act(() => {
      result.current.bindings.onTouchStart(createTouchEvent(100));
      result.current.bindings.onTouchMove(createTouchEvent(150));
    });

    expect(result.current.isPulling).toBe(true);
    expect(result.current.pullDistance).toBeCloseTo(27.5, 1);
    expect(result.current.progress).toBeCloseTo(0.38, 2);
  });

  it("runs refresh when the pull crosses the threshold", async () => {
    const onRefresh = vi.fn().mockResolvedValue(undefined);
    const { result } = renderHook(() =>
      usePullToRefresh({ onRefresh, threshold: 72, maxPull: 120 })
    );

    act(() => {
      result.current.bindings.onTouchStart(createTouchEvent(100));
      result.current.bindings.onTouchMove(createTouchEvent(320));
    });

    await act(async () => {
      result.current.bindings.onTouchEnd();
      await Promise.resolve();
    });

    expect(onRefresh).toHaveBeenCalledTimes(1);
    expect(result.current.isRefreshing).toBe(false);
    expect(result.current.pullDistance).toBe(0);
    expect(result.current.isPulling).toBe(false);
  });

  it("cancels and resets when the gesture is canceled", () => {
    const onRefresh = vi.fn();
    const { result } = renderHook(() =>
      usePullToRefresh({ onRefresh, threshold: 72, maxPull: 120 })
    );

    act(() => {
      result.current.bindings.onTouchStart(createTouchEvent(100));
      result.current.bindings.onTouchMove(createTouchEvent(170));
    });

    expect(result.current.isPulling).toBe(true);

    act(() => {
      result.current.bindings.onTouchCancel();
    });

    expect(result.current.isPulling).toBe(false);
    expect(result.current.pullDistance).toBe(0);
    expect(result.current.progress).toBe(0);
    expect(onRefresh).not.toHaveBeenCalled();
  });
});
