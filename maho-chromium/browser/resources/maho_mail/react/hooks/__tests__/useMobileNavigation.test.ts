import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { useMobileNavigation } from "../useMobileNavigation";

describe("useMobileNavigation", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("initializes with the provided screen and writes the history state", () => {
    const replaceStateSpy = vi.spyOn(window.history, "replaceState");

    const { result } = renderHook(() => useMobileNavigation("compose"));

    expect(result.current.stack).toEqual(["compose"]);
    expect(result.current.currentScreen).toBe("compose");
    expect(result.current.canGoBack).toBe(false);
    expect(replaceStateSpy).toHaveBeenCalledWith(
      expect.objectContaining({
        __mahoMobileNavigation: true,
        __mahoMobileNavigationDepth: 0,
      }),
      "",
    );
  });

  it("pushes, pops, and resets the navigation stack", () => {
    const pushStateSpy = vi.spyOn(window.history, "pushState");
    const backSpy = vi.spyOn(window.history, "back").mockImplementation(() => {});
    const replaceStateSpy = vi.spyOn(window.history, "replaceState");

    const { result } = renderHook(() => useMobileNavigation());

    act(() => {
      result.current.push("detail");
    });

    expect(result.current.stack).toEqual(["list", "detail"]);
    expect(result.current.currentScreen).toBe("detail");
    expect(result.current.canGoBack).toBe(true);
    expect(pushStateSpy).toHaveBeenCalledWith(
      expect.objectContaining({
        __mahoMobileNavigation: true,
        __mahoMobileNavigationDepth: 1,
      }),
      "",
    );

    act(() => {
      expect(result.current.pop()).toBe(true);
    });

    expect(backSpy).toHaveBeenCalledTimes(1);

    act(() => {
      result.current.reset("search");
    });

    expect(result.current.stack).toEqual(["search"]);
    expect(result.current.currentScreen).toBe("search");
    expect(result.current.canGoBack).toBe(false);
    expect(replaceStateSpy).toHaveBeenLastCalledWith(
      expect.objectContaining({
        __mahoMobileNavigation: true,
        __mahoMobileNavigationDepth: 0,
      }),
      "",
    );
  });

  it("responds to browser back navigation and removes the listener on cleanup", () => {
    const addSpy = vi.spyOn(window, "addEventListener");
    const removeSpy = vi.spyOn(window, "removeEventListener");

    const { result, unmount } = renderHook(() => useMobileNavigation());

    act(() => {
      result.current.push("detail");
      result.current.push("compose");
    });

    expect(result.current.stack).toEqual(["list", "detail", "compose"]);
    expect(addSpy).toHaveBeenCalledWith("popstate", expect.any(Function));

    act(() => {
      window.dispatchEvent(new PopStateEvent("popstate"));
    });

    expect(result.current.stack).toEqual(["list", "detail"]);
    expect(result.current.currentScreen).toBe("detail");

    unmount();

    expect(removeSpy).toHaveBeenCalledWith("popstate", expect.any(Function));
  });
});
