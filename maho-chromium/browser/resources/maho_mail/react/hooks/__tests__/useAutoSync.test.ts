import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useAutoSync } from "../useAutoSync";

const mockListeners = new Map<string, Function>();

vi.mock("../../events.js", () => ({
  listen: vi.fn((event: string, handler: Function) => {
    mockListeners.set(event, handler);
    const unlisten = vi.fn(() => { mockListeners.delete(event); });
    return Promise.resolve(unlisten);
  }),
}));

describe("useAutoSync", () => {
  beforeEach(() => {
    mockListeners.clear();
    vi.clearAllMocks();
  });

  it("registers event listeners for provided callbacks", async () => {
    const onNewEmails = vi.fn();
    const onSyncStarted = vi.fn();
    const onSyncCompleted = vi.fn();
    const onSyncError = vi.fn();

    const { listen } = await import("../../events.js");

    renderHook(() =>
      useAutoSync({
        onNewEmails,
        onSyncStarted,
        onSyncCompleted,
        onSyncError,
      })
    );

    await act(async () => {
      await Promise.resolve();
    });

    expect(listen).toHaveBeenCalledWith("new-emails", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("sync-started", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("sync-completed", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("sync-error", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("snooze-triggered", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("reminder-triggered", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("send-later-sent", expect.any(Function));
    expect(listen).toHaveBeenCalledWith("send-later-failed", expect.any(Function));
    expect(listen).not.toHaveBeenCalledWith("idle:new-mail", expect.any(Function));
    expect(listen).toHaveBeenCalledTimes(8);
  });

  it("only registers listeners for provided callbacks", async () => {
    const onNewEmails = vi.fn();

    const { listen } = await import("../../events.js");

    renderHook(() =>
      useAutoSync({
        onNewEmails,
      })
    );

    await act(async () => {
      await Promise.resolve();
    });

    expect(listen).toHaveBeenCalledWith("new-emails", expect.any(Function));
    expect(listen).toHaveBeenCalledTimes(5);
  });

  it("calls onNewEmails when event fires", async () => {
    const onNewEmails = vi.fn();

    renderHook(() =>
      useAutoSync({
        onNewEmails,
      })
    );

    await act(async () => {
      await Promise.resolve();
    });

    const handler = mockListeners.get("new-emails");
    expect(handler).toBeDefined();
    act(() => {
      handler!({ payload: { account_id: "account-123", message_ids: ["<msg1@test>"] } });
    });

    expect(onNewEmails).toHaveBeenCalledTimes(1);
    expect(onNewEmails).toHaveBeenCalledWith("account-123", ["<msg1@test>"]);
  });

  it("calls onSyncError with sync error event payload", async () => {
    const onSyncError = vi.fn();

    renderHook(() =>
      useAutoSync({
        onSyncError,
      })
    );

    await act(async () => {
      await Promise.resolve();
    });

    const handler = mockListeners.get("sync-error");
    expect(handler).toBeDefined();
    act(() => {
      handler!({
        payload: {
          account_id: "account-123",
          error: "connection failed",
          error_type: "network",
        },
      });
    });

    expect(onSyncError).toHaveBeenCalledWith({
      account_id: "account-123",
      error: "connection failed",
      error_type: "network",
    });
  });

  it("cleanup unlistens all listeners on unmount", async () => {
    const onNewEmails = vi.fn();
    const onSyncStarted = vi.fn();
    const onSyncCompleted = vi.fn();
    const onSyncError = vi.fn();

    const { listen } = await import("../../events.js");

    const { unmount } = renderHook(() =>
      useAutoSync({
        onNewEmails,
        onSyncStarted,
        onSyncCompleted,
        onSyncError,
      })
    );

    await act(async () => {
      await Promise.resolve();
    });

    const listenMock = vi.mocked(listen);
    const unlistenFns: Array<() => void> = [];
    for (const result of listenMock.mock.results) {
      if (result.type === "return") {
        unlistenFns.push(await result.value);
      }
    }

    unmount();

    for (const unlisten of unlistenFns) {
      expect(unlisten).toHaveBeenCalledTimes(1);
    }
  });
});
