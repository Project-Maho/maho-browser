import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { useLongPress } from "../useLongPress";

describe("useLongPress", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  function createPointerEvent(x: number, y: number) {
    return { clientX: x, clientY: y } as never;
  }

  it("calls onPress for a short press", () => {
    const onLongPress = vi.fn();
    const onPress = vi.fn();

    const { result } = renderHook(() =>
      useLongPress({ onLongPress, onPress, delay: 300 })
    );

    act(() => {
      result.current.onPointerDown(createPointerEvent(10, 10));
      result.current.onPointerUp();
    });

    expect(onPress).toHaveBeenCalledTimes(1);
    expect(onLongPress).not.toHaveBeenCalled();
  });

  it("calls onLongPress after the delay and suppresses onPress", async () => {
    const onLongPress = vi.fn();
    const onPress = vi.fn();

    const { result } = renderHook(() =>
      useLongPress({ onLongPress, onPress, delay: 300 })
    );

    act(() => {
      result.current.onPointerDown(createPointerEvent(10, 10));
    });

    await act(async () => {
      await vi.advanceTimersByTimeAsync(300);
    });

    act(() => {
      result.current.onPointerUp();
    });

    expect(onLongPress).toHaveBeenCalledTimes(1);
    expect(onPress).not.toHaveBeenCalled();
  });

  it("clears the timer on unmount before the long press fires", async () => {
    const onLongPress = vi.fn();

    const { result, unmount } = renderHook(() =>
      useLongPress({ onLongPress, delay: 300 })
    );

    act(() => {
      result.current.onPointerDown(createPointerEvent(10, 10));
    });

    unmount();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(300);
    });

    expect(onLongPress).not.toHaveBeenCalled();
  });
});
