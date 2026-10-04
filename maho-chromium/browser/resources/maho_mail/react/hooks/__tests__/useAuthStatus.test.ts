import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { listen } from "../../events.js";
import { useAuthStatus } from "../useAuthStatus";

vi.mock("../../events.js", () => ({ listen: vi.fn() }));

const listeners = new Map<string, (event: { payload: any }) => void>();
const unlistenFns: Array<() => void> = [];
const listenMock = vi.mocked(listen);

describe("useAuthStatus", () => {
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
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("registers auth reauth and refresh listeners and cleans them up on unmount", async () => {
    const { unmount } = renderHook(() => useAuthStatus());

    await act(async () => {
      await Promise.resolve();
    });

    expect(listenMock).toHaveBeenCalledWith("auth-reauth-required", expect.any(Function));
    expect(listenMock).toHaveBeenCalledWith("auth-refresh-succeeded", expect.any(Function));
    expect(listeners.has("auth-reauth-required")).toBe(true);
    expect(listeners.has("auth-refresh-succeeded")).toBe(true);

    unmount();

    await act(async () => {
      await Promise.resolve();
    });

    expect(unlistenFns).toHaveLength(2);
    expect(unlistenFns[0]).toHaveBeenCalledTimes(1);
    expect(unlistenFns[1]).toHaveBeenCalledTimes(1);
  });

  it("collects and deduplicates auth reauth-required events", async () => {
    const { result } = renderHook(() => useAuthStatus());

    await act(async () => {
      await Promise.resolve();
    });

    const handler = listeners.get("auth-reauth-required");
    expect(handler).toBeDefined();

    act(() => {
      handler?.({
        payload: {
          account_id: "acc-1",
          provider: "gmail",
          reason: "invalid_grant",
        },
      });
    });

    expect(result.current.authErrors).toEqual([
      {
        account_id: "acc-1",
        provider: "gmail",
        reason: "invalid_grant",
        timestamp: expect.any(Number),
      },
    ]);

    act(() => {
      handler?.({
        payload: {
          account_id: "acc-1",
          provider: "gmail",
          reason: "refresh_token_missing",
        },
      });
    });

    expect(result.current.authErrors).toEqual([
      {
        account_id: "acc-1",
        provider: "gmail",
        reason: "refresh_token_missing",
        timestamp: expect.any(Number),
      },
    ]);
  });

  it("clears auth errors manually and when refresh succeeds", async () => {
    const { result } = renderHook(() => useAuthStatus());

    await act(async () => {
      await Promise.resolve();
    });

    const reauthHandler = listeners.get("auth-reauth-required");
    const refreshHandler = listeners.get("auth-refresh-succeeded");

    act(() => {
      reauthHandler?.({
        payload: {
          account_id: "acc-1",
          provider: "gmail",
          reason: "invalid_grant",
        },
      });
      reauthHandler?.({
        payload: {
          account_id: "acc-2",
          provider: "outlook",
          reason: "unknown",
        },
      });
    });

    expect(result.current.authErrors).toHaveLength(2);

    act(() => {
      result.current.clearAuthError("acc-1");
    });

    expect(result.current.authErrors).toHaveLength(1);
    expect(result.current.authErrors[0]?.account_id).toBe("acc-2");

    act(() => {
      refreshHandler?.({ payload: { account_id: "acc-2" } });
    });

    expect(result.current.authErrors).toEqual([]);
  });
});
