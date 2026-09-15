import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { listen } from "../../events.js";
import { useNetworkStatus } from "../useNetworkStatus";
import * as api from "../../api";

vi.mock("../../events.js", () => ({ listen: vi.fn() }));

vi.mock("../../api", () => ({
  flushOutbox: vi.fn(),
  listAllPendingMutations: vi.fn(),
}));

const listeners = new Map<string, (event: { payload: any }) => void>();
const unlistenFns: Array<() => void> = [];
const listenMock = vi.mocked(listen);

describe("useNetworkStatus", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    listeners.clear();
    unlistenFns.length = 0;
    listenMock.mockImplementation((event, handler) => {
      listeners.set(event, handler as (event: { payload: any }) => void);
      const unlisten = vi.fn(() => listeners.delete(event));
      unlistenFns.push(unlisten);
      return Promise.resolve(unlisten);
    });
    Object.defineProperty(navigator, "onLine", {
      value: true,
      configurable: true,
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("loads the pending mutation count on mount", async () => {
    vi.mocked(api.listAllPendingMutations).mockResolvedValue([
      {
        id: "m1",
        account_id: "acc-1",
        email_uid: 10,
        folder_path: "INBOX",
        mutation_type: "mark_read",
        target_folder: null,
        uid_validity: 1,
        created_at: new Date().toISOString(),
      },
      {
        id: "m2",
        account_id: "acc-1",
        email_uid: 11,
        folder_path: "INBOX",
        mutation_type: "star",
        target_folder: null,
        uid_validity: 1,
        created_at: new Date(Date.now() - 25 * 60 * 60 * 1000).toISOString(),
      },
    ]);

    const { result } = renderHook(() => useNetworkStatus());

    await act(async () => {
      await Promise.resolve();
    });

    expect(api.listAllPendingMutations).toHaveBeenCalledTimes(1);
    expect(result.current.isOnline).toBe(true);
    expect(result.current.pendingMutationCount).toBe(2);
    expect(result.current.staleMutationCount).toBe(1);
    expect(result.current.connectionStatus).toBe("online");
  });

  it("updates online/offline status and flushes queued mutations when connectivity returns", async () => {
    vi.mocked(api.listAllPendingMutations).mockResolvedValue([
      {
        id: "m1",
        account_id: "acc-1",
        email_uid: 10,
        folder_path: "INBOX",
        mutation_type: "mark_read",
        target_folder: null,
        uid_validity: 1,
        created_at: new Date().toISOString(),
      },
    ]);
    vi.mocked(api.flushOutbox).mockResolvedValue(0);

    const { result } = renderHook(() => useNetworkStatus());

    await act(async () => {
      await Promise.resolve();
    });

    act(() => {
      window.dispatchEvent(new Event("offline"));
    });

    expect(result.current.isOnline).toBe(false);
    expect(result.current.connectionStatus).toBe("offline");

    act(() => {
      window.dispatchEvent(new Event("online"));
    });

    await act(async () => {
      await Promise.resolve();
    });

    expect(api.flushOutbox).toHaveBeenCalledTimes(1);
    expect(api.listAllPendingMutations).toHaveBeenCalledTimes(2);
    expect(result.current.isOnline).toBe(true);
    expect(result.current.connectionStatus).toBe("online");
    expect(result.current.staleMutationCount).toBe(0);
  });

  it("tracks backend connection changes and unlistens on cleanup", async () => {
    vi.mocked(api.listAllPendingMutations).mockResolvedValue([
      {
        id: "m1",
        account_id: "acc-1",
        email_uid: 10,
        folder_path: "INBOX",
        mutation_type: "mark_read",
        target_folder: null,
        uid_validity: 1,
        created_at: new Date().toISOString(),
      },
    ]);

    const { result, unmount } = renderHook(() => useNetworkStatus());

    await act(async () => {
      await Promise.resolve();
    });

    const connectionHandler = listeners.get("connection-status-changed");
    expect(connectionHandler).toBeDefined();

    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: false } });
    });

    expect(result.current.connectionStatus).toBe("partial");

    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: true } });
    });

    await act(async () => {
      await Promise.resolve();
    });

    expect(result.current.connectionStatus).toBe("online");
    expect(api.listAllPendingMutations).toHaveBeenCalledTimes(2);

    unmount();

    await act(async () => {
      await Promise.resolve();
    });

    expect(unlistenFns).toHaveLength(3);
    unlistenFns.forEach((fn) => {
      expect(fn).toHaveBeenCalledTimes(1);
    });
  });

  it("keeps global connection online when one account disconnects while another remains connected", async () => {
    vi.mocked(api.listAllPendingMutations).mockResolvedValue([]);

    const { result } = renderHook(() => useNetworkStatus());

    await act(async () => {
      await Promise.resolve();
    });

    const connectionHandler = listeners.get("connection-status-changed");
    expect(connectionHandler).toBeDefined();

    // Account 1 and Account 2 both report connected
    await act(async () => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: true } });
      connectionHandler?.({ payload: { account_id: "acc-2", connected: true } });
      await Promise.resolve();
    });

    expect(result.current.connectionStatus).toBe("online");

    // Account 1 disconnects, but Account 2 is still connected
    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: false } });
    });

    // Global backend connected status must remain online while ANY account is connected
    expect(result.current.connectionStatus).toBe("online");

    // Account 2 also disconnects: all accounts disconnected -> partial
    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-2", connected: false } });
    });

    expect(result.current.connectionStatus).toBe("partial");

    // Account 1 reconnects -> online
    await act(async () => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: true } });
      await Promise.resolve();
    });

    expect(result.current.connectionStatus).toBe("online");
  });

  it("handles multiple accounts flapping, duplicate events, and recovery", async () => {
    vi.mocked(api.listAllPendingMutations).mockResolvedValue([]);

    const { result } = renderHook(() => useNetworkStatus());

    await act(async () => {
      await Promise.resolve();
    });

    const connectionHandler = listeners.get("connection-status-changed");
    expect(connectionHandler).toBeDefined();

    // Acc 1 and Acc 2 connect
    await act(async () => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: true } });
      connectionHandler?.({ payload: { account_id: "acc-2", connected: true } });
      await Promise.resolve();
    });
    expect(result.current.connectionStatus).toBe("online");

    // Acc 1 duplicate connect event (idempotency check)
    await act(async () => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: true } });
      await Promise.resolve();
    });
    expect(result.current.connectionStatus).toBe("online");

    // Acc 1 disconnects -> Acc 2 still online -> stays online
    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: false } });
    });
    expect(result.current.connectionStatus).toBe("online");

    // Acc 1 duplicate disconnect event
    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-1", connected: false } });
    });
    expect(result.current.connectionStatus).toBe("online");

    // Acc 2 disconnects -> all disconnected -> partial
    act(() => {
      connectionHandler?.({ payload: { account_id: "acc-2", connected: false } });
    });
    expect(result.current.connectionStatus).toBe("partial");

    // Acc 2 reconnects -> back to online
    await act(async () => {
      connectionHandler?.({ payload: { account_id: "acc-2", connected: true } });
      await Promise.resolve();
    });
    expect(result.current.connectionStatus).toBe("online");
  });
});
