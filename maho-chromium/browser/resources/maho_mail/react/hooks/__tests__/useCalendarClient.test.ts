import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act, waitFor } from "@testing-library/react";
import { useCalendarClient } from "../useCalendarClient";
import * as api from "../../api";
import { listen } from "@tauri-apps/api/event";
import { toast as sonnerToast } from "sonner";

vi.mock("@tauri-apps/api/event", () => ({ listen: vi.fn() }));

vi.mock("../../api", () => ({
  listCalendarEvents: vi.fn().mockResolvedValue([]),
  listAccountCalendars: vi.fn().mockResolvedValue([]),
  listCalendarCategories: vi.fn().mockResolvedValue([]),
  getAppSetting: vi.fn().mockResolvedValue("false"),
  setAppSetting: vi.fn().mockResolvedValue({}),
  createCalendarEvent: vi.fn().mockResolvedValue({}),
  duplicateCalendarEvent: vi.fn().mockResolvedValue({}),
  deleteCalendarEvent: vi.fn().mockResolvedValue({}),
  exportCalendarIcs: vi.fn().mockResolvedValue("BEGIN:VCALENDAR..."),
}));

const mockT = (key: string) => key;
vi.mock("react-i18next", () => ({
  useTranslation: () => ({ t: mockT }),
}));

const mockToast = vi.fn();
vi.mock("../../components/ui/Toast", () => ({
  useToast: () => ({ toast: mockToast }),
}));

vi.mock("sonner", () => ({
  toast: {
    success: vi.fn(),
    error: vi.fn(),
    warning: vi.fn(),
    info: vi.fn(),
  },
}));

describe("useCalendarClient", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("loads data and handles selections", async () => {
    const { result } = renderHook(() => useCalendarClient({ accountId: "acc-1" }));

    // Wait for the auto-fetch triggered on mount to resolve
    await waitFor(() => {
      expect(result.current.loading).toBe(false);
    });

    expect(result.current.events).toEqual([]);
    expect(api.listCalendarEvents).toHaveBeenCalledWith("acc-1", undefined, undefined);
  });

  it("handles event quick add", async () => {
    const { result } = renderHook(() => useCalendarClient({ accountId: "acc-1" }));

    // Wait for the auto-fetch triggered on mount to resolve
    await waitFor(() => {
      expect(result.current.loading).toBe(false);
    });

    act(() => {
      result.current.setQuickAddText("Lunch tomorrow at 1pm");
    });

    const event = { preventDefault: vi.fn() };
    await act(async () => {
      await result.current.handleQuickAdd(event as any);
    });

    expect(api.createCalendarEvent).toHaveBeenCalled();
  });
});

describe("useCalendarClient RSVP reply event listeners", () => {
  const listeners = new Map<string, (event: { payload: any }) => void>();
  const unlistenFns: Array<() => void> = [];

  beforeEach(() => {
    vi.clearAllMocks();
    listeners.clear();
    unlistenFns.length = 0;
    vi.mocked(listen).mockImplementation((event: string, handler: any) => {
      listeners.set(event, handler as (event: { payload: any }) => void);
      const unlisten = vi.fn(() => listeners.delete(event));
      unlistenFns.push(unlisten);
      return Promise.resolve(unlisten);
    });
  });

  it("shows error toast when calendar-rsvp-reply-failed fires", async () => {
    renderHook(() => useCalendarClient({ accountId: "acc-1" }));

    await waitFor(() => {
      expect(listeners.has("calendar-rsvp-reply-failed")).toBe(true);
    });

    const handler = listeners.get("calendar-rsvp-reply-failed")!;
    act(() => {
      handler({
        payload: {
          event_id: "evt-1",
          rsvp_status: "accepted",
          summary: "Team Standup",
          error: "Auth token expired",
        },
      });
    });

    expect(sonnerToast.error).toHaveBeenCalledWith(
      "calendar.rsvp.reply_failed_title",
      expect.objectContaining({ description: expect.any(String) }),
    );
  });

  it("shows info toast when calendar-rsvp-reply-queued fires", async () => {
    renderHook(() => useCalendarClient({ accountId: "acc-1" }));

    await waitFor(() => {
      expect(listeners.has("calendar-rsvp-reply-queued")).toBe(true);
    });

    const handler = listeners.get("calendar-rsvp-reply-queued")!;
    act(() => {
      handler({
        payload: {
          event_id: "evt-2",
          rsvp_status: "tentative",
          summary: "Design Review",
        },
      });
    });

    expect(sonnerToast.info).toHaveBeenCalledWith(
      "calendar.rsvp.reply_queued_title",
      expect.objectContaining({ description: expect.any(String) }),
    );
  });

  it("shows success toast when calendar-rsvp-reply-drained fires", async () => {
    renderHook(() => useCalendarClient({ accountId: "acc-1" }));

    await waitFor(() => {
      expect(listeners.has("calendar-rsvp-reply-drained")).toBe(true);
    });

    const handler = listeners.get("calendar-rsvp-reply-drained")!;
    act(() => {
      handler({ payload: { event_id: "evt-3" } });
    });

    expect(sonnerToast.success).toHaveBeenCalledWith(
      "calendar.rsvp.reply_drained_title",
      expect.objectContaining({ description: expect.any(String) }),
    );
  });

  it("unlistens all RSVP listeners on cleanup", async () => {
    const { unmount } = renderHook(() => useCalendarClient({ accountId: "acc-1" }));

    await waitFor(() => {
      expect(listeners.has("calendar-rsvp-reply-failed")).toBe(true);
    });

    unmount();

    expect(unlistenFns.length).toBeGreaterThanOrEqual(3);
    const rsvpUnlistens = unlistenFns.slice(-3);
    rsvpUnlistens.forEach((fn) => {
      expect(fn).toHaveBeenCalledTimes(1);
    });
  });
});
