import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { useAppUpdate } from "../useAppUpdate";

vi.mock("../../api", () => ({
  checkForAppUpdate: vi.fn(),
  relaunchApp: vi.fn(),
}));

import * as api from "../../api";

describe("useAppUpdate", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("initializes with correct initial state", () => {
    const { result } = renderHook(() => useAppUpdate());

    expect(result.current.updateAvailable).toBe(false);
    expect(result.current.version).toBeNull();
    expect(result.current.downloading).toBe(false);
    expect(result.current.error).toBeNull();
  });

  it("auto-checks for updates after 2 second delay", async () => {
    vi.mocked(api.checkForAppUpdate).mockResolvedValue(null);

    renderHook(() => useAppUpdate());

    expect(api.checkForAppUpdate).not.toHaveBeenCalled();

    await vi.advanceTimersByTimeAsync(2000);

    expect(api.checkForAppUpdate).toHaveBeenCalledTimes(1);
  });

  it("sets update info when update is found", async () => {
    const mockDownloadAndInstall = vi.fn();
    const mockUpdate = {
      version: "2.0.0",
      body: "Release notes",
      downloadAndInstall: mockDownloadAndInstall,
    };

    vi.mocked(api.checkForAppUpdate).mockResolvedValue(mockUpdate as never);

    const { result } = renderHook(() => useAppUpdate());

    await act(async () => {
      await result.current.checkForUpdate();
    });

    expect(result.current.updateAvailable).toBe(true);
    expect(result.current.version).toBe("2.0.0");
    expect(result.current.releaseNotes).toBe("Release notes");
  });

  it("keeps updateAvailable false when no update is available", async () => {
    vi.mocked(api.checkForAppUpdate).mockResolvedValue(null);

    const { result } = renderHook(() => useAppUpdate());

    await act(async () => {
      await result.current.checkForUpdate();
    });

    expect(result.current.updateAvailable).toBe(false);
    expect(result.current.version).toBeNull();
  });

  it("sets error when check fails", async () => {
    vi.mocked(api.checkForAppUpdate).mockRejectedValue(new Error("Network error"));

    const { result } = renderHook(() => useAppUpdate());

    await act(async () => {
      await result.current.checkForUpdate();
    });

    expect(result.current.error).toBe("Network error");
    expect(result.current.updateAvailable).toBe(false);
  });

  it("calls downloadAndInstall when installUpdate is invoked", async () => {
    const mockDownloadAndInstall = vi.fn().mockResolvedValue(undefined);
    const mockUpdate = {
      version: "2.0.0",
      body: "Release notes",
      downloadAndInstall: mockDownloadAndInstall,
    };

    vi.mocked(api.checkForAppUpdate).mockResolvedValue(mockUpdate as never);
    vi.mocked(api.relaunchApp).mockResolvedValue(undefined);

    const { result } = renderHook(() => useAppUpdate());

    await act(async () => {
      await result.current.checkForUpdate();
    });

    await act(async () => {
      await result.current.installUpdate();
    });

    expect(mockDownloadAndInstall).toHaveBeenCalledTimes(1);
    expect(mockDownloadAndInstall).toHaveBeenCalledWith(expect.any(Function));
    expect(api.relaunchApp).toHaveBeenCalledTimes(1);
  });

  it("resets state when dismissUpdate is called", async () => {
    const mockDownloadAndInstall = vi.fn();
    const mockUpdate = {
      version: "2.0.0",
      body: "Release notes",
      downloadAndInstall: mockDownloadAndInstall,
    };

    vi.mocked(api.checkForAppUpdate).mockResolvedValue(mockUpdate as never);

    const { result } = renderHook(() => useAppUpdate());

    await act(async () => {
      await result.current.checkForUpdate();
    });

    expect(result.current.updateAvailable).toBe(true);
    expect(result.current.version).toBe("2.0.0");

    act(() => {
      result.current.dismissUpdate();
    });

    expect(result.current.updateAvailable).toBe(false);
    expect(result.current.downloading).toBe(false);
    expect(result.current.downloadProgress).toBe(0);
    expect(result.current.error).toBeNull();
  });

  it("checks for updates periodically every 30 minutes", async () => {
    vi.mocked(api.checkForAppUpdate).mockResolvedValue(null);

    renderHook(() => useAppUpdate());

    await vi.advanceTimersByTimeAsync(2000);

    expect(api.checkForAppUpdate).toHaveBeenCalledTimes(1);

    await vi.advanceTimersByTimeAsync(30 * 60 * 1000);

    expect(api.checkForAppUpdate).toHaveBeenCalledTimes(2);
  });
});
