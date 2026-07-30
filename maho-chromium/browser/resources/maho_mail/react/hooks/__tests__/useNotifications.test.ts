import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import {
  isPermissionGranted,
  requestPermission,
  sendNotification,
} from "@tauri-apps/plugin-notification";
import { useNotifications } from "../useNotifications";
import { isMobile } from "../../utils/platform";

vi.mock("@tauri-apps/plugin-notification", () => ({
  isPermissionGranted: vi.fn(),
  requestPermission: vi.fn(),
  sendNotification: vi.fn(),
}));

vi.mock("../../utils/platform", () => ({
  isMobile: vi.fn(),
}));

describe("useNotifications", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(isMobile).mockReturnValue(false);
  });

  it("does not notify when disabled", async () => {
    vi.mocked(isPermissionGranted).mockResolvedValue(true);

    const { result } = renderHook(() => useNotifications({ enabled: false }));

    await act(async () => {
      await result.current.notify("Subject", "Body");
    });

    expect(isPermissionGranted).not.toHaveBeenCalled();
    expect(requestPermission).not.toHaveBeenCalled();
    expect(sendNotification).not.toHaveBeenCalled();
  });

  it("requests permission before sending a notification when permission is missing", async () => {
    vi.mocked(isPermissionGranted).mockResolvedValue(false);
    vi.mocked(requestPermission).mockResolvedValue("granted");

    const { result } = renderHook(() => useNotifications({ enabled: true }));

    await act(async () => {
      await result.current.notify("Subject", "Body");
    });

    expect(isPermissionGranted).toHaveBeenCalledTimes(1);
    expect(requestPermission).toHaveBeenCalledTimes(1);
    expect(sendNotification).toHaveBeenCalledWith({
      title: "Subject",
      body: "Body",
    });
  });

  it("requests permission eagerly on mobile when enabled", async () => {
    vi.mocked(isMobile).mockReturnValue(true);
    vi.mocked(isPermissionGranted).mockResolvedValue(false);
    vi.mocked(requestPermission).mockResolvedValue("granted");

    renderHook(() => useNotifications({ enabled: true }));

    await act(async () => {
      await Promise.resolve();
    });

    expect(isPermissionGranted).toHaveBeenCalledTimes(1);
    expect(requestPermission).toHaveBeenCalledTimes(1);
    expect(sendNotification).not.toHaveBeenCalled();
  });
});
