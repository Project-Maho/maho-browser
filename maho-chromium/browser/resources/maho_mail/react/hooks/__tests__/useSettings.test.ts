import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useSettings } from "../useSettings";
import * as api from "../../api";

vi.mock("../../api", () => ({
  getAppSetting: vi.fn(),
  setAppSetting: vi.fn(),
}));

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
    vi.mocked(api.getAppSetting).mockResolvedValue(null);
    vi.mocked(api.setAppSetting).mockResolvedValue(undefined);
  });

  afterEach(() => {
    Object.defineProperty(window, "localStorage", { value: originalLocalStorage, writable: true });
  });

  it("returns default values when localStorage is empty", () => {
    const { result } = renderHook(() => useSettings());

    expect(result.current.syncInterval).toBe(15);
    expect(result.current.sidebarWidth).toBe(240);
    expect(result.current.sidebarCollapsed).toBe(true);
    expect(result.current.fontSize).toBe(14);
    expect(result.current.density).toBe("comfortable");
    expect(result.current.desktopNotifications).toBe(true);
    expect(result.current.soundEnabled).toBe(true);
  });

  it("reads saved syncInterval from localStorage", () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ sync_interval: 300 }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.syncInterval).toBe(5);
  });

  it("replaces a conflicting startup cache with backend settings", async () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({
      sync_interval: 300,
      block_remote_images: true,
    }));
    vi.mocked(api.getAppSetting).mockResolvedValue(JSON.stringify({
      sync_interval: 900,
      block_remote_images: false,
    }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.syncInterval).toBe(5);
    await act(async () => {
      await Promise.resolve();
    });
    expect(result.current.syncInterval).toBe(15);
    expect(result.current.blockRemoteImages).toBe(false);
  });

  it("persists one complete startup cache instead of legacy behavior keys", async () => {
    vi.mocked(api.getAppSetting).mockResolvedValue(JSON.stringify({
      sync_interval: 900,
      block_remote_images: true,
    }));

    renderHook(() => useSettings());

    await act(async () => {
      await Promise.resolve();
    });
    expect(localStorageMock.getItem("maho-mail-behavior-prefs-cache-v1")).not.toBeNull();
    expect(localStorageMock.getItem("maho-sync-interval")).toBeNull();
    expect(localStorageMock.getItem("maho-block-remote-images")).toBeNull();
  });

  it("reads saved sidebarWidth from localStorage when within valid range", () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ sidebar_width: 300 }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.sidebarWidth).toBe(300);
  });

  it("falls back to 240 when sidebarWidth is outside 200-400 range", () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ sidebar_width: 150 }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.sidebarWidth).toBe(240);
  });

  it("reads saved sidebarCollapsed from localStorage", () => {
    localStorageMock.setItem("maho-sidebar-collapsed", "false");

    const { result } = renderHook(() => useSettings());

    expect(result.current.sidebarCollapsed).toBe(false);
  });

  it("reads saved fontSize from localStorage when within valid range", () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ font_size: 16 }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.fontSize).toBe(16);
  });

  it("falls back to 14 when fontSize is outside 12-20 range", () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ font_size: 25 }));

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
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ desktop_notifications: true }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.desktopNotifications).toBe(true);
  });

  it("defaults soundEnabled to true when localStorage key is absent", () => {
    const { result } = renderHook(() => useSettings());

    expect(result.current.soundEnabled).toBe(true);
  });

  it("reads soundEnabled false from localStorage", () => {
    localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ sound_enabled: false }));

    const { result } = renderHook(() => useSettings());

    expect(result.current.soundEnabled).toBe(false);
  });

  it("setSyncInterval updates state and persists to localStorage", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setSyncInterval(600);
    });

    expect(result.current.syncInterval).toBe(15);
    expect(JSON.parse(localStorageMock.getItem("maho-mail-behavior-prefs-cache-v1") ?? "{}").sync_interval).toBe(15);
  });

  it("setSidebarWidth updates state and immediately persists to localStorage", () => {
    const { result } = renderHook(() => useSettings());

    act(() => {
      result.current.setSidebarWidth(320);
    });

    expect(result.current.sidebarWidth).toBe(320);
    expect(JSON.parse(localStorageMock.getItem("maho-mail-behavior-prefs-cache-v1") ?? "{}").sidebar_width).toBe(320);
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
    expect(JSON.parse(localStorageMock.getItem("maho-mail-behavior-prefs-cache-v1") ?? "{}").font_size).toBe(18);
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
      localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ email_list_width: 420 }));
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(420);
    });

    it("falls back to 350 when emailListWidth is below 280", () => {
      localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ email_list_width: 100 }));
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(350);
    });

    it("falls back to 350 when emailListWidth is above 600", () => {
      localStorageMock.setItem("maho-mail-behavior-prefs-cache-v1", JSON.stringify({ email_list_width: 900 }));
      const { result } = renderHook(() => useSettings());
      expect(result.current.emailListWidth).toBe(350);
    });

    it("setEmailListWidth updates state and persists to localStorage", () => {
      const { result } = renderHook(() => useSettings());

      act(() => {
        result.current.setEmailListWidth(480);
      });

      expect(result.current.emailListWidth).toBe(480);
      expect(JSON.parse(localStorageMock.getItem("maho-mail-behavior-prefs-cache-v1") ?? "{}").email_list_width).toBe(480);
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
