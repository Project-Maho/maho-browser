import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { renderHook } from "@testing-library/react";
import { useTheme } from "./useTheme";

describe("useTheme", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    document.documentElement.removeAttribute("data-theme");
  });

  it("applies dark theme", () => {
    renderHook(() => useTheme("dark"));
    expect(document.documentElement.getAttribute("data-theme")).toBe("dark");
  });

  it("applies light theme", () => {
    renderHook(() => useTheme("light"));
    expect(document.documentElement.getAttribute("data-theme")).toBe("light");
  });

  it("handles system theme with dark preference", () => {
    vi.stubGlobal("matchMedia", () => ({
      matches: true,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    }));

    renderHook(() => useTheme("system"));
    expect(document.documentElement.getAttribute("data-theme")).toBe("dark");
  });

  it("handles system theme with light preference", () => {
    vi.stubGlobal("matchMedia", () => ({
      matches: false,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    }));

    renderHook(() => useTheme("system"));
    expect(document.documentElement.getAttribute("data-theme")).toBe("light");
  });

  it("listens to and cleans up matchMedia changes when theme is system", () => {
    const addEventListenerMock = vi.fn();
    const removeEventListenerMock = vi.fn();

    vi.stubGlobal("matchMedia", () => ({
      matches: true,
      addEventListener: addEventListenerMock,
      removeEventListener: removeEventListenerMock,
    }));

    const { unmount } = renderHook(() => useTheme("system"));
    expect(addEventListenerMock).toHaveBeenCalledWith("change", expect.any(Function));

    unmount();
    expect(removeEventListenerMock).toHaveBeenCalledWith("change", expect.any(Function));
  });
});
