import { renderHook, render, act } from "@testing-library/react";
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
      cancelable: true,
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

  it("registers touchmove as a non-passive listener so preventDefault actually cancels the scroll", () => {
    // React attaches JSX event props (onTouchMove et al.) through a single
    // delegated listener on the root container, and registers touchstart /
    // touchmove / wheel as passive by default regardless of what any
    // component does inside its handler. Calling event.preventDefault() from
    // a passive-registered handler is a silent no-op in real browsers (and
    // logs a console warning) - dispatching real, cancelable DOM events
    // through the rendered tree is the only way to observe that. A
    // hand-built fake event with a vi.fn() preventDefault cannot catch this
    // because it never goes through the browser/React passive-listener path.
    function Probe() {
      const { bindings } = usePullToRefresh({
        onRefresh: vi.fn(),
        threshold: 10,
        maxPull: 100,
      });
      return (
        <div data-testid="scroller" {...bindings}>
          content
        </div>
      );
    }

    const { getByTestId } = render(<Probe />);
    const el = getByTestId("scroller");

    const startEvent = new Event("touchstart", { bubbles: true, cancelable: true });
    Object.defineProperty(startEvent, "touches", { value: [{ clientY: 100 }] });
    act(() => {
      el.dispatchEvent(startEvent);
    });

    const moveEvent = new Event("touchmove", { bubbles: true, cancelable: true });
    Object.defineProperty(moveEvent, "touches", { value: [{ clientY: 150 }] });
    act(() => {
      el.dispatchEvent(moveEvent);
    });

    expect(moveEvent.defaultPrevented).toBe(true);
  });

  it("does not throw or invoke preventDefault when event is non-cancelable or passive", () => {
    const onRefresh = vi.fn();
    const { result } = renderHook(() =>
      usePullToRefresh({ onRefresh, threshold: 72, maxPull: 120 })
    );

    const preventDefault = vi.fn(() => {
      throw new Error("Unable to preventDefault inside passive event listener");
    });

    const passiveEvent = {
      currentTarget: { scrollTop: 0 },
      touches: [{ clientY: 150 }],
      cancelable: false,
      preventDefault,
    } as never;

    act(() => {
      result.current.bindings.onTouchStart(createTouchEvent(100));
      expect(() => {
        result.current.bindings.onTouchMove(passiveEvent);
      }).not.toThrow();
    });

    expect(preventDefault).not.toHaveBeenCalled();
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
