// Copyright 2026 Maho Browser. All rights reserved.

import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { useNotifications } from "../useNotifications";
import { isMobile } from "../../utils/platform";

vi.mock("../../utils/platform", () => ({
  isMobile: vi.fn(),
}));

type PermissionState = "granted" | "denied" | "default";

let permissionState: PermissionState = "default";
let notificationCtor: ReturnType<typeof vi.fn>;
let requestPermissionMock: ReturnType<typeof vi.fn>;

function stubNotificationApi(): void {
  class StubNotification {
    static get permission(): PermissionState {
      return permissionState;
    }
    static requestPermission = requestPermissionMock;
    constructor(title: string, options?: { body?: string }) {
      notificationCtor(title, options);
    }
  }
  vi.stubGlobal("Notification", StubNotification);
}

describe("useNotifications", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    permissionState = "default";
    notificationCtor = vi.fn();
    requestPermissionMock = vi.fn().mockResolvedValue("granted");
    stubNotificationApi();
    vi.mocked(isMobile).mockReturnValue(false);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("does not notify when disabled", async () => {
    const { result } = renderHook(() => useNotifications({ enabled: false }));

    await act(async () => {
      await result.current.notify("Subject", "Body");
    });

    expect(notificationCtor).not.toHaveBeenCalled();
    expect(requestPermissionMock).not.toHaveBeenCalled();
  });

  it("requests permission before sending a notification when permission is missing", async () => {
    permissionState = "default";
    const { result } = renderHook(() => useNotifications({ enabled: true }));

    await act(async () => {
      await result.current.notify("Subject", "Body");
    });

    expect(requestPermissionMock).toHaveBeenCalledTimes(1);
    expect(notificationCtor).toHaveBeenCalledWith("Subject", { body: "Body" });
  });

  it("sends immediately when permission is already granted", async () => {
    permissionState = "granted";
    const { result } = renderHook(() => useNotifications({ enabled: true }));

    await act(async () => {
      await result.current.notify("Subject", "Body");
    });

    expect(requestPermissionMock).not.toHaveBeenCalled();
    expect(notificationCtor).toHaveBeenCalledWith("Subject", { body: "Body" });
  });

  it("requests permission eagerly on mobile when enabled", async () => {
    vi.mocked(isMobile).mockReturnValue(true);
    permissionState = "default";

    renderHook(() => useNotifications({ enabled: true }));

    await act(async () => {
      await Promise.resolve();
    });

    expect(requestPermissionMock).toHaveBeenCalledTimes(1);
    expect(notificationCtor).not.toHaveBeenCalled();
  });
});
