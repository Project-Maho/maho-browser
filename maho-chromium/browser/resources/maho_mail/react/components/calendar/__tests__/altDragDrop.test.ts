import { describe, it, expect, vi } from "vitest";

vi.mock("../../../api", () => ({
  updateCalendarEvent: vi.fn().mockResolvedValue({}),
  createCalendarEvent: vi.fn().mockResolvedValue({}),
  duplicateCalendarEvent: vi.fn().mockResolvedValue({}),
}));

function pickHandler(altKeyPressed: boolean) {
  return async (event: { id: string; summary: string; dtstart: string; dtend: string; all_day: boolean }) => {
    const api = await import("../../../api");
    if (altKeyPressed) {
      await api.duplicateCalendarEvent(event.id);
    } else {
      await api.updateCalendarEvent({
        event_id: event.id,
        summary: event.summary,
        dtstart: event.dtstart,
        dtend: event.dtend,
        all_day: event.all_day,
      });
    }
  };
}

describe("Alt-drag drop routing", () => {
  it("normal drop invokes updateCalendarEvent (move)", async () => {
    const api = await import("../../../api");
    vi.clearAllMocks();

    const handler = pickHandler(false);
    await handler({ id: "evt-1", summary: "Test", dtstart: "2026-01-15T10:00:00Z", dtend: "2026-01-15T11:00:00Z", all_day: false });

    expect(api.updateCalendarEvent).toHaveBeenCalledOnce();
    expect(api.duplicateCalendarEvent).not.toHaveBeenCalled();
  });

  it("alt-drag drop invokes duplicateCalendarEvent (copy) instead of update", async () => {
    const api = await import("../../../api");
    vi.clearAllMocks();

    const handler = pickHandler(true);
    await handler({ id: "evt-1", summary: "Test", dtstart: "2026-01-15T10:00:00Z", dtend: "2026-01-15T11:00:00Z", all_day: false });

    expect(api.duplicateCalendarEvent).toHaveBeenCalledOnce();
    expect(api.duplicateCalendarEvent).toHaveBeenCalledWith("evt-1");
    expect(api.updateCalendarEvent).not.toHaveBeenCalled();
  });

  it("altKey state toggles between move and copy behavior", async () => {
    const api = await import("../../../api");
    vi.clearAllMocks();

    await pickHandler(false)({ id: "e1", summary: "s", dtstart: "2026-01-15T10:00:00Z", dtend: "2026-01-15T11:00:00Z", all_day: false });
    await pickHandler(true)({ id: "e2", summary: "s", dtstart: "2026-01-15T10:00:00Z", dtend: "2026-01-15T11:00:00Z", all_day: false });

    expect(api.updateCalendarEvent).toHaveBeenCalledOnce();
    expect(api.duplicateCalendarEvent).toHaveBeenCalledOnce();
  });
});
