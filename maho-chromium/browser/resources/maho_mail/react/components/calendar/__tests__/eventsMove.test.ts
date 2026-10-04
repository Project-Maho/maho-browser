import { describe, it, expect, vi, beforeEach } from "vitest";
import * as api from "../../../api";

vi.mock("../../../api", () => ({
  updateCalendarEvent: vi.fn().mockResolvedValue({
    id: "evt-1",
    account_id: "acc-1",
    email_id: null,
    uid: "evt-1",
    summary: "Meeting",
    description: null,
    dtstart: "2026-01-15T10:00:00Z",
    dtend: "2026-01-15T11:00:00Z",
    location: null,
    organizer: null,
    status: "confirmed",
    rsvp_status: null,
    recurrence_rule: null,
    all_day: false,
    created_at: "",
    updated_at: "",
    google_calendar_id: "cal-target",
  }),
}));

describe("update_calendar_event target_calendar_id routing", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("passes target_calendar_id to backend when moving event across calendars", async () => {
    await api.updateCalendarEvent({
      event_id: "evt-1",
      summary: "Meeting",
      dtstart: "2026-01-15T10:00:00Z",
      dtend: "2026-01-15T11:00:00Z",
      all_day: false,
      target_calendar_id: "cal-target",
    });

    expect(api.updateCalendarEvent).toHaveBeenCalledWith(
      expect.objectContaining({
        event_id: "evt-1",
        target_calendar_id: "cal-target",
      }),
    );
  });

  it("omits target_calendar_id when not moving", async () => {
    await api.updateCalendarEvent({
      event_id: "evt-1",
      summary: "Meeting",
      dtstart: "2026-01-15T10:00:00Z",
      dtend: "2026-01-15T11:00:00Z",
      all_day: false,
    });

    const call = vi.mocked(api.updateCalendarEvent).mock.calls[0][0];
    expect(call.target_calendar_id).toBeUndefined();
  });

  it("returns updated event with new google_calendar_id after move", async () => {
    const result = await api.updateCalendarEvent({
      event_id: "evt-1",
      summary: "Meeting",
      dtstart: "2026-01-15T10:00:00Z",
      dtend: "2026-01-15T11:00:00Z",
      all_day: false,
      target_calendar_id: "cal-target",
    });

    expect(result.google_calendar_id).toBe("cal-target");
  });
});
