import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useSettings } from "../useSettings";

describe("useSettings", () => {
  const localStorageMock = (() => {
    let store: Record<string, string> = {};
    return {
      getItem: (key: string) => store[key] || null,
      setItem: (key: string, value: string) => { store[key] = value.toString(); },
      removeItem: (key: string) => { delete store[key]; },
      clear: () => { store = {}; },
    };
  })();

  const originalLocalStorage = window.localStorage;

  beforeEach(() => {
    localStorageMock.clear();
    vi.clearAllMocks();
    Object.defineProperty(window, "localStorage", { value: localStorageMock, writable: true });
  });

  afterEach(() => {
    Object.defineProperty(window, "localStorage", { value: originalLocalStorage, writable: true });
  });

  it("returns default values when localStorage is empty", () => {
    const { result } = renderHook(() => useSettings());

    expect(result.current.syncInterval).toBe(0);
    expect(result.current.sidebarWidth).toBe(240);
    expect(result.current.sidebarCollapsed).toBe(true);
    expect(result.current.fontSize).toBe(14);
    expect(result.current.density).toBe("comfortable");
    expect(result.current.desktopNotifications).toBe(false);
    expect(result.current.soundEnabled).toBe(true);
  });

  it("reads saved syncInterval from localStorage", () => {
    localStorageMock.setItem("maho-sync-interval", "300");

    const { result } = renderHook(() => useSettings());

    expect(result.current.syncInterval).toBe(300);
  });

  it("reads saved sidebarWidth from localStorage when within valid range", () => {
    localStorageMock.setItem("maho-sidebar-width", "300");

    const { result } = renderHook(() => useSettings());

    expect(result.current.sidebarWidth).toBe(300);
  });

  it("falls back to 240 when sidebarWidth is outside 200-400 range", () => {
    localStorageMock.setItem("maho-sidebar-width", "150");

    const { result } = renderHook(() => useSettings());

    expect(result.current.sidebarWidth).toBe(240);
  });

  it("reads saved sidebarCollapsed from localStorage", () => {
    localStorageMock.setItem("maho-sidebar-collapsed", "false");

    const { result } = renderHook(() => useSettings());

    expect(result.current.sidebarCollapsed).toBe(false);
  });

  it("reads saved fontSize from localStorage when within valid range", () => {
    localStorageMock.setItem("maho-font-size", "16");

    const { result } = renderHook(() => useSettings());

    expect(result.current.fontSize).toBe(16);
  });

  it("falls back to 14 when fontSize is outside 12-20 range", () => {
    localStorageMock.setItem("maho-font-size", "25");

    const { result } = renderHook(() => useSettings());

    expect(result.current.fontSize).toBe(14);
  });

  it("reads saved density from localStorage", () => {
    localStorageMock.setItem("maho-density", "compact");

    const { result } = renderHook(() => useSettings());

    expect(result.current.density).toBe("compact");
  });

  it("falls back to comfortable when density value is invalid", () => {
    localStorageMock.setItem("maho-density", "invalid");

    const { result } = renderHook(() => useSettings());

    expect(result.current.density).toBe("comfortable");
  });

  it("reads desktopNotifications true from localStorage", () => {
    localStorageMock.setItem("maho-desktop-notifications", "true");

    const { result } = renderHook(() => useSettings());

    expect(result.current.desktopNotifications).toBe(true);
  });

  it("defaults soundEnabled to true when localStorage key is absent", () => {
    const { result } = renderHook(() => useSettings());

    expect(result.current.soundEnabled).toBe(true);
  });

  it("reads soundEnabled false from localStorage", () => {
    localStorageMock.setItem("maho-sound-enabled", "false");

    const { result } = renderHook(() => useSettings());

    expect(result.current.soundEnabled).toBe(false);
  });

  it("setSyncInterval updates state and persists to localStorage", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setSyncInterval(600);
    });

    expect(result.current.syncInterval).toBe(600);
    expect(localStorageMock.getItem("maho-sync-interval")).toBe("600");
  });

  it("setSidebarWidth updates state and immediately persists to localStorage", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setSidebarWidth(320);
    });

    expect(result.current.sidebarWidth).toBe(320);
    expect(localStorageMock.getItem("maho-sidebar-width")).toBe("320");
  });

  it("setSidebarCollapsed updates state and immediately persists to localStorage", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setSidebarCollapsed(false);
    });

    expect(result.current.sidebarCollapsed).toBe(false);
    expect(localStorageMock.getItem("maho-sidebar-collapsed")).toBe("false");
  });

  it("setFontSize updates state, persists via useEffect, and injects CSS property", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setFontSize(18);
    });

    expect(result.current.fontSize).toBe(18);
    expect(localStorageMock.getItem("maho-font-size")).toBe("18");
    expect(document.documentElement.style.getPropertyValue("--maho-base-font-size")).toBe("18px");
  });

  it("openSettings and closeSettings toggle showSettings", () => {
    const { result } = renderHook(() => useSettings());

    expect(result.current.showSettings).toBe(false);

    act(() => {
      result.current.openSettings();
    });

    expect(result.current.showSettings).toBe(true);

    act(() => {
      result.current.closeSettings();
    });

    expect(result.current.showSettings).toBe(false);
  });

  it("setDensity updates state and persists to localStorage", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setDensity("compact");
    });

    expect(result.current.density).toBe("compact");
    expect(localStorageMock.getItem("maho-density")).toBe("compact");
  });

  describe("emailListWidth", () => {
    it("defaults to 350 when localStorage is empty", () => {
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(350);
    });

    it("reads a saved emailListWidth within the 280-600 range", () => {
      localStorageMock.setItem("maho-email-list-width", "420");
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(420);
    });

    it("falls back to 350 when emailListWidth is below 280", () => {
      localStorageMock.setItem("maho-email-list-width", "100");
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(350);
    });

    it("falls back to 350 when emailListWidth is above 600", () => {
      localStorageMock.setItem("maho-email-list-width", "900");
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(350);
    });

    it("setEmailListWidth updates state and persists to localStorage", () => {
      const { result } = renderHook(() => useSettings());

      act(() => {
        result.current.setEmailListWidth(480);
      });

      expect(result.current.emailListWidth).toBe(480);
      expect(localStorageMock.getItem("maho-email-list-width")).toBe("480");
    });

    it("exposes a handleEmailListResizeMouseDown callback and an isEmailListResizing flag", () => {
      const { result } = renderHook(() => useSettings());
      expect(typeof result.current.handleEmailListResizeMouseDown).toBe("function");
      expect(result.current.isEmailListResizing).toBe(false);
    });

    it("toggles isEmailListResizing when handleEmailListResizeMouseDown fires", () => {
      const { result } = renderHook(() => useSettings());
      const fakeEvent = { preventDefault: vi.fn() } as unknown as React.MouseEvent;

      act(() => {
        result.current.handleEmailListResizeMouseDown(fakeEvent);
      });

      expect(result.current.isEmailListResizing).toBe(true);
      expect(fakeEvent.preventDefault).toHaveBeenCalledTimes(1);
    });
  });
});
